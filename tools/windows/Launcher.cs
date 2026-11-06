using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class Launcher
{
    internal const string Title = "Mosaico 同步助手";

    [STAThread]
    private static int Main(string[] args)
    {
        bool selfTest = args.Length == 2 && args[0] == "--self-test";
        string root = selfTest ? args[1] : Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MosaicoAgentPet");
        try
        {
            if (selfTest)
            {
                using (Process test = StartWorker(PreparePayload(root), true))
                {
                    var errors = test.StandardError.ReadToEndAsync();
                    if (!test.WaitForExit(90000))
                    {
                        test.Kill();
                        return 3;
                    }
                    if (test.ExitCode != 0)
                        File.WriteAllText(Path.Combine(root, "self-test-stderr.txt"), errors.Result);
                    return test.ExitCode;
                }
            }

            bool first;
            using (var instance = new Mutex(true, @"Local\MosaicoAgentPet.WindowsHelper", out first))
            {
                if (!first)
                {
                    MessageBox.Show("同步助手已经运行，可从任务栏托盘打开。", Title);
                    return 0;
                }
                string payload = PreparePayload(root);
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                using (Stream artwork = Assembly.GetExecutingAssembly().GetManifestResourceStream("AgentPetIcon"))
                using (var icon = new Icon(artwork, SystemInformation.SmallIconSize))
                using (var window = new SyncWindow(payload, icon))
                using (var context = new ApplicationContext(window))
                {
                    if (args.Length != 1 || args[0] != "--tray")
                        window.Show();
                    else
                        window.CreateHiddenHandle();
                    Application.Run(context);
                }
            }
            return 0;
        }
        catch (Exception error)
        {
            if (selfTest)
                File.WriteAllText(Path.Combine(root, "launcher-error.txt"), error.ToString());
            else
                MessageBox.Show(error.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    private static string PreparePayload(string root)
    {
        root = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);
        Directory.CreateDirectory(root);
        string release = Path.Combine(root, "payload-" + BuildInfo.Id);
        if (File.Exists(Path.Combine(release, ".ready")))
            return release;
        string staged = Path.GetFullPath(Path.Combine(root, "extract-" + Guid.NewGuid().ToString("N")));
        if (!String.Equals(Path.GetDirectoryName(staged), root, StringComparison.OrdinalIgnoreCase))
            throw new IOException("安装目录无效。");
        Directory.CreateDirectory(staged);
        try
        {
            using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("AgentPetPayload"))
            using (var archive = new ZipArchive(stream, ZipArchiveMode.Read))
            {
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    if (entry.Name.Length == 0)
                        continue;
                    string target = Path.GetFullPath(Path.Combine(staged, entry.FullName));
                    if (!target.StartsWith(staged + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                        throw new IOException("安装包路径无效。");
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    using (Stream input = entry.Open())
                    using (FileStream output = File.Open(target, FileMode.CreateNew, FileAccess.Write))
                        input.CopyTo(output);
                }
            }
            File.WriteAllText(Path.Combine(staged, ".ready"), BuildInfo.Id);
            Directory.Move(staged, release);
            return release;
        }
        finally
        {
            if (Directory.Exists(staged))
                Directory.Delete(staged, true);
        }
    }

    internal static Process StartWorker(string payload, bool selfTest)
    {
        string workspace = Path.Combine(payload, "workspace");
        string script = Path.Combine(workspace, @"projects\my_app\tools\portable_agent.py");
        var start = new ProcessStartInfo {
            FileName = Path.Combine(payload, @"runtime\python.exe"),
            Arguments = "-X utf8 \"" + script + "\"" + (selfTest ? " --self-test" : ""),
            WorkingDirectory = workspace,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = !selfTest,
            RedirectStandardError = selfTest
        };
        start.EnvironmentVariables.Remove("MOSAICO_WORKSPACE");
        if (selfTest)
        {
            start.EnvironmentVariables["PATH"] = Environment.SystemDirectory;
            start.StandardErrorEncoding = Encoding.UTF8;
        }
        return Process.Start(start);
    }
}

internal sealed class SyncWindow : Form
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string RunName = "MosaicoAgentPet";
    private readonly string logFolder;
    private readonly Process worker;
    private readonly Label status = new Label();
    private readonly NotifyIcon tray = new NotifyIcon();
    private readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
    private readonly JavaScriptSerializer json = new JavaScriptSerializer();
    private bool exiting;

    internal SyncWindow(string payload, Icon icon)
    {
        Text = Launcher.Title;
        ClientSize = new Size(450, 194);
        AutoScaleMode = AutoScaleMode.Dpi;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        Icon = icon;

        logFolder = Path.Combine(payload, @"workspace\projects\my_app\.gateway\agent-pet");
        Directory.CreateDirectory(logFolder);
        File.Delete(Path.Combine(logFolder, "stop"));
        File.Delete(Path.Combine(logFolder, "status.json"));
        var heading = new Label {
            Text = "Mosaico 桌宠同步", Font = new Font(Font, FontStyle.Bold), AutoSize = true,
            Location = new Point(18, 16)
        };
        status.Text = "正在启动同步助手…";
        status.SetBounds(18, 47, 415, 68);

        var startup = new CheckBox {
            Text = "随 Windows 登录启动", AutoSize = true, Location = new Point(18, 119)
        };
        string runCommand = "\"" + Application.ExecutablePath + "\" --tray";
        using (RegistryKey key = Registry.CurrentUser.OpenSubKey(RunKey))
            startup.Checked = key != null && String.Equals(key.GetValue(RunName) as string,
                runCommand, StringComparison.OrdinalIgnoreCase);
        startup.CheckedChanged += delegate {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(RunKey))
                {
                    if (startup.Checked)
                        key.SetValue(RunName, runCommand);
                    else
                        key.DeleteValue(RunName, false);
                }
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, Launcher.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        };
        var buttons = new FlowLayoutPanel {
            Location = new Point(14, 151), Size = new Size(419, 37)
        };
        buttons.Controls.Add(ActionButton("打开日志", delegate {
            Process.Start(new ProcessStartInfo { FileName = logFolder, UseShellExecute = true });
        }));
        buttons.Controls.Add(ActionButton("隐藏到托盘", delegate { Hide(); }));
        buttons.Controls.Add(ActionButton("退出", delegate { Close(); }));
        Controls.AddRange(new Control[] { heading, status, startup, buttons });

        tray.Icon = icon;
        tray.Text = Launcher.Title;
        tray.ContextMenuStrip = new ContextMenuStrip();
        tray.ContextMenuStrip.Items.Add("打开同步助手", null, delegate { ShowWindow(); });
        tray.ContextMenuStrip.Items.Add("退出", null, delegate { Close(); });
        tray.DoubleClick += delegate { ShowWindow(); };

        worker = Launcher.StartWorker(payload, false);
        tray.Visible = true;
        timer.Interval = 800;
        timer.Tick += delegate { RefreshStatus(); };
        timer.Start();
        FormClosing += RequestExit;
        Resize += delegate {
            if (WindowState == FormWindowState.Minimized)
                Hide();
        };
    }

    private static Button ActionButton(string text, EventHandler action)
    {
        var button = new Button { Text = text, AutoSize = true };
        button.Click += action;
        return button;
    }

    private void ShowWindow()
    {
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
    }

    internal void CreateHiddenHandle()
    {
        CreateHandle();
    }

    private void RefreshStatus()
    {
        if (worker.HasExited)
        {
            if (exiting)
                Close();
            else
                status.Text = "同步进程已退出（" + worker.ExitCode + "）。可打开日志查看原因。";
            return;
        }
        if (exiting)
            return;

        try
        {
            string path = Path.Combine(logFolder, "status.json");
            if (!File.Exists(path))
                return;
            Dictionary<string, object> state;
            using (var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var reader = new StreamReader(input))
                state = json.Deserialize<Dictionary<string, object>>(reader.ReadToEnd());
            object connected, source, phrase, error;
            if (state.TryGetValue("connected", out connected) && Equals(connected, true))
            {
                state.TryGetValue("source", out source);
                state.TryGetValue("phrase", out phrase);
                status.Text = "已连接 · " + source + " · " + phrase + "\n拔线后自动等待下次连接。";
            }
            else if (state.TryGetValue("error", out error))
            {
                string reason = Convert.ToString(error);
                status.Text = "等待连接\n" + (reason.Length <= 150 ? reason : reason.Substring(0, 150) + "…");
            }
            else
                status.Text = "等待 USB 连接\n请插入运行 Agent Pet 的 Mosaico。";
        }
        catch (IOException) { }
        catch (ArgumentException) { }
        catch (InvalidOperationException) { }
    }

    private void RequestExit(object sender, FormClosingEventArgs args)
    {
        if (worker.HasExited)
            return;
        File.WriteAllText(Path.Combine(logFolder, "stop"), "");
        if (args.CloseReason == CloseReason.WindowsShutDown)
            return;
        args.Cancel = true;
        exiting = true;
        status.Text = "正在退出同步助手…";
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            timer.Dispose();
            tray.Visible = false;
            if (tray.ContextMenuStrip != null)
                tray.ContextMenuStrip.Dispose();
            tray.Dispose();
            if (worker != null)
                worker.Dispose();
        }
        base.Dispose(disposing);
    }
}

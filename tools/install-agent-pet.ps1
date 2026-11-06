# Install a per-user, windowless Windows login shortcut. No administrator or
# stored password is needed. The watcher selects the sole board by default.
param(
    [ValidateSet('Install', 'Uninstall', 'Start', 'Stop', 'Status')]
    [string]$Action = 'Install',
    [string]$Python = '',
    [string]$DeviceId = 'auto'
)
$ErrorActionPreference = 'Stop'
if ($DeviceId -ieq 'auto') {
    $DeviceId = 'auto'
} elseif ($DeviceId -notmatch '^[0-9A-Fa-f]{12,}$' -or ($DeviceId.Length % 2) -ne 0) {
    throw 'DeviceId must be auto or the full hexadecimal ESP-Iris Device ID'
}
$projectPath = Split-Path $PSScriptRoot -Parent
$workspacePath = Split-Path (Split-Path $projectPath -Parent) -Parent
$entry = Join-Path $PSScriptRoot 'agent_pet.py'
$startup = [Environment]::GetFolderPath('Startup')
$shortcutPath = Join-Path $startup 'Mosaico Agent Pet.lnk'

if (-not $Python) {
    $Python = (& python -c 'import sys; print(sys.executable)').Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.10+ is required' }
}
$consolePython = Join-Path (Split-Path $Python -Parent) 'python.exe'
$windowlessPython = Join-Path (Split-Path $Python -Parent) 'pythonw.exe'
if (-not (Test-Path -LiteralPath $consolePython) -or
    -not (Test-Path -LiteralPath $windowlessPython)) {
    throw 'Pass -Python with an installed Python that includes python.exe and pythonw.exe'
}
$arguments = '"' + $entry + '" --background --device-id ' + $DeviceId
switch ($Action) {
    'Install' {
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $windowlessPython
        $shortcut.Arguments = $arguments
        $shortcut.WorkingDirectory = $workspacePath
        $shortcut.Description = 'Mosaico Agent Pet USB watcher'
        $shortcut.WindowStyle = 7
        $shortcut.Save()
        Start-Process -FilePath $windowlessPython -ArgumentList $arguments -WorkingDirectory $workspacePath -WindowStyle Hidden
        Write-Output ('Installed: ' + $shortcutPath)
    }
    'Uninstall' {
        if (Test-Path -LiteralPath $shortcutPath) { Remove-Item -LiteralPath $shortcutPath }
        & $consolePython $entry --stop
        Write-Output 'Agent Pet login startup removed; stop requested.'
    }
    'Start' {
        Start-Process -FilePath $windowlessPython -ArgumentList $arguments -WorkingDirectory $workspacePath -WindowStyle Hidden
        Write-Output 'Agent Pet started in the background.'
    }
    'Stop' { & $consolePython $entry --stop }
    'Status' {
        & $consolePython $entry --status
        Write-Output ('Login shortcut installed: ' + (Test-Path -LiteralPath $shortcutPath))
    }
}

# Mosaico Agent Pet 桌宠

[English](README.md)

ESP-Mosaico 桌宠汇总并行的 Codex、桌面应用内 ChatGPT、WorkBuddy、Claude Code 和
PandaCode 任务状态。仍在工作的任务优先显示，不会被另一个已完成的任务盖掉。
设备还支持小智语音、Wi-Fi 切换和音量调节。

第二屏为 **Codex 用量面板**：从桌宠向左滑动或点击 `1/2 ›` 打开，显示剩余额度、
重置时间和官方累计 token；向右滑动或点击 `‹ 桌宠` 返回。Wi-Fi、音量设置沿用
原有操作；查看用量时，桌宠仍在后台接收最新状态。

同一个 Windows 助手负责两屏同步。用量在启动、重新连接、点击刷新及每隔 5 分钟
更新一次，独立查询不会阻塞桌宠心跳。电脑和板子先恢复最近的数据，更新失败保留
数字和原更新时间，并标记为历史数据。
PC 响应超时时，页脚改为显示“PC 未响应 · 历史数据”；已保存的数字和更新时间不变。

独立 EXE 的构建方法见 [Windows 同步助手说明](tools/windows/README.md)。

采集用量时，电脑需安装并登录 Codex，窗口可以关闭。助手通过官方 app-server 读取账户数据，
无需再启动独立的 usage_panel watch。显示缓存保存在
`%LOCALAPPDATA%/MosaicoAgentPet/usage.json`，更新 EXE 也保留；首次运行会迁移有效的
旧 `mosaico-usage/usage.json`。缓存不含登录凭据或账户原始响应。

## 准备工作区

克隆 [ESP-Mosaico 工作区](https://github.com/esp-mosaico/esp-mosaico-vibe)，
将本仓库放在 `projects/Agent_Pet`。固件在设备和 Vibe Mode 中的名称为 `Agent_Pet`。

```sh
git clone https://github.com/esp-mosaico/esp-mosaico-vibe.git
cd esp-mosaico-vibe
git checkout 1872fd88c01d7807556d01fd2a19864e40a307c9
git submodule update --init submodule/esp-mosaico-utils submodule/esp-mosaico-bsp
git clone https://github.com/leco0119/mosaico-agent-pet.git projects/Agent_Pet
git -C submodule/esp-mosaico-bsp apply ../../projects/Agent_Pet/patches/bsp-psram-display.patch
git -C submodule/esp-mosaico-utils apply ../../projects/Agent_Pet/patches/utils-windowless-gateway.patch
```

显示补丁允许直接从 PSRAM 传输图像。Gateway 补丁隐藏 Windows 数据传输进程的控制台窗口。
构建使用 ESP-IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，目标芯片为
`esp32s31`。安装时请按照工作区的 [CLI 指南](https://github.com/esp-mosaico/esp-mosaico-vibe/blob/1872fd88c01d7807556d01fd2a19864e40a307c9/docs/mosaico-cli_CN.md)
选择更新方式。

```sh
idf.py -C projects/Agent_Pet build
python mosaico.py project sim --project projects/Agent_Pet --headless --duration 3
python -m unittest discover -s projects/Agent_Pet/tools/tests -v
```

## Windows 同步

[单文件同步助手](tools/windows/README.md)自带运行时，自动选择通过 USB 连接的唯一一块
Mosaico。它可以缩到系统托盘，也可以随 Windows 登录启动。
开发时可以运行 `python projects/Agent_Pet/tools/agent_pet.py`。
连接多块设备时，用 `--device-id` 指定目标板的完整设备 ID。

识别 ChatGPT 任务时，Codex 桌面应用需保持运行，窗口可以最小化。助手每 3 秒读取
应用的实时任务列表，并与所有本地会话日志一起判断；只要还有任务运行，就保持工作状态。
短暂读取失败时最多保留 15 秒的状态，应用退出后清除该来源。此本地接口可能随桌面应用
升级变化；不可用时仍保留本地日志识别。详见[并行任务验证记录](docs/evidence/parallel-activity-2026-11-07.md)。

## 目录说明

- `components/app_pet` 包含桌宠与用量 C 控制器、两屏 GSP 场景、统一事件路由和模拟器后端。
- `components/app_usage_link` 负责用量 RPC、刷新请求计数和 NVS 历史数据。
- `components/app_wifi` 负责扫描、连接和保存最近使用的网络。
- `components/app_display` 适配设备显示和触摸。
- [`components/app_voice`](components/app_voice/README.md) 处理小智绑定和 AI 键语音对话。
- `main` 负责应用启动和组件初始化。
- `tools/agent_pet.py` 从会话日志提取状态并汇总并行任务。
- `tools/desktop_activity.py` 只读获取 Windows 桌面应用里的实时任务状态。
- `tools/pet_runner.py` 管理单实例、USB 重连和心跳。
- `tools/host_runtime.py` 通过 Mosaico CLI 同步状态，并隐藏子进程窗口。
- `tools/usage_sync.py` 和 `tools/usage_agent` 负责后台账户读取、显示缓存，
  并复用桌宠连接传输用量。
- `tools/install-agent-pet.ps1` 管理当前用户的登录快捷方式、同步启停和状态查询。

[整合验证记录](docs/evidence/codex-usage/README.md)包含测试结果、模拟器截图、
新版助手及已完成的实机验收。

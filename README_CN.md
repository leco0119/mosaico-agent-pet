# Mosaico Agent Pet 桌宠

[English](README.md)

ESP-Mosaico 桌宠会显示最近活跃的 Codex、WorkBuddy、Claude Code 或 PandaCode
会话状态。设备还支持小智语音、Wi-Fi 切换和音量调节。

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

## 目录说明

- `components/app_pet` 包含桌宠 C 控制器、GSP 场景、Iris 消息适配和模拟器后端。
- `components/app_wifi` 负责扫描、连接和保存最近使用的网络。
- `components/app_display` 适配设备显示和触摸。
- [`components/app_voice`](components/app_voice/README.md) 处理小智绑定和 AI 键语音对话。
- `main` 负责应用启动和组件初始化。
- `tools/agent_pet.py` 从会话日志提取智能体状态。
- `tools/pet_runner.py` 管理单实例、USB 重连和心跳。
- `tools/host_runtime.py` 通过 Mosaico CLI 同步状态，并隐藏子进程窗口。
- `tools/install-agent-pet.ps1` 管理当前用户的登录快捷方式、同步启停和状态查询。

# JumpingClock

Windows 11 x64 多时区时钟。通过悬浮窗查看时间，也可以隐藏悬浮窗，只保留系统托盘图标。时区和夏令时规则来自 Windows。

## 构建

需要 Visual Studio Build Tools 的 C++ 桌面开发工具、Windows SDK、CMake 3.25 或更新版本及 Ninja。在 **x64 Native Tools Command Prompt** 中，从仓库目录执行：

```bat
chcp 65001
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

运行 `build\JumpingClock.exe`。Release 静态链接 MSVC 运行时，程序可单独放在固定目录中使用。

## 使用

- 拖动悬浮窗调整位置。右键悬浮窗或托盘图标可打开设置、切换悬浮窗和退出。
- 左键点击托盘图标查看多时区列表，按 Esc 或点击窗外关闭。
- 默认显示北京和洛杉矶。可编辑城市标签、选择 Windows 时区及添加或移除时钟，支持 1–16 个时钟。城市和 UTC 偏移可分别隐藏。
- 首次运行时，登录自启动默认关闭。需要时在设置中勾选「登录后自启动」。移动 EXE 后，可关闭再重新打开此开关来更新启动路径。
- 输入单播 IPv4 地址启用 ICMP 探测，留空即停用。绿点表示目标可达，红点表示连续两次探测无应答，空心点表示未配置或等待，黄点表示探测错误。托盘提示和设置页提供文字状态。ICMP 结果对应指定目标，目标屏蔽 ICMP 时也可能显示无应答。

设置和悬浮窗位置保存在当前用户的 `HKCU\Software\JumpingClock` 中。

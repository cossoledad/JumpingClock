# JumpingClock

Windows 10 22H2 和 Windows 11 x64 多时区时钟。时区和夏令时规则来自 Windows。

## 构建

需要 Visual Studio Build Tools 的 C++ 桌面开发工具、Windows SDK、CMake 3.25 或更新版本及 Ninja。在 **x64 Native Tools Command Prompt** 中，从仓库目录执行：

```bat
chcp 65001
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

运行 `build\JumpingClock.exe`。Release 静态链接 MSVC 运行时，程序可单独放在固定目录中使用。

## 使用

- 拖动调整位置。右键时钟或托盘图标打开菜单。左键点击托盘图标查看时区。
- 支持 1–16 个时钟。城市用英文下拉选择，时区随城市更新。可隐藏城市或 UTC 偏移。
- 先选中一行，再填写该行的 IPv4。留空时该行无状态点。绿点表示可达，红点表示连续两次无应答，空心点表示等待，黄点表示探测失败。相同地址共用一次探测，托盘显示总体状态。
- 城市、时区、IPv4 和不透明度即时预览。「取消」还原，「保存」保留设置。登录自启在保存时写入 Windows，默认关闭。移动 EXE 后可关闭再打开自启，更新路径。

配置保存在当前用户的 `HKCU\Software\JumpingClock`。升级时，旧版全局 IPv4 会分配到已有时钟行。旧版自定义城市名可以保留，也可改用下拉中的英文名。

## GitHub Actions

推送或提交 PR 后，工作流在 Windows Server 2022 和 2025 上构建 x64 Release，运行迁移、时区和回环探测测试，并上传 EXE。Windows 10 和 11 的桌面验收需在对应系统上运行，重点检查托盘、DPI、登录自启和睡眠恢复。

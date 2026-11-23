# JumpingClock

Windows 多时区悬浮时钟，支持为每个时钟探测 IPv4 连通性。

[下载 JumpingClock.exe](https://github.com/cossoledad/JumpingClock/releases/latest/download/JumpingClock.exe) · Windows 10 22H2 / 11 x64

## 构建

使用 MSVC、Windows SDK、CMake 3.25+ 和 Ninja。在 **x64 Native Tools Command Prompt** 中运行。

```bat
chcp 65001
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

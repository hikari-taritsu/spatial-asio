# Spatial ASIO

简体中文 Windows 空间音频工作台：**原生对象捕获 → OAR 点声源渲染 → ASIO 多声道输出**。

![工作台](docs/workspace.png)

这是独立的新项目：保留项目作者的 Spatial Pip 捕获组件与本地开发的点声源/ASIO 实现，重新编写桌面界面、配置、队列、程序入口及安装流程。没有 spatial-audio-lab 的原有程序代码、驱动、电影播放器或解码器。依赖的第三方组件见 [许可清单](THIRD-PARTY-NOTICES.md)。

## 下载与运行

从 [Releases](https://github.com/hikari-taritsu/spatial-asio/releases) 下载 Windows x64 安装包。内含 .NET 10 桌面运行时、Python 3.12.10 与 Frida 17.23.3，运行时无需联网下载。

1. 安装声卡/音频接口厂商的 **64 位 ASIO 驱动**。
2. 安装并启动 Spatial ASIO，允许程序以管理员身份运行。
3. 选择 ASIO 驱动、扬声器布局，在“声道路由”中逐一核对物理输出。默认输出音量为 −12 dB。
4. 在**游戏实际使用的 Windows 播放设备**上启用 Windows Sonic，然后重新启动支持 Windows Spatial Audio 的游戏。游戏的播放端点与最终 ASIO 输出是两个不同的选择。
5. 点击“检查捕获兼容性”，通过后“开始捕获”。观察“正在接收对象”与输出电平。进程运行但没有空间对象时，界面明确显示等待。
6. 退出前点击“停止捕获”，让程序恢复音频宿主挂钩并关闭 ASIO。

不安装虚拟音频驱动。ASIO 设备自身仍需要厂商驱动。不会注入游戏进程，也不会自动修改 Windows 安全设置。

## 兼容范围与当前限制

**这是预览版本，不能宣称覆盖所有 Windows 10/11 系统。**

| 项目 | 范围 |
|---|---|
| 安装器/桌面目标 | Windows 10 2004+、Windows 11，原生 x64；本机验证，未做完整系统矩阵测试 |
| 原生捕获 | 仅允许已验证的 `audiodg.exe` 哈希；详见 [兼容性](docs/COMPATIBILITY.md) |
| 输入 | Windows 基础空间对象，48 kHz float32，最多 160 槽/流，最多 16 流 |
| 输出 | 一个 64 位 ASIO 驱动；声道映射、缓冲、采样率、总音量与逐声道微调 |
| 布局 | 9.1.6、7.1.4、7.1.2、5.1.4、5.1.2、5.1、2.0 |
| 渲染 | OAR 点声源；位置、增益、生命周期；512 帧前视 |
| 其他布局 | 从 9.1.6 规范总线进行固定语义折叠，未声称与官方布局渲染等价 |

不提供 WDM/WASAPI 输出、虚拟端点、ADM/WAV/电影文件输入、MAT/DTS/JOC/TrueHD 解码或其他渲染器。对象尺寸、扩散、个性化 HRTF 不在实现范围。ASIO 整数/浮点 PCM 受支持，DSD 不支持。非 48 kHz 输出采用线性采样率转换，48 kHz 是推荐路径。

一个音频宿主中的多个应用空间流会合并捕获；尚无逐游戏选择。不同流依靠时间戳近似对齐，不承诺跨应用逐样本同步。Windows 更新可能改变捕获 ABI；未知版本会拒绝捕获，不会猜测地址。受保护的音频宿主不支持。

## 构建

需要 Windows x64、Visual Studio 2022 C++ Build Tools、Windows SDK、CMake 3.24+、.NET 10 SDK；创建安装包另需 Inno Setup 6.5+。

```powershell
powershell -ExecutionPolicy Bypass -File tools/Build.ps1 -Package
```

首次构建从 Python/PyPI 官方站点下载固定版本，校验 SHA-256。产物在 `dist/`，应用在 `dist/app/`。安装包保留用户配置于 `%LOCALAPPDATA%\SpatialASIO`，卸载不删除该目录。构建不依赖旧项目或商业音频软件。

## 许可与验证

项目代码采用 **GPL-3.0-only**；ASIO SDK 使用其 GPLv3 授权选项。第三方组件保留各自许可。没有商业 Dolby 二进制、参考音乐或游戏内容。

[测试说明](docs/VALIDATION.md) · [实现与来源](docs/PROVENANCE.md) · [兼容性](docs/COMPATIBILITY.md)

本项目并非 Dolby 官方产品，未经 Dolby 认证。点声源模型的局部参考一致性，不等于完整 Dolby 渲染器的一致性。

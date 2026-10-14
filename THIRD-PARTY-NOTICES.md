# 第三方许可

Spatial ASIO 项目代码与项目作者提供的 Spatial Pip 组件按根目录 GPL-3.0-only 发布。以下第三方材料保留其原始归属与许可。

| 组件 | 许可/用途 | 来源 |
|---|---|---|
| Steinberg ASIO SDK 接口头文件 | 选择 GPLv3 分支；接口定义 | https://www.steinberg.net/developers/ |
| Python 3.12.10 embedded x64 | PSF License；捕获宿主运行时 | https://www.python.org/downloads/release/python-31210/ |
| Frida 17.23.3 未修改 Windows wheel | wxWindows Library Licence 3.1；用户态捕获 | https://github.com/frida/frida/tree/17.23.3 |
| .NET / Windows Desktop 10.0.8 | MIT 及各自第三方 notices；桌面运行时 | https://github.com/dotnet/runtime / https://github.com/dotnet/wpf |
| Inno Setup 安装器与简体中文消息 | Inno Setup License；安装器 | https://github.com/jrsoftware/issrc |
| Source Han Sans CN Regular 2.005 | SIL Open Font License 1.1；内置简体中文界面字体 | https://github.com/adobe-fonts/source-han-sans/tree/2.005R |

ASIO 许可原文：`third_party/asio/LICENSE.txt`（源码）、`Licenses/ASIO.txt`（安装版）。
Python 许可：`Tools/NativeSpatial/python/LICENSE.txt`。
Frida 许可：`Tools/NativeSpatial/deps/frida-17.23.3.dist-info/licenses/COPYING`，该版本源代码与构建脚本可从上方对应标签获得。
.NET 许可与第三方 notices 随安装版置于 `Licenses/`。安装器中文消息的作者与来源保留在 `packaging/languages/ChineseSimplified.isl`。

思源黑体原文件来自 Adobe 官方仓库，提交 `6c709ca72d3d7c46ab42ebecc1a26e7d69595a37`，`SubsetOTF/CN/SourceHanSansCN-Regular.otf`，未修改；SHA-256 `e2bc8a2e7f37474b774fff8db758681ece40bb6947a90d571bce9dd60671a8e4`。许可位于 `third_party/SourceHanSans-LICENSE.txt` 和安装版 `Licenses/SourceHanSans-LICENSE.txt`。英文字标使用系统自带 Bahnschrift，不分发该字体文件。

ASIO 是 Steinberg Media Technologies GmbH 的商标。本项目没有以任何第三方品牌作背书，也不包含音频接口厂商驱动。

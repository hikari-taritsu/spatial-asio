# 实现来源

项目作者确认 Spatial Pip 是其自行编写的组件，并要求新程序不保留 spatial-audio-lab 原有代码。

本仓库仅保留在此前本地开发中新增的 ASIO 后端、PCM 格式转换、基础对象捕获适配、点声源几何与元数据时间处理实现。它们曾在本地 spatial-audio-lab 工作区中开发，但并非该上游原有实现。

配置读取、音频工作队列、Windows 资源封装、程序入口、运行控制、遥测、WPF 界面、安装/构建脚本均为新编写。没有复制上游 `audio_platform.cpp`、`multi_endpoint_renderer.cpp`、`speaker_layout.cpp` 的原始解析/环形分配部分，未包含它的 Core/Studio/Cinema/driver。

点声源几何从本地新增的 `PanAdmOarPoint` 数学实现提取；时间模型仍使用此前经参考实验校准的平滑、量化与分段增益算法。研究参考涉及本地音频渲染结果与 CPU 行为探测。发布内容没有 Studio One/Dolby 二进制、反汇编清单、专有代码片段或参考音乐。

本仓库中的可选 `OracleCheck` 仅接受开发者自行指定的本地测试 DLL，默认构建/运行/安装都不需要它；它没有链接或分发任何旧项目或商业 DLL。

第三方 ASIO 接口定义和捕获/桌面运行时仍有各自作者，独立项目不等于没有第三方依赖。

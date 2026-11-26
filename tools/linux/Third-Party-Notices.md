# Linux SDK 第三方组件

- Qt 6.11.1 的 Qt Base 模块和 OpenSSL TLS 插件采用 LGPL-3.0 许可选项。兼容的 Qt 共享库可替换。源码可从 [Qt 6.11.1 官方归档](https://download.qt.io/archive/qt/6.11/6.11.1/single/) 获取。Qt 的 LGPL 正文和引用的 GPL 正文随包提供。
- FFmpeg 7.1.3 采用 LGPL-2.1-or-later 构建。源码可从 [FFmpeg 7.1.3 官方归档](https://ffmpeg.org/releases/ffmpeg-7.1.3.tar.xz) 获取。许可证随包提供。
- TLS 插件在运行时使用 Ubuntu 的 OpenSSL 3（`libssl3`），遵循系统软件包的许可及安全更新。

SDK 采用动态链接和运行期加载。再分发时携带对应许可，并保留客户替换兼容组件和调试替换结果的权利。

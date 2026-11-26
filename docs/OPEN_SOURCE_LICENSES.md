# 第三方组件与许可边界

SDK 运行库适用根目录产品许可。C、C++ 和 Qt Viewer 示例源码及示例构建脚本适用 [MIT 许可](../tools/sdk_package/Example.LICENSE)。客户可以修改示例并用于商业应用。公开接口没有 Qt 类型。

交付不使用 GPL-only 的 Qt Quick 3D。点云由 Qt Widgets 绘制。Qt Core、Gui、Widgets、Network、Concurrent、SVG 与图像插件选择 LGPL-3.0；FFmpeg 7.1.3 使用 LGPL-2.1-or-later 构建，组包时读取实际 DLL 的 license/configuration 并拒绝启用 GPL 或 nonfree 的二进制。各库原有著作权与许可继续适用。

完整材料见 [Third-Party-Notices.txt](../tools/installer/Third-Party-Notices.txt)。Qt 和 FFmpeg 采用动态库分发，可替换兼容库。客户 SDK 的 Licenses 目录包含源码获取说明、构建配置、许可正文和 Qt SBOM。工厂工具的维护组件和板端载荷另附相应许可。随附的 GPL 正文属于 LGPL 引用的第三方许可材料。

板端私有实现与本机工具链不进入客户 SDK。设备身份、凭据与运行记录遵守 [源码与本地构建材料](SOURCE_DISTRIBUTION.md) 的边界。

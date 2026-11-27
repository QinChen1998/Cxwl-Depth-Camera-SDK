# 构建与分发

客户 SDK 使用 Windows DLL、头文件、示例、配套 Qt/FFmpeg 运行依赖和许可证。先运行 `tools/installer/build.ps1 -StageOnly` 暂存运行依赖，使用 `tools/build_depth_sdk_docs.ps1` 生成 API 手册，再用 `tools/package_depth_sdk.ps1` 指定 BinaryDirectory、RuntimePayload 和新的 OutputDirectory。文档生成使用 Doxygen 1.9.7 与含 ReportLab 的 Python。

`private_build/windows-device-service` 留在本地开发环境。板端签名包构建使用 `make_board_package.py`，生成于 `x64/factory`。原始固定输入通过已有签名验证。更换板端服务后应在授权签发工位完成签名。私钥保存在项目外的签发库，设备登记档案与实测场景数据独立于源码。工位人员可在本地私有目录打开 `工位操作说明.md`。

当前固定地址 USB 直连镜像、签名载荷、制镜像源码和验收记录由本机工厂项目保存。固件完整性签名与客户首次 USB 登记分别处理。以下命令仅用于归档的 `1.0.0-preview.1` 工厂程序，使用装有 cryptography、Tk 和 PyInstaller 6.16 的 Python。

```powershell
$factoryPython = (Get-Command python).Source
& $factoryPython -B private_build/windows-device-service/make_board_package.py
./private_build/windows-device-service/build_factory.ps1 -Python $factoryPython
$p = Start-Process ./x64/factory/CXWLFactory.exe -ArgumentList '--verify-packages --report x64/validation/factory.json' -Wait -PassThru -WindowStyle Hidden
if ($p.ExitCode) { throw '工装签名自检失败' }
```

## Windows SDK 本地交付

从项目根目录使用 PowerShell 7。先按 [测试说明](TESTING.md) 完成源码离线回归。将 `$qt` 设置为 Qt 6.11.1 MSVC x64 套件目录，将 `$pdfPython` 设置为安装了 ReportLab 的 Python 可执行文件。版本从产品清单读取。每次运行选用新的目录，保留已有交付。

```powershell
$ErrorActionPreference = 'Stop'
$version = (Get-Content sdk/product.json -Raw | ConvertFrom-Json).sdk_version
$run = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$work = Join-Path $PWD "x64/validation/sdk-$run"
$stage = Join-Path $work 'production'
$doc = Join-Path $work 'api'
$trial = Join-Path $work 'trial-sdk'
$final = Join-Path $PWD "output/Windows-SDK-$version-$run"
./tools/installer/build.ps1 -BuildDirectory $stage -QtRoot $qt -Version $version -StageOnly
./tools/build_depth_sdk_docs.ps1 -OutputDirectory $doc -PdfPython $pdfPython
$inputs = @{ BinaryDirectory = "$stage/desktop"; RuntimePayload = "$stage/payload"; DocumentationDirectory = $doc; QtRoot = $qt }
./tools/package_depth_sdk.ps1 @inputs -OutputDirectory $trial -ExampleBuildDirectory "$work/examples-trial"
```

打包脚本会从随包源码编译七个 C/C++ 示例和 Qt Viewer，并执行 SDK 自检和 ABI 回归。用一台相机和此次试制包的 DLL 完成单机实测。将 `$profilePath` 指向授权使用的临时登记副本。使用随包 PointCloud 和 CppQuickStart 样例核对 PLY、深度图和点数。

```powershell
python -B tools/validate_sdk_live_outputs.py --runtime "$trial/SDK/bin" --registry $profilePath --output "$work/camera" --all-modes --transitions --seconds 2
if ($LASTEXITCODE) { throw '实机验收失败' }
New-Item -ItemType Directory -Force "$doc/measurements" | Out-Null
Copy-Item -LiteralPath "$work/camera/summary.json" -Destination "$doc/measurements/sdk-$version-outputs.json"
./tools/package_depth_sdk.ps1 @inputs -OutputDirectory $final -ExampleBuildDirectory "$work/examples-final"
$receipt = Get-Content "$final/Doc/build-validation.json" -Raw | ConvertFrom-Json
if (!$receipt.hardware_connected_for_validation) { throw '实机报告未匹配成品运行库' }
& "$final/Script/verify-sdk.ps1" -Root $final -Strict
if ($LASTEXITCODE) { throw '成品清单校验失败' }
python -B tools/validate_sdk_package.py --root $final --output "$work/package-check.json"
if ($LASTEXITCODE) { throw '成品内容检查失败' }
```

最终只压缩 `$final` 客户目录。将 ZIP 解压到新的临时目录，在解压后的包根再次运行 `./Script/verify-sdk.ps1 -Strict`，并记录 ZIP 的 SHA-256。缺少实机或登记档案时仍可完成试制包和离线校验，交付记录须注明实机待验。正式发布需另行授权。

## Linux SDK 本地交付

参照 [Linux SDK 接入说明](DEPTH_SDK_LINUX.md) 分别暂存 x64、ARM64 运行库和七个样例。每个架构独立完成 [离线及实机验收](TESTING.md#linux-x86-64-与-arm64) 后，再用原样运行目录生成对应的 `.tar.gz` 和 `.sha256`。

```bash
python3 -B tools/linux/package.py --arch x64 --stage "$stage" \
  --offline "$work/offline/summary.json" --live "$work/live/camera/summary.json" \
  --samples "$work/live/samples.json" --qt-licenses "$qtbase/LICENSES" \
  --output "$output"
```

ARM64 传入 `--arch arm64` 及该架构的记录。`tools/linux/build_docs.py` 生成该架构的离线 API 手册。可用 `--manual` 与 `--qt-sbom` 加入手册和 Qt SBOM，用 `--windows-reference` 核对已解压 Windows x64 成品的三个公开头文件和 DLL 导出。制包脚本核对架构、ABI、完整运行时和样例的摘要，再解压归档复核包内 `SHA256SUMS`。随包提供统计与验收回执，设备档案、主机授权及原始采集留在本地验证目录。

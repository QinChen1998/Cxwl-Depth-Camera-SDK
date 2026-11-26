param([switch]$Check)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$product = Get-Content -LiteralPath (Join-Path $project 'sdk/product.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($product.sdk_version -notmatch '^(\d+)\.(\d+)\.(\d+)$') { throw 'Invalid SDK semantic version.' }
$major, $minor, $patch = $Matches[1], $Matches[2], $Matches[3]
$text = @"
/** @file DepthSdkVersion.h
 * @brief 版本与 ABI 宏。由 sdk/product.json 生成。
 */
#ifndef CXWL_DEPTH_SDK_VERSION_H
#define CXWL_DEPTH_SDK_VERSION_H
/** SDK 语义版本字符串。 */
#define CXWL_DEPTH_VERSION "$($product.sdk_version)"
/** SDK 主版本号。 */
#define CXWL_DEPTH_VERSION_MAJOR $major
/** SDK 次版本号。 */
#define CXWL_DEPTH_VERSION_MINOR $minor
/** SDK 修订版本号。 */
#define CXWL_DEPTH_VERSION_PATCH $patch
/** 当前 C 接口的 ABI 版本。首次创建句柄前与 CxwlDepth_AbiVersion() 比较。 */
#define CXWL_DEPTH_ABI_VERSION $($product.abi_version)u
#define CXWL_DEPTH_FILE_VERSION $major,$minor,$patch,0
#define CXWL_DEPTH_FILE_VERSION_STRING "$($product.sdk_version).0"
#define CXWL_DEPTH_PRODUCT_NAME "$($product.product)"
#define CXWL_DEPTH_RELEASE_DATE "$($product.release_date)"
#define CXWL_DEPTH_COPYRIGHT "Copyright (C) 2026 RGBD Depth Camera SDK authors"
#endif
"@ + "`n"
$path = Join-Path $project 'sdk/DepthSdkVersion.h'
$current = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllText($path) } else { '' }
if ($current -cne $text) {
    if ($Check) { throw 'Regenerate sdk/DepthSdkVersion.h from sdk/product.json.' }
    [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
}

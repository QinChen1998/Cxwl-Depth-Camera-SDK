#pragma once

#include <DepthSdk.h>
#include <QtCore/QString>
#include "../tools/rv_capture/Contract.h"

namespace RvDtofCandidates {
struct ImagingScheme {
    uint32_t sdkMode;
    int boardMode;
    const char* name;
    bool highAccuracy;
};
inline constexpr ImagingScheme ImagingSchemes[] = {
    {CXWL_DEPTH_BOARD, RvCaptureContract::Native48, "board_native_tof48", false},
    {CXWL_DEPTH_BOARD_HIGH_ACCURACY, RvCaptureContract::Native48HighAccuracy, "board_native_tof48_high_accuracy", true},
    {CXWL_DEPTH_BOARD_8X8, RvCaptureContract::Native8, "board_native_tof8", false},
    {CXWL_DEPTH_BOARD_8X8_HIGH_ACCURACY, RvCaptureContract::Native8HighAccuracy, "board_native_tof8_high_accuracy", true},
    {CXWL_DEPTH_BOARD_16X16, RvCaptureContract::Native16, "board_native_tof16", false},
    {CXWL_DEPTH_BOARD_16X16_HIGH_ACCURACY, RvCaptureContract::Native16HighAccuracy, "board_native_tof16_high_accuracy", true},
};
inline const ImagingScheme* schemeForSdkMode(uint32_t mode) {
    for (const auto& scheme : ImagingSchemes)
        if (scheme.sdkMode == mode) return &scheme;
    return nullptr;
}
inline const ImagingScheme* schemeForName(const QString& name) {
    for (const auto& scheme : ImagingSchemes)
        if (name == QLatin1String(scheme.name)) return &scheme;
    return nullptr;
}
}

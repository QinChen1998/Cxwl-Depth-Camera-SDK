#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

// Shared by the camera capture programs and stream receivers.
namespace RvCaptureContract {
constexpr int NativeHd=14, Native48=24, Native48HighAccuracy=25;
constexpr int Native8=30, Native8HighAccuracy=31, Native16=32, Native16HighAccuracy=33;
constexpr int FreshMs=1000;
// These LSG receipt fields are independent of the native measurement grid.
constexpr int ReceiptWidth=256, ReceiptHeight=256;
constexpr char NativeEngine[]="native-hd-capture-v1";
constexpr const char* Protocol="LSG1";
constexpr const char* PairedProtocol="LSG2";
constexpr const char* NativeCaptureProtocolV2="native-m48-m16-m8-v2";
constexpr char CandidateRecoveryProtocol[]="clean-candidate-restart-v1";
constexpr uint32_t RequestMagic=0x3151534c, ResponseMagic=0x3147534c;
constexpr uint32_t PairedRequestMagic=0x3251534c, PairedResponseMagic=0x3247534c;
constexpr int Native48Columns=48, Native48Rows=32;

// LSG2 clients request organized XYZ in addition to the original native cells.
constexpr uint32_t RequestOrganizedXyz = 1;
constexpr char PointCloudFormat[] = "xyz-f32le-metres";
struct Point { float x, y, z; };
static_assert(sizeof(Point) == 12, "Packed XYZ wire format");

struct Preset {
    int mode, sensorMode, columns, rows;
};
inline constexpr Preset ProductPresets[] = {
    {Native48, 11, Native48Columns, Native48Rows},
    {Native48HighAccuracy, 12, Native48Columns, Native48Rows},
    {Native8, 0, 8, 8},
    {Native8HighAccuracy, 2, 8, 8},
    {Native16, 5, 16, 16},
    {Native16HighAccuracy, 6, 16, 16},
};
inline constexpr Preset LegacyNativeHd{NativeHd, 5, 16, 16};

inline constexpr const Preset* productPreset(int mode) {
    for (const auto& preset : ProductPresets)
        if (preset.mode == mode) return &preset;
    return nullptr;
}
inline constexpr bool supportsProductMode(int mode) { return productPreset(mode) != nullptr; }
inline constexpr bool supportsProductGrid(unsigned columns, unsigned rows) {
    for (const auto& preset : ProductPresets)
        if (unsigned(preset.columns) == columns && unsigned(preset.rows) == rows) return true;
    return false;
}
inline constexpr const Preset& presetForMode(int mode) {
    const auto* preset = productPreset(mode);
    return preset ? *preset : LegacyNativeHd;
}
inline bool supports(int mode) { return mode == NativeHd || supportsProductMode(mode); }
inline int columns(int mode) { return presetForMode(mode).columns; }
inline int rows(int mode) { return presetForMode(mode).rows; }
inline int sensorMode(int mode) { return presetForMode(mode).sensorMode; }
inline void validate(int mode,const std::string& token,const std::string& options){
    if(!supports(mode)||token.size()!=32||token.find_first_not_of("0123456789abcdef")!=std::string::npos
       ||options!="1,1")throw std::runtime_error("Invalid board imaging session");
}
}

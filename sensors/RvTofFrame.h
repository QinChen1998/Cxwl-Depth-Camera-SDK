#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaType>
#include <QtCore/QtEndian>
#include <QtCore/QRegularExpression>
#include <cmath>
#include <cstring>
#include "../tools/rv_capture/Contract.h"

namespace RvCaptureContract {
inline bool boardImagingIdentity(const QJsonObject& m){
    const int mode=m["candidate_mode"].toInt(-1);
    return supportsProductMode(mode)&&m["engine"]==NativeEngine&&m["model_sha256"]==""
        &&m["capture_only"]==true&&m["inference_location"]=="none"
        &&m["depth_present"]==false&&m["photon_enabled"]==false
        &&m["width"]==ReceiptWidth&&m["height"]==ReceiptHeight&&m["fresh_ms"]==FreshMs;
}
}

// Native measurements in sensor order.
// One cell: range_uq2 uint16 LE (0.25 mm), first-peak SNR uint8,
// valid uint8 (0 or 1), copied from an unchanged native IPC measurement.
// Native radial range, not camera-Z, a histogram, or all multiple peaks.
struct RvTofFrame {
    QByteArray samples;
    QJsonObject metadata;
    QByteArray boardPoints; // Organized, same-measurement XYZ in metres.
    int columns() const { return metadata["dtof_width"].toInt(); }
    int rows() const { return metadata["dtof_height"].toInt(); }
    int sampleCount() const { return samples.size()/4; }
    bool isValid() const {
        const auto integer = [this](const char *key, double minimum) {
            const auto v = metadata[key]; const double n = v.toDouble(-1);
            return v.isDouble() && std::isfinite(n) && n >= minimum
                && n <= 9007199254740991.0 && std::floor(n) == n;
        };
        const bool candidate = metadata["work_mode"] == "dtof_candidate"
            && metadata["protocol"] == RvCaptureContract::PairedProtocol
            && RvCaptureContract::boardImagingIdentity(metadata)
            && QRegularExpression("^[0-9a-f]{32}$").match(metadata["generation"].toString()).hasMatch()
            && QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$").match(metadata["boot_id"].toString()).hasMatch()
            && metadata["candidate_mode"].isDouble() && integer("candidate_mode",0);
        const auto* preset = RvCaptureContract::productPreset(metadata["candidate_mode"].toInt(-1));
        if (!preset || metadata["dtof_width"] != preset->columns || metadata["dtof_height"] != preset->rows
            || samples.size() != preset->columns * preset->rows * 4
            || metadata["dtof_format"] != "range_uq2_u16le_snr_u8_valid_u8"
            || metadata["dtof_range_unit"] != "0.25_mm"
            || metadata["dtof_source"] != "tmf8829_ipc_first_peak"
            || metadata["dtof_bytes"].toInt() != samples.size()
            || metadata["dtof_coordinate"] != "sensor_radial_native"
            || !integer("frame_id",1) || !candidate
            || !integer("tof_sequence",1) || !integer("tof_timestamp_ns",1)
            || metadata["tof_reused"] != false) return false;
        for (int i=0; i<sampleCount(); ++i) if (quint8(samples[i*4+3])>1) return false;
        return true;
    }
    quint16 rangeUq2(int index) const { return qFromLittleEndian<quint16>(samples.constData()+index*4); }
    double distanceMm(int index) const { return rangeUq2(index)*0.25; }
    quint8 snr(int index) const { return quint8(samples[index*4+2]); }
    bool valid(int index) const { return samples[index*4+3] == 1; }
    QByteArray radialMetres() const {
        if(!isValid())return {};
        QByteArray result(samples.size(),0);
        for(int i=0;i<sampleCount();++i){
            const float metres=valid(i)?float(rangeUq2(i))*.00025f:0.0f;
            quint32 bits;std::memcpy(&bits,&metres,4);qToLittleEndian(bits,result.data()+i*4);
        }
        return result;
    }
};
Q_DECLARE_METATYPE(RvTofFrame)

#include "DepthSdk.h"
#include "../tools/rv_capture/Contract.h"
#include <QDataStream>
#include <QSaveFile>
#include <QtEndian>
#include <array>
#include <cmath>
#include <cstring>

CxwlDepthResult cxwlDepthWritePointCloudPly(const CxwlDepthFrame *frame, const char *path)
{
    if (!path || !*path || !QByteArrayView(path).isValidUtf8() || !frame || frame->size < sizeof(*frame))
        return CXWL_DEPTH_INVALID_ARGUMENT;

    std::array<CxwlDepthPoint, CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT> points;
    const auto &image = frame->image;
    if (frame->stream == CXWL_DEPTH_STREAM_POINT_CLOUD && frame->coordinate == CXWL_DEPTH_COORDINATE_SENSOR_XYZ) {
        if (!image.data || image.pixel_format != CXWL_DEPTH_PIXEL_XYZ_F32_LE
            || !RvCaptureContract::supportsProductGrid(image.width, image.height)) return CXWL_DEPTH_INVALID_ARGUMENT;
        const uint32_t rowBytes = image.width * sizeof(CxwlDepthPoint);
        if (image.row_stride_bytes < rowBytes
            || image.byte_count < uint64_t(image.height-1) * image.row_stride_bytes + rowBytes) return CXWL_DEPTH_INVALID_ARGUMENT;
        for (uint32_t row = 0; row < image.height; ++row)
            std::memcpy(points.data() + row * image.width,
                        static_cast<const char *>(image.data) + uint64_t(row) * image.row_stride_bytes, rowBytes);
    } else return CXWL_DEPTH_INVALID_ARGUMENT;

    QByteArray vertices;
    vertices.reserve(image.width * image.height * 3 * sizeof(float));
    QDataStream stream(&vertices, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    uint32_t count = 0;
    for (uint32_t i = 0; i < image.width * image.height; ++i) {
        const auto &point = points[i];
        if (!std::isfinite(point.x_m) || !std::isfinite(point.y_m) || !std::isfinite(point.z_m)
            || point.z_m < 0 || (point.z_m == 0 && (point.x_m != 0 || point.y_m != 0))) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (point.z_m == 0) continue;
        stream << point.x_m << point.y_m << point.z_m;
        ++count;
    }
    const QByteArray header = "ply\nformat binary_little_endian 1.0\ncomment coordinates tof_sensor metres\n"
        "comment width " + QByteArray::number(image.width) + "\ncomment height " + QByteArray::number(image.height)
        + "\ncomment mode " + QByteArray::number(frame->mode)
        + "\ncomment session_epoch " + QByteArray::number(frame->session_epoch)
        + "\ncomment sequence " + QByteArray::number(frame->sequence)
        + "\ncomment timestamp_ns " + QByteArray::number(frame->timestamp_ns)
        + "\ncomment clock_domain " + QByteArray::number(frame->clock_domain)
        + "\nelement vertex " + QByteArray::number(count) + "\nproperty float x\nproperty float y\nproperty float z\nend_header\n";
    QSaveFile file(QString::fromUtf8(path));
    if (stream.status() != QDataStream::Ok || !file.open(QIODevice::WriteOnly)
        || file.write(header) != header.size() || file.write(vertices) != vertices.size() || !file.commit())
        return CXWL_DEPTH_UNAVAILABLE;
    return CXWL_DEPTH_OK;
}

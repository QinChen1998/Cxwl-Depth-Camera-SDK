#include "../sensors/RvDtofBoardStream.h"
#include <QtCore/QEventLoop>
#include <QtNetwork/QTcpServer>
#include <cstdio>

namespace {
struct Exchange {
    quint64 cursor;
    int sequence;
    int timestamp;
    int boardAgeMs;
};

QJsonObject receipt()
{
    using namespace RvCaptureContract;
    return {{"protocol", Protocol}, {"boot_id", "12345678-1234-1234-1234-123456789abc"},
            {"generation", QString(32, 'a')}, {"candidate_mode", Native8}, {"options", "1,1"},
            {"engine", NativeEngine}, {"model_sha256", ""}, {"capture_only", true},
            {"inference_location", "none"}, {"depth_present", false}, {"photon_enabled", false},
            {"width", ReceiptWidth}, {"height", ReceiptHeight}, {"fresh_ms", FreshMs},
            {"video_transport", "rtsp_h265"}, {"video_width", 2304}, {"video_height", 1296},
            {"video_fps", 30}, {"video_kbps", 4096}, {"native_capture_protocol", NativeCaptureProtocolV2},
            {"native_columns", 8}, {"native_rows", 8}, {"point_cloud_format", PointCloudFormat}};
}

QByteArray reply(QJsonObject meta, int request, const Exchange &sample)
{
    using namespace RvCaptureContract;
    meta["protocol"] = PairedProtocol;
    meta["request"] = request;
    meta["data"] = false;
    meta["photon_data"] = false;
    meta["rgb_data"] = false;
    meta["raw_data"] = true;
    meta["raw_columns"] = 8;
    meta["raw_rows"] = 8;
    meta["raw_fov_correction"] = 6;
    meta["tof_warning_flags"] = 0;
    meta["raw_sequence"] = QString::number(sample.sequence);
    meta["raw_timestamp_ns"] = QString::number(sample.timestamp);
    meta["raw_age_ms"] = sample.boardAgeMs;
    meta["video_progress_source"] = "venc_info:chn0";
    meta["video_encoded_sequence"] = "1";
    meta["video_retrieved_sequence"] = "1";
    meta["video_observed_ns"] = "1";
    meta["video_age_ms"] = 0;
    const auto json = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    constexpr int rawBytes = 8 * 8 * 4, xyzBytes = 8 * 8 * sizeof(Point);
    QByteArray bytes(36, '\0');
    qToLittleEndian<quint32>(PairedResponseMagic, bytes.data());
    qToLittleEndian<quint32>(quint32(json.size()), bytes.data() + 4);
    qToLittleEndian<quint32>(rawBytes, bytes.data() + 16);
    qToLittleEndian<quint32>(xyzBytes, bytes.data() + 20);
    return bytes + json + QByteArray(rawBytes + xyzBytes, '\0');
}

// Every script ends with an invalid identity. All earlier replies go through
// the real socket, asynchronous decoder, freshness check and frame callback.
bool run(const QList<Exchange> &script, const QList<int> &expectedFrames)
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return false;
    const auto identity = receipt();
    QEventLoop loop;
    QList<int> frames;
    int requests = 0;
    QString failure;
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
        auto *peer = server.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, &loop, [&, peer] {
            if (peer->bytesAvailable() < 32) return;
            const auto req = peer->read(32);
            if (requests >= script.size() ||
                qFromLittleEndian<quint32>(req.constData() + 8) != RvCaptureContract::RequestOrganizedXyz ||
                qFromLittleEndian<quint64>(req.constData() + 16) != script[requests].cursor) {
                loop.quit();
                return;
            }
            const auto &sample = script.at(requests++);
            peer->write(reply(identity, requests, sample));
        });
    });
    RvDtofBoardStream stream(identity, nullptr, "127.0.0.1", server.serverPort());
    stream.rawFrame = [&](const RvTofFrame &frame) { frames.append(frame.metadata["tof_sequence"].toInt()); };
    stream.error = [&](const QString &reason) { failure = reason; loop.quit(); };
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    loop.exec();
    return requests == script.size() && frames == expectedFrames &&
           failure == RvDtofBoardStream::tr("Invalid candidate measurement identity");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QList<Exchange> start{{0, 10, 100, 0}, {10, 11, 110, 230}};
    // A newest read may repeat the dropped frame. Resume ordered reads and
    // reject duplicates there. A later newest read may skip queued frames.
    auto recover = start;
    recover.append({{0, 11, 110, 0}, {11, 12, 120, 0}, {12, 13, 130, 230},
                    {0, 16, 160, 0}, {16, 16, 160, 0}});
    auto changedTimestamp = start;
    changedTimestamp.append({0, 11, 111, 0});
    auto olderSequence = start;
    olderSequence.append({0, 9, 90, 0});
    if (run(recover, {10, 12, 16}) && run(changedTimestamp, {10}) && run(olderSequence, {10})) return 0;
    std::fprintf(stderr, "Depth freshness recovery or sequence validation failed\n");
    return 1;
}

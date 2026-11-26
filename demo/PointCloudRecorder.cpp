#include "PointCloudRecorder.h"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QSemaphore>
#include <QUuid>
#include <utility>

namespace {
bool writeRecordingFile(const QString &path, const QByteArray &bytes)
{
    // The take is already staged. Flush each complete frame to the OS before publishing its name.
    QTemporaryFile file(path + ".XXXXXX");
    if (!file.open() || file.write(bytes) != bytes.size() || !file.flush() || !file.rename(path))
        return false;
    file.setAutoRemove(false);
    return true;
}
}

struct PointCloudRecorder::EncodedRgb {
    QSemaphore ready;
    QByteArray jpeg;
};

PointCloudRecorder::PointCloudRecorder()
{
    m_encoders.setMaxThreadCount(qBound(2, QThread::idealThreadCount() / 2, 4));
    connect(this, &QThread::finished, this, [this] {
        wait();
        m_state = State::Idle;
        emit completed(m_saved, m_savedRgb, m_resultDirectory, m_error);
        emit stateChanged();
    }, Qt::QueuedConnection);
}

PointCloudRecorder::~PointCloudRecorder()
{
    stopRecording();
    wait();
}

QString PointCloudRecorder::startRecording(const QString &parentDirectory, bool includeDepth, bool includeRgb)
{
    if (m_state != State::Idle) return tr("A recording is already in progress.");
    const QDir parent(parentDirectory);
    const auto stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    const auto name = "pointcloud-" + stamp + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_finalDirectory = parent.absoluteFilePath(name);
    m_partialDirectory = m_finalDirectory + ".partial";
    if (parentDirectory.isEmpty() || !parent.mkdir(name + ".partial"))
        return tr("Cannot create the recording folder in %1.").arg(parentDirectory);
    QDir staging(m_partialDirectory);
    if ((includeDepth && !staging.mkdir("depth")) || (includeRgb && !staging.mkdir("rgb"))) {
        staging.rmdir("depth");
        QDir().rmdir(m_partialDirectory);
        return tr("Cannot create the recording folder in %1.").arg(m_partialDirectory);
    }
    m_includeDepth = includeDepth;
    m_includeRgb = includeRgb;
    m_saved = m_savedRgb = 0;
    m_bufferedBytes = 0;
    m_clock.start();
    m_error.clear();
    m_resultDirectory.clear();
    m_stop = false;
    m_state = State::Recording;
    start();
    emit stateChanged();
    return {};
}

void PointCloudRecorder::stopRecording()
{
    if (m_state != State::Recording) return;
    {
        QMutexLocker lock(&m_mutex);
        m_stop = true;
        m_available.wakeOne();
    }
    m_state = State::Saving;
    emit stateChanged();
}

void PointCloudRecorder::appendPointCloud(const CxwlDepthFrame &cloud, const QByteArray &xyz, const QByteArray &depth)
{
    if (m_state != State::Recording) return;
    Sample sample{cloud, xyz, m_includeDepth ? depth : QByteArray{}, m_clock.nsecsElapsed(), {}};
    sample.frame.image.byte_count = uint64_t(xyz.size());
    sample.frame.image.row_stride_bytes = cloud.image.width * sizeof(CxwlDepthPoint);
    enqueue(std::move(sample));
}

void PointCloudRecorder::appendRgb(const CxwlDepthFrame &frame, const QImage &image)
{
    if (m_state == State::Recording && m_includeRgb)
        enqueue({frame, {}, {}, m_clock.nsecsElapsed(), {}}, image);
}

void PointCloudRecorder::enqueue(Sample sample, const QImage &rgb)
{
    QMutexLocker lock(&m_mutex);
    if (m_stop) return;
    constexpr qsizetype maxBufferedBytes = 128 * 1024 * 1024;
    constexpr qsizetype maxQueuedFrames = 300;
    const qsizetype bytes = sample.xyz.size() + sample.depth.size() + rgb.sizeInBytes();
    if (m_queue.size() >= maxQueuedFrames || bytes > maxBufferedBytes - m_bufferedBytes) {
        m_error = tr("The disk writer could not keep up with capture.");
        lock.unlock();
        stopRecording();
        return;
    }
    // Workers use owned pixels. No borrowed SDK pointer crosses threads.
    sample.frame.device_id_utf8 = nullptr;
    sample.frame.capture_generation_utf8 = nullptr;
    sample.frame.metadata_json_utf8 = nullptr;
    sample.frame.image.data = nullptr;
    m_bufferedBytes += bytes;
    if (!rgb.isNull()) {
        // Full-resolution JPEG encoding runs concurrently. The writer still publishes in callback order.
        sample.encoded = std::make_shared<EncodedRgb>();
        m_encoders.start([this, image = rgb, encoded = sample.encoded]() mutable {
            const auto rawBytes = image.sizeInBytes();
            QBuffer buffer(&encoded->jpeg);
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG", 90)) encoded->jpeg.clear();
            buffer.close();
            image = {};
            {
                QMutexLocker lock(&m_mutex);
                // A completed JPEG no longer holds a full RGB frame while it waits for disk.
                m_bufferedBytes += encoded->jpeg.size() - rawBytes;
            }
            encoded->ready.release();
        });
    }
    m_queue.enqueue(std::move(sample));
    m_available.wakeOne();
}

QString PointCloudRecorder::writeSample(const QDir &staging, Sample &sample, QFile &index)
{
    auto &frame = sample.frame;
    const bool video = bool(sample.encoded);
    const auto stem = QStringLiteral("frame-%1").arg((video ? m_savedRgb : m_saved) + 1, 8, 10, QLatin1Char('0'));
    const QString name = video ? "rgb/" + stem + ".jpg" : stem + ".ply";
    if (video) {
        sample.encoded->ready.acquire();
        const auto &jpeg = sample.encoded->jpeg;
        if (jpeg.isEmpty() || !writeRecordingFile(staging.filePath(name), jpeg)) return name;
        ++m_savedRgb;
    } else {
        const QString depthName = "depth/" + stem + ".f32";
        if (m_includeDepth) {
            if (size_t(sample.depth.size()) != size_t(frame.image.width) * frame.image.height * sizeof(float) ||
                !writeRecordingFile(staging.filePath(depthName), sample.depth)) return depthName;
        }
        frame.image.data = sample.xyz.constData();
        if (CxwlDepth_SavePointCloudPly(&frame, staging.filePath(name).toUtf8().constData()) != CXWL_DEPTH_OK) {
            if (m_includeDepth) QFile::remove(staging.filePath(depthName));
            return name;
        }
        ++m_saved;
    }
    if (index.isOpen()) {
        const auto row = name.toUtf8() + ',' + QByteArray::number(frame.mode) + ',' + QByteArray::number(frame.session_epoch)
            + ',' + QByteArray::number(frame.sequence) + ',' + QByteArray::number(frame.timestamp_ns)
            + ',' + QByteArray::number(frame.clock_domain) + ',' + QByteArray::number(sample.elapsedNs) + '\n';
        if (index.write(row) != row.size()) return QStringLiteral("frames.csv");
    }
    return {};
}

void PointCloudRecorder::run()
{
    const QDir staging(m_partialDirectory);
    QFile index(staging.filePath("frames.csv"));
    const QByteArray header("file,mode,session_epoch,sequence,timestamp_ns,clock_domain,host_elapsed_ns\n");
    QString failedFile;
    if (m_includeRgb && (!index.open(QIODevice::WriteOnly) || index.write(header) != header.size()))
        failedFile = QStringLiteral("frames.csv");
    while (failedFile.isEmpty()) {
        Sample sample;
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.isEmpty() && !m_stop) m_available.wait(&m_mutex);
            if (m_queue.isEmpty()) break;
            sample = m_queue.dequeue();
        }
        failedFile = writeSample(staging, sample, index);
        QMutexLocker lock(&m_mutex);
        m_bufferedBytes -= sample.xyz.size() + sample.depth.size();
        if (sample.encoded) m_bufferedBytes -= sample.encoded->jpeg.size();
    }
    if (index.isOpen()) {
        if (!index.flush() && failedFile.isEmpty()) failedFile = QStringLiteral("frames.csv");
        index.close();
    }
    if (!failedFile.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        m_stop = true;
        m_error = tr("Could not write %1.").arg(failedFile);
    }

    m_encoders.waitForDone();
    {
        QMutexLocker lock(&m_mutex);
        m_queue.clear();
        m_bufferedBytes = 0;
    }
    m_resultDirectory = m_partialDirectory;
    if (!m_saved && !m_savedRgb && m_error.isEmpty()) {
        if (m_includeDepth) staging.rmdir("depth");
        if (m_includeRgb) {
            QFile::remove(staging.filePath("frames.csv"));
            staging.rmdir("rgb");
        }
        if (QDir().rmdir(m_partialDirectory)) m_resultDirectory.clear();
        else m_error = tr("Could not remove the empty recording folder.");
    } else if (m_error.isEmpty()) {
        if (QDir().rename(m_partialDirectory, m_finalDirectory)) m_resultDirectory = m_finalDirectory;
        else m_error = tr("Could not rename the completed recording folder.");
    }
}

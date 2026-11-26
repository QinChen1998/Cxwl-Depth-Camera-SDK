#pragma once

#include <DepthSdk.h>
#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QThreadPool>
#include <memory>
#include <QWaitCondition>

class QDir;
class QFile;

// The owner thread accepts SDK callbacks. The writer drains them in capture order.
class PointCloudRecorder final : public QThread {
    Q_OBJECT
public:
    enum class State { Idle, Recording, Saving };
    PointCloudRecorder();
    ~PointCloudRecorder() override;
    State state() const { return m_state; }
    QString startRecording(const QString &parentDirectory, bool includeDepth, bool includeRgb);
    void stopRecording();
    void appendPointCloud(const CxwlDepthFrame &cloud, const QByteArray &xyz, const QByteArray &depth);
    void appendRgb(const CxwlDepthFrame &frame, const QImage &image);

signals:
    void stateChanged();
    void completed(quint64 cloudFrames, quint64 rgbFrames, const QString &directory, const QString &error);

private:
    void run() override;
    struct EncodedRgb;
    struct Sample {
        CxwlDepthFrame frame;
        QByteArray xyz, depth;
        qint64 elapsedNs;
        std::shared_ptr<EncodedRgb> encoded;
    };
    void enqueue(Sample sample, const QImage &rgb = {});
    QString writeSample(const QDir &staging, Sample &sample, QFile &index);
    QThreadPool m_encoders;
    QElapsedTimer m_clock;
    bool m_includeDepth = false, m_includeRgb = false;
    qsizetype m_bufferedBytes = 0;
    State m_state = State::Idle;
    QMutex m_mutex;
    QWaitCondition m_available;
    QQueue<Sample> m_queue;
    bool m_stop = false;
    QString m_error, m_partialDirectory, m_finalDirectory, m_resultDirectory;
    quint64 m_saved = 0, m_savedRgb = 0;
};

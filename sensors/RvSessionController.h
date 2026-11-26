#pragma once

#include <memory>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include "RvDeviceStore.h"
#include "RvDtofCandidates.h"
#include "RvTofFrame.h"

class QProcess;
class RvDtofBoardStream;
class RvHdVideoStream;
class RvTunerController;
class SensorMainWindow;

// Owns a native ToF and H.265 session independently of the display widgets.
// Calls and signals run on the owner thread.
class CXWL_DEPTH_INTERNAL RvSessionController final : public QObject
{
    Q_OBJECT
public:
    enum class AutomaticConnectionStage { Idle = 0, CheckingDevice = 1, StartingCapture = 3, WaitingForDepth = 4 };

    explicit RvSessionController(QObject *parent = nullptr, const QString &deviceStorePath = RvDeviceStore::defaultPath());
    ~RvSessionController() override;
    QString deviceStorePath() const { return m_deviceStorePath; }
    RvDeviceProfile deviceProfile() const { return m_device; }
    void reloadDeviceProfiles();
    bool selectDevice(const QString &id);

    bool busy() const { return m_tunerExclusive || m_controlPending || m_waitingForSelectedFrame || m_transitioning || m_boardFileOperation; }
    bool beginBoardFileOperation();
    void endBoardFileOperation();
    AutomaticConnectionStage automaticConnectionStage() const { return m_automaticConnectionStage; }
    bool automaticConnecting() const { return m_automaticConnectionStage != AutomaticConnectionStage::Idle; }
    RvTunerController *tuner() const { return m_tuner; }
    bool userStopped() const { return m_userStopped; }
    bool retainingRecoveryDisplay() const { return !m_destroying && !m_userStopped && m_recoveryDisplayTimer.isActive(); }
    bool waitingForReconnect() const { return m_reconnectTimer.isActive(); }
    bool controlProcessActive() const;
    QString stateText() const { return m_stateText; }
    QString detailText() const { return m_detailText; }
    double videoFps() const { return m_videoFps; }
    quint64 presentationEpoch() const { return m_presentationEpoch; }
    QString currentImagingScheme() const;
    bool imagingSwitchActive() const { return m_imagingSwitchActive; }
    QJsonObject imagingSwitchDiagnostics() const;
    bool controlledSource() const;
    int dtofCandidateMode() const { return m_dtofMode; }
    quint64 stopRevision() const { return m_stopRevision; }

public slots:
    void selectImagingScheme(const QString &scheme);
    void start();
    void startAutomatically(const QString &expectedBoot = {});
    void stop();
    void reconnectNow();

signals:
    void boardFilesRequired(const RvDeviceProfile &device);
    void deviceChanged();
    void configurationChanged();
    void stateChanged();
    void cameraFrameReady(const QImage &image);
    void videoFrameStamped(const QImage &image, quint64 sequence, qint64 decodedMonotonicNs, const QString &captureGeneration);
    void streamCleared();
    void recoveryDisplayExpired();
    void depthFrameReady(const QByteArray &depth, const QJsonObject &metadata);
    void tofFrameReady(const RvTofFrame &frame);
    void depthStreamCleared(const QString &reason);
    void imagingSwitchStarted(const QString &scheme, int estimatedMs);
    void imagingSwitchFinished(const QString &scheme, qint64 elapsedMs);
    void imagingSwitchAborted(const QString &reason);

private:
    friend class RvTunerController;
    friend class RvUsbEnrollment;
    friend int runUsbEnrollmentLiveValidation(SensorMainWindow &, const QString &);
    friend int runPortSmokeTests(SensorMainWindow &, const QString &);
    friend int runRvTunerTests(const QString &);
    void applyDeviceProfile(const RvDeviceProfile &device);
    void setAutomaticConnectionStage(AutomaticConnectionStage stage);
    void startImaging(bool automatic, const QString &expectedBoot = {});
    void submitCapture(bool startAfter = false, bool reconnecting = false, bool automatic = false, const QString &expectedBoot = {});
    void updateState();
    void failPreviewControl(const QString &reason, bool retry = false);
    void retryConnection();
    void beginConnection();
    void presentVideo();
    bool reconnectVideo();
    void stopReceivers(bool retainBoardReceiver = false);
    void beginImagingSwitch(const QString &scheme);
    void finishImagingSwitch();
    void cancelImagingSwitch(const QString &reason);
    void clearCurrentFrame(const QString &reason);
    void scheduleReconnect(const QString &reason);
    void setState(const QString &state, const QString &detail);

    RvTunerController *m_tuner = nullptr;
    bool m_tunerExclusive = false;
    bool m_boardFileOperation = false;
    QJsonObject m_captureOwner;
    RvDeviceProfile m_device;
    QString m_deviceStorePath, m_deviceError, m_deviceRevision;
    AutomaticConnectionStage m_automaticConnectionStage = AutomaticConnectionStage::Idle;
    quint64 m_stopRevision = 0;
    int m_dtofMode = RvDtofCandidates::Native48;
    const QString m_dtofOptions = QStringLiteral("1,1");
    QString m_dtofToken;
    QJsonObject m_dtofReceipt, m_currentMetadata;
    RvDtofBoardStream *m_dtofStream = nullptr;
    QPointer<RvDtofBoardStream> m_retiringDtofStream;
    bool m_destroying = false;
    QString m_stateText, m_detailText;
    double m_videoFps = 0;
    QTimer m_reconnectTimer, m_frameTimer, m_recoveryDisplayTimer, m_nativeDepthExpiry;
    QElapsedTimer m_fpsClock, m_hdFrameClock, m_encoderProgressClock;
    bool m_hdVideoSeen = false;
    int m_hdVideoReconnects = 0;
    int m_reconnectAttempt = 0, m_framesInWindow = 0;
    QString m_lastReconnectReason;
    int m_nextReconnectDelayMs = 1000;
    bool m_userStopped = true;
    RvHdVideoStream *m_hdVideo = nullptr;
    quint64 m_hdVideoSequence = 0, m_hdOutputSequence = 0;
    struct ServiceState;
    std::unique_ptr<ServiceState> m_service;
    void launchServiceCapture(const QString &expectedBoot);
    void stopServiceCapture();
    QString serviceVideoUrl() const;
    QTimer m_controlTimeout, m_switchFrameTimeout;
    bool m_controlPending = false, m_waitingForSelectedFrame = false, m_controlTimedOut = false;
    quint64 m_presentationEpoch = 0;
    bool m_transitioning = false;
    bool m_imagingSwitchActive = false;
    int m_imagingSwitchEstimateMs = 0;
    QString m_imagingSwitchScheme;
    QElapsedTimer m_imagingSwitchClock;
    QJsonObject m_lastImagingSwitch;
    bool m_rtspStatusReady = false;
    quint64 m_rtspDecodeFloor = 0;
    qint64 m_lastEncodedFrame = 0, m_lastRetrievedFrame = 0;
};

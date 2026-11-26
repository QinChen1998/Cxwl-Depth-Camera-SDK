#include "RvSessionController.h"
#include "RvTunerController.h"
#include "../RuntimeDiscovery.h"
#include "RvHdVideoStream.h"
#include "RvDtofBoardStream.h"
#include "RvCaptureCalibration.h"
#include <QtCore/QUuid>
#include <QtCore/QDateTime>

#include <QtCore/QProcess>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QSettings>
#include <QtCore/qscopeguard.h>
#include <QtNetwork/QHostAddress>
#include <algorithm>
#include <limits>

#include "../sdk/windows/DeviceService.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QFutureWatcher>
struct RvSessionController::ServiceState {
    struct Result {
        std::shared_ptr<DeviceService::Capture> capture;
        QString error;
        bool stopFailed=false;
    };
    QFutureWatcher<Result> operation;
    QFutureWatcher<QString> ping;
    QTimer heartbeat;
    std::shared_ptr<DeviceService::Capture> capture;
    std::unique_ptr<DeviceService::MediaPipe> depth, rgb;
    quint16 depthPort=0,rgbPort=0;
    bool pending=false;
};

RvSessionController::RvSessionController(QObject *parent, const QString &deviceStorePath)
    : QObject(parent)
    , m_deviceStorePath(deviceStorePath)
    , m_service(std::make_unique<ServiceState>())
{
    setObjectName(QStringLiteral("rvSessionController"));
    m_tuner = new RvTunerController(this);
    try { m_device = RvDeviceStore(m_deviceStorePath).active(); }
    catch (const std::exception &error) { m_deviceError = QString::fromUtf8(error.what()); }

    auto settings = RuntimeDiscovery::depthSettings();
    const int savedMode = RvDtofCandidates::modeForScheme(settings.value("depth/imagingScheme").toString());
    if (savedMode >= 0) m_dtofMode = savedMode;
    settings.setValue("depth/imagingScheme", currentImagingScheme());

    m_controlTimeout.setSingleShot(true);
    m_controlTimeout.setInterval(60000);
    m_switchFrameTimeout.setSingleShot(true);
    m_switchFrameTimeout.setInterval(15000);
    connect(&m_switchFrameTimeout, &QTimer::timeout, this, [this] {
        failPreviewControl(tr("No frames arrived for the selected board preset. Check board status."), true);
    });
    m_service->heartbeat.setInterval(5000);
    connect(&m_service->heartbeat,&QTimer::timeout,this,[this] {
        auto c=m_service->capture;
        if(!c || m_service->ping.isRunning() || m_service->pending)return;
        m_service->ping.setFuture(QtConcurrent::run([c] {
            try{DeviceService::heartbeat(*c);return QString();}
            catch(const std::exception &e){return QString::fromUtf8(e.what());}
        }));
    });
    connect(&m_service->operation,&QFutureWatcher<ServiceState::Result>::finished,this,[this] {
        if(m_destroying)return;
        auto result=m_service->operation.result();m_service->pending=false;
        setProperty("rvReleaseUnconfirmed",result.stopFailed);
        m_service->capture=result.capture;
        if(!result.error.isEmpty()){failPreviewControl(result.error);return;}
        if(m_userStopped || !result.capture || !m_controlPending) {
            m_controlPending=false;
            if(result.capture)stopServiceCapture();
            updateState();return;
        }
        try {
            RvDeviceStore store(m_deviceStorePath);
            if(store.revision()!=m_deviceRevision || store.byId(m_device.id).json()!=m_device.json() || result.capture->generation!=m_dtofToken)
                throw std::runtime_error("Device registration changed during capture start");
            m_service->depth=std::make_unique<DeviceService::MediaPipe>(*result.capture,false);
            m_service->rgb=std::make_unique<DeviceService::MediaPipe>(*result.capture,true);
            m_service->depthPort=m_service->depth->listenPort();m_service->rgbPort=m_service->rgb->listenPort();
            if(!m_service->depthPort || !m_service->rgbPort)throw std::runtime_error("Cannot open the media channels");
            m_dtofReceipt=result.capture->receipt;
            m_captureOwner={{"owner_boot",result.capture->identity.boot},{"owner_generation",m_dtofToken}};
            RuntimeDiscovery::depthSettings().setValue("rvDevices/lastConnected/"+m_device.fingerprint(),QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            m_controlPending=false;m_waitingForSelectedFrame=true;m_switchFrameTimeout.start();
            m_service->heartbeat.start();updateState();beginConnection();
        } catch(const std::exception &e){failPreviewControl(QString::fromUtf8(e.what()));}
    });
    m_nativeDepthExpiry.setSingleShot(true);
    connect(&m_nativeDepthExpiry,&QTimer::timeout,this,[this]{
        if(!m_destroying)emit depthStreamCleared(tr("Native ToF depth expired"));
    });
    m_frameTimer.setInterval(15);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer,&QTimer::timeout,this,[this]{presentVideo();});
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setObjectName(QStringLiteral("rvReconnectTimer"));
    m_frameTimer.setObjectName(QStringLiteral("rvFrameTimer"));
    m_switchFrameTimeout.setObjectName(QStringLiteral("rvSelectedFrameTimeout"));
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RvSessionController::retryConnection);
    m_recoveryDisplayTimer.setSingleShot(true);
    m_recoveryDisplayTimer.setInterval(15000);
    m_recoveryDisplayTimer.setObjectName(QStringLiteral("rvRecoveryDisplayTimer"));
    connect(&m_recoveryDisplayTimer, &QTimer::timeout, this, [this] {
        m_recoveryDisplayTimer.stop();
        if (!m_destroying) { emit recoveryDisplayExpired(); updateState(); }
    });
    m_stateText = tr("Stopped");
    m_detailText = tr("Camera is not started");
    setProperty("rvMode", "stopped");
}

RvSessionController::~RvSessionController()
{
    // Tear down callback sources before the member timers are destroyed.
    m_destroying = true;
    m_userStopped = true;
    m_controlPending = false;
    m_waitingForSelectedFrame = false;
    delete m_tuner;
    m_recoveryDisplayTimer.stop();
    m_reconnectTimer.stop();
    m_controlTimeout.stop();
    m_switchFrameTimeout.stop();
    m_service->heartbeat.stop();
    m_service->operation.disconnect(this);
    m_service->operation.waitForFinished();
    m_service->ping.waitForFinished();
    if(m_service->pending)m_service->capture=m_service->operation.result().capture;
    m_service->depth.reset();m_service->rgb.reset();
    if(m_service->capture)try{DeviceService::stop(*m_service->capture);}catch(const std::exception&){}
    stopReceivers();
    emit streamCleared();
    emit depthStreamCleared(tr("Camera is not started"));
}

bool RvSessionController::controlledSource() const
{
    return !m_device.host.isEmpty();
}

bool RvSessionController::controlProcessActive() const
{
    return m_service->pending;
}

bool RvSessionController::beginBoardFileOperation()
{
    if (m_destroying || !userStopped() || busy() || automaticConnecting() || controlProcessActive()
        || m_tuner->shutdownPending()) return false;
    m_boardFileOperation = true;
    updateState();
    return true;
}

void RvSessionController::endBoardFileOperation()
{
    m_boardFileOperation = false;
    updateState();
}

void RvSessionController::reloadDeviceProfiles()
{
    if(m_tunerExclusive){setState(tr("dToF Parameter Tuning"),tr("Stop dToF tuning before selecting another device."));return;}
    stop();
    try {
        const RvDeviceStore store(m_deviceStorePath);
        applyDeviceProfile(store.activeId().isEmpty() ? RvDeviceProfile{} : store.active());
    } catch (const std::exception &error) {
        applyDeviceProfile({});
        m_deviceError = QString::fromUtf8(error.what());
        setState(tr("Error"), m_deviceError);
    }
}

bool RvSessionController::selectDevice(const QString &id)
{
    if (m_destroying || !m_userStopped || busy()) return false;
    for (const auto &profile : RvDeviceStore(m_deviceStorePath).profiles()) {
        if (profile.id != id) continue;
        stop();
        applyDeviceProfile(profile);
        return true;
    }
    return false;
}

void RvSessionController::applyDeviceProfile(const RvDeviceProfile &device)
{
    m_device=device;m_deviceError.clear();
    m_deviceRevision.clear();
    m_dtofReceipt = {};
    m_captureOwner={};
    setState(tr("Stopped"), m_device.id.isEmpty()
        ? tr("Connect a device using USB one-click connection in Device Manager.")
        : tr("Device selected; click Start when ready."));
    emit deviceChanged();emit configurationChanged();
}

void RvSessionController::selectImagingScheme(const QString &scheme)
{
    const int mode = RvDtofCandidates::modeForScheme(scheme);
    if (mode < 0 || mode == m_dtofMode || m_destroying || busy() || automaticConnecting()
        || m_imagingSwitchActive || controlProcessActive()) return;

    if (!m_userStopped) {
        beginImagingSwitch(scheme);
        if (m_userStopped || !m_imagingSwitchActive) return;
    }
    m_dtofMode = mode;
    m_dtofReceipt = {};
    RuntimeDiscovery::depthSettings().setValue("depth/imagingScheme", scheme);
    if (!m_userStopped) submitCapture();
    else { emit configurationChanged(); updateState(); }
}

QString RvSessionController::currentImagingScheme() const
{
    return RvDtofCandidates::name(m_dtofMode);
}

void RvSessionController::beginImagingSwitch(const QString &scheme)
{
    if (m_destroying || m_userStopped) return;
    m_imagingSwitchScheme = scheme;
    const QString key = "depth/imagingSwitchMs/" + m_device.id + "/capture/" + scheme;
    m_imagingSwitchEstimateMs = std::clamp(RuntimeDiscovery::depthSettings().value(key, 12000).toInt(), 1000, 60000);
    m_imagingSwitchActive = true;
    m_imagingSwitchClock.restart();
    emit imagingSwitchStarted(scheme, m_imagingSwitchEstimateMs);
}

void RvSessionController::finishImagingSwitch()
{
    if(!m_imagingSwitchActive||m_userStopped||currentImagingScheme()!=m_imagingSwitchScheme)return;
    const auto elapsed=m_imagingSwitchClock.elapsed();const auto scheme=m_imagingSwitchScheme;
    m_imagingSwitchActive=false;
    m_lastImagingSwitch={{"scheme",scheme},{"completed",true},{"elapsed_ms",double(elapsed)},
        {"board_restart",true},{"capture_generation",m_dtofToken},{"boot_id",m_dtofReceipt["boot_id"]}};
    if(elapsed>=100&&elapsed<=60000){
        const QString key="depth/imagingSwitchMs/"+m_device.id+"/capture/"+scheme;
        auto settings=RuntimeDiscovery::depthSettings();const int prior=settings.value(key,int(elapsed)).toInt();
        settings.setValue(key,std::clamp(int(.65*prior+.35*elapsed),1000,60000));
    }
    emit imagingSwitchFinished(scheme,elapsed);
}

void RvSessionController::cancelImagingSwitch(const QString &reason)
{
    if(!m_imagingSwitchActive)return;
    m_imagingSwitchActive=false;
    m_lastImagingSwitch={{"scheme",m_imagingSwitchScheme},{"completed",false},
        {"elapsed_ms",double(m_imagingSwitchClock.elapsed())},{"reason",reason}};
    if(!m_destroying)emit imagingSwitchAborted(reason);
}

QJsonObject RvSessionController::imagingSwitchDiagnostics() const
{
    return {{"active",m_imagingSwitchActive},{"scheme",m_imagingSwitchScheme},
        {"estimated_ms",m_imagingSwitchEstimateMs},{"elapsed_ms",m_imagingSwitchClock.isValid()?double(m_imagingSwitchClock.elapsed()):0.},
        {"board_restart",true},{"last",m_lastImagingSwitch}};
}

void RvSessionController::submitCapture(bool startAfter, bool reconnecting, bool automatic, const QString &expectedBoot)
{
    if (busy() || controlProcessActive()) return;
    const auto priorOwner=m_captureOwner;
    if(reconnecting&&(priorOwner["owner_generation"].toString().isEmpty()||priorOwner["owner_boot"].toString().isEmpty())){
        failPreviewControl(tr("The previous board capture identity is unavailable. Reconnect from Device Manager."));
        return;
    }
    const auto videoError = RvHdVideoStream::runtimeError();
    if (!videoError.isEmpty()) { failPreviewControl(videoError); return; }
    m_transitioning = true;
    const auto transition = qScopeGuard([this] { m_transitioning = false; updateState(); });
    if (startAfter) m_userStopped = false;
    if (!reconnecting) { m_reconnectAttempt = 0; m_nextReconnectDelayMs = 1000; m_lastReconnectReason.clear(); m_recoveryDisplayTimer.stop(); }
    m_controlPending = true; m_waitingForSelectedFrame = false; m_controlTimedOut = false;
    const auto receptionAuthorized = [this] {
        if (m_controlPending && !m_userStopped) return true;
        // Stop arrived in a synchronous notification before a retry command
        // was launched. Revoke this local transaction without touching board.
        m_controlPending = false;
        m_controlTimeout.stop(); m_switchFrameTimeout.stop();
        m_waitingForSelectedFrame = false;
        setProperty("rvMode", "stopped");
        setState(tr("Stopped"), tr("Camera is not started"));
        return false;
    };
    if(!receptionAuthorized())return;
    // Revoke old callbacks immediately, but keep the old LSG2 receiver polling
    // until the coordinator has stopped its owner. Disconnecting first races
    // the board's normal session cleanup against the next candidate command.
    m_reconnectTimer.stop();
    stopReceivers(true);
    if (!receptionAuthorized()) return;
    m_switchFrameTimeout.stop();
    emit configurationChanged();
    if (!receptionAuthorized()) return;
    updateState();
    if (!receptionAuthorized()) return;
    emit streamCleared();
    if (!receptionAuthorized()) return;
    const auto connectingDetail = automatic ? tr("Starting camera and depth capture...")
        : reconnecting ? tr("Automatically restoring camera and depth...") : tr("Switching work mode...");
    setProperty("rvMode", "switching"); emit depthStreamCleared(connectingDetail);
    if (!receptionAuthorized()) return;
    setState(tr("Connecting"), connectingDetail);
    if (!receptionAuthorized()) return;
    m_dtofToken = QUuid::createUuid().toString(QUuid::Id128);
    m_dtofReceipt = {};
    try {
        RvDeviceStore store(m_deviceStorePath);const auto selected=store.byId(m_device.id);
        if(reconnecting&&(store.revision()!=m_deviceRevision||selected.json()!=m_device.json()))
            throw std::runtime_error("Device registry or trusted profile changed before stream recovery");
        if(!m_deviceError.isEmpty() || !selected.ready())throw std::runtime_error("Select a configured trusted device in Settings > Device Manager");
        m_device=selected;m_deviceRevision=store.revision();
    }catch(const std::exception &e){failPreviewControl(QString::fromUtf8(e.what()));return;}
    if(receptionAuthorized())launchServiceCapture(reconnecting?priorOwner["owner_boot"].toString():expectedBoot);
}

void RvSessionController::updateState()
{
    setProperty("rvControlPending", busy());
    setProperty("rvReconnectPending", m_reconnectTimer.isActive());
    setProperty("rvReconnectAttempt", m_reconnectAttempt);
    setProperty("rvReconnectDelayMs", m_reconnectTimer.isActive() ? m_reconnectTimer.interval() : 0);
    if (!m_destroying) emit stateChanged();
}

void RvSessionController::failPreviewControl(const QString &reason, bool retry)
{
    if (m_destroying) return;
    cancelImagingSwitch(reason);
    if (retry && !m_userStopped) {
        m_controlPending = false;
        scheduleReconnect(reason);
        return;
    }
    const bool previousTransition = m_transitioning;
    m_transitioning = true;
    const auto transition = qScopeGuard([this, previousTransition] {
        m_transitioning = previousTransition; updateState();
    });
    m_controlTimeout.stop(); m_switchFrameTimeout.stop();
    m_controlPending = false; m_waitingForSelectedFrame = false;
    m_userStopped = true;
    m_recoveryDisplayTimer.stop();
    m_reconnectTimer.stop(); stopReceivers();
    if(!m_service->pending && m_service->capture)stopServiceCapture();
    emit configurationChanged(); updateState(); setProperty("rvMode", "error");
    setState(tr("Error"), reason); emit depthStreamCleared(reason);
    setAutomaticConnectionStage(AutomaticConnectionStage::Idle);
}

void RvSessionController::setState(const QString &state, const QString &detail)
{
    const auto displayDetail = detail;
    if (m_stateText == state && m_detailText == displayDetail) return;
    m_stateText = state; m_detailText = displayDetail;
    if (!m_destroying) emit stateChanged();
}

void RvSessionController::start()
{
    if (!automaticConnecting()) startImaging(false);
}

void RvSessionController::setAutomaticConnectionStage(AutomaticConnectionStage stage)
{
    if (m_automaticConnectionStage == stage) return;
    m_automaticConnectionStage = stage;
    if (!m_destroying) emit stateChanged();
}

void RvSessionController::startAutomatically(const QString &expectedBoot)
{
    if (qEnvironmentVariable("RV_TEST_OFFLINE_ONLY") == "1" || !m_userStopped || busy()
        || !controlledSource() || !m_device.ready()) return;
    const auto revision = m_stopRevision;
    setAutomaticConnectionStage(AutomaticConnectionStage::StartingCapture);
    if (revision != m_stopRevision) return;
    startImaging(true,expectedBoot);
    if (m_userStopped) setAutomaticConnectionStage(AutomaticConnectionStage::Idle);
}

void RvSessionController::startImaging(bool automatic,const QString &expectedBoot)
{
    if (m_destroying) return;
    if (m_device.id.isEmpty() && m_deviceError.isEmpty()) {
        if (!automatic) setState(tr("Stopped"),tr("Connect a device using USB one-click connection in Device Manager."));
        return;
    }
    if(m_tunerExclusive){setState(tr("dToF Parameter Tuning"),tr("CXWL Depth Camera is owned by dToF tuning. Stop it before starting shared imaging."));return;}
    // A saved video URL cannot bypass a missing or invalid device identity.
    if (!m_deviceError.isEmpty()) {
        failPreviewControl(m_deviceError + " " + tr("Open Settings > Device Manager to review this device."));
        return;
    }
    if(busy()||m_imagingSwitchActive)return;
    if (!controlledSource()) {
        setState(tr("Error"), tr("Open Settings > Device Manager to review this device."));
        return;
    }
    submitCapture(true, false, automatic, expectedBoot);
}

void RvSessionController::stop()
{
    ++m_stopRevision;
    if (m_destroying) return;
    if (m_userStopped && m_transitioning) return;
    const bool previousTransition = m_transitioning;
    m_transitioning = true;
    const auto transition = qScopeGuard([this, previousTransition] {
        m_transitioning = previousTransition; updateState();
    });
    m_userStopped = true;
    setAutomaticConnectionStage(AutomaticConnectionStage::Idle);
    cancelImagingSwitch(tr("Camera is not started"));
    m_recoveryDisplayTimer.stop();
    // Revoke before kill: a queued completion cannot accept a late receipt or
    // revive reception after Stop. Already submitted board configure is not undone.
    m_controlPending = false; m_controlTimeout.stop();
    stopServiceCapture();
    m_switchFrameTimeout.stop();
    m_waitingForSelectedFrame = false;
    emit depthStreamCleared(tr("Camera is not started"));
    m_reconnectTimer.stop();
    stopReceivers();
    updateState();
    setProperty("rvMode",m_controlPending?"switching":"stopped");
    setState(tr("Stopped"), tr("Camera is not started"));
    emit stateChanged();
}

void RvSessionController::reconnectNow()
{
    start();
}

void RvSessionController::retryConnection()
{
    if (m_destroying || m_userStopped || busy()) return;
    // Re-confirm every managed reconnect. The coordinator permits only a
    // classified stream fault to reset, after clean exit and within its budget;
    // a new boot/generation cannot reuse an old receipt or frame counter.
    submitCapture(false, true);
}

void RvSessionController::beginConnection()
{
    if (m_destroying || m_tunerExclusive || m_userStopped || m_controlPending || m_transitioning) return;
    if (qEnvironmentVariable("RV_TEST_OFFLINE_ONLY") == "1" && m_device.host != "localhost"
        && !QHostAddress(m_device.host).isLoopback()) {
        failPreviewControl(QStringLiteral("Offline test forbids external camera connections"));return;
    }
    m_transitioning = true;
    const auto transition = qScopeGuard([this] { m_transitioning = false; updateState(); });
    m_reconnectTimer.stop();
    stopReceivers();
    const quint64 epoch = m_presentationEpoch;
    const auto stillConnecting = [this, epoch] {
        return !m_userStopped && !m_controlPending && epoch == m_presentationEpoch;
    };
    if (!stillConnecting()) return;
    if(!dtofBoardReceipt(m_dtofReceipt,m_dtofMode,m_dtofToken,m_dtofOptions)){failPreviewControl(tr("Candidate confirmation is missing"));return;}
    auto *stream = new RvDtofBoardStream(m_dtofReceipt,this,"127.0.0.1",m_service->depthPort);
    m_dtofStream = stream;
    m_rtspStatusReady = false;
    m_lastEncodedFrame = m_lastRetrievedFrame = 0;
    m_hdVideoSeen = false;
    m_hdVideoReconnects = 0;
    setProperty("rvVideoReconnectCount", 0);
    m_encoderProgressClock.restart();
    m_hdFrameClock.restart();
    stream->videoProgress = [this, stream](const QJsonObject &meta) {
        if (m_dtofStream != stream || m_userStopped || m_controlPending || !m_hdVideo) return;
        const auto encoded = meta["video_encoded_sequence"].toString().toLongLong();
        const auto retrieved = meta["video_retrieved_sequence"].toString().toLongLong();
        if (encoded <= m_lastEncodedFrame || retrieved <= m_lastRetrievedFrame) return;
        m_lastEncodedFrame = encoded;
        m_lastRetrievedFrame = retrieved;
        m_encoderProgressClock.restart();
        if (!m_rtspStatusReady) {
            m_rtspDecodeFloor = m_hdVideo->snapshot().sequence;
            m_rtspStatusReady = true;
        }
    };
    const auto current=[this,stream,epoch]{
        return m_dtofStream==stream&&epoch==m_presentationEpoch&&!m_userStopped&&!m_controlPending;
    };
    const auto publishDepth=[this,current](const QByteArray& bytes,QJsonObject metadata,double age){
        if(!current())return;
        RvCaptureCalibration::annotate(metadata,m_dtofReceipt);
        metadata["source_generation"]=metadata["generation"];metadata["generation"]=double(m_presentationEpoch);
        metadata["calibration_release"]=RvCalibration::Release;
        metadata["sensor_mode"]=RvCaptureContract::sensorMode(m_dtofMode);
        m_nativeDepthExpiry.start(std::max(1,RvCaptureContract::FreshMs-int(std::ceil(age))));
        if(m_waitingForSelectedFrame){m_waitingForSelectedFrame=false;m_switchFrameTimeout.stop();updateState();}
        if(!current())return;
        finishImagingSwitch();
        if(!current())return;
        m_currentMetadata = metadata;
        emit depthFrameReady(bytes, metadata);
        if(current()&&m_automaticConnectionStage==AutomaticConnectionStage::WaitingForDepth)
            setAutomaticConnectionStage(AutomaticConnectionStage::Idle);
    };
    stream->rawFrame=[this,current,publishDepth](const RvTofFrame& received){
        if(!current())return;
        auto raw=received;RvCaptureCalibration::annotate(raw.metadata,m_dtofReceipt);
        setProperty("rvRawTofSequence", raw.metadata["tof_sequence"].toVariant());
        emit tofFrameReady(raw);
        if (!current()) return;
        const auto bytes = raw.radialMetres();
        if (bytes.isEmpty()) return;
        auto metadata = raw.metadata;
        metadata["model"] = RvDtofCandidates::name(m_dtofMode);
        metadata["width"] = raw.columns();
        metadata["height"] = raw.rows();
        metadata["depth_unit"] = "sensor_radial_m_float32";
        metadata["depth_location"] = "native_sensor";
        metadata["native_order"] = true;
        metadata["interpolation"] = false;
        publishDepth(bytes, metadata, metadata["raw_age_ms"].toDouble());
    };
    stream->error=[this,stream,epoch](const QString& reason){
        if(m_dtofStream!=stream||m_userStopped)return;
        const QPointer<RvDtofBoardStream> alive(stream);
        QTimer::singleShot(0,this,[this,alive,epoch,reason]{
            if(alive&&m_dtofStream==alive&&epoch==m_presentationEpoch&&!m_userStopped)failPreviewControl(reason,true);
        });
    };
    m_framesInWindow = 0;
    m_fpsClock.restart();
    m_hdVideo=new RvHdVideoStream(
        serviceVideoUrl()
    );m_hdVideoSequence=0;m_hdOutputSequence=0;
    m_hdVideo->start();m_frameTimer.start();
    setState(tr("Connecting"),tr("Waiting for CXWL Depth Camera candidate output"));
    if(m_automaticConnectionStage==AutomaticConnectionStage::StartingCapture)
        setAutomaticConnectionStage(AutomaticConnectionStage::WaitingForDepth);
}

void RvSessionController::scheduleReconnect(const QString &reason)
{
    if (m_destroying || m_tunerExclusive || m_userStopped || m_controlPending || m_reconnectTimer.isActive())
        return;
    cancelImagingSwitch(reason);
    if(m_userStopped)return;
    m_lastReconnectReason=reason;
    const bool previousTransition = m_transitioning;
    m_transitioning = true;
    const auto transition = qScopeGuard([this, previousTransition] {
        m_transitioning = previousTransition; updateState();
    });
    if (m_reconnectAttempt < std::numeric_limits<int>::max()) ++m_reconnectAttempt;
    const int delayMs = m_nextReconnectDelayMs;
    m_nextReconnectDelayMs = std::min(delayMs * 2, 5000);
    m_controlTimeout.stop(); m_switchFrameTimeout.stop();
    m_waitingForSelectedFrame = false;
    // Keep only the last displayed pixels during this bounded recovery window.
    // All source metadata, metric arrays and worker epochs are still revoked.
    if (!m_recoveryDisplayTimer.isActive() && (!m_currentMetadata.isEmpty() || property("rvSourceWidth").isValid()))
        m_recoveryDisplayTimer.start();
    stopReceivers();
    setAutomaticConnectionStage(AutomaticConnectionStage::Idle);
    const quint64 epoch = m_presentationEpoch;
    if (m_userStopped || m_controlPending) return;
    setProperty("rvMode", "reconnecting");
    setState(tr("Reconnecting"),
             tr("%1; reconnect %2 in %3 ms").arg(reason).arg(m_reconnectAttempt).arg(delayMs));
    if (m_userStopped || m_controlPending || epoch != m_presentationEpoch) return;
    m_reconnectTimer.start(delayMs);
}

void RvSessionController::stopReceivers(bool retainBoardReceiver)
{
    ++m_presentationEpoch;
    m_hdVideoSeen=false;
    m_rtspStatusReady = false;
    m_rtspDecodeFloor = 0;
    m_frameTimer.stop();
    // Detach ownership before deleting or notifying: direct observers can Stop
    // again without touching either receiver while its destructor is running.
    auto *video = m_hdVideo;
    m_hdVideo = nullptr;
    delete video;
    auto* dtof=m_dtofStream;m_dtofStream=nullptr;
    if(retainBoardReceiver&&dtof){
        auto *previous=m_retiringDtofStream.data();m_retiringDtofStream=dtof;
        if(previous!=dtof)delete previous;
    }else{
        auto *previous=m_retiringDtofStream.data();m_retiringDtofStream=nullptr;
        delete dtof;if(previous!=dtof)delete previous;
    }
    clearCurrentFrame(tr("Disconnected / No data"));
}

void RvSessionController::clearCurrentFrame(const QString &reason)
{
    m_videoFps = 0;
    m_nativeDepthExpiry.stop();
    m_currentMetadata = {};
    for(const auto* name:{"rvDepthMetadata","rvModel","rvFrameId","rvGeneration","rvWidth","rvRawTofSequence","rvRgbSequence",
            "rvTransport","rvIdentitySource","rvBootId","rvSourceWidth","rvSourceHeight",
            "rvDecodedSequence","rvEncodedSequence","rvFirstFrameMs","rvReconnectCount"})setProperty(name,QVariant());
    if (!m_destroying) {
        emit depthStreamCleared(reason);
        emit streamCleared();
    }
}

bool RvSessionController::reconnectVideo()
{
    // Renew the decoder once while the same board generation is still encoding.
    if (!m_dtofStream || m_hdVideoReconnects >= 1 || m_encoderProgressClock.elapsed() >= 3000) return false;
    ++m_hdVideoReconnects;
    setProperty("rvVideoReconnectCount", m_hdVideoReconnects);
    delete m_hdVideo;
    m_hdVideo = new RvHdVideoStream(
        serviceVideoUrl()
    );
    m_hdVideoSequence = 0;
    m_hdVideoSeen = false;
    m_rtspStatusReady = false;
    m_hdFrameClock.restart();
    m_hdVideo->start();
    return true;
}

void RvSessionController::presentVideo()
{
    if(!m_hdVideo||!m_dtofStream||m_userStopped||m_controlPending||m_transitioning)return;
    if(m_encoderProgressClock.elapsed()>=3000){failPreviewControl(tr("Managed camera encoder progress stopped; check board status"),true);return;}
    auto s=m_hdVideo->snapshot();
    if(!s.error.isEmpty()){if(!reconnectVideo())failPreviewControl(s.error,true);return;}
    if(s.sequence==m_hdVideoSequence||s.image.isNull()){
        if(m_hdFrameClock.elapsed()>(m_hdVideoSeen?3000:15000)&&!reconnectVideo())failPreviewControl(tr("The stream is temporarily unavailable"),true);
        return;
    }
    m_hdVideoSequence=s.sequence;
    if(!m_rtspStatusReady||s.sequence<=m_rtspDecodeFloor)return;
    const auto sourceSequence=++m_hdOutputSequence;
    m_hdVideoSeen=true;m_hdFrameClock.restart();
    if(m_waitingForSelectedFrame){m_waitingForSelectedFrame=false;m_switchFrameTimeout.stop();updateState();}
    setProperty("rvMode","native_depth");setProperty("rvTransport","rtsp_h265+lsg2");
    setProperty("rvIdentitySource","lsg2_receipt_and_venc_progress");setProperty("rvBootId",m_dtofReceipt["boot_id"].toString());
    setProperty("rvGeneration",m_dtofToken);setProperty("rvModel","Native");setProperty("rvWidth",2304);
    setProperty("rvSourceWidth",2304);setProperty("rvSourceHeight",1296);setProperty("rvDecodedSequence",s.sequence);
    setProperty("rvEncodedSequence",m_lastEncodedFrame);setProperty("rvPresentedFrames",property("rvPresentedFrames").toULongLong()+1);
    const auto epoch=m_presentationEpoch;emit cameraFrameReady(s.image);
    if(m_userStopped||epoch!=m_presentationEpoch)return;
    emit videoFrameStamped(s.image, sourceSequence, s.receivedNs, m_dtofToken);
    if(m_userStopped||epoch!=m_presentationEpoch)return;
    if (!m_waitingForSelectedFrame)
        setState(tr("Playing"), tr("H.265 video and native %1×%2 ToF")
                 .arg(RvCaptureContract::columns(m_dtofMode)).arg(RvCaptureContract::rows(m_dtofMode)));
    ++m_framesInWindow;
    if (m_fpsClock.elapsed() >= 1000) {
        m_videoFps = m_framesInWindow * 1000.0 / m_fpsClock.elapsed();
        m_framesInWindow = 0;
        m_fpsClock.restart();
        emit stateChanged();
    }
}

QString RvSessionController::serviceVideoUrl() const {
    return QStringLiteral("rtsp://127.0.0.1:%1/live/0").arg(m_service->rgbPort);
}
void RvSessionController::launchServiceCapture(const QString &expectedBoot) {
    auto old=std::move(m_service->capture);
    m_service->heartbeat.stop();m_service->depth.reset();m_service->rgb.reset();
    const auto device=m_device;
    const auto mode=m_dtofMode;
    const auto generation=m_dtofToken;
    m_service->pending=true;
    m_service->operation.setFuture(QtConcurrent::run([=] {
        ServiceState::Result result;
        try {
            if(old)DeviceService::stop(*old);
            result.capture=std::make_shared<DeviceService::Capture>(DeviceService::start(device,mode,generation,expectedBoot));
        } catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}
        return result;
    }));
}
void RvSessionController::stopServiceCapture() {
    m_service->heartbeat.stop();m_service->depth.reset();m_service->rgb.reset();
    if(m_service->pending || !m_service->capture)return;
    auto c=std::move(m_service->capture);
    m_service->pending=true;
    m_service->operation.setFuture(QtConcurrent::run([c] {
        ServiceState::Result result;
        try {DeviceService::stop(*c);}
        catch(const std::exception &e){result.error=QString::fromUtf8(e.what());result.stopFailed=true;}
        return result;
    }));
}

#include "DepthSdk.h"
#include "../sensors/RvImagingSchemes.h"
#include "DepthSdkQt.h"
#include "../RuntimeDiscovery.h"
#include "../sensors/RvSessionController.h"
#include "../sensors/RvUsbEnrollment.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include "windows/DeviceService.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QFutureWatcher>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtCore/qscopeguard.h>
#include <algorithm>
#include <cstddef>

static_assert(sizeof(CxwlDepthOptions) == 24 && sizeof(CxwlDepthImage) == 32 && sizeof(CxwlDepthPoint) == 12,
              "ABI v3 public data layouts must not change");
static_assert(sizeof(CxwlDepthFrame) == 104 && offsetof(CxwlDepthFrame, image) == 64
              && offsetof(CxwlDepthFrame, metadata_json_utf8) == 96, "ABI v3 frame layout must not change");
static_assert(sizeof(CxwlDepthDeviceInfo) == 616 && sizeof(CxwlDepthFrameEx) == 176,
              "ABI v3 device/extended frame layouts must not change");
#include <cstring>
#include <iterator>
#include <mutex>
#include <unordered_set>
#include <limits>
#include <chrono>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <cmath>
#include <algorithm>

namespace {
QMutex sessionMutex;
CxwlDepthSession *activeSession = nullptr;
CxwlDepthDeviceManager *activeManager = nullptr;
bool changingSession = false;
struct BorrowedCloud {
    const CxwlDepthFrame *depth;
    CxwlDepthImage image;
};
thread_local BorrowedCloud callbackCloud{};
thread_local CxwlDepthErrorInfo lastError{sizeof(CxwlDepthErrorInfo)};
thread_local bool inLogCallback = false;
CxwlDepthLogCallback logCallback = nullptr;
void *logUser = nullptr;
uint32_t logSeverity = CXWL_DEPTH_LOG_INFO;

template<size_t N> void textField(char (&out)[N], const QByteArray &value) {
    const auto count = std::min(size_t(value.size()), N - 1);
    // Do not leave a partial UTF-8 sequence at the truncation boundary.
    size_t end = count;
    if (end < size_t(value.size()))
        while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
    std::memcpy(out, value.constData(), end); out[end] = 0;
}
template<class T> CxwlDepthResult outputStruct(T *output, const T &value) {
    if (!output) return CXWL_DEPTH_INVALID_ARGUMENT;
    if (output->size < sizeof(T)) return CXWL_DEPTH_BUFFER_TOO_SMALL;
    *output = value; output->size = sizeof(T); return CXWL_DEPTH_OK;
}
const char *resultText(CxwlDepthResult result) {
    switch (result) {
    case CXWL_DEPTH_INVALID_ARGUMENT: return "Invalid handle, options or argument.";
    case CXWL_DEPTH_BUSY: return "Operation requires an idle session or callback completion.";
    case CXWL_DEPTH_WRONG_THREAD: return "Call on the thread that created the session/manager.";
    case CXWL_DEPTH_DEVICE_NOT_FOUND: return "Selected device or verified USB attachment is unavailable.";
    case CXWL_DEPTH_BUFFER_TOO_SMALL: return "Output capacity/structure size is too small.";
    default: return "Operation unavailable; check runtime, offline guard and state diagnostics.";
    }
}
template<class Call> CxwlDepthResult publicCall(const char *operation, Call call) {
    if (inLogCallback) return CXWL_DEPTH_BUSY;
    CxwlDepthResult result;
    try { result = call(); } catch (...) { result = CXWL_DEPTH_UNAVAILABLE; }
    if (result != CXWL_DEPTH_OK) {
        lastError = {}; lastError.size = sizeof(lastError); lastError.code = result;
        textField(lastError.function_utf8, QByteArray(operation));
        textField(lastError.message_utf8, QByteArray(resultText(result)));
        CxwlDepthLogCallback callback; void *context; uint32_t severity;
        { std::lock_guard<QMutex> lock(sessionMutex); callback = logCallback; context = logUser; severity = logSeverity; }
        if (callback && severity >= CXWL_DEPTH_LOG_ERROR) {
            QScopedValueRollback<bool> guard(inLogCallback, true);
            const auto message = QByteArray(operation) + ": " + lastError.message_utf8;
            try { callback(context, CXWL_DEPTH_LOG_ERROR, message.constData()); } catch (...) {}
        }
    }
    return result;
}

QString sdkModuleDirectory()
{
    static const char moduleAnchor = 0;
#ifdef _WIN32
    HMODULE module = nullptr;
    wchar_t path[32768];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&moduleAnchor), &module)) return {};
    const DWORD length = GetModuleFileNameW(module, path, DWORD(std::size(path)));
    if (!length || length >= std::size(path)) return {};
    return QFileInfo(QString::fromWCharArray(path, int(length))).absolutePath();
#else
    Dl_info info{};
    if (!dladdr(&moduleAnchor, &info) || !info.dli_fname) return {};
    return QFileInfo(QFile::decodeName(info.dli_fname)).canonicalPath();
#endif
}

struct StandaloneApplication {
    int argc = 1;
    char name[13] = "CxwlDepthSdk";
    char *argv[2] = {name, nullptr};
    QCoreApplication application{argc, argv};
};

uint32_t modeOf(const RvSessionController &controller)
{
    for (const auto &scheme : RvDtofCandidates::ImagingSchemes)
        if (scheme.boardMode == controller.dtofCandidateMode()) return scheme.sdkMode;
    return CXWL_DEPTH_BOARD;
}

CxwlDepthImage rgbView(const QImage &image)
{
    if (image.isNull()) return {};
    return {image.constBits(), uint64_t(image.sizeInBytes()), uint32_t(image.width()),
            uint32_t(image.height()), uint32_t(image.bytesPerLine()), CXWL_DEPTH_PIXEL_RGB8};
}

QByteArray managedId(const RvDeviceProfile &device)
{
    if (device.id.isEmpty()) return {};
    return QCryptographicHash::hash(device.publicKey.toLatin1(), QCryptographicHash::Sha256).toHex();
}

QString managedStore()
{
    return QDir(RuntimeDiscovery::managedConfigurationDirectory()).filePath("devices.json");
}
}

struct CxwlDepthSession {
    const std::thread::id thread = std::this_thread::get_id();
    std::unique_ptr<StandaloneApplication> standalone;
    std::unique_ptr<RvSessionController> controller;
    RvUsbEnrollment *usb = nullptr;
    CxwlDepthFrameCallback onFrame = nullptr;
    CxwlDepthFrameExCallback onFrameEx = nullptr;
    RvTofFrame rawTof;
    QJsonObject observedMetadata;
    bool captureRequested = false;
    CxwlDepthEventCallback onEvent = nullptr;
    void *user = nullptr;
    std::function<void(uint32_t)> managerEvent;
    QString error;
    QString depthClearReason;
    QByteArray repairDevice;
    bool delivering = false;
    quint64 callbackRevision = 0;
    QEventLoop *callbackWaiter = nullptr;
    bool videoAvailable = false, depthAvailable = false, boardFilesRequired = false;
    bool managed = false, connectPending = false, disconnecting = false;
    quint64 connectRevision = 0;
    uint32_t target = CXWL_DEPTH_TARGET_AUTO;
    quint64 usbCheckId = 0;
    int switchEstimateMs = 0;

    ~CxwlDepthSession()
    {
        onFrame = nullptr;
        onFrameEx = nullptr;
        onEvent = nullptr;
        controller.reset();
        RuntimeDiscovery::setSdkPaths({}, {});
    }

    void noteCallback()
    {
        ++callbackRevision;
        if (callbackWaiter) callbackWaiter->quit();
    }

    void event(uint32_t type)
    {
        if (!onEvent || delivering) return;
        QScopedValueRollback<bool> dispatch(delivering, true);
        noteCallback();
        onEvent(user, type);
    }

    QByteArray deviceIdentifier() const
    {
        const auto device = controller->deviceProfile();
        if (!managed) return device.id.toUtf8();
        return managedId(device);
    }

    void video(const QImage &image, quint64 sequence, qint64 decodedNs, const QString &generation)
    {
        if (delivering || controller->userStopped()) return;
        if (!videoAvailable) {
            videoAvailable = true;
            event(CXWL_DEPTH_EVENT_STATE_CHANGED);
        }
        if (!onFrame && !onFrameEx) return;
        const auto rgb = image.convertToFormat(QImage::Format_RGB888);
        const auto device = deviceIdentifier();
        const auto capture = generation.toUtf8();
        CxwlDepthFrame frame{};
        frame.size = sizeof(frame);
        frame.stream = CXWL_DEPTH_STREAM_VIDEO;
        frame.mode = modeOf(*controller);
        frame.session_epoch = controller->presentationEpoch();
        frame.sequence = sequence;
        frame.timestamp_ns = uint64_t(decodedNs);
        frame.clock_domain = CXWL_DEPTH_CLOCK_HOST_MONOTONIC;
        frame.device_id_utf8 = device.constData();
        frame.capture_generation_utf8 = capture.constData();
        frame.image = rgbView(rgb);
        QScopedValueRollback<bool> dispatch(delivering, true);
        noteCallback();
        if (onFrame) onFrame(user, &frame);
        if (onFrameEx) { CxwlDepthFrameEx extended{}; extended.size = sizeof(extended); extended.frame = frame;
            onFrameEx(user, &extended); }
    }

    void depth(const QByteArray &bytes, const QJsonObject &metadata)
    {
        if (delivering || controller->userStopped()) return;
        const uint32_t width = RvCaptureContract::columns(controller->dtofCandidateMode());
        const uint32_t height = RvCaptureContract::rows(controller->dtofCandidateMode());
        if (metadata["depth_unit"] != "sensor_radial_m_float32" || metadata["width"] != int(width)
            || metadata["height"] != int(height) || bytes.size() != qsizetype(width) * height * 4) return;
        observedMetadata = metadata;
        const auto hostStart = metadata["raw_host_dispatch_ns"].toString().toULongLong();
        if (hostStart && metadata.contains("raw_board_age_ms") && metadata.contains("raw_request_roundtrip_ms")) {
            const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const double dispatchMs = std::max(0., (double(now)-double(hostStart))/1e6);
            observedMetadata["raw_callback_dispatch_ms"] = dispatchMs;
            observedMetadata["raw_age_lower_bound_ms"] = metadata["raw_board_age_ms"].toDouble()+dispatchMs;
            observedMetadata["raw_age_upper_bound_ms"] = metadata["raw_board_age_ms"].toDouble()+metadata["raw_request_roundtrip_ms"].toDouble()+dispatchMs;
        }
        if (!depthAvailable) {
            depthAvailable = true;
            event(CXWL_DEPTH_EVENT_STATE_CHANGED);
        }
        if (!onFrame && !onFrameEx) return;
        const auto device = deviceIdentifier();
        const auto capture = metadata["source_generation"].toString().toUtf8();
        const auto json = QJsonDocument(observedMetadata).toJson(QJsonDocument::Compact);

        CxwlDepthFrame frame{};
        frame.size = sizeof(frame);
        frame.stream = CXWL_DEPTH_STREAM_DEPTH;
        frame.mode = modeOf(*controller);
        frame.coordinate = CXWL_DEPTH_COORDINATE_SENSOR_RADIAL;
        frame.session_epoch = controller->presentationEpoch();
        frame.sequence = uint64_t(metadata["tof_sequence"].toInteger());
        frame.timestamp_ns = uint64_t(metadata["tof_timestamp_ns"].toInteger());
        frame.clock_domain = CXWL_DEPTH_CLOCK_BOARD_MONOTONIC;
        frame.device_id_utf8 = device.constData();
        frame.capture_generation_utf8 = capture.constData();
        frame.image = {bytes.constData(), uint64_t(bytes.size()), width, height, width * 4, CXWL_DEPTH_PIXEL_F32_LE};
        frame.metadata_json_utf8 = json.constData();
        QScopedValueRollback<bool> dispatch(delivering, true);
        const auto raw = rawTof;
        const bool matched = raw.isValid() && raw.columns() == int(width) && raw.rows() == int(height)
            && raw.metadata["tof_sequence"] == metadata["tof_sequence"]
            && raw.metadata["tof_timestamp_ns"] == metadata["tof_timestamp_ns"]
            && raw.metadata["generation"].toString() == metadata["source_generation"].toString();
        CxwlDepthImage xyz{};
        if (matched && raw.boardPoints.size() == qsizetype(width) * height * sizeof(CxwlDepthPoint)) {
            xyz = {raw.boardPoints.constData(), uint64_t(raw.boardPoints.size()), width, height,
                   width * uint32_t(sizeof(CxwlDepthPoint)), CXWL_DEPTH_PIXEL_XYZ_F32_LE};
        }
        noteCallback();
        if (onFrame) {
            QScopedValueRollback<BorrowedCloud> borrow(callbackCloud, {&frame, xyz});
            onFrame(user, &frame);
        }
        if (onFrameEx) {
            CxwlDepthFrameEx extended{}; extended.size = sizeof(extended); extended.frame = frame;
            QScopedValueRollback<BorrowedCloud> borrow(callbackCloud, {&extended.frame, xyz});
            QByteArray snr, valid;
            if (matched) {
                snr.resize(width * height); valid.resize(width * height);
                for (int i = 0; i < snr.size(); ++i) { snr[i] = char(raw.snr(i)); valid[i] = char(raw.valid(i)); }
                extended.snr = {snr.constData(), uint64_t(snr.size()), width, height, width, CXWL_DEPTH_PIXEL_U8};
                extended.valid_mask = {valid.constData(), uint64_t(valid.size()), width, height, width, CXWL_DEPTH_PIXEL_U8};
            }
            onFrameEx(user, &extended);
        }
    }

    void bindCallbacks()
    {
        auto *source = controller.get();
        QObject::connect(source, &RvSessionController::tofFrameReady, source,
                         [this](const RvTofFrame &raw) { rawTof = raw; });
        QObject::connect(source, &RvSessionController::videoFrameStamped, source,
                         [this](const QImage &rgb, quint64 sequence, qint64 decodedNs, const QString &generation) {
            video(rgb, sequence, decodedNs, generation);
        });
        QObject::connect(source, &RvSessionController::depthFrameReady, source,
                         [this](const QByteArray &bytes, const QJsonObject &metadata) { depth(bytes, metadata); });
        QObject::connect(source, &RvSessionController::stateChanged, source, [this] { event(CXWL_DEPTH_EVENT_STATE_CHANGED); });
        QObject::connect(source, &RvSessionController::configurationChanged, source, [this] { event(CXWL_DEPTH_EVENT_STATE_CHANGED); });
        QObject::connect(source, &RvSessionController::streamCleared, source, [this] {
            videoAvailable = false;
            event(CXWL_DEPTH_EVENT_VIDEO_CLEARED);
        });
        const auto clearDepth = [this](const QString &reason) {
            depthAvailable = false;
            rawTof = {}; observedMetadata = {};
            depthClearReason = reason;
            event(CXWL_DEPTH_EVENT_DEPTH_CLEARED);
        };
        QObject::connect(source, &RvSessionController::depthStreamCleared, source, clearDepth);
        QObject::connect(source, &RvSessionController::boardFilesRequired, source, [this](const RvDeviceProfile &device) {
            boardFilesRequired = true;
            repairDevice = managedId(device);
            event(CXWL_DEPTH_EVENT_BOARD_FILES_REQUIRED);
        });
        QObject::connect(source, &RvSessionController::recoveryDisplayExpired, source,
                         [this] { event(CXWL_DEPTH_EVENT_STATE_CHANGED); });
        QObject::connect(source, &RvSessionController::imagingSwitchStarted, source,
                         [this](const QString &, int estimateMs) {
            switchEstimateMs = estimateMs;
            event(CXWL_DEPTH_EVENT_STATE_CHANGED);
        });
        QObject::connect(source, &RvSessionController::imagingSwitchFinished, source,
                         [this] { event(CXWL_DEPTH_EVENT_STATE_CHANGED); });
        QObject::connect(source, &RvSessionController::imagingSwitchAborted, source,
                         [this] { event(CXWL_DEPTH_EVENT_STATE_CHANGED); });
        QObject::connect(source, &RvSessionController::deviceChanged, source, [this] {
            if (managerEvent) managerEvent(CXWL_DEPTH_EVENT_DEVICES_CHANGED);
        });
    }

    bool busy() const
    {
        return controller->busy() || controller->automaticConnecting() ||
            controller->imagingSwitchActive() || controller->controlProcessActive() || rvUsbOperationBusy(usb)
            || connectPending || disconnecting;
    }

    void prepareUsb()
    {
        if (usb) return;
        usb = createRvUsbEnrollment(controller.get(), [this] {
            if (managerEvent) managerEvent(CXWL_DEPTH_EVENT_USB_CHANGED);
            event(managed ? CXWL_DEPTH_EVENT_STATE_CHANGED : CXWL_DEPTH_EVENT_USB_CHANGED);
        });
    }

    void connectManagedDevice()
    {
        if (!connectPending) return;
        connectPending = false;
        try {
            if (target == CXWL_DEPTH_TARGET_SAVED) {
                controller->start();
                return;
            }
            prepareUsb();
            if (usbCheckId ? enrollCheckedRvUsbDevice(usb, usbCheckId, true, true)
                           : connectRvDevice(usb, target == CXWL_DEPTH_TARGET_AUTO)) return;
        } catch (const std::exception &) {}
        error = "usb_setup_failed";
        event(CXWL_DEPTH_EVENT_STATE_CHANGED);
    }

    QJsonObject managedState() const
    {
        QString state = "connecting";
        QString reason;
        const auto usbStatus = rvUsbOperationStatus(usb);
        if (disconnecting) state = "disconnecting";
        else if (!captureRequested) {
            if (busy()) state = "stopping";
            else if (controller->property("rvReleaseUnconfirmed").toBool()) {state="error";reason="capture_failed";}
            else state = "stopped";
        }
        else if (!connectPending && !rvUsbOperationBusy(usb)) {
            if (controller->userStopped()) {
                state = "error";
                if (boardFilesRequired) reason = "board_files_required";
                else if (target != CXWL_DEPTH_TARGET_SAVED
                         && (!error.isEmpty() || usbStatus["state"] == "error")) reason = "usb_setup_failed";
                else reason = "capture_failed";
            } else if (videoAvailable && depthAvailable) state = "connected";
            else if (controller->waitingForReconnect()) state = "reconnecting";
        }
        const int stage = connectPending || rvUsbOperationBusy(usb) ? CXWL_DEPTH_STAGE_CHECKING
            : int(controller->automaticConnectionStage());
        return {{"connection_state", state}, {"error", reason}, {"busy", busy()},
                {"mode", int(modeOf(*controller))}, {"video_available", videoAvailable}, {"depth_available", depthAvailable},
                {"connection_stage", stage}, {"retaining_display", controller->retainingRecoveryDisplay()},
                {"switch_active", controller->imagingSwitchActive()}, {"switch_estimated_ms", switchEstimateMs},
                {"video_fps", controller->videoFps()},
                {"usb_error", usbStatus["error"]}, {"depth_clear_reason", depthClearReason},
                {"board_files_device_id", QString::fromLatin1(repairDevice)}};
    }
};

struct CxwlDepthDeviceManager {
    std::unique_ptr<CxwlDepthSession> sdk;
    CxwlDepthEventCallback callback = nullptr;
    void *user = nullptr;
    struct Response { QJsonObject body; QString error; };
    QFutureWatcher<Response> request;
    QJsonObject serviceResponse;
    void launch(const RvDeviceProfile &device,const QString &revision,const QByteArray &op) {
        expectedProfile=device.json();expectedRevision=revision;
        request.setFuture(QtConcurrent::run([=] {
            Response result;
            try {
                result.body=DeviceService::manage(device,op);
                result.body["trusted_device"]=device.json();
                result.body["device_registry_revision"]=revision;
            } catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}
            return result;
        }));
    }
    QTimer timeout;
    QJsonObject maintenance{{"state", "idle"}};
    QJsonObject recovery{{"state", "idle"}};
    QJsonObject expectedProfile;
    QString expectedRevision;
    uint32_t nextTarget = CXWL_DEPTH_TARGET_AUTO;
    quint64 nextCheck = 0;
    bool closing = false;

    void event(uint32_t type)
    {
        if (!callback || sdk->delivering) return;
        QScopedValueRollback<bool> dispatch(sdk->delivering, true);
        callback(user, type);
    }

    bool maintaining() const { return maintenance["state"] == "running"; }
    bool recovering() const { return recovery["state"] == "running"; }

    QJsonObject response() {
        return serviceResponse;
    }
    QString diagnostic() {
        return {};
    }
    void completeMaintenance(int exitCode, const QString &failure = {})
    {
        const bool isRecovery = recovering();
        if (!maintaining() && !isRecovery) return;
        timeout.stop();
        auto *controller = sdk->controller.get();
        const auto receipt = response();
        const auto workerError = diagnostic();
        bool registryMatches = false;
        try { registryMatches = RvDeviceStore(controller->deviceStorePath()).revision() == expectedRevision; }
        catch (const std::exception &) {}
        const bool ready = exitCode == 0 && registryMatches && receipt["trusted_device"].toObject() == expectedProfile
            && receipt["device_registry_revision"] == expectedRevision && receipt["capture_runtime"] == "ready"
            && receipt["automatic_tof"] == "ready"
            && (!isRecovery || (receipt["recovery_protocol"] == "capture-fault-recovery-v1" && receipt["faults_cleared"] == true));
        auto &operation = isRecovery ? recovery : maintenance;
        const auto detail = failure.isEmpty() ? workerError : failure;
        operation = {{"state", ready ? "ready" : "error"}, {"exit_code", exitCode},
                       {"error", ready ? QString() : detail.isEmpty() ? QStringLiteral("Device operation receipt could not be verified.") : detail},
                       {"archived_rollbacks", receipt["archived_rollbacks"].toInt()},
                       {"runtime_replaced", receipt["runtime_replaced"].toBool()}};
        if (isRecovery) {
            operation["faults_cleared"] = ready && receipt["faults_cleared"].toBool();
            operation["cleared_fault_count"] = receipt["cleared_fault_count"].toInt();
            operation["board_archive"] = receipt["board_archive"].toString();
        }
        controller->endBoardFileOperation();
        event(isRecovery ? CXWL_DEPTH_EVENT_RECOVERY_CHANGED : CXWL_DEPTH_EVENT_MAINTENANCE_CHANGED);
    }
};

namespace {
std::unique_ptr<CxwlDepthSession> createSession(const QString &runtime, const QString &store)
{
    auto sdk = std::make_unique<CxwlDepthSession>();
    if (!QCoreApplication::instance()) sdk->standalone = std::make_unique<StandaloneApplication>();
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) return {};
    if (!runtime.isEmpty()) {
        if (sdk->standalone) QCoreApplication::addLibraryPath(runtime);
        const auto preferences = QFileInfo(store).absoluteDir().filePath("depth-sdk.ini");
        RuntimeDiscovery::setSdkPaths(runtime, preferences);
    }
    sdk->controller = std::make_unique<RvSessionController>(nullptr, store);
    if (!runtime.isEmpty()) sdk->bindCallbacks();
    return sdk;
}

CxwlDepthResult reserveSession()
{
    std::lock_guard<QMutex> lock(sessionMutex);
    if (activeSession || activeManager || changingSession) return CXWL_DEPTH_BUSY;
    changingSession = true;
    return CXWL_DEPTH_OK;
}

void publishSession(CxwlDepthSession *session)
{
    std::lock_guard<QMutex> lock(sessionMutex);
    activeSession = session;
    changingSession = false;
}

void releaseSession(CxwlDepthSession *session)
{
    {
        std::lock_guard<QMutex> lock(sessionMutex);
        activeSession = nullptr;
        changingSession = true;
    }
    delete session;
    publishSession(nullptr);
}

template <typename Call>
CxwlDepthResult withManager(CxwlDepthDeviceManager *manager, Call call, bool readOnly = false)
{
    {
        std::lock_guard<QMutex> lock(sessionMutex);
        if (!manager || manager != activeManager) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (manager->sdk->thread != std::this_thread::get_id()) return CXWL_DEPTH_WRONG_THREAD;
    }
    if (!readOnly && (manager->sdk->delivering || manager->sdk->callbackWaiter)) return CXWL_DEPTH_BUSY;
    try {
        return call(*manager);
    } catch (...) {
        return CXWL_DEPTH_UNAVAILABLE;
    }
}

bool idleManager(const CxwlDepthDeviceManager &manager)
{
    std::lock_guard<QMutex> lock(sessionMutex);
    return !activeSession && !manager.closing
        && !manager.sdk->busy() && manager.sdk->controller->userStopped();
}

void beginManagedConnection(CxwlDepthSession &sdk, const RvDtofCandidates::ImagingScheme &scheme, CxwlDepthFrameCallback frame,
                            CxwlDepthEventCallback event, void *user, uint32_t target, quint64 checkId)
{
    sdk.managed = true;
    sdk.captureRequested = true;
    sdk.rawTof = {}; sdk.observedMetadata = {};
    sdk.error.clear();
    sdk.repairDevice.clear();
    sdk.depthClearReason.clear();
    sdk.boardFilesRequired = false;
    sdk.videoAvailable = false;
    sdk.depthAvailable = false;
    sdk.switchEstimateMs = 0;
    sdk.controller->selectImagingScheme(QLatin1String(scheme.name));
    sdk.onFrame = frame;
    sdk.onFrameEx = nullptr;
    sdk.onEvent = event;
    sdk.user = user;
    sdk.target = target;
    sdk.usbCheckId = checkId;
    sdk.connectPending = true;
    const auto revision = ++sdk.connectRevision;
    QTimer::singleShot(0, sdk.controller.get(), [&sdk, revision] {
        if (sdk.connectRevision == revision) sdk.connectManagedDevice();
    });
}

CxwlDepthResult disconnectSession(CxwlDepthSession &sdk)
{
    if (!sdk.disconnecting) {
        sdk.disconnecting = true;
        sdk.captureRequested = false;
        sdk.connectPending = false;
        ++sdk.connectRevision;
        sdk.controller->stop();
    }
    if (rvUsbOperationBusy(sdk.usb) || sdk.controller->controlProcessActive()) return CXWL_DEPTH_BUSY;
    if (sdk.managerEvent) {
        sdk.onFrame = nullptr;
        sdk.onFrameEx = nullptr;
        sdk.onEvent = nullptr;
        sdk.user = nullptr;
        sdk.disconnecting = false;
        publishSession(nullptr);
    } else {
        releaseSession(&sdk);
    }
    return CXWL_DEPTH_OK;
}

template <typename Call>
CxwlDepthResult withSession(CxwlDepthSession *session, Call call, bool readOnly = false)
{
    {
        std::lock_guard<QMutex> lock(sessionMutex);
        if (!session || session != activeSession) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (session->thread != std::this_thread::get_id()) return CXWL_DEPTH_WRONG_THREAD;
    }
    if (!readOnly && (session->delivering || session->callbackWaiter)) return CXWL_DEPTH_BUSY;
    try {
        return call(*session);
    } catch (const std::exception &error) {
        session->error = QString::fromUtf8(error.what());
        return CXWL_DEPTH_UNAVAILABLE;
    } catch (...) {
        return CXWL_DEPTH_UNAVAILABLE;
    }
}

QMutex jsonAllocationMutex;
std::unordered_set<char *> jsonAllocations;
CxwlDepthResult allocateJson(const QJsonDocument &document, char **out, uint32_t *size)
{
    const auto json = document.toJson(QJsonDocument::Compact);
    if (uint64_t(json.size()) >= UINT32_MAX) return CXWL_DEPTH_UNAVAILABLE;
    const auto count = uint32_t(json.size()) + 1;
    auto owned = std::make_unique<char[]>(count);
    std::memcpy(owned.get(), json.constData(), count);
    { std::lock_guard<QMutex> lock(jsonAllocationMutex); jsonAllocations.insert(owned.get()); }
    *size = count; *out = owned.release(); return CXWL_DEPTH_OK;
}

CxwlDepthResult copyJson(const QJsonDocument &document, char *buffer, uint32_t *size)
{
    if (!size) return CXWL_DEPTH_INVALID_ARGUMENT;
    const auto json = document.toJson(QJsonDocument::Compact);
    const uint32_t required = uint32_t(json.size()) + 1;
    const uint32_t capacity = buffer ? *size : 0;
    *size = required;
    if (!buffer) return CXWL_DEPTH_OK;
    if (capacity < required) return CXWL_DEPTH_BUFFER_TOO_SMALL;
    std::memcpy(buffer, json.constData(), required);
    return CXWL_DEPTH_OK;
}
}

// Shared result boundary for the allocation-free projection translation unit.
CxwlDepthResult cxwlDepthReportResult(const char *operation, CxwlDepthResult result)
{
    return publicCall(operation, [result] { return result; });
}

CxwlDepthResult cxwlDepthWritePointCloudPly(const CxwlDepthFrame *frame, const char *path);

static CxwlDepthFrame boardCloudView(const CxwlDepthFrame &depth)
{
    auto cloud = depth;
    cloud.stream = CXWL_DEPTH_STREAM_POINT_CLOUD;
    cloud.coordinate = CXWL_DEPTH_COORDINATE_SENSOR_XYZ;
    cloud.image = callbackCloud.image;
    return cloud;
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetFramePointCloud(const CxwlDepthFrame *depth, CxwlDepthFrame *cloud)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!depth || !cloud || depth == cloud) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (cloud->size < sizeof(*cloud)) return CXWL_DEPTH_BUFFER_TOO_SMALL;
        if (callbackCloud.depth != depth || !callbackCloud.image.data) return CXWL_DEPTH_UNAVAILABLE;
        return outputStruct(cloud, boardCloudView(*depth));
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SavePointCloudPly(const CxwlDepthFrame *frame, const char *path_utf8)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (frame && frame == callbackCloud.depth) {
            if (!callbackCloud.image.data) return CXWL_DEPTH_UNAVAILABLE;
            const auto cloud = boardCloudView(*frame);
            return cxwlDepthWritePointCloudPly(&cloud, path_utf8);
        }
        return cxwlDepthWritePointCloudPly(frame, path_utf8);
    });
}

RvSessionController *DepthSdkQt::validationController(CxwlDepthDeviceManager *manager)
{
    RvSessionController *controller = nullptr;
    withManager(manager, [&](CxwlDepthDeviceManager &entry) {
        controller = entry.sdk->controller.get();
        return CXWL_DEPTH_OK;
    }, true);
    return controller;
}

uint32_t CXWL_DEPTH_CALL CxwlDepth_AbiVersion(void)
{
    return CXWL_DEPTH_ABI_VERSION;
}

CxwlDepthSession *DepthSdkQt::validationSession(CxwlDepthDeviceManager *manager, bool managed)
{
    CxwlDepthSession *session = nullptr;
    if (qEnvironmentVariable("RV_TEST_OFFLINE_ONLY") != "1") return nullptr;
    withManager(manager, [&](CxwlDepthDeviceManager &entry) {
        if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
        entry.sdk->managed = managed; entry.sdk->captureRequested = false;
        session = entry.sdk.get(); publishSession(session); return CXWL_DEPTH_OK;
    });
    return session;
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Connect(
    uint32_t mode, CxwlDepthFrameCallback frame, CxwlDepthEventCallback event, void *user, CxwlDepthSession **session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    CxwlDepthConnectOptions options{};
    options.size = sizeof(options); options.abi_version = CXWL_DEPTH_ABI_VERSION; options.mode = mode;
    CxwlDepthDeviceManager *manager;
    { std::lock_guard<QMutex> lock(sessionMutex); manager = activeManager; }
    if (manager) {
        const auto result = withManager(manager, [&](CxwlDepthDeviceManager &entry) {
            options.target = entry.nextTarget; options.usb_check_id = entry.nextCheck; return CXWL_DEPTH_OK;
        }, true);
        if (result != CXWL_DEPTH_OK) { if (session) *session = nullptr; return result; }
    }
    const auto result = CxwlDepth_ConnectWithOptions(&options, frame, event, user, session);
    if (result == CXWL_DEPTH_OK && manager) { manager->nextTarget = CXWL_DEPTH_TARGET_AUTO; manager->nextCheck = 0; }
    return result;

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ConnectWithOptions(
    const CxwlDepthConnectOptions *input, CxwlDepthFrameCallback frame,
    CxwlDepthEventCallback event, void *user, CxwlDepthSession **session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (!session) return CXWL_DEPTH_INVALID_ARGUMENT;
    *session = nullptr;
    CxwlDepthConnectOptions options{};
    options.size = sizeof(options); options.abi_version = CXWL_DEPTH_ABI_VERSION; options.mode = CXWL_DEPTH_BOARD;
    if (input) {
        if (input->size < sizeof(options)) return CXWL_DEPTH_INVALID_ARGUMENT;
        options = *input;
    }
    const auto *scheme = RvDtofCandidates::schemeForSdkMode(options.mode);
    if (options.abi_version != CXWL_DEPTH_ABI_VERSION || !scheme
        || options.target > CXWL_DEPTH_TARGET_USB || options.policy > CXWL_DEPTH_CONNECT_SAVED_ONLY
        || (options.usb_check_id && options.target != CXWL_DEPTH_TARGET_USB)
        || (options.device_id_utf8 && (!*options.device_id_utf8 || options.target != CXWL_DEPTH_TARGET_SAVED))
        || (options.policy == CXWL_DEPTH_CONNECT_SAVED_ONLY && options.target == CXWL_DEPTH_TARGET_USB))
        return CXWL_DEPTH_INVALID_ARGUMENT;
    if (options.policy == CXWL_DEPTH_CONNECT_SAVED_ONLY) options.target = CXWL_DEPTH_TARGET_SAVED;
    if (qEnvironmentVariable("RV_TEST_OFFLINE_ONLY") == "1") return CXWL_DEPTH_UNAVAILABLE;
    CxwlDepthDeviceManager *manager;
    { std::lock_guard<QMutex> lock(sessionMutex); manager = activeManager; }
    const auto selectTarget = [&](CxwlDepthSession &sdk) -> CxwlDepthResult {
        if (options.device_id_utf8) {
            QString selected;
            for (const auto &device : RvDeviceStore(sdk.controller->deviceStorePath()).profiles())
                if (device.id == QString::fromUtf8(options.device_id_utf8) || managedId(device) == options.device_id_utf8)
                    selected = device.id;
            if (selected.isEmpty() || !sdk.controller->selectDevice(selected)) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        }
        if (options.target == CXWL_DEPTH_TARGET_SAVED && !sdk.controller->deviceProfile().ready())
            return CXWL_DEPTH_DEVICE_NOT_FOUND;
        if (options.usb_check_id && rvUsbOperationStatus(sdk.usb)["check_id"].toInteger() != qint64(options.usb_check_id))
            return CXWL_DEPTH_DEVICE_NOT_FOUND;
        beginManagedConnection(sdk, *scheme, frame, event, user, options.target, options.usb_check_id);
        return CXWL_DEPTH_OK;
    };
    if (manager) return withManager(manager, [&](CxwlDepthDeviceManager &entry) -> CxwlDepthResult {
        if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
        // A manager owns runtime paths; conflicting overrides are never silently ignored.
        if (options.runtime_directory_utf8 || options.device_store_utf8) return CXWL_DEPTH_INVALID_ARGUMENT;
        // A verified USB check is bound to stopRevision and the registry revision.
        // Reloading profiles calls stop() and would invalidate the check ourselves.
        // enrollCheckedRvUsbDevice still revalidates both revisions and the USB identity.
        if (!options.device_id_utf8 && !options.usb_check_id) entry.sdk->controller->reloadDeviceProfiles();
        const auto result = selectTarget(*entry.sdk);
        if (result == CXWL_DEPTH_OK) { *session = entry.sdk.get(); publishSession(*session); }
        return result;
    });
    if (options.usb_check_id) return CXWL_DEPTH_INVALID_ARGUMENT;
    const auto runtime = options.runtime_directory_utf8 ? QByteArray(options.runtime_directory_utf8) : sdkModuleDirectory().toUtf8();
    const auto store = options.device_store_utf8 ? QByteArray(options.device_store_utf8) : managedStore().toUtf8();
    CxwlDepthOptions paths{sizeof(paths), CXWL_DEPTH_ABI_VERSION, runtime.constData(), store.constData()};
    auto result = CxwlDepth_Open(&paths, session);
    if (result != CXWL_DEPTH_OK) return result;
    try { result = selectTarget(**session); }
    catch (...) { releaseSession(*session); *session = nullptr; throw; }
    if (result != CXWL_DEPTH_OK) { releaseSession(*session); *session = nullptr; }
    return result;

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Disconnect(CxwlDepthSession *session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, disconnectSession);

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Open(const CxwlDepthOptions *options, CxwlDepthSession **session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (!session) return CXWL_DEPTH_INVALID_ARGUMENT;
    *session = nullptr;
    if (!options || options->size < sizeof(*options) || options->abi_version != CXWL_DEPTH_ABI_VERSION
        || !options->runtime_directory_utf8 || !options->device_store_utf8) return CXWL_DEPTH_INVALID_ARGUMENT;
    const auto reserved = reserveSession();
    if (reserved != CXWL_DEPTH_OK) return reserved;
    auto abandon = qScopeGuard([] { publishSession(nullptr); });
    try {
        const auto runtime = QString::fromUtf8(options->runtime_directory_utf8);
        const auto store = QString::fromUtf8(options->device_store_utf8);
        if (!QDir::isAbsolutePath(runtime) || !QFileInfo(runtime).isDir() ||
            !QDir::isAbsolutePath(store) || QFileInfo(store).isDir()) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread())
            return CXWL_DEPTH_WRONG_THREAD;
        auto sdk = createSession(QDir::cleanPath(runtime), QDir::cleanPath(store));
        if (!sdk) return CXWL_DEPTH_UNAVAILABLE;
        *session = sdk.release();
        publishSession(*session);
        abandon.dismiss();
        return CXWL_DEPTH_OK;
    } catch (...) {
        return CXWL_DEPTH_UNAVAILABLE;
    }

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Close(CxwlDepthSession *session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, disconnectSession);

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Subscribe(
    CxwlDepthSession *session, CxwlDepthFrameCallback frame, CxwlDepthEventCallback event, void *user)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [&](CxwlDepthSession &sdk) {
        sdk.onFrame = frame;
    sdk.onFrameEx = nullptr;
        sdk.onEvent = event;
        sdk.user = user;
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Poll(CxwlDepthSession *session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [](CxwlDepthSession &sdk) {
        if (!sdk.standalone) return CXWL_DEPTH_INVALID_ARGUMENT;
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_WaitForEvent(
    CxwlDepthSession *session, uint32_t timeout_ms, uint32_t *dispatched)
{
    if (dispatched) *dispatched = 0;
    return publicCall(__func__, [&] {
        return withSession(session, [&](CxwlDepthSession &sdk) {
            if (!sdk.standalone || !dispatched || timeout_ms > INT32_MAX)
                return CXWL_DEPTH_INVALID_ARGUMENT;
            const auto before = sdk.callbackRevision;
            QEventLoop loop;
            QScopedValueRollback<QEventLoop *> waiting(sdk.callbackWaiter, &loop);
            if (timeout_ms) {
                QTimer timeout;
                timeout.setSingleShot(true);
                timeout.setTimerType(Qt::PreciseTimer);
                QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                timeout.start(int(timeout_ms));
                loop.exec();
            } else {
                QCoreApplication::processEvents();
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            *dispatched = sdk.callbackRevision != before;
            return CXWL_DEPTH_OK;
        });
    });
}

template<class Sink>
static CxwlDepthResult queryListDevices(CxwlDepthSession *session, Sink sink)
{
    return withSession(session, [&](CxwlDepthSession &sdk) -> CxwlDepthResult {
        const auto current = sdk.controller->deviceProfile().id;
        QJsonArray supportedModes;
        for (const auto &scheme : RvDtofCandidates::ImagingSchemes) supportedModes.append(int(scheme.sdkMode));
        QJsonArray rows;
        for (const auto &device : RvDeviceStore(sdk.controller->deviceStorePath()).profiles()) {
            rows.append(QJsonObject{{"id", sdk.managed ? QString::fromLatin1(managedId(device)) : device.id}, {"name", device.name}, {"host", device.host},
                                    {"serial", device.serial}, {"ready", device.ready()}, {"selected", device.id == current},
                                    {"family", "CXWL Depth Camera"}, {"modes", QJsonArray{CXWL_DEPTH_BOARD, CXWL_DEPTH_BOARD_HIGH_ACCURACY}},
                                    {"supported_modes", supportedModes}});
        }
        return sink(QJsonDocument(rows));
    }, true);

}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListDevices(CxwlDepthSession *session, char *buffer, uint32_t *size)
{
    return publicCall(__func__, [&]() {
        return queryListDevices(session, [&](const QJsonDocument &document) { return copyJson(document, buffer, size); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListDevicesAlloc(CxwlDepthSession *session, char **json_utf8, uint32_t *byte_count)
{
    if (json_utf8) *json_utf8 = nullptr;
    if (byte_count) *byte_count = 0;
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!json_utf8 || !byte_count) return CXWL_DEPTH_INVALID_ARGUMENT;
        return queryListDevices(session, [&](const QJsonDocument &document) { return allocateJson(document, json_utf8, byte_count); });
    });
}

template<class Sink>
static CxwlDepthResult queryGetState(CxwlDepthSession *session, Sink sink)
{
    return withSession(session, [&](CxwlDepthSession &sdk) {
        if (sdk.managed) return sink(QJsonDocument(sdk.managedState()));
        const auto &source = *sdk.controller;
        QJsonObject state{
            {"device_id", source.deviceProfile().id}, {"mode", int(modeOf(source))},
            {"user_stopped", source.userStopped()}, {"busy", sdk.busy()},
            {"retry_pending", source.waitingForReconnect()}, {"retaining_display", source.retainingRecoveryDisplay()},
            {"video_available", sdk.videoAvailable}, {"depth_available", sdk.depthAvailable},
            {"board_files_required", sdk.boardFilesRequired}, {"connection_stage", int(source.automaticConnectionStage())},
            {"epoch", qint64(source.presentationEpoch())}, {"state", source.stateText()}, {"detail", source.detailText()},
            {"error", sdk.error}, {"video_fps", source.videoFps()}
        };
        const auto common = sdk.managedState();
        for (auto it = common.begin(); it != common.end(); ++it)
            if (!state.contains(it.key())) state[it.key()] = it.value();
        return sink(QJsonDocument(state));
    }, true);

}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetState(CxwlDepthSession *session, char *buffer, uint32_t *size)
{
    return publicCall(__func__, [&]() {
        return queryGetState(session, [&](const QJsonDocument &document) { return copyJson(document, buffer, size); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetStateAlloc(CxwlDepthSession *session, char **json_utf8, uint32_t *byte_count)
{
    if (json_utf8) *json_utf8 = nullptr;
    if (byte_count) *byte_count = 0;
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!json_utf8 || !byte_count) return CXWL_DEPTH_INVALID_ARGUMENT;
        return queryGetState(session, [&](const QJsonDocument &document) { return allocateJson(document, json_utf8, byte_count); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SelectDevice(CxwlDepthSession *session, const char *id)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [&](CxwlDepthSession &sdk) {
        if (!id || !*id) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (!sdk.controller->userStopped() || sdk.busy()) return CXWL_DEPTH_BUSY;
        QString selected;
        for (const auto &device : RvDeviceStore(sdk.controller->deviceStorePath()).profiles())
            if (device.id == QString::fromUtf8(id) || managedId(device) == id) { selected = device.id; break; }
        if (selected.isEmpty() || !sdk.controller->selectDevice(selected)) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        sdk.rawTof = {}; sdk.observedMetadata = {};
        sdk.target = CXWL_DEPTH_TARGET_SAVED; sdk.usbCheckId = 0;
        sdk.error.clear();
        sdk.boardFilesRequired = false;
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetMode(CxwlDepthSession *session, uint32_t mode)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [&](CxwlDepthSession &sdk) {
        const auto *scheme = RvDtofCandidates::schemeForSdkMode(mode);
        if (!scheme) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (sdk.controller->deviceProfile().id.isEmpty()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        if (sdk.busy()) return CXWL_DEPTH_BUSY;
        sdk.error.clear();
        sdk.controller->selectImagingScheme(QLatin1String(scheme->name));
        return modeOf(*sdk.controller) == mode ? CXWL_DEPTH_OK : CXWL_DEPTH_UNAVAILABLE;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Start(CxwlDepthSession *session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [](CxwlDepthSession &sdk) {
        if (!sdk.controller->deviceProfile().ready()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        if (sdk.busy()) return CXWL_DEPTH_BUSY;
        sdk.error.clear();
        sdk.boardFilesRequired = false;
        sdk.captureRequested = true;
        sdk.rawTof = {}; sdk.observedMetadata = {};
        sdk.controller->start();
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Stop(CxwlDepthSession *session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [](CxwlDepthSession &sdk) {
        sdk.error.clear();
        sdk.captureRequested = false;
        sdk.connectPending = false; ++sdk.connectRevision;
        sdk.rawTof = {}; sdk.observedMetadata = {};
        sdk.controller->stop();
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Reconnect(CxwlDepthSession *session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [](CxwlDepthSession &sdk) {
        if (!sdk.controller->deviceProfile().ready()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        if (sdk.busy()) return CXWL_DEPTH_BUSY;
        sdk.error.clear();
        sdk.repairDevice.clear();
        sdk.boardFilesRequired = false;
        sdk.captureRequested = true;
        sdk.controller->reconnectNow();
        return CXWL_DEPTH_OK;
    });

    });
}


CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_CheckUsb(CxwlDepthSession *session)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [](CxwlDepthSession &sdk) {
        if (sdk.busy() || !sdk.controller->userStopped()) return CXWL_DEPTH_BUSY;
        sdk.prepareUsb();
        if (!sdk.usb) return CXWL_DEPTH_UNAVAILABLE;
        return checkRvUsbDevice(sdk.usb) ? CXWL_DEPTH_OK : CXWL_DEPTH_BUSY;
    });

    });
}

template<class Sink>
static CxwlDepthResult queryGetUsbStatus(CxwlDepthSession *session, Sink sink)
{
    return withSession(session, [&](CxwlDepthSession &sdk) -> CxwlDepthResult {
        return sink(QJsonDocument(rvUsbOperationStatus(sdk.usb)));
    }, true);

}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetUsbStatus(CxwlDepthSession *session, char *buffer, uint32_t *size)
{
    return publicCall(__func__, [&]() {
        return queryGetUsbStatus(session, [&](const QJsonDocument &document) { return copyJson(document, buffer, size); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetUsbStatusAlloc(CxwlDepthSession *session, char **json_utf8, uint32_t *byte_count)
{
    if (json_utf8) *json_utf8 = nullptr;
    if (byte_count) *byte_count = 0;
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!json_utf8 || !byte_count) return CXWL_DEPTH_INVALID_ARGUMENT;
        return queryGetUsbStatus(session, [&](const QJsonDocument &document) { return allocateJson(document, json_utf8, byte_count); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_EnrollUsb(CxwlDepthSession *session, uint64_t checkId, uint32_t startAfter)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [&](CxwlDepthSession &sdk) {
        if (!checkId || startAfter > 1) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (sdk.busy() || !sdk.controller->userStopped()) return CXWL_DEPTH_BUSY;
        if (!enrollCheckedRvUsbDevice(sdk.usb, checkId, startAfter != 0)) return CXWL_DEPTH_UNAVAILABLE;
        sdk.captureRequested = startAfter != 0;
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_OpenDeviceManager(
    const CxwlDepthOptions *options, CxwlDepthEventCallback event, void *user, CxwlDepthDeviceManager **manager)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (!manager) return CXWL_DEPTH_INVALID_ARGUMENT;
    *manager = nullptr;
    if (options && (options->size < sizeof(*options) || options->abi_version != CXWL_DEPTH_ABI_VERSION
                    || !options->runtime_directory_utf8 || !options->device_store_utf8)) return CXWL_DEPTH_INVALID_ARGUMENT;
    const auto reserved = reserveSession();
    if (reserved != CXWL_DEPTH_OK) return reserved;
    auto abandon = qScopeGuard([] { publishSession(nullptr); });
    try {
        const auto runtime = options ? QString::fromUtf8(options->runtime_directory_utf8) : sdkModuleDirectory();
        const auto store = options ? QString::fromUtf8(options->device_store_utf8) : managedStore();
        if (!QDir::isAbsolutePath(runtime) || !QFileInfo(runtime).isDir()
            || !QDir::isAbsolutePath(store) || QFileInfo(store).isDir()) return CXWL_DEPTH_INVALID_ARGUMENT;
        if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread())
            return CXWL_DEPTH_WRONG_THREAD;
        auto sdk = createSession(QDir::cleanPath(runtime), QDir::cleanPath(store));
        if (!sdk) return CXWL_DEPTH_UNAVAILABLE;
        auto entry = std::make_unique<CxwlDepthDeviceManager>();
        entry->sdk = std::move(sdk);
        entry->callback = event;
        entry->user = user;
        auto *owner = entry.get();
        entry->sdk->managerEvent = [owner](uint32_t type) { owner->event(type); };
        entry->timeout.setSingleShot(true);
        QObject::connect(&owner->request,&QFutureWatcher<CxwlDepthDeviceManager::Response>::finished,&owner->request,[owner] {
            const auto reply=owner->request.result();owner->serviceResponse=reply.body;
            owner->completeMaintenance(reply.error.isEmpty()?0:2,reply.error);
        });
        {
            std::lock_guard<QMutex> lock(sessionMutex);
            *manager = activeManager = entry.release();
            changingSession = false;
        }
        abandon.dismiss();
        return CXWL_DEPTH_OK;
    } catch (...) {
        return CXWL_DEPTH_UNAVAILABLE;
    }

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_CloseDeviceManager(CxwlDepthDeviceManager *manager)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withManager(manager, [](CxwlDepthDeviceManager &entry) {
        {
            std::lock_guard<QMutex> lock(sessionMutex);
            if (activeSession) return CXWL_DEPTH_BUSY;
            entry.closing = true;
            if (entry.sdk->busy()) return CXWL_DEPTH_BUSY;
            activeManager = nullptr;
            changingSession = true;
        }
        delete &entry;
        publishSession(nullptr);
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_PollDeviceManager(CxwlDepthDeviceManager *manager)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withManager(manager, [](CxwlDepthDeviceManager &entry) {
        if (!entry.sdk->standalone) return CXWL_DEPTH_INVALID_ARGUMENT;
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return CXWL_DEPTH_OK;
    });

    });
}

template<class Sink>
static CxwlDepthResult queryListSavedDevices(CxwlDepthDeviceManager *manager, Sink sink)
{
    return withManager(manager, [&](CxwlDepthDeviceManager &entry) {
        const RvDeviceStore store(entry.sdk->controller->deviceStorePath());
        QJsonArray devices;
        for (const auto &device : store.profiles()) {
            const auto name = device.name == device.defaultName() ? QString() : device.name;
            devices.append(QJsonObject{{"id", QString::fromLatin1(managedId(device))}, {"name", name},
                                       {"selected", device.id == store.activeId()}, {"ready", device.ready()}});
        }
        return sink(QJsonDocument(devices));
    }, true);

}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListSavedDevices(CxwlDepthDeviceManager *manager, char *buffer, uint32_t *size)
{
    return publicCall(__func__, [&]() {
        return queryListSavedDevices(manager, [&](const QJsonDocument &document) { return copyJson(document, buffer, size); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListSavedDevicesAlloc(CxwlDepthDeviceManager *manager, char **json_utf8, uint32_t *byte_count)
{
    if (json_utf8) *json_utf8 = nullptr;
    if (byte_count) *byte_count = 0;
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!json_utf8 || !byte_count) return CXWL_DEPTH_INVALID_ARGUMENT;
        return queryListSavedDevices(manager, [&](const QJsonDocument &document) { return allocateJson(document, json_utf8, byte_count); });
    });
}

namespace {
RvDeviceProfile findManagedDevice(const RvDeviceStore &store, const char *id)
{
    for (const auto &device : store.profiles())
        if (managedId(device) == id) return device;
    return {};
}
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetActiveDevice(CxwlDepthDeviceManager *manager, const char *id)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (!id || !*id) return CXWL_DEPTH_INVALID_ARGUMENT;
    return withManager(manager, [&](CxwlDepthDeviceManager &entry) -> CxwlDepthResult {
        if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
        auto *controller = entry.sdk->controller.get();
        RvDeviceStore store(controller->deviceStorePath());
        const auto device = findManagedDevice(store, id);
        if (!device.ready()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        store.save(store.profiles(), device.id);
        controller->reloadDeviceProfiles();
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_RemoveSavedDevice(CxwlDepthDeviceManager *manager, const char *id)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (!id || !*id) return CXWL_DEPTH_INVALID_ARGUMENT;
    return withManager(manager, [&](CxwlDepthDeviceManager &entry) -> CxwlDepthResult {
        if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
        auto *controller = entry.sdk->controller.get();
        RvDeviceStore store(controller->deviceStorePath());
        const auto device = findManagedDevice(store, id);
        if (device.id.isEmpty()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        auto profiles = store.profiles();
        profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                      [&](const RvDeviceProfile &profile) { return profile.id == device.id; }), profiles.end());
        store.save(profiles, store.activeId() == device.id ? QString() : store.activeId());
        controller->reloadDeviceProfiles();
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ScanUsb(CxwlDepthDeviceManager *manager)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withManager(manager, [](CxwlDepthDeviceManager &entry) {
        if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
        entry.sdk->prepareUsb();
        if (!entry.sdk->usb) return CXWL_DEPTH_UNAVAILABLE;
        return checkRvUsbDevice(entry.sdk->usb) ? CXWL_DEPTH_OK : CXWL_DEPTH_BUSY;
    });

    });
}

template<class Sink>
static CxwlDepthResult queryGetDeviceManagerState(CxwlDepthDeviceManager *manager, Sink sink)
{
    return withManager(manager, [&](CxwlDepthDeviceManager &entry) {
        auto usb = rvUsbOperationStatus(entry.sdk->usb);
        usb.remove("device_id");
        const QJsonObject state{{"busy", entry.sdk->busy()}, {"can_manage", idleManager(entry)},
                                {"usb", usb}, {"maintenance", entry.maintenance}, {"recovery", entry.recovery}};
        return sink(QJsonDocument(state));
    }, true);

}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetDeviceManagerState(CxwlDepthDeviceManager *manager, char *buffer, uint32_t *size)
{
    return publicCall(__func__, [&]() {
        return queryGetDeviceManagerState(manager, [&](const QJsonDocument &document) { return copyJson(document, buffer, size); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetDeviceManagerStateAlloc(CxwlDepthDeviceManager *manager, char **json_utf8, uint32_t *byte_count)
{
    if (json_utf8) *json_utf8 = nullptr;
    if (byte_count) *byte_count = 0;
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!json_utf8 || !byte_count) return CXWL_DEPTH_INVALID_ARGUMENT;
        return queryGetDeviceManagerState(manager, [&](const QJsonDocument &document) { return allocateJson(document, json_utf8, byte_count); });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetConnectionTarget(CxwlDepthDeviceManager *manager, uint32_t target, uint64_t checkId)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (target > CXWL_DEPTH_TARGET_USB || (target != CXWL_DEPTH_TARGET_USB && checkId))
        return CXWL_DEPTH_INVALID_ARGUMENT;
    return withManager(manager, [&](CxwlDepthDeviceManager &entry) -> CxwlDepthResult {
        if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
        if (checkId && rvUsbOperationStatus(entry.sdk->usb)["check_id"].toInteger() != qint64(checkId))
            return CXWL_DEPTH_DEVICE_NOT_FOUND;
        entry.nextTarget = target;
        entry.nextCheck = checkId;
        return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_MaintainBoardFiles(CxwlDepthDeviceManager *manager, const char *id, uint32_t operation)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (!id || !*id || operation > CXWL_DEPTH_BOARD_MAINTENANCE) return CXWL_DEPTH_INVALID_ARGUMENT;
    return withManager(manager, [&](CxwlDepthDeviceManager &entry) -> CxwlDepthResult {
        if (qEnvironmentVariable("RV_TEST_OFFLINE_ONLY") == "1") return CXWL_DEPTH_UNAVAILABLE;
        if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
        auto *controller = entry.sdk->controller.get();
        const RvDeviceStore store(controller->deviceStorePath());
        const auto device = findManagedDevice(store, id);
        if (!device.ready()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        if (operation == CXWL_DEPTH_BOARD_REPAIR && entry.sdk->boardFilesRequired
            && (entry.sdk->repairDevice != id || device.id != store.activeId()
                || device.json() != controller->deviceProfile().json())) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        if (!controller->beginBoardFileOperation()) return CXWL_DEPTH_BUSY;
        entry.maintenance={{"state","running"}};
        entry.launch(device,store.revision(),operation==CXWL_DEPTH_BOARD_MAINTENANCE?"MAINTAIN":"REPAIR");
        entry.event(CXWL_DEPTH_EVENT_MAINTENANCE_CHANGED);
        return CXWL_DEPTH_OK;
    });

    });
}


CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_RecoverDevice(CxwlDepthDeviceManager *manager, const char *id)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!id || !*id) return CXWL_DEPTH_INVALID_ARGUMENT;
        return withManager(manager, [&](CxwlDepthDeviceManager &entry) -> CxwlDepthResult {
            if (qEnvironmentVariable("RV_TEST_OFFLINE_ONLY") == "1") return CXWL_DEPTH_UNAVAILABLE;
            if (!idleManager(entry)) return CXWL_DEPTH_BUSY;
            auto *controller = entry.sdk->controller.get();
            const RvDeviceStore store(controller->deviceStorePath());
            const auto device = findManagedDevice(store, id);
            if (!device.ready()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
            if(!controller->beginBoardFileOperation())return CXWL_DEPTH_BUSY;
            entry.recovery={{"state","running"}};
            entry.launch(device,store.revision(),"RECOVER");
            entry.event(CXWL_DEPTH_EVENT_RECOVERY_CHANGED);
            return CXWL_DEPTH_OK;
        });
    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetVersionInfo(CxwlDepthVersionInfo *info)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    CxwlDepthVersionInfo value{}; value.abi_version = CXWL_DEPTH_ABI_VERSION;
    value.major = CXWL_DEPTH_VERSION_MAJOR; value.minor = CXWL_DEPTH_VERSION_MINOR; value.patch = CXWL_DEPTH_VERSION_PATCH;
    textField(value.version_utf8, QByteArray(CXWL_DEPTH_VERSION));
    textField(value.build_utf8, QByteArray(__DATE__ " " __TIME__));
    return outputStruct(info, value);

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetLastError(CxwlDepthErrorInfo *error)
{
    return outputStruct(error, lastError);
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetLogCallback(uint32_t severity, CxwlDepthLogCallback callback, void *user)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    if (severity < CXWL_DEPTH_LOG_ERROR || severity > CXWL_DEPTH_LOG_INFO) return CXWL_DEPTH_INVALID_ARGUMENT;
    std::lock_guard<QMutex> lock(sessionMutex);
    if (changingSession) return CXWL_DEPTH_BUSY;
    const auto *owner = activeSession ? activeSession : activeManager ? activeManager->sdk.get() : nullptr;
    if (owner && owner->thread != std::this_thread::get_id()) return CXWL_DEPTH_WRONG_THREAD;
    if (owner && owner->delivering) return CXWL_DEPTH_BUSY;
    logCallback = callback; logUser = user; logSeverity = severity;
    return CXWL_DEPTH_OK;

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetSessionStatus(CxwlDepthSession *session, CxwlDepthSessionStatus *status)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [&](CxwlDepthSession &sdk) {
        const auto state = sdk.managedState();
        CxwlDepthSessionStatus value{};
        const auto phase = state["connection_state"].toString();
        value.state = phase == "stopped" ? CXWL_DEPTH_STATE_STOPPED
            : phase == "stopping" || phase == "disconnecting" ? CXWL_DEPTH_STATE_STOPPING
            : phase == "connected" ? CXWL_DEPTH_STATE_STREAMING
            : phase == "reconnecting" ? CXWL_DEPTH_STATE_RECOVERING
            : phase == "error" ? CXWL_DEPTH_STATE_ERROR : CXWL_DEPTH_STATE_CONNECTING;
        const auto error = state["error"].toString();
        value.error_category = error == "board_files_required" ? CXWL_DEPTH_ERROR_BOARD_FILES
            : error == "usb_setup_failed" ? CXWL_DEPTH_ERROR_USB
            : error.isEmpty() ? CXWL_DEPTH_ERROR_NONE : CXWL_DEPTH_ERROR_CAPTURE;
        value.mode = modeOf(*sdk.controller); value.connection_stage = state["connection_stage"].toInt();
        value.busy = sdk.busy(); value.video_available = sdk.videoAvailable; value.depth_available = sdk.depthAvailable;
        value.retaining_display = sdk.controller->retainingRecoveryDisplay(); value.switch_active = sdk.controller->imagingSwitchActive();
        value.switch_estimated_ms = uint32_t(std::max(0, sdk.switchEstimateMs)); value.board_files_required = sdk.boardFilesRequired;
        value.session_epoch = sdk.controller->presentationEpoch(); value.video_fps = sdk.controller->videoFps();
        textField(value.device_id_utf8, sdk.deviceIdentifier()); textField(value.board_files_device_id_utf8, sdk.repairDevice);
        return outputStruct(status, value);
    }, true);

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetManagerStatus(CxwlDepthDeviceManager *manager, CxwlDepthManagerStatus *status)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withManager(manager, [&](CxwlDepthDeviceManager &entry) {
        CxwlDepthManagerStatus value{}; value.busy = entry.sdk->busy(); value.can_manage = idleManager(entry);
        value.maintenance_running = entry.maintaining();
        value.usb_check_id = uint64_t(rvUsbOperationStatus(entry.sdk->usb)["check_id"].toInteger());
        return outputStruct(status, value);
    }, true);

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetDeviceInfo(CxwlDepthSession *session, CxwlDepthDeviceInfo *info)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [&](CxwlDepthSession &sdk) -> CxwlDepthResult {
        const auto device = sdk.controller->deviceProfile();
        if (device.id.isEmpty()) return CXWL_DEPTH_DEVICE_NOT_FOUND;
        CxwlDepthDeviceInfo value{}; value.profile_ready = device.ready();
        textField(value.device_id_utf8, sdk.deviceIdentifier()); textField(value.name_utf8, device.name.toUtf8());
        textField(value.serial_utf8, device.serial.toUtf8());
        textField(value.calibration_release_utf8, sdk.observedMetadata["calibration_release"].toString().toUtf8());
        return outputStruct(info, value);
    }, true);

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetCapabilities(CxwlDepthCapabilities *capabilities)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    CxwlDepthCapabilities value{};
    static_assert(std::size(RvDtofCandidates::ImagingSchemes) == std::size(value.modes));
    for (const auto &scheme : RvDtofCandidates::ImagingSchemes) value.modes[value.mode_count++] = scheme.sdkMode;
    value.depth_width = CXWL_DEPTH_WIDTH; value.depth_height = CXWL_DEPTH_HEIGHT; value.depth_pixel_format = CXWL_DEPTH_PIXEL_F32_LE;
    value.video_width = 2304; value.video_height = 1296; value.video_pixel_format = CXWL_DEPTH_PIXEL_RGB8;
    value.has_snr = 1; value.has_valid_mask = 1; value.max_sessions = 1;
    return outputStruct(capabilities, value);

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SubscribeEx(CxwlDepthSession *session, CxwlDepthFrameExCallback frame,
    CxwlDepthEventCallback event, void *user)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
    return withSession(session, [&](CxwlDepthSession &sdk) {
        sdk.onFrame = nullptr; sdk.onFrameEx = frame; sdk.onEvent = event; sdk.user = user; return CXWL_DEPTH_OK;
    });

    });
}

CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_FreeJson(char *json_utf8)
{
    return publicCall(__func__, [&]() -> CxwlDepthResult {
        if (!json_utf8) return CXWL_DEPTH_OK;
        std::lock_guard<QMutex> lock(jsonAllocationMutex);
        if (!jsonAllocations.erase(json_utf8)) return CXWL_DEPTH_INVALID_ARGUMENT;
        delete[] json_utf8;
        return CXWL_DEPTH_OK;
    });
}

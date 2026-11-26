#include "DepthSdkClient.h"
#include "../sensors/RvImagingSchemes.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QSettings>
#include <QString>
#include <climits>
#include <cstring>
#include <memory>
#include <utility>

namespace {
constexpr auto Offline = "RV_TEST_OFFLINE_ONLY";

template <typename Read>
QJsonDocument readJson(Read read)
{
    uint32_t size = 0;
    char *bytes = nullptr;
    if (read(&bytes, &size) != CXWL_DEPTH_OK) return {};
    const std::unique_ptr<char, decltype(&CxwlDepth_FreeJson)> json(bytes, &CxwlDepth_FreeJson);
    return QJsonDocument::fromJson(QByteArray::fromRawData(json.get(), size - 1));
}

bool validImage(const CxwlDepthImage &image, uint32_t pixelBytes)
{
    return image.data && image.width && image.height && uint64_t(image.width) * pixelBytes <= image.row_stride_bytes
        && image.byte_count >= uint64_t(image.height - 1) * image.row_stride_bytes + uint64_t(image.width) * pixelBytes;
}
}

DepthSdkClient::DepthSdkClient(const QString &deviceStore, QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    const auto savedMode = settings.value("depth/imagingScheme").toString();
    if (const auto *scheme = RvDtofCandidates::schemeForName(savedMode))
        m_mode = scheme->highAccuracy ? CXWL_DEPTH_BOARD_HIGH_ACCURACY : CXWL_DEPTH_BOARD;
    if (savedMode != currentImagingScheme())
        settings.setValue("depth/imagingScheme", currentImagingScheme());
    m_dispatch.setSingleShot(true);
    m_drain.setSingleShot(true);
    m_drain.setInterval(30);
    connect(&m_dispatch, &QTimer::timeout, this, &DepthSdkClient::dispatch);
    connect(&m_drain, &QTimer::timeout, this, [this] {
        if (drain() && m_shuttingDown) emit closeReady();
    });
    const auto runtime = QCoreApplication::applicationDirPath().toUtf8();
    const auto registry = deviceStore.toUtf8();
    const CxwlDepthOptions options{sizeof(CxwlDepthOptions), CXWL_DEPTH_ABI_VERSION,
                                   runtime.constData(), registry.constData()};
    const auto result = CxwlDepth_OpenDeviceManager(registry.isEmpty() ? nullptr : &options, managerEvent, this, &m_manager);
    if (result != CXWL_DEPTH_OK) rememberCaptureFailure("CxwlDepth_OpenDeviceManager", result);
    refreshManager();
}

DepthSdkClient::~DepthSdkClient()
{
    // Normal window close drains asynchronously before destroying the adapter.
    // Also handle destruction by an embedding application which skips closeEvent.
    while (!shutdown()) QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);
}

QString DepthSdkClient::currentImagingScheme() const
{
    return QLatin1String(RvDtofCandidates::schemeForSdkMode(m_mode)->name);
}

void DepthSdkClient::selectImagingScheme(const QString &scheme)
{
    const auto *selected = RvDtofCandidates::schemeForName(scheme);
    if (!selected || !supportsImagingMode(selected->sdkMode)) return;
    const auto mode = selected->sdkMode;
    if (mode == m_mode) return;
    if (m_session) {
        const auto result = CxwlDepth_SetMode(m_session, mode);
        if (result != CXWL_DEPTH_OK) {
            rememberCaptureFailure("CxwlDepth_SetMode", result);
            emit stateChanged();
            return;
        }
    }
    m_mode = mode;
    QSettings().setValue("depth/imagingScheme", scheme);
    emit stateChanged();
}

bool DepthSdkClient::busy() const
{
    return m_shuttingDown || m_stopping || m_request.has_value()
        || m_capture.value("busy").toBool() || (!m_scanning && m_managerState.value("busy").toBool());
}

QString DepthSdkClient::captureStatusText() const
{
    switch (m_status) {
    case CaptureStatus::Starting: return tr("Starting capture...");
    case CaptureStatus::Stopping: return tr("Stopping capture...");
    case CaptureStatus::Stopped: return tr("Capture stopped");
    case CaptureStatus::Failed: return tr("Could not start or resume capture. Check the device and try again.");
    case CaptureStatus::StopFailed: return tr("Could not confirm that capture stopped. Try again.");
    case CaptureStatus::Idle:
    case CaptureStatus::Running: return {};
    }
    return {};
}

int DepthSdkClient::connectionStage() const
{
    if (m_stopping || m_shuttingDown) return CXWL_DEPTH_STAGE_IDLE;
    if (m_request && !m_session) return CXWL_DEPTH_STAGE_CHECKING;
    return m_capture.value("connection_stage").toInt();
}

QJsonArray DepthSdkClient::savedDevices(bool *ok) const
{
    const auto devices = readJson([this](char **buffer, uint32_t *size) {
        return CxwlDepth_ListSavedDevicesAlloc(m_manager, buffer, size);
    });
    if (ok) *ok = devices.isArray();
    return devices.array();
}

QString DepthSdkClient::activeDeviceId() const
{
    for (const auto &device : savedDevices())
        if (device.toObject()["selected"].toBool()) return device.toObject()["id"].toString();
    return {};
}

CxwlDepthResult DepthSdkClient::selectDevice(const QString &id)
{
    return CxwlDepth_SetActiveDevice(m_manager, id.toUtf8().constData());
}

CxwlDepthResult DepthSdkClient::removeDevice(const QString &id)
{
    return CxwlDepth_RemoveSavedDevice(m_manager, id.toUtf8().constData());
}

CxwlDepthResult DepthSdkClient::maintainBoardFiles(const QString &id, uint32_t operation)
{
    return CxwlDepth_MaintainBoardFiles(m_manager, id.toUtf8().constData(), operation);
}

void DepthSdkClient::scanUsb()
{
    if (!m_manager || m_shuttingDown || m_stopping || m_session || m_request || m_scanning
        || qEnvironmentVariable(Offline) == "1") return;
    m_scanning = CxwlDepth_ScanUsb(m_manager) == CXWL_DEPTH_OK;
    if (m_scanning) m_dispatch.start();
}

void DepthSdkClient::start()
{
    const auto target = activeDeviceId().isEmpty() ? CXWL_DEPTH_TARGET_AUTO : CXWL_DEPTH_TARGET_SAVED;
    const auto result = requestConnection({uint32_t(target), 0, {}});
    if (result != CXWL_DEPTH_OK && result != CXWL_DEPTH_BUSY) {
        m_status = CaptureStatus::Failed;
        emit stateChanged();
        emit captureError(result == CXWL_DEPTH_UNAVAILABLE ? "unavailable" : "capture_failed", {});
    }
}

CxwlDepthResult DepthSdkClient::connectViaUsb(quint64 checkId)
{
    return requestConnection({CXWL_DEPTH_TARGET_USB, checkId, {}});
}

CxwlDepthResult DepthSdkClient::requestConnection(ConnectionRequest request)
{
    if (!m_manager || qEnvironmentVariable(Offline) == "1") {
        if (m_manager || m_lastCaptureFailure.isEmpty())
            rememberCaptureFailure("requestConnection", CXWL_DEPTH_UNAVAILABLE, false);
        return CXWL_DEPTH_UNAVAILABLE;
    }
    if (m_shuttingDown || m_stopping || m_request || m_session || (busy() && !m_scanning)) return CXWL_DEPTH_BUSY;
    m_request = request;
    m_status = CaptureStatus::Starting;
    const auto result = m_scanning ? CXWL_DEPTH_OK : connectRequested();
    if (result != CXWL_DEPTH_OK) m_status = CaptureStatus::Failed;
    emit stateChanged();
    return result;
}

CxwlDepthResult DepthSdkClient::connectRequested()
{
    const auto request = *std::exchange(m_request, std::nullopt);
    // The two callbacks are installed with the capture handle. Their data is
    // copied before returning and presented later on the Qt event loop.
    CxwlDepthConnectOptions options{};
    options.size = sizeof(options); options.abi_version = CXWL_DEPTH_ABI_VERSION;
    options.mode = m_mode; options.target = request.target; options.usb_check_id = request.checkId;
    options.policy = request.target == CXWL_DEPTH_TARGET_SAVED ? CXWL_DEPTH_CONNECT_SAVED_ONLY
        : CXWL_DEPTH_CONNECT_PREPARE;
    const auto id = request.deviceId.toUtf8();
    options.device_id_utf8 = id.isEmpty() ? nullptr : id.constData();
    const auto result = CxwlDepth_ConnectWithOptions(&options, receiveFrame, captureEvent, this, &m_session);
    if (result == CXWL_DEPTH_OK) {
        m_capture = {{"connection_stage", CXWL_DEPTH_STAGE_CHECKING}, {"busy", true}};
        m_dispatch.start();
    } else rememberCaptureFailure("CxwlDepth_ConnectWithOptions", result);
    return result;
}

void DepthSdkClient::resetPresentation()
{
    m_pointCloudRecorder.stopRecording();
    m_capture = {};
    m_video = {};
    m_depth.clear();
    m_points.clear();
    m_depthMetadata = {};
    m_clearDepth = m_clearVideo = false;
    emit depthStreamCleared(tr("Camera is not started"));
    emit streamCleared();
    emit stateChanged();
}

void DepthSdkClient::beginDisconnect()
{
    m_stopping = true;
    resetPresentation();
    m_drain.start();
}

void DepthSdkClient::stop()
{
    ++m_stopRevision;
    m_suppressNextAttachment = m_seenAttachment.isEmpty();
    m_pendingBoardRepair.clear();
    if (m_stopping || m_shuttingDown) return;
    const bool wasActive = !userStopped();
    m_request.reset();
    if (wasActive) {
        m_status = CaptureStatus::Stopping;
        beginDisconnect();
    } else {
        resetPresentation();
    }
}

void DepthSdkClient::reconnectNow()
{
    if (m_shuttingDown || m_stopping || m_request || qEnvironmentVariable(Offline) == "1") return;
    if (!m_session) {
        start();
        return;
    }
    const auto result = CxwlDepth_Reconnect(m_session);
    if (result != CXWL_DEPTH_OK && result != CXWL_DEPTH_BUSY) {
        rememberCaptureFailure("CxwlDepth_Reconnect", result);
        emit captureError("capture_failed", {});
    }
}

bool DepthSdkClient::shutdown()
{
    if (!m_shuttingDown) {
        m_shuttingDown = true;
        m_pointCloudRecorder.stopRecording();
        m_request.reset();
        m_pendingBoardRepair.clear();
        if (m_session) m_stopping = true;
    }
    return drain();
}

bool DepthSdkClient::drain()
{
    if (m_stopping) {
        const auto result = m_session ? CxwlDepth_Disconnect(m_session) : CXWL_DEPTH_OK;
        if (result == CXWL_DEPTH_BUSY) {
            m_drain.start();
            return false;
        }
        if (result == CXWL_DEPTH_OK || result == CXWL_DEPTH_INVALID_ARGUMENT) m_session = nullptr;
        m_stopping = false;
        if (result != CXWL_DEPTH_OK) {
            rememberCaptureFailure("CxwlDepth_Disconnect", result);
            m_status = CaptureStatus::StopFailed;
            m_pendingBoardRepair.clear();
        } else if (m_status == CaptureStatus::Stopping) {
            m_status = CaptureStatus::Stopped;
        }
        m_capture = {};
        refreshManager();
        if (!m_shuttingDown) emit stateChanged();
        if (result == CXWL_DEPTH_OK && !m_pendingBoardRepair.isEmpty())
            emit boardFilesRequired(std::exchange(m_pendingBoardRepair, {}));
        if (m_shuttingDown && m_session) {
            m_stopping = true;
            m_drain.start();
            return false;
        }
        m_dispatch.start();
    }
    if (m_shuttingDown && m_manager) {
        const auto result = CxwlDepth_CloseDeviceManager(m_manager);
        if (result == CXWL_DEPTH_BUSY) {
            m_drain.start();
            return false;
        }
        m_manager = nullptr;
    }
    return true;
}

void DepthSdkClient::acceptPointCloud(const CxwlDepthFrame &cloud)
{
    const auto &image = cloud.image;
    const uint32_t rowBytes = image.width * sizeof(CxwlDepthPoint);
    m_points.resize(rowBytes * image.height);
    for (uint32_t row = 0; row < image.height; ++row)
        std::memcpy(m_points.data() + row * rowBytes,
                    static_cast<const char *>(image.data) + uint64_t(row) * image.row_stride_bytes, rowBytes);
    m_pointCloudRecorder.appendPointCloud(cloud, m_points, m_depth);
}

void CXWL_DEPTH_CALL DepthSdkClient::receiveFrame(void *user, const CxwlDepthFrame *frame)
{
    auto &client = *static_cast<DepthSdkClient *>(user);
    if (client.m_stopping || client.m_shuttingDown || client.m_status == CaptureStatus::StopFailed
        || !frame || frame->size < sizeof(*frame)) return;
    const auto &image = frame->image;
    if (frame->stream == CXWL_DEPTH_STREAM_DEPTH && frame->coordinate == CXWL_DEPTH_COORDINATE_SENSOR_RADIAL
        && image.pixel_format == CXWL_DEPTH_PIXEL_F32_LE
        && RvCaptureContract::supportsProductGrid(image.width, image.height) && validImage(image, sizeof(float))) {
        const uint32_t rowBytes = image.width * sizeof(float);
        client.m_depth.resize(rowBytes * image.height);
        const auto *pixels = static_cast<const char *>(image.data);
        for (uint32_t y = 0; y < image.height; ++y)
            std::memcpy(client.m_depth.data() + y * rowBytes, pixels + uint64_t(y) * image.row_stride_bytes, rowBytes);
        CxwlDepthFrame cloud{};
        cloud.size = sizeof(cloud);
        client.m_points.clear();
        if (CxwlDepth_GetFramePointCloud(frame, &cloud) == CXWL_DEPTH_OK) client.acceptPointCloud(cloud);
        client.m_depthMetadata = frame->metadata_json_utf8
            ? QJsonDocument::fromJson(QByteArray(frame->metadata_json_utf8)).object() : QJsonObject{};
        client.m_depthMetadata["width"] = int(image.width);
        client.m_depthMetadata["height"] = int(image.height);
    } else if (frame->stream == CXWL_DEPTH_STREAM_VIDEO && image.pixel_format == CXWL_DEPTH_PIXEL_RGB8
               && validImage(image, 3) && image.width <= INT_MAX && image.height <= INT_MAX) {
        // QImage::copy expects every scanline, including its padding, to be accessible.
        QImage copy(int(image.width), int(image.height), QImage::Format_RGB888);
        if (copy.isNull()) return;
        const auto *pixels = static_cast<const char *>(image.data);
        for (uint32_t y = 0; y < image.height; ++y)
            std::memcpy(copy.scanLine(int(y)), pixels + uint64_t(y) * image.row_stride_bytes, image.width * 3);
        client.m_pointCloudRecorder.appendRgb(*frame, copy);
        client.m_video = std::move(copy);
    } else {
        return;
    }
    if (!client.m_dispatch.isActive()) client.m_dispatch.start();
}

void CXWL_DEPTH_CALL DepthSdkClient::captureEvent(void *user, uint32_t event)
{
    auto &client = *static_cast<DepthSdkClient *>(user);
    if (event == CXWL_DEPTH_EVENT_DEPTH_CLEARED) {
        client.m_depth.clear();
        client.m_points.clear();
        client.m_depthMetadata = {};
        client.m_clearDepth = true;
    }
    if (event == CXWL_DEPTH_EVENT_VIDEO_CLEARED) {
        client.m_video = {};
        client.m_clearVideo = true;
    }
    if (!client.m_dispatch.isActive()) client.m_dispatch.start();
}

void CXWL_DEPTH_CALL DepthSdkClient::managerEvent(void *user, uint32_t event)
{
    auto &client = *static_cast<DepthSdkClient *>(user);
    if (event == CXWL_DEPTH_EVENT_DEVICES_CHANGED) client.m_devicesDirty = true;
    if (event == CXWL_DEPTH_EVENT_MAINTENANCE_CHANGED) client.m_maintenanceDirty = true;
    if (event == CXWL_DEPTH_EVENT_RECOVERY_CHANGED) client.m_recoveryDirty = true;
    if (!client.m_dispatch.isActive()) client.m_dispatch.start();
}

CxwlDepthResult DepthSdkClient::recoverDevice(const QString &id)
{
    if (!canManageDevices()) return CXWL_DEPTH_BUSY;
    return CxwlDepth_RecoverDevice(m_manager, id.toUtf8().constData());
}

void DepthSdkClient::refreshManager()
{
    m_managerState = readJson([this](char **buffer, uint32_t *size) {
        return CxwlDepth_GetDeviceManagerStateAlloc(m_manager, buffer, size);
    }).object();
    CxwlDepthManagerStatus status{}; status.size = sizeof(status);
    if (CxwlDepth_GetManagerStatus(m_manager, &status) == CXWL_DEPTH_OK) {
        m_managerState["busy"] = bool(status.busy); m_managerState["can_manage"] = bool(status.can_manage);
    }
}

void DepthSdkClient::rememberCaptureFailure(const char *operation, CxwlDepthResult result, bool fromSdk)
{
    m_lastCaptureFailure = {
        {"operation", QString::fromLatin1(operation)},
        {"result", result == CXWL_DEPTH_OK ? QJsonValue(QJsonValue::Null) : QJsonValue(result)},
        {"connection_state", m_capture.value("connection_state").toString("stopped")},
        {"error_category", m_capture.value("error_category").toInt()},
        {"connection_stage", connectionStage()},
        {"device_id", m_capture.value("device_id").toString()}
    };
    if (fromSdk && result != CXWL_DEPTH_OK) {
        CxwlDepthErrorInfo error{};
        error.size = sizeof(error);
        if (CxwlDepth_GetLastError(&error) == CXWL_DEPTH_OK && error.code == result) {
            m_lastCaptureFailure["function"] = QString::fromUtf8(error.function_utf8);
            m_lastCaptureFailure["message"] = QString::fromUtf8(error.message_utf8);
        }
    }
}

QJsonObject DepthSdkClient::captureDiagnostics() const
{
    CxwlDepthVersionInfo version{};
    version.size = sizeof(version);
    CxwlDepth_GetVersionInfo(&version);
    const auto device = m_capture.value("device_id").toString();
    return {
        {"sdk_version", QString::fromUtf8(version.version_utf8)},
        {"abi_version", int(version.abi_version)},
        {"device_id", device.isEmpty() ? activeDeviceId() : device},
        {"connection_state", m_capture.value("connection_state").toString(
            m_status == CaptureStatus::Starting ? "connecting" : m_stopping ? "stopping" : "stopped")},
        {"connection_stage", connectionStage()},
        {"last_capture_failure", m_lastCaptureFailure.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                               : QJsonValue(m_lastCaptureFailure)}
    };
}

void DepthSdkClient::dispatch()
{
    if (m_shuttingDown) return;
    const auto previous = m_capture;
    const auto previousStatus = m_status;
    if (m_session) {
        m_capture = readJson([this](char **buffer, uint32_t *size) {
            return CxwlDepth_GetStateAlloc(m_session, buffer, size);
        }).object(); // Retain extended diagnostics (clear reason, USB details).
        CxwlDepthSessionStatus status{}; status.size = sizeof(status);
        if (CxwlDepth_GetSessionStatus(m_session, &status) == CXWL_DEPTH_OK) {
            static const char *states[] = {"stopped", "connecting", "connected", "reconnecting", "stopping", "error"};
            static const char *errors[] = {"", "usb_setup_failed", "board_files_required", "capture_failed"};
            if (status.state < std::size(states)) m_capture["connection_state"] = states[status.state];
            if (status.error_category < std::size(errors)) m_capture["error"] = errors[status.error_category];
            m_capture["error_category"] = int(status.error_category);
            m_capture["device_id"] = QString::fromUtf8(status.device_id_utf8);
            m_capture["busy"] = bool(status.busy); m_capture["connection_stage"] = int(status.connection_stage);
            m_capture["video_available"] = bool(status.video_available); m_capture["depth_available"] = bool(status.depth_available);
            m_capture["retaining_display"] = bool(status.retaining_display); m_capture["video_fps"] = status.video_fps;
            m_capture["switch_active"] = bool(status.switch_active); m_capture["switch_estimated_ms"] = int(status.switch_estimated_ms);
            m_capture["board_files_device_id"] = QString::fromUtf8(status.board_files_device_id_utf8);
        }
    }
    if (m_status == CaptureStatus::Starting && m_capture.value("connection_state") == "connected")
        m_status = CaptureStatus::Running;
    const auto oldManager = m_managerState;
    refreshManager();
    const auto usb = m_managerState.value("usb").toObject();
    const auto maintenance = m_managerState.value("maintenance").toObject();
    if (usb["state"] == "enrolled") m_seenAttachment = usb["attachment_id"].toString();
    if (std::exchange(m_devicesDirty, false)) emit deviceChanged();
    if (usb != oldManager["usb"].toObject()) emit usbStatusChanged(usb);
    if (std::exchange(m_maintenanceDirty, false)) emit maintenanceChanged(maintenance);
    const auto recovery = m_managerState.value("recovery").toObject();
    if (std::exchange(m_recoveryDirty, false) || recovery != oldManager.value("recovery").toObject()) emit deviceRecoveryChanged(recovery);
    if (m_capture.value("switch_active").toBool() && !previous["switch_active"].toBool())
        emit imagingSwitchStarted(currentImagingScheme(), m_capture.value("switch_estimated_ms").toInt());
    if (!m_capture.value("switch_active").toBool() && previous["switch_active"].toBool()) {
        if (m_capture.value("depth_available").toBool()) emit imagingSwitchFinished();
        else emit imagingSwitchAborted();
    }
    if (previous["retaining_display"].toBool() && !retainingRecoveryDisplay()) emit recoveryDisplayExpired();
    if (std::exchange(m_clearDepth, false)) emit depthStreamCleared(m_capture.value("depth_clear_reason").toString());
    if (std::exchange(m_clearVideo, false)) emit streamCleared();
    if (!m_stopping) {
        const auto depth = std::exchange(m_depth, {});
        const auto points = std::exchange(m_points, {});
        const auto metadata = std::exchange(m_depthMetadata, {});
        const auto video = std::exchange(m_video, {});
        if (!depth.isEmpty()) {
            emit depthFrameReady(depth, metadata);
            emit pointCloudFrameReady(points, metadata);
        }
        if (!video.isNull()) emit cameraFrameReady(video);
    }
    if (m_capture != previous || m_managerState != oldManager || m_status != previousStatus) emit stateChanged();

    if (m_session && !m_stopping && m_status != CaptureStatus::StopFailed
        && m_capture.value("connection_state") == "error") {
        const auto reason = m_capture.value("error").toString();
        const auto repair = m_capture.value("board_files_device_id").toString();
        rememberCaptureFailure("capture", CXWL_DEPTH_OK);
        m_status = CaptureStatus::Failed;
        if (reason == "board_files_required") m_pendingBoardRepair = repair;
        beginDisconnect();
        if (m_pendingBoardRepair.isEmpty()) emit captureError(reason, usb);
        return;
    }

    if (m_scanning && !m_managerState.value("busy").toBool()) {
        m_scanning = false;
        const auto attachment = usb["attachment_id"].toString();
        if (usb["absent"].toBool()) {
            m_seenAttachment.clear();
            m_suppressNextAttachment = false;
        }
        if (usb["check_id"].toInteger() > 0 && !attachment.isEmpty() && attachment != m_seenAttachment) {
            m_seenAttachment = attachment;
            if (!std::exchange(m_suppressNextAttachment, false) && !m_request && !m_stopping)
                emit usbDiscovered(usb);
        }
    }
    if (m_request && !m_session && !m_scanning && !m_stopping) {
        const auto request = *m_request;
        if (connectRequested() != CXWL_DEPTH_OK) {
            m_status = CaptureStatus::Failed;
            emit captureError(request.target == CXWL_DEPTH_TARGET_USB ? "usb_setup_failed" : "capture_failed", {});
        }
        emit stateChanged();
    }
}

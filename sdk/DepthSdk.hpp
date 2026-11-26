/** @file DepthSdk.hpp
 * Optional C++17 helpers over the shared C ABI in DepthSdk.h.
 * Handle ownership and thread rules remain those of the C API.
 * @warning Session and Manager destructors can call std::terminate() on a close error or
 * when a busy close cannot be dispatched. Keep the owner alive and retry try_close()
 * on its creating thread. Close a borrowed session before closing its manager.
 * Catch C++ exceptions inside frame, event and log callbacks before returning to the C API.
 */
#ifndef CXWL_DEPTH_SDK_HPP
#define CXWL_DEPTH_SDK_HPP
#include "DepthSdk.h"
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <memory>
namespace cxwl {
inline void require_abi() {
    if (CxwlDepth_AbiVersion() != CXWL_DEPTH_ABI_VERSION)
        throw std::runtime_error("SDK ABI mismatch; redeploy and rebuild with matching headers");
}
template<class T> T initialized() { T value{}; value.size = sizeof(T); return value; }
class Error : public std::runtime_error {
public:
    explicit Error(const CxwlDepthErrorInfo &error, std::optional<CxwlDepthSessionStatus> status = {})
        : Error(error.code, error, status) {}
    Error(CxwlDepthResult result, const CxwlDepthErrorInfo &last, std::optional<CxwlDepthSessionStatus> status)
        : std::runtime_error(describe(result, last)), code(result), info(last), session_status(status) {}
    CxwlDepthResult code;
    CxwlDepthErrorInfo info;
    std::optional<CxwlDepthSessionStatus> session_status;
private:
    static std::string describe(CxwlDepthResult result, const CxwlDepthErrorInfo &last) {
        const auto current = "SDK error " + std::to_string(result);
        if (!last.function_utf8[0]) return current;
        const auto detail = std::string(last.function_utf8) + ": " + last.message_utf8;
        if (last.code == result) return detail;
        return current + " (previous thread error " + std::to_string(last.code) + " from " + detail + ")";
    }
};
inline void check(CxwlDepthResult result, CxwlDepthSession *session = nullptr) {
    if (result == CXWL_DEPTH_OK) return;
    auto info = initialized<CxwlDepthErrorInfo>();
    CxwlDepth_GetLastError(&info);
    auto status = initialized<CxwlDepthSessionStatus>();
    if (session && CxwlDepth_GetSessionStatus(session, &status) == CXWL_DEPTH_OK)
        throw Error(result, info, status);
    throw Error(result, info, {});
}
inline CxwlDepthVersionInfo version() {
    require_abi();
    auto v = initialized<CxwlDepthVersionInfo>(); check(CxwlDepth_GetVersionInfo(&v)); return v;
}
inline CxwlDepthCapabilities capabilities() {
    require_abi();
    auto v = initialized<CxwlDepthCapabilities>(); check(CxwlDepth_GetCapabilities(&v)); return v;
}
/* An owned, tightly packed image. An empty source must have null data and zero width and height.
 * view() remains valid until this object is changed or destroyed. */
class Image {
public:
    Image() = default;
    explicit Image(const CxwlDepthImage &source) {
        const uint32_t bpp = source.pixel_format == CXWL_DEPTH_PIXEL_XYZ_F32_LE ? 12 :
            source.pixel_format == CXWL_DEPTH_PIXEL_F32_LE ? 4 :
            source.pixel_format == CXWL_DEPTH_PIXEL_RGB8 ? 3 : source.pixel_format == CXWL_DEPTH_PIXEL_U8 ? 1 : 0;
        const uint64_t row = uint64_t(source.width) * bpp;
        if (!source.data && !source.width && !source.height) return;
        if (!bpp || !source.data || !source.width || !source.height || row > UINT32_MAX
            || source.row_stride_bytes < row)
            throw std::invalid_argument("Invalid SDK image view");
        const uint64_t required = uint64_t(source.height - 1) * source.row_stride_bytes + row;
        const uint64_t bytes = row * source.height;
        if (required > source.byte_count || required > (std::numeric_limits<std::ptrdiff_t>::max)()
            || bytes > pixels_.max_size())
            throw std::invalid_argument("Invalid SDK image size");
        pixels_.resize(size_t(bytes));
        width_=source.width; height_=source.height; stride_=uint32_t(row); format_=source.pixel_format;
        for(uint32_t y=0;y<height_;++y)
            std::memcpy(pixels_.data()+size_t(y)*stride_,static_cast<const char *>(source.data)+size_t(y)*source.row_stride_bytes,stride_);
    }
    CxwlDepthImage view() const {
        if (pixels_.empty()) return {};
        return {pixels_.data(), uint64_t(pixels_.size()), width_, height_, stride_, format_};
    }
private:
    std::vector<unsigned char> pixels_;
    uint32_t width_=0,height_=0,stride_=0,format_=0;
};
namespace detail {
struct OwnedFrame {
    Image image;
    uint64_t epoch=0, sequence=0, timestamp_ns=0;
    uint32_t clock_domain=0;
    std::string device_id, capture_generation, metadata;
protected:
    OwnedFrame(const CxwlDepthFrame &frame, uint32_t stream, const char *stream_error)
        : image(image_for_stream(frame, stream, stream_error)),
          epoch(frame.session_epoch), sequence(frame.sequence), timestamp_ns(frame.timestamp_ns),
          clock_domain(frame.clock_domain), device_id(frame.device_id_utf8?frame.device_id_utf8:""),
          capture_generation(frame.capture_generation_utf8?frame.capture_generation_utf8:""),
          metadata(frame.metadata_json_utf8?frame.metadata_json_utf8:"") {}
private:
    static const CxwlDepthImage &image_for_stream(const CxwlDepthFrame &frame, uint32_t stream, const char *message) {
        if (frame.stream != stream) throw std::invalid_argument(message);
        return frame.image;
    }
};
} // namespace detail

struct DepthFrame : detail::OwnedFrame {
    explicit DepthFrame(const CxwlDepthFrame &frame)
        : OwnedFrame(frame, CXWL_DEPTH_STREAM_DEPTH, "Expected a depth frame") {}
};
struct VideoFrame : detail::OwnedFrame {
    explicit VideoFrame(const CxwlDepthFrame &frame)
        : OwnedFrame(frame, CXWL_DEPTH_STREAM_VIDEO, "Expected a video frame") {}
};
// Borrowed only until the depth callback returns. Image(view.image) makes an owned copy.
inline CxwlDepthFrame frame_point_cloud(const CxwlDepthFrame &depth) {
    CxwlDepthFrame view{};
    view.size = sizeof(view);
    check(CxwlDepth_GetFramePointCloud(&depth, &view));
    return view;
}
inline void save_point_cloud_ply(const CxwlDepthFrame &frame, const std::string &path_utf8) {
    check(CxwlDepth_SavePointCloudPly(&frame, path_utf8.c_str()));
}
template<class Handle> inline std::string json_snapshot(
    Handle *handle, CxwlDepthResult (CXWL_DEPTH_CALL *query)(Handle *, char **, uint32_t *)) {
    require_abi(); char *raw=nullptr; uint32_t count=0;
    check(query(handle,&raw,&count));
    std::unique_ptr<char,decltype(&CxwlDepth_FreeJson)> owned(raw,&CxwlDepth_FreeJson);
    if (!raw || !count || raw[count - 1] != '\0')
        throw std::runtime_error("Invalid SDK JSON snapshot");
    std::string value(raw, count - 1);
    check(CxwlDepth_FreeJson(owned.release()));
    return value;
}
inline std::string state(CxwlDepthSession *session) { return json_snapshot(session,CxwlDepth_GetStateAlloc); }
inline std::string devices(CxwlDepthDeviceManager *manager) { return json_snapshot(manager,CxwlDepth_ListSavedDevicesAlloc); }
inline std::string manager_state(CxwlDepthDeviceManager *manager) { return json_snapshot(manager,CxwlDepth_GetDeviceManagerStateAlloc); }

/* An owning handle. Keep the callbacks' context alive until close succeeds. Use and destroy on
 * the creating thread, outside callbacks. In a Qt host, keep the object alive and retry
 * try_close() in the host event loop before destroying it if close returns BUSY. */
class Session {
public:
    explicit Session(const CxwlDepthOptions &options) {
        require_abi();
        check(CxwlDepth_Open(&options, &handle_));
    }
    explicit Session(uint32_t mode, CxwlDepthFrameCallback frame = nullptr,
                     CxwlDepthEventCallback event = nullptr, void *user = nullptr) {
        require_abi();
        check(CxwlDepth_Connect(mode, frame, event, user, &handle_));
    }
    explicit Session(const CxwlDepthConnectOptions &options, CxwlDepthFrameCallback frame = nullptr,
                     CxwlDepthEventCallback event = nullptr, void *user = nullptr) {
        require_abi();
        check(CxwlDepth_ConnectWithOptions(&options, frame, event, user, &handle_));
    }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Session(Session &&) = delete;
    Session &operator=(Session &&) = delete;

    ~Session() noexcept {
        while (handle_) {
            const auto result = CxwlDepth_Close(handle_);
            if (result == CXWL_DEPTH_OK) return;
            uint32_t dispatched = 0;
            if (result != CXWL_DEPTH_BUSY || CxwlDepth_WaitForEvent(handle_, 50, &dispatched) != CXWL_DEPTH_OK)
                std::terminate();
        }
    }
    CxwlDepthSession *get() const noexcept { return handle_; }
    bool try_close() {
        if (!handle_) return true;
        const auto result = CxwlDepth_Close(handle_);
        if (result == CXWL_DEPTH_BUSY) return false;
        check(result, handle_);
        handle_ = nullptr;
        return true;
    }
    bool wait(uint32_t timeout_ms) const {
        uint32_t dispatched = 0;
        check(CxwlDepth_WaitForEvent(handle_, timeout_ms, &dispatched), handle_);
        return dispatched != 0;
    }
private:
    CxwlDepthSession *handle_ = nullptr;
};

/* Similar ownership rules to Session. Close any borrowed capture session before closing Manager. */
class Manager {
public:
    explicit Manager(const CxwlDepthOptions *options = nullptr,
                     CxwlDepthEventCallback event = nullptr, void *user = nullptr) {
        require_abi();
        check(CxwlDepth_OpenDeviceManager(options, event, user, &handle_));
    }
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;
    Manager(Manager &&) = delete;
    Manager &operator=(Manager &&) = delete;

    ~Manager() noexcept {
        while (handle_) {
            const auto result = CxwlDepth_CloseDeviceManager(handle_);
            if (result == CXWL_DEPTH_OK) return;
            auto status = initialized<CxwlDepthManagerStatus>();
            if (result != CXWL_DEPTH_BUSY || CxwlDepth_GetManagerStatus(handle_, &status) != CXWL_DEPTH_OK
                || !status.busy || CxwlDepth_PollDeviceManager(handle_) != CXWL_DEPTH_OK)
                std::terminate();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    CxwlDepthDeviceManager *get() const noexcept { return handle_; }
    bool try_close() {
        if (!handle_) return true;
        const auto result = CxwlDepth_CloseDeviceManager(handle_);
        if (result == CXWL_DEPTH_BUSY) return false;
        check(result);
        handle_ = nullptr;
        return true;
    }
private:
    CxwlDepthDeviceManager *handle_ = nullptr;
};
} // namespace cxwl
#endif

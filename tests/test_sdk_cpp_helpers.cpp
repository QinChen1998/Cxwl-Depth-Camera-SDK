// Offline C++ consumer test; links only the public SDK import library.
#pragma pack(push, 1)
#include <DepthSdk.h>
struct CallerPacked { char tag; unsigned value; };
#pragma pack(pop)
static_assert(offsetof(CallerPacked, value) == 1, "SDK must restore the caller's packing");
static_assert(sizeof(CxwlDepthFrame) == 104 && offsetof(CxwlDepthFrame, image) == 64);
#include <DepthSdk.hpp>
#include <type_traits>
static_assert(!std::is_move_constructible_v<cxwl::Session> && !std::is_move_assignable_v<cxwl::Manager>);
#include <cassert>
#include <cstring>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <utility>
int main(int argc, char **argv) {
    assert(cxwl::version().abi_version == CXWL_DEPTH_ABI_VERSION);
    assert(cxwl::capabilities().max_sessions == 1);
    unsigned char source[16] = {1,2,3,4,99,99,99,99,5,6,7,8,99,99,99,99};
    CxwlDepthImage padded{source,sizeof(source),4,2,8,CXWL_DEPTH_PIXEL_U8};
    cxwl::Image copy(padded);
    std::memset(source,0,sizeof(source));
    auto owned=copy.view();
    assert(owned.row_stride_bytes==4 && owned.byte_count==8);
    assert(static_cast<const unsigned char *>(owned.data)[0]==1 && static_cast<const unsigned char *>(owned.data)[7]==8);
    auto second=copy; copy=cxwl::Image();
    assert(static_cast<const unsigned char *>(second.view().data)[7]==8);
    auto moved=std::move(second);
    assert(moved.view().byte_count==8);
    assert(!second.view().data && !second.view().width && !second.view().height);
    padded.byte_count=3;
    bool rejected=false;
    try { cxwl::Image bad(padded); } catch(const std::invalid_argument &) { rejected=true; }
    assert(rejected);
    float depth[48*32]; for(auto &v:depth) v=1;
    CxwlDepthImage radial{depth,sizeof(depth),48,32,48*4,CXWL_DEPTH_PIXEL_F32_LE};
    char metadata[]="{\"sequence\":17}";
    CxwlDepthFrame frame{}; frame.stream=CXWL_DEPTH_STREAM_DEPTH; frame.image=radial;
    frame.metadata_json_utf8=metadata; frame.sequence=17; frame.session_epoch=3;
    cxwl::DepthFrame stored(frame); metadata[0]='!'; depth[0]=0;
    assert(stored.metadata[0]=='{' && stored.sequence==17 && stored.epoch==3);
    float copiedDepth=0;
    std::memcpy(&copiedDepth,stored.image.view().data,sizeof(copiedDepth));
    assert(copiedDepth==1);
    try { (void)cxwl::frame_point_cloud(frame); return 1; }
    catch (const cxwl::Error &error) { assert(error.code == CXWL_DEPTH_UNAVAILABLE); }
    unsigned char rgb[] = {20,40,60};
    frame.stream = CXWL_DEPTH_STREAM_VIDEO;
    frame.image = {rgb, sizeof(rgb), 1, 1, 3, CXWL_DEPTH_PIXEL_RGB8};
    cxwl::VideoFrame video(frame);
    rgb[0] = 0;
    assert(static_cast<const unsigned char *>(video.image.view().data)[0]==20 && video.sequence==17);
    rejected = false;
    frame.image.data = nullptr;
    try { cxwl::DepthFrame wrongStream(frame); }
    catch(const std::invalid_argument &error) { rejected = std::string(error.what()) == "Expected a depth frame"; }
    assert(rejected);
    try { cxwl::check(CxwlDepth_GetCapabilities(nullptr)); return 1; }
    catch(const cxwl::Error &error) {
        assert(error.code==CXWL_DEPTH_INVALID_ARGUMENT && error.info.code==error.code);
        assert(std::string(error.info.function_utf8)=="CxwlDepth_GetCapabilities" && error.info.message_utf8[0]);
    }
    try { cxwl::check(CXWL_DEPTH_BUSY); return 1; }
    catch (const cxwl::Error &error) {
        assert(error.code == CXWL_DEPTH_BUSY && error.info.code == CXWL_DEPTH_INVALID_ARGUMENT);
        assert(std::string(error.what()).find("previous thread error") != std::string::npos);
    }
    if (argc == 2) {
        auto paths = cxwl::initialized<CxwlDepthOptions>();
        paths.abi_version = CXWL_DEPTH_ABI_VERSION;
        const auto runtime = std::filesystem::absolute(argv[0]).parent_path().u8string();
        paths.runtime_directory_utf8 = runtime.c_str();
        paths.device_store_utf8 = argv[1];
        {
            cxwl::Manager manager(&paths);
            auto malformed = +[](CxwlDepthDeviceManager *, char **out, uint32_t *count) -> CxwlDepthResult {
                *out = nullptr; *count = 0; return CXWL_DEPTH_OK;
            };
            try { cxwl::json_snapshot(manager.get(), malformed); return 1; }
            catch (const std::runtime_error &error) { assert(std::string(error.what()) == "Invalid SDK JSON snapshot"); }
            auto truncated = +[](CxwlDepthDeviceManager *m, char **out, uint32_t *count) -> CxwlDepthResult {
                const auto result = CxwlDepth_ListSavedDevicesAlloc(m, out, count);
                if (!result) *count = 1;
                return result;
            };
            try { cxwl::json_snapshot(manager.get(), truncated); return 1; }
            catch (const std::runtime_error &error) { assert(std::string(error.what()) == "Invalid SDK JSON snapshot"); }
            const auto list = cxwl::devices(manager.get()), status = cxwl::manager_state(manager.get());
            assert(!list.empty() && list.front() == '[' && status.find("can_manage") != std::string::npos);
        }
        {
            cxwl::Session session(paths);
            assert(!session.wait(0));
            assert(cxwl::state(session.get()).find("connection_state") != std::string::npos);
            try { cxwl::check(CxwlDepth_SetMode(session.get(), 0), session.get()); return 1; }
            catch (const cxwl::Error &error) {
                assert(error.session_status && error.session_status->state==CXWL_DEPTH_STATE_STOPPED);
                assert(std::string(error.info.function_utf8)=="CxwlDepth_SetMode");
            }
        }
        cxwl::Session fresh(paths);
        assert(fresh.try_close() && fresh.try_close() && !fresh.get());
    }
    std::cout<<"PASS C++ consumer: ABI, padded images, owned copies, metadata lifetime, board XYZ callback scope, JSON snapshots and errors\n";
}

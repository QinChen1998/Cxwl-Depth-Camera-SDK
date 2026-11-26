/* C++17 uses the same shared SDK library as C. Requires a camera.
 * Copy callback data before returning; exceptions must never cross the C callback boundary.
 * Expected output: version, copied depth dimensions and point count. */
#include <DepthSdk.hpp>
#include "SampleSupport.h"
#include <iostream>
#include <memory>
struct Capture { std::unique_ptr<cxwl::DepthFrame> frame; std::unique_ptr<cxwl::Image> cloud; std::exception_ptr error; };
static void CXWL_DEPTH_CALL receive(void *user,const CxwlDepthFrame *frame) {
    auto &capture=*static_cast<Capture *>(user);
    if(frame->stream!=CXWL_DEPTH_STREAM_DEPTH || capture.frame || capture.error) return;
    try {
        const auto cloud = cxwl::frame_point_cloud(*frame);
        capture.cloud = std::make_unique<cxwl::Image>(cloud.image);
        capture.frame = std::make_unique<cxwl::DepthFrame>(*frame);
    }
    catch (const cxwl::Error &e) {
        if (e.code != CXWL_DEPTH_UNAVAILABLE) capture.error = std::current_exception();
    }
    catch(...) { capture.error=std::current_exception(); }
}
int main() {
    sample_console();
    Capture capture;
    if(!sample_logging()) return 1;
    try {
        std::cout<<"SDK "<<cxwl::version().version_utf8<<"\n";
        cxwl::Session session(CXWL_DEPTH_BOARD, receive, nullptr, &capture);
        const auto deadline=sample_deadline(SAMPLE_CAPTURE_TIMEOUT_MS);
        while(!capture.frame && !capture.error && sample_now_ms()<deadline)
            if(!sample_poll(session.get())) return 1;
        if(capture.error) std::rethrow_exception(capture.error);
        if(!capture.frame) throw std::runtime_error("等待深度帧超时 / No depth frame before timeout");
        const auto image=capture.frame->image.view();
        const auto xyz=capture.cloud->view();
        std::cout<<"深度副本 / Owned depth "<<image.width<<"x"<<image.height<<", "<<xyz.width * xyz.height<<" points\n";
        return 0;
    } catch(const std::exception &error) { std::cerr<<error.what()<<"\n"; }
    return 1;
}

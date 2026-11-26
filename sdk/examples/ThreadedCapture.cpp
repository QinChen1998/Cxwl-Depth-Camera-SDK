/* Working-thread ownership example. The worker creates, polls and releases the
 * session; the consumer thread only sees owned copies. Requires a camera.
 * No Qt development environment. Consumer processes up to 5 depth frames.
 * Latest-frame handoff bounds memory and latency instead of accumulating a queue. */
#include <DepthSdk.hpp>
#include "SampleSupport.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
struct Shared {
    std::mutex mutex;
    std::condition_variable changed;
    std::unique_ptr<cxwl::DepthFrame> latest;
    std::atomic<bool> stop{false};
    bool done=false;
    std::string error;
};
static void CXWL_DEPTH_CALL receive(void *user,const CxwlDepthFrame *frame) {
    if(frame->stream!=CXWL_DEPTH_STREAM_DEPTH) return;
    auto &shared=*static_cast<Shared *>(user);
    try {
        auto copy=std::make_unique<cxwl::DepthFrame>(*frame);
        {std::lock_guard<std::mutex> guard(shared.mutex); shared.latest=std::move(copy);}
        shared.changed.notify_one();
    } catch(const std::exception &error) {
        {std::lock_guard<std::mutex> guard(shared.mutex); shared.error=error.what();}
        shared.stop=true; shared.changed.notify_one();
    } catch(...) { shared.stop=true; }
}
int main() {
    sample_console();
    Shared shared;
    std::thread worker([&] {
        CxwlDepthSession *session=nullptr;
        if(!sample_logging()) { std::lock_guard<std::mutex> lock(shared.mutex); shared.error="日志初始化失败 / Logger setup failed"; }
        else {
            auto r=CxwlDepth_Connect(CXWL_DEPTH_BOARD,receive,nullptr,&shared,&session);
            if(r) { sample_failure(nullptr,"Connect",r); std::lock_guard<std::mutex> lock(shared.mutex); shared.error="连接失败 / Connect failed"; }
            else {
                const auto deadline=sample_deadline(SAMPLE_CAPTURE_TIMEOUT_MS);
                while(!shared.stop && sample_now_ms()<deadline) if(!sample_poll(session)) break;
            }
            if(!sample_disconnect(&session)) { std::lock_guard<std::mutex> lock(shared.mutex); shared.error="关闭失败 / Shutdown failed"; }
        }
        {std::lock_guard<std::mutex> lock(shared.mutex); shared.done=true;}
        shared.changed.notify_one();
    });
    int count=0;
    try {
        while(count<5) {
            std::unique_ptr<cxwl::DepthFrame> frame;
            {std::unique_lock<std::mutex> lock(shared.mutex);
                shared.changed.wait(lock,[&]{return shared.latest || shared.done;});
                frame=std::move(shared.latest);
                if(!frame && shared.done) break;
            }
            if(frame) { std::cout<<"读取副本 / Consume copy: epoch="<<frame->epoch<<" sequence="<<frame->sequence<<"\n"; ++count; }
        }
    } catch(...) { shared.stop=true; worker.join(); throw; }
    shared.stop=true; worker.join();
    if(!shared.error.empty()) std::cerr<<shared.error<<"\n";
    if(count<5) std::cerr<<"采集不足五帧 / Capture ended before five frames.\n";
    return count==5 && shared.error.empty()?0:1;
}

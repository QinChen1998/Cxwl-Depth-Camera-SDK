/* QuickStart: connect and print both stream sizes; optionally reconnect once.
 * Requires a camera or a ready saved device. Expected exit code: 0.
 * 用法 / Usage: CxwlDepthQuickStart [board|high] [--reconnect]
 * All SDK calls stay on this thread; no Qt development environment is needed. */
#include "DepthSdk.h"
#include "SampleSupport.h"
typedef struct Capture { int video_received, depth_received; } Capture;
static void CXWL_DEPTH_CALL on_frame(void *user, const CxwlDepthFrame *frame)
{
    Capture *capture = (Capture *)user;
    const CxwlDepthImage *image = &frame->image;
    if (frame->stream == CXWL_DEPTH_STREAM_VIDEO && !capture->video_received) {
        printf("视频 / Video: %u x %u RGB8\n", image->width, image->height); capture->video_received = 1;
    } else if (frame->stream == CXWL_DEPTH_STREAM_DEPTH && !capture->depth_received) {
        printf("深度 / Depth: %u x %u radial metres, epoch=%llu\n", image->width, image->height,
            (unsigned long long)frame->session_epoch); capture->depth_received = 1;
    }
}
static int wait_frames(CxwlDepthSession *session, Capture *capture)
{
    const uint64_t deadline = sample_deadline(SAMPLE_CAPTURE_TIMEOUT_MS);
    uint32_t previous_stage = UINT32_MAX;
    while (sample_now_ms() < deadline) {
        CxwlDepthSessionStatus status = {0};
        if (!sample_poll_status(session, &status)) return 0;
        if (capture->video_received && capture->depth_received) return 1;
        sample_capture_progress(&status, &previous_stage);
    }
    fputs("等待两条流超时 / Timed out waiting for both streams.\n", stderr); return 0;
}
int main(int argc, char **argv)
{
    sample_console();
    uint32_t mode = CXWL_DEPTH_BOARD;
    int have_mode = 0, reconnect = 0;
    for (int i = 1; i < argc; ++i) {
        if (!reconnect && !strcmp(argv[i], "--reconnect")) {
            reconnect = 1;
        } else if (!have_mode && (!strcmp(argv[i], "board") || !strcmp(argv[i], "high"))) {
            have_mode = 1;
            mode = !strcmp(argv[i], "high") ? CXWL_DEPTH_BOARD_HIGH_ACCURACY : CXWL_DEPTH_BOARD;
        } else {
            fputs("用法 / Usage: CxwlDepthQuickStart [board|high] [--reconnect]\n", stderr);
            return 2;
        }
    }
    if (!sample_logging()) return 1;
    Capture capture = {0};
    CxwlDepthSession *session = NULL;
    CxwlDepthResult r = CxwlDepth_Connect(mode, on_frame, NULL, &capture, &session);
    if (r) { sample_failure(NULL, "Connect", r); return 1; }
    int success = wait_frames(session, &capture);
    if (success && reconnect) {
        capture.video_received = capture.depth_received = 0;
        r = CxwlDepth_Reconnect(session);
        if (r) { sample_failure(session, "Reconnect", r); success = 0; }
        else success = wait_frames(session, &capture);
    }
    if (!sample_disconnect(&session)) success = 0;
    return success ? 0 : 1;
}

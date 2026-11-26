/* Shared C/C++ example helpers. No Qt headers or Qt link libraries are needed.
 * SDK calls and cleanup stay on the handle's owner thread. */
#ifndef CXWL_SAMPLE_SUPPORT_H
#define CXWL_SAMPLE_SUPPORT_H
#include <DepthSdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

enum {
    SAMPLE_CAPTURE_TIMEOUT_MS = 360000,
    SAMPLE_MANAGEMENT_TIMEOUT_MS = 660000
};
#ifdef _WIN32
static uint64_t sample_now_ms(void) { return GetTickCount64(); }
static void sample_sleep_ms(uint32_t ms) { Sleep(ms); }
static UINT sample_previous_codepage;
static void sample_restore_console(void) { if (sample_previous_codepage) SetConsoleOutputCP(sample_previous_codepage); }
static void sample_console(void)
{
    DWORD mode;
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode)) {
        sample_previous_codepage = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8); atexit(sample_restore_console);
    }
}
#else
static uint64_t sample_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void sample_sleep_ms(uint32_t ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
    while (nanosleep(&t, &t) && errno == EINTR) {}
}
static void sample_console(void) {}
#endif
static uint64_t sample_deadline(uint32_t timeout_ms) { return sample_now_ms() + timeout_ms; }
static void sample_capture_help(void)
{
#ifdef _WIN32
    fputs("下一步 / Next: 检查状态与 USB 数据线；在 SDK 包根目录运行 / check state and USB, then run:\n"
          "  powershell -File Script/diagnose_cxwl_depth_camera_usb.ps1\n", stderr);
#else
    fputs("检查 USB 数据线、设备权限和直连网卡。请参照配套 Linux SDK 的 USB 接入说明。\n", stderr);
#endif
}
static void sample_file_help(void)
{
    fputs("下一步 / Next: 在可写目录重试，检查磁盘空间与文件占用。\n"
          "Retry in a writable working directory; check free space and file locks.\n", stderr);
}

static void sample_failure(CxwlDepthSession *session, const char *operation, CxwlDepthResult result)
{
    CxwlDepthErrorInfo error = {0};
    CxwlDepthSessionStatus status = {0};
    error.size = sizeof(error); status.size = sizeof(status);
    CxwlDepth_GetLastError(&error); /* Snapshot before making another SDK call. */
    fprintf(stderr, "调用失败 / Failed %s: result=%d, last_function=%s, message=%s\n",
        operation, result, error.function_utf8, error.message_utf8);
    if (session && CxwlDepth_GetSessionStatus(session, &status) == CXWL_DEPTH_OK)
        fprintf(stderr, "state=%u, error_category=%u, connection_stage=%u\n",
            status.state, status.error_category, status.connection_stage);
    if (result == CXWL_DEPTH_BUSY)
        fputs("下一步 / Next: 回调退出后在所有者线程派发事件并重试 / dispatch on the owner thread and retry.\n", stderr);
    else if (result == CXWL_DEPTH_WRONG_THREAD)
        fputs("下一步 / Next: 在创建句柄的线程调用 / use the handle owner thread.\n", stderr);
    else if (result == CXWL_DEPTH_INVALID_ARGUMENT || result == CXWL_DEPTH_BUFFER_TOO_SMALL)
        fputs("下一步 / Next: 检查参数与 size / check arguments and structure or buffer size.\n", stderr);
    else if (result == CXWL_DEPTH_UNAVAILABLE || result == CXWL_DEPTH_DEVICE_NOT_FOUND)
        sample_capture_help();
}
static void CXWL_DEPTH_CALL sample_log(void *user, uint32_t severity, const char *message)
{
    (void)user;
    fprintf(stderr, "[SDK %u] %s\n", severity, message);
    /* Never call SDK functions in a log callback. */
}
static int sample_logging(void)
{
    if (CxwlDepth_AbiVersion() != CXWL_DEPTH_ABI_VERSION) {
        fputs("SDK ABI 不匹配 / ABI mismatch: 请部署与头文件配套的运行库。\n", stderr); return 0;
    }
    CxwlDepthResult r = CxwlDepth_SetLogCallback(CXWL_DEPTH_LOG_WARNING, sample_log, NULL);
    if (r) sample_failure(NULL, "SetLogCallback", r);
    return r == CXWL_DEPTH_OK;
}
static int sample_disconnect(CxwlDepthSession **session)
{
    CxwlDepthResult r;
    if (!*session) return 1;
    while ((r = CxwlDepth_Disconnect(*session)) == CXWL_DEPTH_BUSY) {
        uint32_t dispatched = 0;
        CxwlDepthResult wait = CxwlDepth_WaitForEvent(*session, 50, &dispatched);
        if (wait) { sample_failure(*session, "WaitForEvent during shutdown", wait); return 0; }
    }
    if (r) { sample_failure(*session, "Disconnect", r); return 0; }
    *session = NULL;
    return 1;
}
static int sample_close_manager(CxwlDepthDeviceManager **manager)
{
    CxwlDepthResult r;
    if (!*manager) return 1;
    while ((r = CxwlDepth_CloseDeviceManager(*manager)) == CXWL_DEPTH_BUSY) {
        CxwlDepthResult poll = CxwlDepth_PollDeviceManager(*manager);
        if (poll) { sample_failure(NULL, "PollDeviceManager", poll); return 0; }
        sample_sleep_ms(10);
    }
    if (r) { sample_failure(NULL, "CloseDeviceManager", r); return 0; }
    *manager = NULL;
    return 1;
}
static int sample_poll_status(CxwlDepthSession *session, CxwlDepthSessionStatus *status)
{
    status->size = sizeof(*status);
    uint32_t dispatched = 0;
    CxwlDepthResult r = CxwlDepth_WaitForEvent(session, 50, &dispatched);
    if (!r) r = CxwlDepth_GetSessionStatus(session, status);
    if (r) { sample_failure(session, "WaitForEvent/GetSessionStatus", r); return 0; }
    if (status->state == CXWL_DEPTH_STATE_ERROR) {
        fprintf(stderr, "异步采集失败 / Asynchronous capture failure: error_category=%u, stage=%u\n",
            status->error_category, status->connection_stage);
        sample_capture_help();
        return 0;
    }
    return 1;
}
static int sample_poll(CxwlDepthSession *session)
{
    CxwlDepthSessionStatus status = {0};
    return sample_poll_status(session, &status);
}
static void sample_capture_progress(const CxwlDepthSessionStatus *status, uint32_t *previous_stage)
{
    if (status->connection_stage == *previous_stage) return;
    *previous_stage = status->connection_stage;
    const char *stage = "等待视频和深度 / waiting for both streams";
    switch (status->connection_stage) {
    case CXWL_DEPTH_STAGE_CHECKING: stage = "检查接入条件 / checking device"; break;
    case CXWL_DEPTH_STAGE_STARTING: stage = "启动采集 / starting capture"; break;
    case CXWL_DEPTH_STAGE_WAITING_FOR_DEPTH: stage = "等待深度 / waiting for depth"; break;
    }
    fprintf(stderr, "%s (state=%u, stage=%u)\n", stage, status->state, status->connection_stage);
}
static int sample_image(const CxwlDepthImage *im, uint32_t format, uint32_t bytes)
{
    if (!im || !im->data || !im->width || !im->height || im->pixel_format != format) return 0;
    const uint64_t row = (uint64_t)im->width * bytes;
    return im->row_stride_bytes >= row && im->byte_count >= (uint64_t)(im->height - 1) * im->row_stride_bytes + row;
}
static int sample_same_grid(const CxwlDepthImage *a, const CxwlDepthImage *b)
{
    return a->width == b->width && a->height == b->height;
}
/* Caller owns the result and must use CxwlDepth_FreeJson. An empty list is still valid JSON. */
static char *sample_manager_json(CxwlDepthDeviceManager *manager, int list)
{
    uint32_t size = 0; char *json = NULL;
    CxwlDepthResult r = list ? CxwlDepth_ListSavedDevicesAlloc(manager, &json, &size)
                            : CxwlDepth_GetDeviceManagerStateAlloc(manager, &json, &size);
    if (r) sample_failure(NULL, list ? "ListSavedDevicesAlloc" : "GetDeviceManagerStateAlloc", r);
    return json; /* Release with CxwlDepth_FreeJson, never free(). */
}
#endif

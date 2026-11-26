/* DeviceManagement: list (offline), scan, connect-usb, connect-saved, select/remove, repair/maintain/recover.
 * Only explicitly requested commands change device state. IDs come from list.
 * scan requires USB; connect-usb verifies the scan ID again before enrollment.
 * repair/maintain require a ready saved device and no capture session.
 * Prints JSON diagnostics; nonzero exit indicates failure or timeout. */
#include "SampleSupport.h"
#include "SampleJson.h"
static int wait_manager(CxwlDepthDeviceManager *manager)
{
    const uint64_t deadline=sample_deadline(SAMPLE_MANAGEMENT_TIMEOUT_MS);
    do {
        CxwlDepthManagerStatus status={0}; status.size=sizeof(status);
        CxwlDepthResult r=CxwlDepth_PollDeviceManager(manager);
        if(!r) r=CxwlDepth_GetManagerStatus(manager,&status);
        if(r) { sample_failure(NULL,"Poll/GetManagerStatus",r); return 0; }
        if(!status.busy && !status.maintenance_running) return 1;
        sample_sleep_ms(10);
    } while(sample_now_ms()<deadline);
    fputs("管理请求超时，等待清理 / Management request timed out; finishing pending cleanup before exit.\n",stderr); return 0;
}
/* The result object is authoritative. Poll at a modest rate while the operation is pending. */
static int wait_operation(CxwlDepthDeviceManager *manager, const char *operation)
{
    const uint64_t deadline = sample_deadline(SAMPLE_MANAGEMENT_TIMEOUT_MS);
    do {
        CxwlDepthResult result = CxwlDepth_PollDeviceManager(manager);
        if (result) { sample_failure(NULL, "PollDeviceManager", result); return 0; }
        char *json = sample_manager_json(manager, 0);
        if (!json) return 0;
        const char *state = sample_json_member(sample_json_member(json, operation), "state");
        const int pending = sample_json_text(state, "running");
        const int ready = sample_json_text(state, "ready");
        if (!pending) puts(json);
        CxwlDepth_FreeJson(json);
        if (!pending) return ready;
        sample_sleep_ms(50);
    } while (sample_now_ms() < deadline);
    fprintf(stderr, "Timed out waiting for %s.\n", operation);
    return 0;
}

static int connect_and_wait(const CxwlDepthConnectOptions *options, CxwlDepthSession **session)
{
    CxwlDepthResult result = CxwlDepth_ConnectWithOptions(options, NULL, NULL, NULL, session);
    if (result != CXWL_DEPTH_OK) {
        sample_failure(NULL, "ConnectWithOptions", result);
        return 0;
    }
    const uint64_t deadline = sample_deadline(SAMPLE_CAPTURE_TIMEOUT_MS);
    uint32_t previous_stage = UINT32_MAX;
    do {
        CxwlDepthSessionStatus status = {0};
        if (!sample_poll_status(*session, &status)) return 0;
        if (status.state == CXWL_DEPTH_STATE_STREAMING && status.depth_available && status.video_available) {
            if (options->device_id_utf8 && strcmp(options->device_id_utf8, status.device_id_utf8) != 0) {
                fputs("设备 ID 不匹配 / Unexpected device ID.\n", stderr);
                return 0;
            }
            printf("深度和视频已就绪 / Both streams ready. ID=%s\n", status.device_id_utf8);
            return 1;
        }
        sample_capture_progress(&status, &previous_stage);
    } while (sample_now_ms() < deadline);
    fputs("等待深度和视频超时 / Timed out waiting for both streams.\n", stderr);
    return 0;
}

int main(int argc,char **argv)
{
    sample_console();
    const char *command=argc>1?argv[1]:"list";
    const int connect_saved = !strcmp(command, "connect-saved");
    const int with_id=connect_saved || !strcmp(command,"select") || !strcmp(command,"remove") || !strcmp(command,"repair") || !strcmp(command,"maintain") || !strcmp(command,"recover");
    if(argc>3 || (with_id && argc!=3) || (!with_id && argc>2)
        || (!with_id && strcmp(command,"list") && strcmp(command,"scan") && strcmp(command,"connect-usb"))) {
        fputs("用法 / Usage: CxwlDepthDeviceManagement [list|scan|connect-usb|connect-saved ID|select ID|remove ID|repair ID|maintain ID|recover ID]\n",stderr); return 2;
    }
    if(!sample_logging()) return 1;
    CxwlDepthDeviceManager *manager=NULL;
    CxwlDepthSession *session=NULL;
    CxwlDepthConnectOptions options = {0};
    options.size = sizeof(options);
    options.abi_version = CXWL_DEPTH_ABI_VERSION;
    options.mode = CXWL_DEPTH_BOARD;
    if (connect_saved) {
        options.target = CXWL_DEPTH_TARGET_SAVED;
        options.policy = CXWL_DEPTH_CONNECT_SAVED_ONLY;
        options.device_id_utf8 = argv[2];
        const int ready = connect_and_wait(&options, &session);
        const int released = sample_disconnect(&session);
        return ready && released ? 0 : 1;
    }
    CxwlDepthResult r=CxwlDepth_OpenDeviceManager(NULL,NULL,NULL,&manager);
    if(r) { sample_failure(NULL,"OpenDeviceManager",r); return 1; }
    int success=0;
    if(!strcmp(command,"list")) {
        char *json=sample_manager_json(manager,1);
        if(json) { puts(json); CxwlDepth_FreeJson(json); success=1; }
    } else if(!strcmp(command,"scan") || !strcmp(command,"connect-usb")) {
        fputs("正在检查 USB 设备 / Scanning USB device...\n", stderr);
        r=CxwlDepth_ScanUsb(manager);
        if(r) { sample_failure(NULL,"ScanUsb",r); goto cleanup; }
        if(!wait_manager(manager)) goto cleanup;
        CxwlDepthManagerStatus status={0}; status.size=sizeof(status);
        r=CxwlDepth_GetManagerStatus(manager,&status);
        if(r) { sample_failure(NULL,"GetManagerStatus",r); goto cleanup; }
        char *json = sample_manager_json(manager, 0);
        if (!json) goto cleanup;
        puts(json);
        CxwlDepth_FreeJson(json);
        if(!status.usb_check_id) { fputs("未发现已核验的 USB 设备 / No verified USB attachment.\n",stderr); goto cleanup; }
        success=1;
        if(!strcmp(command,"connect-usb")) {
            options.target = CXWL_DEPTH_TARGET_USB;
            options.usb_check_id = status.usb_check_id;
            success = connect_and_wait(&options, &session);
        }
    } else if(!strcmp(command,"select") || !strcmp(command,"remove")) {
        r=!strcmp(command,"select")?CxwlDepth_SetActiveDevice(manager,argv[2]):CxwlDepth_RemoveSavedDevice(manager,argv[2]);
        if(r) sample_failure(NULL,command,r);
        else success=1;
    } else {
        const int recovering=!strcmp(command,"recover");
        r=recovering?CxwlDepth_RecoverDevice(manager,argv[2]):CxwlDepth_MaintainBoardFiles(manager,argv[2],!strcmp(command,"repair")?CXWL_DEPTH_BOARD_REPAIR:CXWL_DEPTH_BOARD_MAINTENANCE);
        if(r) sample_failure(NULL,recovering?"RecoverDevice":"MaintainBoardFiles",r);
        else {
            fprintf(stderr, "等待板端操作完成 / Waiting for %s...\n", command);
            success=wait_operation(manager,recovering?"recovery":"maintenance");
        }
    }
cleanup:
    if(!sample_disconnect(&session)) success=0;
    if(!sample_close_manager(&manager)) success=0;
    return success?0:1;
}

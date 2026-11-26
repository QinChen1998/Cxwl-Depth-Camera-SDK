/* Copy depth, SNR and valid data from the SAME callback.
 * Requires a camera; prints the first owned sample after capture.
 * Check capabilities and all dimensions/strides before copying. */
#include "SampleSupport.h"
typedef struct Sample {
    float *metres;
    unsigned char *snr, *valid;
    uint32_t width, height;
    uint64_t sequence;
    int received, malformed;
} Sample;
static void CXWL_DEPTH_CALL receive(void *user, const CxwlDepthFrameEx *packet)
{
    Sample *sample = (Sample *)user;
    const CxwlDepthImage *depth = &packet->frame.image;
    if (sample->received || packet->frame.stream != CXWL_DEPTH_STREAM_DEPTH
        || !packet->snr.data || !packet->valid_mask.data) return;
    if (!sample_image(depth, CXWL_DEPTH_PIXEL_F32_LE, 4)
        || !sample_image(&packet->snr, CXWL_DEPTH_PIXEL_U8, 1)
        || !sample_image(&packet->valid_mask, CXWL_DEPTH_PIXEL_U8, 1)
        || !sample_same_grid(depth, &packet->snr) || !sample_same_grid(depth, &packet->valid_mask)
        || depth->width != sample->width || depth->height != sample->height) {
        sample->malformed = 1; return;
    }
    for (uint32_t row = 0; row < depth->height; ++row) {
        memcpy(sample->metres + row * depth->width, (const char *)depth->data + row * depth->row_stride_bytes, depth->width * 4);
        memcpy(sample->snr + row * depth->width, (const char *)packet->snr.data + row * packet->snr.row_stride_bytes, depth->width);
        memcpy(sample->valid + row * depth->width, (const char *)packet->valid_mask.data + row * packet->valid_mask.row_stride_bytes, depth->width);
    }
    sample->sequence = packet->frame.sequence; sample->received = 1;
}
int main(void)
{
    sample_console();
    CxwlDepthSession *session = NULL;
    CxwlDepthCapabilities caps = {0}; caps.size = sizeof(caps);
    Sample sample = {0};
    int success = 0;
    if (!sample_logging()) return 1;
    CxwlDepthResult r = CxwlDepth_GetCapabilities(&caps);
    if (r) { sample_failure(NULL, "GetCapabilities", r); return 1; }
    if (!caps.has_snr || !caps.has_valid_mask) { fputs("质量图不可用 / Quality images unavailable.\n", stderr); return 1; }
    const size_t count = (size_t)caps.depth_width * caps.depth_height;
    sample.width = caps.depth_width; sample.height = caps.depth_height;
    sample.metres = (float *)calloc(count, sizeof(float));
    sample.snr = (unsigned char *)malloc(count); sample.valid = (unsigned char *)malloc(count);
    if (!sample.metres || !sample.snr || !sample.valid) goto cleanup;
    r = CxwlDepth_Connect(CXWL_DEPTH_BOARD, NULL, NULL, NULL, &session);
    if (r) { sample_failure(NULL, "Connect", r); goto cleanup; }
    r = CxwlDepth_SubscribeEx(session, receive, NULL, &sample);
    if (r) { sample_failure(session, "SubscribeEx", r); goto cleanup; }
    const uint64_t deadline = sample_deadline(SAMPLE_CAPTURE_TIMEOUT_MS);
    while (!sample.received && !sample.malformed && sample_now_ms() < deadline)
        if (!sample_poll(session)) break;
    success = sample.received && !sample.malformed;
    if (success) printf("帧 / Frame %llu: %.4f m, SNR %u, valid %u\n", (unsigned long long)sample.sequence,
        sample.metres[0], sample.snr[0], sample.valid[0]);
    else fputs("质量帧超时或布局不匹配 / No valid quality frame before timeout, or image layout mismatch.\n", stderr);
cleanup:
    if (!sample_disconnect(&session)) success = 0;
    free(sample.metres); free(sample.snr); free(sample.valid);
    return success ? 0 : 1;
}

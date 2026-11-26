/* Capture board XYZ with the matching radial depth and save cloud.ply/depth.pgm.
 * Use -Out with an existing writable directory, or run from a writable working directory.
 * PLY uses metres; 16-bit big-endian PGM uses millimetres, invalid pixels are zero. */
#include "SampleSupport.h"
#include <math.h>
#include <wchar.h>
#ifdef _WIN32
typedef wchar_t PathChar;
#define PATH_TEXT(s) L##s
#define path_compare _wcsicmp
#define path_exact wcscmp
#define POINT_MAIN wmain
#define open_output(path) _wfopen(path, L"wb")
#else
#include <limits.h>
#include <strings.h>
#include <sys/stat.h>
typedef char PathChar;
#define PATH_TEXT(s) s
#define path_compare strcasecmp
#define path_exact strcmp
#define POINT_MAIN main
#define open_output(path) fopen(path, "wb")
#endif
typedef struct Sample { float depth[CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT]; CxwlDepthPoint points[CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT]; int received; } Sample;
static void CXWL_DEPTH_CALL receive(void *user, const CxwlDepthFrame *frame)
{
    Sample *sample = (Sample *)user;
    const CxwlDepthImage *im = &frame->image;
    if (sample->received || frame->stream != CXWL_DEPTH_STREAM_DEPTH
        || !sample_image(im, CXWL_DEPTH_PIXEL_F32_LE, 4)
        || im->width != CXWL_DEPTH_WIDTH || im->height != CXWL_DEPTH_HEIGHT) return;
    for (uint32_t row = 0; row < im->height; ++row)
        memcpy(sample->depth + row * im->width, (const char *)im->data + row * im->row_stride_bytes, im->width * 4);
    CxwlDepthFrame cloud = {0};
    cloud.size = sizeof(cloud);
    if (CxwlDepth_GetFramePointCloud(frame, &cloud) != CXWL_DEPTH_OK) return;
    for (uint32_t row = 0; row < im->height; ++row)
        memcpy(sample->points + row * im->width, (const char *)cloud.image.data + row * cloud.image.row_stride_bytes,
               im->width * sizeof(CxwlDepthPoint));
    sample->received = 1;
}
static int save(const Sample *sample, const PathChar *directory)
{
#ifdef _WIN32
    wchar_t cloud_path[32768], depth_path[32768];
    if (_snwprintf_s(cloud_path, _countof(cloud_path), _TRUNCATE, L"%ls\\cloud.ply", directory) < 0
        || _snwprintf_s(depth_path, _countof(depth_path), _TRUNCATE, L"%ls\\depth.pgm", directory) < 0) {
#else
    char cloud_path[PATH_MAX], depth_path[PATH_MAX];
    const int a = snprintf(cloud_path, sizeof cloud_path, "%s/cloud.ply", directory);
    const int b = snprintf(depth_path, sizeof depth_path, "%s/depth.pgm", directory);
    if (a < 0 || (size_t)a >= sizeof cloud_path || b < 0 || (size_t)b >= sizeof depth_path) {
#endif
        sample_file_help(); return 0;
    }
    const CxwlDepthPoint *points = sample->points;
    size_t count = 0;
    for (size_t i = 0; i < CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT; ++i) if (points[i].z_m > 0) ++count;
    FILE *ply = open_output(cloud_path);
    if (!ply) { perror("cloud.ply"); sample_file_help(); return 0; }
    fprintf(ply, "ply\nformat ascii 1.0\ncomment coordinates in metres\nelement vertex %zu\n"
        "property float x\nproperty float y\nproperty float z\nend_header\n", count);
    for (size_t i = 0; i < CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT; ++i)
        if (points[i].z_m > 0) fprintf(ply, "%.9g %.9g %.9g\n", points[i].x_m, points[i].y_m, points[i].z_m);
    int ok = !ferror(ply); if (fclose(ply)) ok = 0;
    FILE *pgm = open_output(depth_path);
    if (!pgm) { perror("depth.pgm"); sample_file_help(); return 0; }
    fprintf(pgm, "P5\n# radial depth in millimetres; zero is invalid\n%u %u\n65535\n", CXWL_DEPTH_WIDTH, CXWL_DEPTH_HEIGHT);
    for (size_t i = 0; i < CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT; ++i) {
        const float d = sample->depth[i];
        const unsigned mm = isfinite(d) && d > 0 ? (unsigned)fmin(65535., floor(d * 1000. + .5)) : 0;
        fputc((int)(mm >> 8), pgm); fputc((int)(mm & 255), pgm);
    }
    if (ferror(pgm)) ok = 0;
    if (fclose(pgm)) ok = 0;
    if (ok) printf("已保存 / Saved cloud.ply (%zu 有效点 / valid points) and depth.pgm.\n", count);
    else sample_file_help();
    return ok;
}
int POINT_MAIN(int argc, PathChar **argv)
{
    sample_console();
    if (argc == 2 && path_exact(argv[1], PATH_TEXT("--help")) == 0) {
        puts("CxwlDepthPointCloud [-Out <directory>]"); return 0;
    }
    Sample sample = {0};
    CxwlDepthSession *session = NULL;
    int success = 0;
    int next = 1;
    const PathChar *directory = PATH_TEXT(".");
    if (argc - next == 2 && path_compare(argv[next], PATH_TEXT("-Out")) == 0) {
        directory = argv[next + 1];
        next += 2;
    }
    if (next != argc) {
        fputs("用法 / Usage: CxwlDepthPointCloud [-Out <directory>]\n", stderr); return 2;
    }
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
#else
    struct stat attributes;
    if (stat(directory, &attributes) || !S_ISDIR(attributes.st_mode)) {
#endif
        fputs("输出目录不存在 / Output directory must exist.\n", stderr); return 2;
    }
    if (!sample_logging()) return 1;
    CxwlDepthResult r = CxwlDepth_Connect(CXWL_DEPTH_BOARD, receive, NULL, &sample, &session);
    if (r) { sample_failure(NULL, "Connect", r); return 1; }
    const uint64_t deadline = sample_deadline(SAMPLE_CAPTURE_TIMEOUT_MS);
    while (!sample.received && sample_now_ms() < deadline) if (!sample_poll(session)) break;
    if (sample.received) {
        success = save(&sample, directory);
    } else fputs("等待深度帧超时 / No depth frame received before timeout.\n", stderr);
    if (!sample_disconnect(&session)) success = 0;
    return success ? 0 : 1;
}

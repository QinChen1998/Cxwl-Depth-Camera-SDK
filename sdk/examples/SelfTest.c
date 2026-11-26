/* Offline self-test of the installed public SDK. */
#ifdef _WIN32
#include "SampleSupport.h"
#else
#include <DepthSdk.h>
#include <stdio.h>
#include <string.h>
#endif
#include "ExpectedExports.h"
#include <math.h>
#ifdef _WIN32
#include <psapi.h>
#include <wchar.h>
typedef HMODULE SdkModule;
typedef wchar_t ReportChar;
#define REPORT_MAIN wmain
#define ARG_EQ(s,lit) (_wcsicmp(s, L##lit) == 0)
#define SDK_FUNCTION(name) CxwlDepth_##name
#else
#include <dlfcn.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
typedef void *SdkModule;
typedef char ReportChar;
#define REPORT_MAIN main
#define ARG_EQ(s,lit) (strcasecmp(s,lit) == 0)
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
/* Resolve calls from the same handle we inspected. The Linux diagnostic must
 * reach main even if the SDK or one of its dependencies cannot be loaded. */
#define SDK_FUNCTION(name) ((__typeof__(&CxwlDepth_##name))dlsym(dll, "CxwlDepth_" #name))
#endif

typedef struct Check {
    const char *name;
    int passed;
} Check;

static void json_string(FILE *out, const char *value)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') fputc('\\', out);
        if (*p < 32) fprintf(out, "\\u%04x", (unsigned)*p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

static void environment_json(FILE *out)
{
#ifdef _WIN32
    OSVERSIONINFOW os = {0};
    os.dwOSVersionInfoSize = sizeof(os);
    typedef LONG (WINAPI *GetWindowsVersion)(OSVERSIONINFOW *);
    const GetWindowsVersion get_version = (GetWindowsVersion)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    fprintf(out, "\"platform\":\"windows-x64\",\"native_architecture\":%u,\"logical_processors\":%lu,\"windows_version\":",
        (unsigned)system.wProcessorArchitecture, system.dwNumberOfProcessors);
    if (get_version && get_version(&os) == 0)
        fprintf(out, "\"%lu.%lu.%lu\"", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
    else fputs("null", out);
    MEMORYSTATUSEX memory = {0};
    memory.dwLength = sizeof(memory);
    fputs(",\"physical_memory_bytes\":", out);
    if (GlobalMemoryStatusEx(&memory)) fprintf(out, "%llu", memory.ullTotalPhys);
    else fputs("null", out);
#else
    struct utsname info;
#ifdef __aarch64__
    fputs("\"platform\":\"linux-arm64\",\"kernel\":", out);
#else
    fputs("\"platform\":\"linux-x64\",\"kernel\":", out);
#endif
    if (uname(&info) == 0) json_string(out, info.release); else fputs("null", out);
    fprintf(out, ",\"logical_processors\":%ld", sysconf(_SC_NPROCESSORS_ONLN));
#endif
}

#ifdef _WIN32
static unsigned long long filetime_ticks(FILETIME value)
{
    return ((unsigned long long)value.dwHighDateTime << 32) | value.dwLowDateTime;
}
#endif

static void process_json(FILE *out)
{
#ifdef _WIN32
    const HANDLE process = GetCurrentProcess();
    FILETIME created, exited, kernel, user, now;
    fputs("{\"wall_time_ms\":", out);
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        GetSystemTimePreciseAsFileTime(&now);
        const unsigned long long start = filetime_ticks(created);
        const unsigned long long end = filetime_ticks(now);
        fprintf(out, "%llu,\"cpu_time_ms\":%llu", end < start ? 0 : (end - start) / 10000,
            (filetime_ticks(kernel) + filetime_ticks(user)) / 10000);
    } else fputs("null,\"cpu_time_ms\":null", out);
    PROCESS_MEMORY_COUNTERS_EX memory = {0};
    memory.cb = sizeof(memory);
    fputs(",\"peak_working_set_bytes\":", out);
    if (K32GetProcessMemoryInfo(process, (PROCESS_MEMORY_COUNTERS *)&memory, sizeof(memory)))
        fprintf(out, "%zu,\"private_bytes\":%zu}", memory.PeakWorkingSetSize, memory.PrivateUsage);
    else fputs("null,\"private_bytes\":null}", out);
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)) fputs("{}", out);
    else fprintf(out, "{\"peak_working_set_bytes\":%llu}", (unsigned long long)usage.ru_maxrss * 1024);
#endif
}

static int has_export(SdkModule dll, const char *name);
static int write_report(const ReportChar *path, int passed, SdkModule dll,
    const CxwlDepthVersionInfo *version, const CxwlDepthCapabilities *caps,
    const CxwlDepthErrorInfo *error, const Check *checks, size_t count)
{
#ifdef _WIN32
    FILE *out = _wfopen(path, L"wb");
#else
    FILE *out = fopen(path, "wb");
#endif
    if (!out) return 0;
    fprintf(out, "{\n  \"schema\":1,\n  \"passed\":%s,\n  \"environment\":{", passed ? "true" : "false");
    environment_json(out);
    fputs("},\n  \"process\":", out);
    process_json(out);
    fputs(",\n  \"expected_sdk_version\":\"" CXWL_DEPTH_VERSION "\",\n  \"sdk_version\":", out);
    json_string(out, version->version_utf8);
    fputs(",\n  \"sdk_build\":", out);
    json_string(out, version->build_utf8);
    fprintf(out, ",\n  \"expected_abi_version\":%u,\n  \"abi_version\":%u,\n"
        "  \"expected_exports\":%zu,\n  \"capabilities\":{\"max_sessions\":%u,\"mode_count\":%u,"
        "\"depth_width\":%u,\"depth_height\":%u},\n  \"missing_exports\":[",
        CXWL_DEPTH_ABI_VERSION, version->abi_version, _countof(cxwl_expected_exports),
        caps->max_sessions, caps->mode_count, caps->depth_width, caps->depth_height);
    const char *separator = "";
    for (size_t i = 0; i < _countof(cxwl_expected_exports); ++i) {
        if (has_export(dll, cxwl_expected_exports[i])) continue;
        fputs(separator, out);
        json_string(out, cxwl_expected_exports[i]);
        separator = ",";
    }
    fputs("],\n  \"checks\":{", out);
    for (size_t i = 0; i < count; ++i)
        fprintf(out, "%s\"%s\":%s", i ? "," : "", checks[i].name, checks[i].passed ? "true" : "false");
    /* Keep the original schema-1 check name readable by existing clients. */
    fprintf(out, ",\"dll_loaded\":%s", dll ? "true" : "false");
    fprintf(out, "},\n  \"last_error\":{\"code\":%d,\"function\":", error->code);
    json_string(out, error->function_utf8);
    fputs(",\"message\":", out);
    json_string(out, error->message_utf8);
    fputs("}\n}\n", out);
    const int ok = !ferror(out);
    return fclose(out) == 0 && ok;
}

static SdkModule load_sdk(void)
{
#ifdef _WIN32
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(NULL, path, _countof(path));
    if (!length || length >= _countof(path)) return NULL;
    wchar_t *file = wcsrchr(path, L'\\');
    if (!file || wcscpy_s(file + 1, _countof(path) - (size_t)(file + 1 - path), L"CxwlDepthSdk.dll")) return NULL;
    HMODULE module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) fprintf(stderr, "Cannot load the SDK next to this executable (Windows error %lu).\n", GetLastError());
    return module;
#else
    SdkModule module = dlopen("libCxwlDepthSdk.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!module) fprintf(stderr, "Cannot load libCxwlDepthSdk.so.3: %s\n", dlerror());
    return module;
#endif
}

static int has_export(SdkModule dll, const char *name)
{
#ifdef _WIN32
    return dll && GetProcAddress(dll, name) != NULL;
#else
    return dll && dlsym(dll, name) != NULL;
#endif
}

int REPORT_MAIN(int argc, ReportChar **argv)
{
#ifdef _WIN32
    sample_console();
#endif
    int verbose = 0;
    const ReportChar *report = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!verbose && ARG_EQ(argv[i], "-v")) verbose = 1;
        else if (!report && ARG_EQ(argv[i], "-Report") && i + 1 < argc && argv[i + 1][0] != '-')
            report = argv[++i];
        else {
            fputs("用法 / Usage: CxwlDepthSelfTest [-v] [-Report <file.json>]\n", stderr);
            return 2;
        }
    }
    SdkModule dll = load_sdk();
    int exports = dll != NULL;
    for (size_t i = 0; dll && i < _countof(cxwl_expected_exports); ++i) {
        if (has_export(dll, cxwl_expected_exports[i])) continue;
        fprintf(stderr, "缺少导出 / Missing export: %s\n", cxwl_expected_exports[i]);
        exports = 0;
    }
    CxwlDepthVersionInfo version = {0}; version.size = sizeof(version);
    CxwlDepthCapabilities caps = {0}; caps.size = sizeof(caps);
    const int supported_abi = has_export(dll, "CxwlDepth_AbiVersion") && SDK_FUNCTION(AbiVersion)() == CXWL_DEPTH_ABI_VERSION;
    const int version_ok = supported_abi && has_export(dll, "CxwlDepth_GetVersionInfo") && SDK_FUNCTION(GetVersionInfo)(&version) == CXWL_DEPTH_OK;
    const int capabilities_ok = supported_abi && has_export(dll, "CxwlDepth_GetCapabilities") && SDK_FUNCTION(GetCapabilities)(&caps) == CXWL_DEPTH_OK;
    const int abi_ok = version_ok && version.abi_version == CXWL_DEPTH_ABI_VERSION;
    const int version_match = version_ok && strcmp(version.version_utf8, CXWL_DEPTH_VERSION) == 0;
    const int capabilities_match = capabilities_ok && caps.max_sessions == 1 && caps.mode_count == 6
        && caps.modes[0] == CXWL_DEPTH_BOARD && caps.modes[1] == CXWL_DEPTH_BOARD_HIGH_ACCURACY
        && caps.depth_width == CXWL_DEPTH_WIDTH && caps.depth_height == CXWL_DEPTH_HEIGHT;
    CxwlDepthErrorInfo error = {0}; error.size = sizeof(error);
    if (supported_abi && has_export(dll, "CxwlDepth_GetLastError")) SDK_FUNCTION(GetLastError)(&error);
    const Check checks[] = {{"library_loaded", dll != NULL}, {"exports", exports}, {"version", version_match},
        {"abi", abi_ok}, {"capabilities", capabilities_match}, {"board_cloud_api", has_export(dll, "CxwlDepth_GetFramePointCloud") && !has_export(dll, "CxwlDepth_ProjectPointCloud")}};
    int passed = 1;
    for (size_t i = 0; i < _countof(checks); ++i) {
        if (!checks[i].passed) {
            fprintf(stderr, "失败 / FAIL: %s\n", checks[i].name);
            passed = 0;
        } else if (verbose) printf("通过 / PASS: %s\n", checks[i].name);
    }
    if (report && !write_report(report, passed, dll, &version, &caps, &error, checks, _countof(checks))) {
        fputs("无法写入报告 / Could not write the report.\n", stderr);
        return 1;
    }
    if (passed) printf("通过 / PASS: SDK %s, ABI %u, %zu exports, six acquisition modes and board point cloud API.\n",
        version.version_utf8, version.abi_version, _countof(cxwl_expected_exports));
    return passed ? 0 : 1;
}

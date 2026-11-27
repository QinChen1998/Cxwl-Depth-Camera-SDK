/** @file DepthSdk.h
 * @brief CXWL Depth Camera SDK 公开 C 接口，支持 Windows x64、Linux x86-64 和 Linux ARM64。
 */
#ifndef CXWL_DEPTH_SDK_H
#define CXWL_DEPTH_SDK_H

#include <stddef.h>
#include <stdint.h>
#include "DepthSdkVersion.h"

#if UINTPTR_MAX != UINT64_MAX || \
    !( (defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))) || \
       (defined(__linux__) && (defined(__x86_64__) || (defined(__aarch64__) && !defined(__AARCH64EB__)))) )
#error "CXWL Depth SDK requires Windows x64, Linux x86-64 or little-endian Linux AArch64."
#endif

#pragma pack(push, 8)

/** @brief 公开共享库符号。Windows 使用 DLL 导入／导出，Linux 使用 ELF 默认可见性。 */
#if defined(__linux__)
#define CXWL_DEPTH_API __attribute__((visibility("default")))
#elif defined(CXWL_DEPTH_SDK_BUILD)
#define CXWL_DEPTH_API __declspec(dllexport)
#else
#define CXWL_DEPTH_API __declspec(dllimport)
#endif

/** @brief 标记保留的旧接口。包含本头文件前定义 CXWL_DEPTH_NO_DEPRECATED 可关闭弃用警告。 */
#if defined(CXWL_DEPTH_NO_DEPRECATED) || defined(CXWL_DEPTH_SDK_BUILD)
#define CXWL_DEPTH_DEPRECATED(message)
#elif defined(_MSC_VER)
#define CXWL_DEPTH_DEPRECATED(message) __declspec(deprecated(message))
#elif defined(__GNUC__) || defined(__clang__)
#define CXWL_DEPTH_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#define CXWL_DEPTH_DEPRECATED(message)
#endif
/** @brief 指定 SDK 函数和回调的调用约定。
 *
 * 定义回调函数时，将此宏放在函数名之前，例如 `void CXWL_DEPTH_CALL on_event(...)`。
 * @warning C++ 应用须在帧、事件和日志回调内部处理异常，避免异常穿过 C 回调边界。
 */
#ifdef _WIN32
#define CXWL_DEPTH_CALL __cdecl
#else
#define CXWL_DEPTH_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** 版本与 ABI 宏由 DepthSdkVersion.h 提供。ABI v3 与 v2、v1 不兼容，应用须重新编译。
 * 首次调用前核对 CxwlDepth_AbiVersion() == CXWL_DEPTH_ABI_VERSION。 */

/** @brief SDK 创建的采集会话句柄类型。通过指针进行采集控制和状态查询。 */
typedef struct CxwlDepthSession CxwlDepthSession;
/** @brief SDK 创建的设备管理器句柄类型。通过指针进行设备登记、选择和维护。 */
typedef struct CxwlDepthDeviceManager CxwlDepthDeviceManager;
/** @brief SDK API 的结果类型，使用有符号 32 位整数表示下列返回码。 */
typedef int32_t CxwlDepthResult;

/** @brief API 返回码
 *
 * 返回值类型为 CxwlDepthResult。异步请求受理后，应用通过状态或事件确认执行结果。
 * 各函数列出其正常调用路径的返回码。日志回调应只记录消息。CxwlDepth_AbiVersion 与 CxwlDepth_GetLastError 可直接读取版本或线程错误，其余受保护入口在日志回调中返回 BUSY。内部异常映射为 UNAVAILABLE。
 * C/C++ 调用方无需创建 Qt 对象；首次创建句柄的线程负责整个会话的事件派发与释放。
 */
enum {
    CXWL_DEPTH_OK = 0, /**< 操作成功，或异步请求已受理。 */
    CXWL_DEPTH_INVALID_ARGUMENT = 1, /**< 句柄、参数、选项或配置不符合接口要求。 */
    CXWL_DEPTH_BUSY = 2, /**< 当前会话或管理器暂不能执行该操作，例如仍在退出或正在派发回调。 */
    CXWL_DEPTH_WRONG_THREAD = 3, /**< API 调用线程不符合要求。句柄操作须在创建线程执行。 */
    CXWL_DEPTH_DEVICE_NOT_FOUND = 4, /**< 指定设备、已准备的档案或对应的 USB 检查结果不可用。 */
    CXWL_DEPTH_BUFFER_TOO_SMALL = 5, /**< 输出缓冲区或结构体容量不足。按该 API 规定的容量重试。 */
    CXWL_DEPTH_UNAVAILABLE = 6 /**< 操作在当前条件下不可用，例如运行依赖缺失、离线保护或内部操作失败。 */
};

/** @brief 采集档位
 *
 * 用于连接选项、CxwlDepth_SetMode 和帧中的 `mode` 字段。SDK 将下列常量映射至对应的板端预设。
 */
enum {
    CXWL_DEPTH_BOARD = 1, /**< 普通档。对应板端 mode24，输出 48×32 径向深度。 */
    CXWL_DEPTH_BOARD_HIGH_ACCURACY = 2, /**< 高精度档。对应板端 mode25，输出 48×32 径向深度。 */
    CXWL_DEPTH_BOARD_8X8 = 3, /**< 8×8 普通档。对应板端 mode30，传感器 -d 0。 */
    CXWL_DEPTH_BOARD_8X8_HIGH_ACCURACY = 4, /**< 8×8 高精度档。对应板端 mode31，传感器 -d 2。 */
    CXWL_DEPTH_BOARD_16X16 = 5, /**< 16×16 普通档。对应板端 mode32，传感器 -d 5。 */
    CXWL_DEPTH_BOARD_16X16_HIGH_ACCURACY = 6 /**< 16×16 高精度档。对应板端 mode33，传感器 -d 6。 */
};
/** @brief 数据流类型
 *
 * 通过 CxwlDepthFrame 的 `stream` 字段区分回调中的视频帧和深度帧。
 */
enum {
    CXWL_DEPTH_STREAM_VIDEO = 1, /**< 摄像头原视频流，图像格式为 RGB8。 */
    CXWL_DEPTH_STREAM_POINT_CLOUD = 3, /**< 与深度同帧的板端有序 XYZ 点云，由 GetFramePointCloud 获取。 */
    CXWL_DEPTH_STREAM_DEPTH = 2 /**< ToF 深度流，输出米制径向距离。 */
};
/** @brief 像素格式
 *
 * 用于 CxwlDepthImage 的 `pixel_format` 字段。读取图像时按 `row_stride_bytes` 定位每一行。
 */
enum {
    CXWL_DEPTH_PIXEL_RGB8 = 1, /**< 每像素 3 字节，依次为 R、G、B 三个 8 位分量。 */
    CXWL_DEPTH_PIXEL_F32_LE = 2, /**< 每像素一个 4 字节小端浮点数。SDK 深度像素为正的有限径向距离（米）或缺测值 0。 */
    CXWL_DEPTH_PIXEL_XYZ_F32_LE = 4, /**< 每个像素 12 字节，小端 float32 X、Y、Z，单位为米。 */
    CXWL_DEPTH_PIXEL_U8 = 3 /**< 每像素一个无符号 8 位整数，用于首峰 SNR 图和测距有效图。 */
};
/** @brief 帧的坐标含义
 *
 * 用于 CxwlDepthFrame 的 `coordinate` 字段。
 */
enum {
    CXWL_DEPTH_COORDINATE_NONE = 0, /**< 图像无需深度坐标解释，用于原视频帧。 */
    CXWL_DEPTH_COORDINATE_SENSOR_XYZ = 2, /**< ToF 传感器笛卡尔坐标，X 沿列、Y 沿行、Z 向前。 */
    CXWL_DEPTH_COORDINATE_SENSOR_RADIAL = 1 /**< 深度像素是沿 ToF 射线测得的径向距离，单位为米。 */
};
/** @brief 帧时间戳的时钟来源
 *
 * 用于 CxwlDepthFrame 的 `clock_domain` 字段。比较时间戳前须确认使用相同的时钟域。
 */
enum {
    CXWL_DEPTH_CLOCK_HOST_MONOTONIC = 1, /**< 主机单调时钟，用于视频帧的解码时间。 */
    CXWL_DEPTH_CLOCK_BOARD_MONOTONIC = 2 /**< 板端单调时钟，用于深度帧的测量时间。 */
};
/** @brief 会话与设备管理事件
 *
 * 通过 CxwlDepthEventCallback 的 `event` 参数传入。收到事件后可查询对应句柄的最新状态。
 */
enum {
    CXWL_DEPTH_EVENT_STATE_CHANGED = 1, /**< 会话状态或采集配置发生变化。通过 CxwlDepth_GetSessionStatus 查询。 */
    CXWL_DEPTH_EVENT_VIDEO_CLEARED = 2, /**< 视频流的可用状态已清除。此前缓存的帧已过期。 */
    CXWL_DEPTH_EVENT_DEPTH_CLEARED = 3, /**< 深度流的可用状态已清除。此前缓存的深度帧和质量图已过期。 */
    CXWL_DEPTH_EVENT_BOARD_FILES_REQUIRED = 4, /**< 当前设备需要检查或修复板端采集文件。通过会话状态查询设备标识。 */
    CXWL_DEPTH_EVENT_RESERVED_5 = 5, /**< ABI 保留值，勿使用。 */
    CXWL_DEPTH_EVENT_USB_CHANGED = 6, /**< USB 检查或登记状态发生变化。管理器用 CxwlDepth_GetDeviceManagerStateAlloc 查询，Open 会话用 CxwlDepth_GetUsbStatusAlloc 查询。 */
    CXWL_DEPTH_EVENT_DEVICES_CHANGED = 7, /**< 管理器的登记设备或当前选择发生变化。可重新调用 CxwlDepth_ListSavedDevicesAlloc。 */
    CXWL_DEPTH_EVENT_MAINTENANCE_CHANGED = 8, /**< 管理器的板端文件维护状态发生变化。可查询维护进度或结果。 */
    CXWL_DEPTH_EVENT_RECOVERY_CHANGED = 9, /**< 设备恢复状态变化。通过 GetDeviceManagerState 的 recovery 对象查询结果。 */
};

/** @brief 连接目标
 *
 * 用于 CxwlDepthConnectOptions 的 `target` 字段和 CxwlDepth_SetConnectionTarget。
 */
enum {
    CXWL_DEPTH_TARGET_AUTO = 0, /**< 自动选择目标。优先尝试 USB，适用时使用当前登记设备。 */
    CXWL_DEPTH_TARGET_SAVED = 1, /**< 连接已登记的设备。可在连接选项中指定设备标识。 */
    CXWL_DEPTH_TARGET_USB = 2 /**< 通过 USB 接入设备。可绑定当前管理器签发的有效 USB 检查 ID。 */
};
/** @brief 板端文件维护操作
 *
 * 作为 CxwlDepth_MaintainBoardFiles 的 `operation` 参数。
 */
enum {
    CXWL_DEPTH_BOARD_REPAIR = 0, /**< 检查并按需修复受管的板端采集文件。 */
    CXWL_DEPTH_BOARD_MAINTENANCE = 1 /**< 检查和修复文件，并在验证本机归档后清理板端可清理的回滚副本。 */
};
/** @brief 自动连接阶段
 *
 * 用于 CxwlDepthSessionStatus 的 `connection_stage` 字段。会话是否已开始采集可通过 `state` 判断。
 */
enum {
    CXWL_DEPTH_STAGE_IDLE = 0, /**< 当前无需推进自动连接准备流程。 */
    CXWL_DEPTH_STAGE_CHECKING = 1, /**< 正在检查设备接入、USB 或运行条件。 */
    CXWL_DEPTH_STAGE_RESERVED_2 = 2, /**< ABI 保留值，勿使用。 */
    CXWL_DEPTH_STAGE_STARTING = 3, /**< 正在启动板端采集。 */
    CXWL_DEPTH_STAGE_WAITING_FOR_DEPTH = 4 /**< 采集已启动，正在等待有效深度帧。 */
};
/** @brief ToF 深度图的宽度，单位为像素。也用于标称射线和点云的网格列数。 */
#define CXWL_DEPTH_WIDTH 48u
/** @brief ToF 深度图的高度，单位为像素。也用于标称射线和点云的网格行数。 */
#define CXWL_DEPTH_HEIGHT 32u

/** @brief 分步创建采集会话或指定设备管理器路径时使用的选项。
 *
 * 传入此结构体时，两个路径都须为绝对路径，UTF-8 字符串在调用期间保持有效。
 * @see CxwlDepth_Open
 * @see CxwlDepth_OpenDeviceManager
 */
typedef struct CxwlDepthOptions {
    uint32_t size; /**< 调用前设为此结构体的字节大小。 */
    uint32_t abi_version; /**< 填写 `CXWL_DEPTH_ABI_VERSION`。 */
    const char *runtime_directory_utf8; /**< 包含 SDK 运行依赖的目录。 */
    const char *device_store_utf8; /**< 设备档案文件路径，通常为 devices.json。 */
} CxwlDepthOptions;

/** @brief 描述一幅图像的像素缓冲区、尺寸、行跨度和格式。
 *
 * 非空图像须满足 `row_stride_bytes >= width * bpp` 及 `byte_count >= (height - 1) * row_stride_bytes + width * bpp`。乘法前先转为 uint64_t，用 64 位无符号算术计算所需字节数并与 byte_count 比较。RGB8、F32_LE、U8 的 bpp 分别为 3、4、1。SDK 空图像的 data 为 NULL，宽高为 0。若图像通过帧回调提供，缓冲区在回调返回后失效。
 * @see @ref sdk_data
 */
typedef struct CxwlDepthImage {
    const void *data; /**< 像素缓冲区的只读地址。无图像数据时为 `NULL`。 */
    uint64_t byte_count; /**< 缓冲区可读的总字节数，可包含行填充。 */
    uint32_t width; /**< 每行的像素数。 */
    uint32_t height; /**< 图像行数。 */
    uint32_t row_stride_bytes; /**< 相邻两行起始地址的字节间隔，含行填充。 */
    uint32_t pixel_format; /**< 像素格式，使用 `CXWL_DEPTH_PIXEL_*` 常量。 */
} CxwlDepthImage;

/** @brief 点云中一个三维点的坐标，也用来表示标称单位射线的三个分量。
 *
 * 点云坐标以米为单位。X 沿列号增加，Y 沿行号增加，Z 向前。用作单位射线时，三个分量为无量纲方向。
 */
typedef struct CxwlDepthPoint {
    float x_m, /**< X 分量，沿图像列号增加方向。 */
          y_m, /**< Y 分量，沿图像行号增加方向。 */
          z_m; /**< Z 分量，沿 ToF 传感器前向。 */
} CxwlDepthPoint;

/** @brief 视频或深度回调提供的一帧图像及其采集信息。
 *
 * 帧和它引用的地址仅在回调期间有效。需要在回调返回后处理时，请先复制所需图像和字符串。
 * @see CxwlDepth_Subscribe
 * @see @ref sdk_data
 */
typedef struct CxwlDepthFrame {
    uint32_t size; /**< SDK 填写的字节数。读取成员前可核对 size 至少覆盖该成员的偏移与大小。 */
    uint32_t stream; /**< 帧所属的数据流，取 `CXWL_DEPTH_STREAM_*` 常量。 */
    uint32_t mode; /**< 采集档位，取公开的 SDK 采集档位常量。 */
    uint32_t coordinate; /**< 深度为 `CXWL_DEPTH_COORDINATE_SENSOR_RADIAL`，视频为 `CXWL_DEPTH_COORDINATE_NONE`。 */
    uint64_t session_epoch; /**< SDK 生成的会话代次，用于区分重启、换档或恢复前后的帧。 */
    uint64_t sequence; /**< 帧在所属数据流内的序号。深度流使用板端测量序号。 */
    uint64_t timestamp_ns; /**< 帧时间戳，单位为纳秒。时钟来源由 `clock_domain` 指定。 */
    uint32_t clock_domain; /**< 视频使用主机单调时钟，深度使用板端单调时钟。 */
    const char *device_id_utf8; /**< 帧来源设备的 UTF-8 标识。 */
    const char *capture_generation_utf8; /**< 当前板端采集代次的不透明 UTF-8 标识，按字节判等。 */
    CxwlDepthImage image; /**< 帧的主图像。视频为 RGB8，深度为小端 float32 径向距离（米），0 表示缺测。 */
    const char *metadata_json_utf8; /**< 深度帧借用的 UTF-8 JSON，仅在回调期间可读，禁止传给 CxwlDepth_FreeJson。视频帧为 `NULL`。 */
} CxwlDepthFrame;

/** @brief 接收一帧视频或深度图像。
 * @param[in] user 注册回调时传入的上下文，由 SDK 原样传回。
 * @param[in] frame 本次回调的只读帧。通过 `stream` 区分视频和深度。
 * @note 在会话所有者线程调用。帧、像素和字符串只在本次回调期间有效，需要延后使用时先复制。可调用 CxwlDepth_GetSessionStatus 或 CxwlDepth_GetStateAlloc 查询。控制、重新订阅和释放会话必须在回调返回后执行。
 * @see CxwlDepth_Subscribe
 */
typedef void (CXWL_DEPTH_CALL *CxwlDepthFrameCallback)(void *user, const CxwlDepthFrame *frame);
/** @brief 接收会话或设备管理器的状态变化通知。
 * @param[in] user 注册回调时传入的上下文，可用来识别事件来源。
 * @param[in] event 事件类型，使用 `CXWL_DEPTH_EVENT_*` 常量。
 * @note 在会话或管理器的所有者线程调用。可在回调中查询状态，控制操作必须在回调返回后执行。
 * @see CxwlDepth_GetSessionStatus
 * @see CxwlDepth_GetManagerStatus
 */
typedef void (CXWL_DEPTH_CALL *CxwlDepthEventCallback)(void *user, uint32_t event);

/** @name 连接配置、状态与测量数据
 * 包含 size 字段的输入选项和输出结构体须先清零，再设置 size=sizeof(结构体类型)。
 * 连接选项中的 abi_version 设置为 CXWL_DEPTH_ABI_VERSION。
 * 字符串使用 UTF-8，以 NUL 结尾；空字符串表示未知或尚未观察到。
 * @{ */
/** @brief 采集会话状态
 *
 * 用于 CxwlDepthSessionStatus 的 `state` 字段。
 */
enum {
    CXWL_DEPTH_STATE_STOPPED = 0, /**< 会话已停止。若设备准备就绪，可再次调用 CxwlDepth_Start。 */
    CXWL_DEPTH_STATE_CONNECTING = 1, /**< 正在准备设备、启动采集或等待两条数据流就绪。 */
    CXWL_DEPTH_STATE_STREAMING = 2, /**< 视频和深度均已就绪。 */
    CXWL_DEPTH_STATE_RECOVERING = 3, /**< 采集链路中断，SDK 正在等待或执行恢复。 */
    CXWL_DEPTH_STATE_STOPPING = 4, /**< 停止或释放请求正在处理。 */
    CXWL_DEPTH_STATE_ERROR = 5 /**< 会话报告连接或采集失败。可读取 `error_category` 和详细诊断。 */
};
/** @brief 会话错误类别
 *
 * 用于 CxwlDepthSessionStatus 的 `error_category` 字段。完整诊断可通过 CxwlDepth_GetState 查询。
 */
enum {
    CXWL_DEPTH_ERROR_NONE = 0, /**< 当前无会话错误。 */
    CXWL_DEPTH_ERROR_USB = 1, /**< USB 发现、验证或准备失败。 */
    CXWL_DEPTH_ERROR_BOARD_FILES = 2, /**< 板端采集文件需要检查或修复。 */
    CXWL_DEPTH_ERROR_CAPTURE = 3 /**< 启动或恢复采集失败。 */
};
/** @brief 自动连接时的设备准备策略
 *
 * 用于 CxwlDepthConnectOptions 的 `policy` 字段。
 */
enum {
    CXWL_DEPTH_CONNECT_PREPARE = 0, /**< 允许按需发现、验证和登记 USB 设备，并核验预装的采集服务。 */
    CXWL_DEPTH_CONNECT_SAVED_ONLY = 1, /**< 仅使用已登记的设备。SDK 将连接目标设为 `CXWL_DEPTH_TARGET_SAVED`。 */
};
/** @brief 日志级别和日志阈值
 *
 * 用于 CxwlDepth_SetLogCallback 的阈值和 CxwlDepthLogCallback 的消息级别。阈值接收数值小于或等于它的级别，当前 SDK 输出同步 API 错误日志。
 */
enum {
    CXWL_DEPTH_LOG_ERROR = 1, /**< 错误级，用于报告同步 API 失败。 */
    CXWL_DEPTH_LOG_WARNING = 2, /**< 警告级阈值，允许错误和警告级别。 */
    CXWL_DEPTH_LOG_INFO = 3 /**< 信息级阈值，允许错误、警告和信息级别。 */
};

/** @brief 指定一次自动连接的采集档位、设备目标和准备策略。
 *
 * 先清零结构体，再设置大小、ABI 版本和档位。已有设备管理器时，两个路径字段须填 `NULL`，由管理器提供路径。
 * @see CxwlDepth_ConnectWithOptions
 */
typedef struct CxwlDepthConnectOptions {
    uint32_t size, /**< 调用前设为此结构体的字节大小。 */
             abi_version, /**< 填写 `CXWL_DEPTH_ABI_VERSION`。 */
             mode, /**< 请求的采集档位，取公开的 SDK 采集档位常量。 */
             target; /**< 设备目标，取 `CXWL_DEPTH_TARGET_AUTO`、`CXWL_DEPTH_TARGET_SAVED` 或 `CXWL_DEPTH_TARGET_USB`。 */
    uint64_t usb_check_id; /**< 指定 USB 目标时可传入当前管理器签发的有效检查 ID。0 表示未指定。 */
    uint32_t policy; /**< 设备准备策略，参见 CXWL_DEPTH_CONNECT_PREPARE 等常量。 */
    const char *runtime_directory_utf8; /**< SDK 运行目录的绝对路径。默认使用共享库所在目录或管理器的路径。 */
    const char *device_store_utf8; /**< 设备档案文件的绝对路径。默认使用当前用户档案或管理器的路径。 */
    const char *device_id_utf8; /**< 可选设备标识，仅用于 `CXWL_DEPTH_TARGET_SAVED`。为 `NULL` 时使用当前选中的设备。 */
} CxwlDepthConnectOptions;

/** @brief SDK 的语义版本、ABI 版本和编译时间。
 * @see CxwlDepth_GetVersionInfo
 */
typedef struct CxwlDepthVersionInfo {
    uint32_t size, /**< 调用前设为此结构体的字节大小。 */
             abi_version, /**< 当前运行库支持的 ABI 版本号。 */
             major, /**< SDK 主版本号。 */
             minor, /**< SDK 次版本号。 */
             patch; /**< SDK 修订版本号。 */
    char version_utf8[32], /**< 运行库的完整版本字符串，格式为 major.minor.patch。 */
         build_utf8[64]; /**< SDK 编译日期和时间字符串。 */
} CxwlDepthVersionInfo;

/** @brief 当前线程最近一次同步 API 失败的结果和诊断信息。
 * @see CxwlDepth_GetLastError
 */
typedef struct CxwlDepthErrorInfo {
    uint32_t size; /**< 调用前设为此结构体的字节大小。 */
    CxwlDepthResult code; /**< 最近一次记录的 API 错误码。尚无记录时为 `CXWL_DEPTH_OK`。 */
    char function_utf8[96], /**< 产生错误的公开 API 名称。 */
         message_utf8[512]; /**< 该错误码的简短诊断说明。 */
} CxwlDepthErrorInfo;

/** @brief 接收 SDK 的同步 API 错误日志。
 * @param[in] user 调用 CxwlDepth_SetLogCallback 时传入的上下文。
 * @param[in] severity 本条日志的级别，使用 `CXWL_DEPTH_LOG_*` 常量。当前消息为错误级。
 * @param[in] message_utf8 以 NUL 结尾的 UTF-8 日志，包含 API 名和结果说明。地址仅在回调期间有效。
 * @note 在报告错误的线程同步调用。回调中禁止调用 SDK。注册和注销必须与其他线程的 SDK 调用串行。
 * @see CxwlDepth_SetLogCallback
 */
typedef void (CXWL_DEPTH_CALL *CxwlDepthLogCallback)(void *user, uint32_t severity, const char *message_utf8);

/** @brief 采集会话的状态快照，包含连接进度、两条数据流状态及切档信息。
 * @see CxwlDepth_GetSessionStatus
 */
typedef struct CxwlDepthSessionStatus {
    uint32_t size, /**< 调用前设为此结构体的字节大小。 */
             state, /**< 当前会话状态，使用 `CXWL_DEPTH_STATE_*` 常量。 */
             error_category, /**< 会话错误类别，使用 `CXWL_DEPTH_ERROR_*` 常量。 */
             mode, /**< 当前采集档位，取公开的 SDK 采集档位常量。 */
             connection_stage; /**< 连接准备阶段，使用 `CXWL_DEPTH_STAGE_*` 常量。 */
    uint32_t busy, /**< 为 1 时，会话有正在执行或等待的操作。 */
             video_available, /**< 为 1 时，当前视频流已有可用帧。 */
             depth_available, /**< 为 1 时，当前深度流已有可用帧。 */
             retaining_display; /**< 为 1 时，断流恢复期间可临时保留上一帧的显示内容。 */
    uint32_t switch_active, /**< 为 1 时，采集档位正在切换。 */
             switch_estimated_ms, /**< 最近一次切换的预计毫秒数，初始为 0。仅当 `switch_active` 为 1 时用于进度提示。 */
             board_files_required; /**< 为 1 时，当前设备需要检查或修复板端采集文件。 */
    uint64_t session_epoch; /**< 当前会话代次，与帧中的同名字段对应。 */
    double video_fps; /**< 主机按约 1 秒统计窗口更新的视频帧率（帧／秒）。清除视频后为 0。 */
    char device_id_utf8[128], /**< 当前设备的 UTF-8 标识。 */
         board_files_device_id_utf8[128]; /**< 需要维护板端文件的设备标识，在 `board_files_required` 为 1 时读取。 */
} CxwlDepthSessionStatus;

/** @brief 设备管理器的可操作状态、维护状态及 USB 检查结果标识。
 * @see CxwlDepth_GetManagerStatus
 */
typedef struct CxwlDepthManagerStatus {
    uint32_t size, /**< 调用前设为此结构体的字节大小。 */
             busy, /**< 为 1 时，采集会话或与其关联的后台操作处于忙状态。 */
             can_manage, /**< 为 1 时，管理器空闲并可管理设备。 */
             maintenance_running; /**< 为 1 时，板端文件维护正在执行。 */
    uint64_t usb_check_id; /**< 当前 USB 检查结果的有效 ID。无有效结果时为 0。 */
} CxwlDepthManagerStatus;

/** @brief 当前选中设备的档案信息和本会话观察到的标定版本。
 * @see CxwlDepth_GetDeviceInfo
 */
typedef struct CxwlDepthDeviceInfo {
    uint32_t size, /**< 调用前设为此结构体的字节大小。 */
             profile_ready; /**< 为 1 时，设备档案已达到连接准备条件。 */
    char device_id_utf8[128], /**< SDK 用于标识当前设备的 UTF-8 字符串。 */
         name_utf8[256], /**< 设备档案中的显示名称。 */
         serial_utf8[128]; /**< 设备档案中的序列号。 */
    char calibration_release_utf8[96]; /**< 最近深度元数据 calibration_release 的副本，与标定结构中的 board_release_utf8 同源。未观察到时为空。 */
} CxwlDepthDeviceInfo;

/** @brief ABI v3 的六档采集能力、图像格式、测量质量数据和会话能力。
 * @see CxwlDepth_GetCapabilities
 */
typedef struct CxwlDepthCapabilities {
    uint32_t size, /**< 调用前设为此结构体的字节大小。 */
             mode_count, /**< `modes` 数组中的有效模式数。 */
             modes[6]; /**< 本 SDK 支持的六个板端模式。 */
    uint32_t depth_width, /**< 基线 48×32 深度图宽度，实际尺寸从帧读取。 */
             depth_height, /**< 基线 48×32 深度图高度，实际尺寸从帧读取。 */
             depth_pixel_format; /**< 深度像素格式，当前为 `CXWL_DEPTH_PIXEL_F32_LE`。 */
    uint32_t video_width, /**< 原视频宽度，单位为像素。 */
             video_height, /**< 原视频高度，单位为像素。 */
             video_pixel_format; /**< 视频像素格式，当前为 `CXWL_DEPTH_PIXEL_RGB8`。 */
    uint32_t has_snr, /**< 为 1 时，可通过扩展帧取得首峰 SNR 图。 */
             has_valid_mask, /**< 为 1 时，可通过扩展帧取得测距有效位图。 */
             synchronized_rgb_depth, /**< 视频与深度的同步能力标志。当前为 0，两条数据流各自输出。 */
             max_sessions; /**< 同一进程的最大采集会话数，当前固定为 1。 */
} CxwlDepthCapabilities;

/** @brief 扩展帧回调提供的基础帧和同帧测量质量图。
 *
 * 质量图可用时，与深度帧的采集代次、序号和像素位置对应。所有图像与字符串在回调返回后失效。
 * @see CxwlDepth_SubscribeEx
 * @see @ref sdk_data
 */
typedef struct CxwlDepthFrameEx {
    uint32_t size; /**< SDK 填写的扩展帧结构体字节大小。 */
    CxwlDepthFrame frame; /**< 本次回调的图像帧及元数据。 */
    CxwlDepthImage snr; /**< 非空时 pixel_format 为 CXWL_DEPTH_PIXEL_U8，表示首峰信噪比。视频或无同帧质量数据时，图像为空。 */
    CxwlDepthImage valid_mask; /**< 非空时 pixel_format 为 CXWL_DEPTH_PIXEL_U8，0 为无效，1 为有效。视频或无同帧质量数据时，图像为空。 */
} CxwlDepthFrameEx;

/** @brief 接收扩展帧及可用的同帧测量质量图。
 * @param[in] user 注册回调时传入的上下文，由 SDK 原样传回。
 * @param[in] frame 只读扩展帧，含基础帧和可用的首峰 SNR、测距有效图。使用前检查质量图的 `data` 指针。
 * @note 在会话所有者线程调用。基础帧和质量图的缓冲区只在本次回调期间有效。可调用 CxwlDepth_GetSessionStatus 或 CxwlDepth_GetStateAlloc 查询。控制、重新订阅和释放会话必须在回调返回后执行。
 * @see CxwlDepth_SubscribeEx
 */
typedef void (CXWL_DEPTH_CALL *CxwlDepthFrameExCallback)(void *user, const CxwlDepthFrameEx *frame);
/** @} */

/**
 * @brief 以本次请求的显式目标、策略和路径创建并启动会话。
 * @param[in] options 带 size 和 abi_version 的选项，字符串在调用期间有效。
 * @param[in] frame 帧回调，可为 NULL。数据在回调返回后失效。
 * @param[in] event 状态事件回调，可为 NULL；事件后读取状态。
 * @param[in] user 原样传递给回调的上下文；有效期须覆盖回调订阅。
 * @param[out] session 输出句柄，失败为 NULL；必须在最终关闭成功后停止使用。
 * @return OK 请求已排队；INVALID_ARGUMENT 选项冲突或格式无效；BUSY 已有会话／管理器忙；WRONG_THREAD 线程错误；DEVICE_NOT_FOUND 目标或检查 ID 失效；UNAVAILABLE 离线保护或初始化失败。
 * @note NULL 选项使用普通模式、AUTO、PREPARE。已有管理器时路径参数必须为 NULL；非零 USB 检查 ID 必须来自该管理器。SAVED_ONLY 不登记 USB。返回 OK 仅表示异步请求已接受。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ConnectWithOptions(
    const CxwlDepthConnectOptions *options, CxwlDepthFrameCallback frame,
    CxwlDepthEventCallback event, void *user, CxwlDepthSession **session);
/**
 * @brief 查询 SDK 语义版本、ABI 版本和编译时间。
 * @param[out] info 调用前清零并设置 size=sizeof(*info)。未知字符串为空。
 * @return OK 成功；INVALID_ARGUMENT 输出指针为空；BUFFER_TOO_SMALL 输出结构 size 不足。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetVersionInfo(CxwlDepthVersionInfo *info);
/**
 * @brief 复制本线程最近一次同步 API 失败。
 * @param[out] error 调用前设置 size；返回值和消息属于最近失败，成功调用不清除。
 * @return OK 已复制快照（快照 code=OK 表示本线程尚无失败）；INVALID_ARGUMENT 指针为空；BUFFER_TOO_SMALL 结构 size 不足。
 * @note 无需会话，线程独立；查询本身不覆盖错误。异步采集失败读取 GetSessionStatus.error_category 和 GetState 诊断。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetLastError(CxwlDepthErrorInfo *error);
/**
 * @brief 设置进程级错误日志回调；空回调关闭日志。
 * @param[in] severity 日志阈值 ERROR、WARNING 或 INFO；当前输出 API 错误。
 * @param[in] callback 同步日志回调，NULL 禁用；回调中禁止调用 SDK。
 * @param[in] user 原样传递给回调的上下文；有效期须覆盖回调订阅。
 * @return OK 成功；INVALID_ARGUMENT 阈值无效；BUSY 正在创建／释放或处于回调；WRONG_THREAD 非现有句柄的所有者线程；UNAVAILABLE 内部异常。
 * @note 有句柄时可在其所有者线程、帧／事件回调外设置。注册／注销必须与其他线程的 SDK 调用串行；回调数据只在回调期间有效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetLogCallback(
    uint32_t severity, CxwlDepthLogCallback callback, void *user);
/**
 * @brief 查询指定会话的结构化状态，各连接入口使用同一格式。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[out] status 调用前清零并设置 size；布尔字段使用 0/1。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 输出结构 size 不足，请设为该结构的 sizeof；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetSessionStatus(
    CxwlDepthSession *session, CxwlDepthSessionStatus *status);
/**
 * @brief 查询结构化管理器状态。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @param[out] status 调用前清零并设置 size；布尔字段使用 0/1。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 输出结构 size 不足，请设为该结构的 sizeof；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetManagerStatus(
    CxwlDepthDeviceManager *manager, CxwlDepthManagerStatus *status);
/**
 * @brief 查询已选设备档案和最近有效深度帧的标定版本。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[out] info 调用前清零并设置 size=sizeof(*info)。未知字符串为空。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 输出结构 size 不足，请设为该结构的 sizeof；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。DEVICE_NOT_FOUND 尚未选中设备。
 * @note 返回登记档案信息；实时连接状态通过 GetSessionStatus 查询。停止／换设备后清除已观察的标定信息。ABI v3 已删除无可靠来源的固件字段。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetDeviceInfo(
    CxwlDepthSession *session, CxwlDepthDeviceInfo *info);
/**
 * @brief 查询本 SDK 的 ABI v3 基线能力与格式，无须连接硬件。
 * @param[out] capabilities 调用前设置 size；描述产品能力，不表示设备当前在线。
 * @return OK 成功；INVALID_ARGUMENT 输出指针为空；BUFFER_TOO_SMALL 输出结构 size 不足。
 * @note modes 返回本 SDK 支持的六个档位，depth_width 和 depth_height 描述 48×32 基线。实际尺寸以每帧 image 为准。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetCapabilities(CxwlDepthCapabilities *capabilities);
/**
 * @brief 设置扩展帧回调，提供同一测量帧的 SNR 和有效位。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[in] frame 帧回调，可为 NULL。数据在回调返回后失效。
 * @param[in] event 状态事件回调，可为 NULL；事件后读取状态。
 * @param[in] user 原样传递给回调的上下文；有效期须覆盖回调订阅。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。
 * @note 替换 Subscribe。所有图像、字符串与元数据仅在回调中有效。SNR 是 U8 首峰测量值，不是概率。质量图 data 为 NULL 或宽高为 0 时跳过该平面，全零像素是可读数据；不得混用跨代次的质量数据。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SubscribeEx(
    CxwlDepthSession *session, CxwlDepthFrameExCallback frame, CxwlDepthEventCallback event, void *user);

/**
 * @brief 查询运行库的 C ABI 版本。
 * @return 运行库编译时的 CXWL_DEPTH_ABI_VERSION；调用方应与配套头文件的同名宏比较。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API uint32_t CXWL_DEPTH_CALL CxwlDepth_AbiVersion(void);
/* C 和 C++ 共用本接口及同一个共享库，无需 Qt 头文件或 Qt 链接库。
   独立程序在创建句柄的线程调用 Poll；已有 Qt 应用使用其主线程和事件循环。 */
/**
 * @brief 使用默认路径自动创建并启动采集会话。
 * @param[in] mode 公开的 SDK 采集档位常量。
 * @param[in] frame 帧回调，可为 NULL。数据在回调返回后失效。
 * @param[in] event 状态事件回调，可为 NULL；事件后读取状态。
 * @param[in] user 原样传递给回调的上下文；有效期须覆盖回调订阅。
 * @param[out] session 输出句柄，失败为 NULL；必须在最终关闭成功后停止使用。
 * @return OK 请求已排队；INVALID_ARGUMENT 参数无效；BUSY 已有活动会话／管理器忙；WRONG_THREAD 线程错误；DEVICE_NOT_FOUND 指定目标失效；UNAVAILABLE 离线保护或初始化失败。
 * @note 默认自动选择设备；可先通过 SetConnectionTarget 指定本次连接目标。需要直接传入连接策略和路径时使用 ConnectWithOptions。返回 OK 仅表示请求已接受。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Connect(
    uint32_t mode, CxwlDepthFrameCallback frame, CxwlDepthEventCallback event, void *user, CxwlDepthSession **session);
/**
 * @brief 停止采集并在后台作业退出后释放会话。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @return OK 已释放；BUSY 退出尚未完成或仍在回调；INVALID_ARGUMENT 句柄无效；WRONG_THREAD 线程错误；UNAVAILABLE 内部操作失败。
 * @note BUSY 表示仍在退出，继续派发事件并重试。只有 OK 后句柄失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Disconnect(CxwlDepthSession *session);
/**
 * @brief 使用显式路径创建停止状态的会话。
 * @param[in] options 带 size 和 abi_version 的选项，字符串在调用期间有效。
 * @param[out] session 输出句柄，失败为 NULL；必须在最终关闭成功后停止使用。
 * @return OK 已创建停止状态会话；INVALID_ARGUMENT 路径或选项无效；BUSY 已有会话／管理器；WRONG_THREAD 与已有应用事件循环的所有者线程不一致；UNAVAILABLE 初始化失败。
 * @note 本函数不自动采集。依次 Subscribe、SelectDevice／SetMode、Start。options 不可为 NULL。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Open(const CxwlDepthOptions *options, CxwlDepthSession **session);
/**
 * @brief 停止并释放会话，退出语义与 Disconnect 一致。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @return OK 已释放；BUSY 退出尚未完成或仍在回调；INVALID_ARGUMENT 句柄无效；WRONG_THREAD 线程错误；UNAVAILABLE 内部操作失败。
 * @note BUSY 表示仍在退出，继续派发事件并重试。只有 OK 后句柄失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Close(CxwlDepthSession *session);
/**
 * @brief 设置图像帧及事件回调，替换 SubscribeEx。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[in] frame 帧回调，可为 NULL。数据在回调返回后失效。
 * @param[in] event 状态事件回调，可为 NULL；事件后读取状态。
 * @param[in] user 原样传递给回调的上下文；有效期须覆盖回调订阅。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。
 * @note 替换 SubscribeEx。回调期间允许状态查询；控制操作须在回调返回后执行。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Subscribe(
    CxwlDepthSession *session, CxwlDepthFrameCallback frame, CxwlDepthEventCallback event, void *user);
/**
 * @brief 在独立 C 程序的所有者线程推进事件。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @return OK 已推进事件循环；INVALID_ARGUMENT 句柄无效或宿主已有事件循环；WRONG_THREAD 非所有者线程；BUSY 回调中禁止嵌套派发；UNAVAILABLE 派发异常。
 * @note Qt 应用在主线程创建会话并使用自身事件循环，不调用 Poll。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Poll(CxwlDepthSession *session);

/**
 * @brief 在独立 C/C++ 程序的所有者线程派发事件，等待会话回调或超时。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[in] timeout_ms 最长等待时间（毫秒，0 至 INT32_MAX）。0 只处理已就绪事件，超出上限返回 INVALID_ARGUMENT。
 * @param[out] dispatched 派发至少一次该会话的帧或事件回调时为 1，等待超时或无就绪回调时为 0。
 * @return OK 等待结束，可读取 dispatched；INVALID_ARGUMENT 句柄、超时或输出参数无效，或宿主自带事件循环；WRONG_THREAD 非所有者线程；BUSY 回调中禁止嵌套派发；UNAVAILABLE 派发异常。
 * @note 超时是正常结果。已开始的回调会执行完毕，耗时可超过 timeout_ms。未订阅回调时仍会推进内部事件。Qt 应用使用自身事件循环。
 * @since 2.2.0
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_WaitForEvent(
    CxwlDepthSession *session, uint32_t timeout_ms, uint32_t *dispatched);

/**
 * @brief 列出登记设备，停止和采集会话均可查询。
 * @deprecated 自 2.2.0 弃用。请使用 CxwlDepth_ListDevicesAlloc 取得一次分配的独立快照，并调用 CxwlDepth_FreeJson 释放。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[out] buffer UTF-8 JSON 缓冲区；NULL 仅查询所需容量。
 * @param[in,out] size 输入缓冲区字节容量。传 NULL 缓冲区或返回 BUFFER_TOO_SMALL 时，写回含 NUL 的所需字节数。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 缓冲区不足，按写回的字节数重试；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_DEPRECATED("Use CxwlDepth_ListDevicesAlloc and CxwlDepth_FreeJson")
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListDevices(CxwlDepthSession *session, char *buffer, uint32_t *size);
/**
 * @brief 获取 UTF-8 JSON 状态与详细诊断。
 * @deprecated 自 2.2.0 弃用。请使用 CxwlDepth_GetStateAlloc 取得一次分配的独立快照，并调用 CxwlDepth_FreeJson 释放。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[out] buffer UTF-8 JSON 缓冲区；NULL 仅查询所需容量。
 * @param[in,out] size 输入缓冲区字节容量。传 NULL 缓冲区或返回 BUFFER_TOO_SMALL 时，写回含 NUL 的所需字节数。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 缓冲区不足，按写回的字节数重试；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。
 * @note 业务状态判断使用 GetSessionStatus；本接口提供便于记录和排查问题的详细信息。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_DEPRECATED("Use CxwlDepth_GetStateAlloc and CxwlDepth_FreeJson")
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetState(CxwlDepthSession *session, char *buffer, uint32_t *size);
/**
 * @brief 在停止且空闲时选择登记设备。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[in] device_id_utf8 枚举得到的设备 ID，不使用列表索引。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。DEVICE_NOT_FOUND 登记目标不存在、未就绪或检查 ID 已失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SelectDevice(CxwlDepthSession *session, const char *device_id_utf8);
/**
 * @brief 选择原生分辨率和精度档位，采集中切换会异步重启。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[in] mode 公开的 SDK 采集档位常量。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。DEVICE_NOT_FOUND 登记目标不存在、未就绪或检查 ID 已失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetMode(CxwlDepthSession *session, uint32_t mode);
/**
 * @brief 启动已选择且准备就绪的设备，适用于 Open 和 Connect。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @return OK 启动请求已受理；DEVICE_NOT_FOUND 设备尚未准备；BUSY 前一操作未完成或仍在回调；INVALID_ARGUMENT 句柄无效；WRONG_THREAD 线程错误；UNAVAILABLE 内部操作失败。
 * @note 设备尚未准备就绪时返回 DEVICE_NOT_FOUND；不会替代 USB 登记流程。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Start(CxwlDepthSession *session);
/**
 * @brief 停止采集但保留句柄、回调和配置，适用于所有会话。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @return OK 停止请求已受理；INVALID_ARGUMENT 句柄无效；WRONG_THREAD 线程错误；BUSY 仍在回调；UNAVAILABLE 内部操作失败。
 * @note OK 表示停止请求已受理，句柄保持有效；GetSessionStatus 在清理结束后变为 STOPPED。等待空闲后可 Start。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Stop(CxwlDepthSession *session);
/**
 * @brief 重新启动当前设备采集并保留会话。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。DEVICE_NOT_FOUND 登记目标不存在、未就绪或检查 ID 已失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_Reconnect(CxwlDepthSession *session);
/**
 * @brief 清理所选设备的故障状态，并恢复配套采集运行环境。
 * @param[in] manager 有效设备管理器，必须在创建它的线程使用。
 * @param[in] device_id_utf8 ListSavedDevices 返回的设备标识。
 * @return OK 恢复请求已受理；BUSY 仍有采集句柄或操作进行中；DEVICE_NOT_FOUND 设备未登记或未就绪；INVALID_ARGUMENT 参数无效；WRONG_THREAD 线程错误；UNAVAILABLE 无法受理。
 * @note 先 Disconnect 至 OK，再调用本接口。继续 PollDeviceManager；收到 RECOVERY_CHANGED 后查询 GetDeviceManagerState.recovery，state=ready 表示恢复完成，再用 ConnectWithOptions 启动所需模式。恢复会保留故障证据、核对采集所有权、清理已退出的故障锁，并按需更新配套运行文件。活动采集客户端须先断开。
 * @see CxwlDepth_GetDeviceManagerState
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_RecoverDevice(
    CxwlDepthDeviceManager *manager, const char *device_id_utf8);
/**
 * @brief 停止且空闲时只读检查 USB 接入，核对物理 USB 网卡、相机序号及设备实例标识。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_CheckUsb(CxwlDepthSession *session);
/**
 * @brief 读取 USB 检查／登记诊断 JSON。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[out] buffer UTF-8 JSON 缓冲区；NULL 仅查询所需容量。
 * @param[in,out] size 输入缓冲区字节容量。传 NULL 缓冲区或返回 BUFFER_TOO_SMALL 时，写回含 NUL 的所需字节数。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 缓冲区不足，按写回的字节数重试；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。
 * @deprecated 自 2.2.0 弃用。请使用 CxwlDepth_GetUsbStatusAlloc 取得一次分配的独立快照，并调用 CxwlDepth_FreeJson 释放。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_DEPRECATED("Use CxwlDepth_GetUsbStatusAlloc and CxwlDepth_FreeJson")
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetUsbStatus(CxwlDepthSession *session, char *buffer, uint32_t *size);
/**
 * @brief 基于有效检查 ID 登记设备，可选择随后启动采集。
 * @param[in] session 有效采集句柄，必须在创建它的线程使用。
 * @param[in] check_id 当前有效的 USB 检查 ID；插拔、停止或登记库变化后重新检查。
 * @param[in] start_after 0 仅登记；1 在登记成功后开始采集。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 检查 ID 失效、USB 登记无法受理或内部操作失败。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_EnrollUsb(
    CxwlDepthSession *session, uint64_t check_id, uint32_t start_after);

/**
 * @brief 打开可选设备管理器。
 * @param[in] options 带 size 和 abi_version 的选项，字符串在调用期间有效。
 * @param[in] event 状态事件回调，可为 NULL；事件后读取状态。
 * @param[in] user 原样传递给回调的上下文；有效期须覆盖回调订阅。
 * @param[out] manager 输出句柄，失败为 NULL；必须在最终关闭成功后停止使用。
 * @return OK 已创建；INVALID_ARGUMENT 输出指针、路径或选项无效；BUSY 已有会话／管理器或正在切换；WRONG_THREAD 与已有应用事件循环的所有者线程不一致；UNAVAILABLE 初始化失败。
 * @note options 可以为 NULL，使用默认用户登记目录及共享库运行目录。每进程最多一个管理器和一个采集会话；管理器存在时由 Connect 系列借用同一内部会话。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_OpenDeviceManager(
    const CxwlDepthOptions *options, CxwlDepthEventCallback event, void *user, CxwlDepthDeviceManager **manager);
/**
 * @brief 关闭空闲管理器，必须先释放采集会话。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_CloseDeviceManager(CxwlDepthDeviceManager *manager);
/**
 * @brief 推进独立程序的管理器事件。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @return OK 已派发事件；INVALID_ARGUMENT 句柄无效或宿主已有事件循环；WRONG_THREAD 非所有者线程；BUSY 回调中禁止嵌套派发；UNAVAILABLE 派发异常。
 * @note Qt 应用使用自身事件循环；独立程序即使没有采集句柄也可推进管理器事件。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_PollDeviceManager(CxwlDepthDeviceManager *manager);
/**
 * @brief 列出保存的设备及不透明 ID，不代表当前在线。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @param[out] buffer UTF-8 JSON 缓冲区；NULL 仅查询所需容量。
 * @param[in,out] size 输入缓冲区字节容量。传 NULL 缓冲区或返回 BUFFER_TOO_SMALL 时，写回含 NUL 的所需字节数。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 缓冲区不足，按写回的字节数重试；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。
 * @deprecated 自 2.2.0 弃用。请使用 CxwlDepth_ListSavedDevicesAlloc 取得一次分配的独立快照，并调用 CxwlDepth_FreeJson 释放。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_DEPRECATED("Use CxwlDepth_ListSavedDevicesAlloc and CxwlDepth_FreeJson")
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListSavedDevices(
    CxwlDepthDeviceManager *manager, char *buffer, uint32_t *size);
/**
 * @brief 在无采集会话时保存默认设备。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @param[in] device_id_utf8 枚举得到的设备 ID，不使用列表索引。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。DEVICE_NOT_FOUND 登记目标不存在、未就绪或检查 ID 已失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetActiveDevice(
    CxwlDepthDeviceManager *manager, const char *device_id_utf8);
/**
 * @brief 删除本机登记记录，不卸载驱动或板端程序。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @param[in] device_id_utf8 枚举得到的设备 ID，不使用列表索引。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。DEVICE_NOT_FOUND 登记目标不存在、未就绪或检查 ID 已失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_RemoveSavedDevice(
    CxwlDepthDeviceManager *manager, const char *device_id_utf8);
/**
 * @brief 在管理器空闲时启动只读 USB 检查。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ScanUsb(CxwlDepthDeviceManager *manager);
/**
 * @brief 读取管理器 USB／维护详细状态 JSON。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @param[out] buffer UTF-8 JSON 缓冲区；NULL 仅查询所需容量。
 * @param[in,out] size 输入缓冲区字节容量。传 NULL 缓冲区或返回 BUFFER_TOO_SMALL 时，写回含 NUL 的所需字节数。
 * @return OK 已写入结果；INVALID_ARGUMENT 句柄或输出指针无效；BUFFER_TOO_SMALL 缓冲区不足，按写回的字节数重试；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部查询失败。
 * @deprecated 自 2.2.0 弃用。请使用 CxwlDepth_GetDeviceManagerStateAlloc 取得一次分配的独立快照，并调用 CxwlDepth_FreeJson 释放。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_DEPRECATED("Use CxwlDepth_GetDeviceManagerStateAlloc and CxwlDepth_FreeJson")
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetDeviceManagerState(
    CxwlDepthDeviceManager *manager, char *buffer, uint32_t *size);
/**
 * @brief 设置下一次 CxwlDepth_Connect 的目标。同步返回 OK 时消耗，同步失败时保留。显式 CxwlDepth_ConnectWithOptions 独立使用自身选项。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @param[in] target AUTO、SAVED 或 USB；仅 USB 可以搭配非零 check_id。
 * @param[in] check_id 当前有效的 USB 检查 ID；插拔、停止或登记库变化后重新检查。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。DEVICE_NOT_FOUND 登记目标不存在、未就绪或检查 ID 已失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SetConnectionTarget(
    CxwlDepthDeviceManager *manager, uint32_t target, uint64_t check_id);
/**
 * @brief 在采集句柄已释放后检查／修复或维护受管板端文件。
 * @param[in] manager 有效管理器句柄，必须在创建它的线程使用。
 * @param[in] device_id_utf8 枚举得到的设备 ID，不使用列表索引。
 * @param[in] operation BOARD_REPAIR 修复文件，BOARD_MAINTENANCE 另归档清理旧副本。
 * @return OK 已受理；INVALID_ARGUMENT 句柄或参数无效；BUSY 尚未空闲或仍在回调；WRONG_THREAD 非所有者线程；UNAVAILABLE 内部操作失败。DEVICE_NOT_FOUND 登记目标不存在、未就绪或检查 ID 已失效。
 * @see @ref sdk_lifecycle
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_MaintainBoardFiles(
    CxwlDepthDeviceManager *manager, const char *device_id_utf8, uint32_t operation);

/** @brief 从当前深度回调取得 RV1103 计算的同帧 XYZ 点云。
 * @param[in] depth 当前 Subscribe 回调的深度帧指针，或 SubscribeEx 回调中的 &frame_ex->frame。
 * @param[out] cloud 调用前设置 size。图像为有序的小端 float32 XYZ，缺测点为 (0,0,0)，单位为米。
 * @return OK 已取得点云，UNAVAILABLE 当前线程没有匹配的板端点云，INVALID_ARGUMENT 参数无效，BUFFER_TOO_SMALL 输出结构太小。
 * @note 与深度共用网格、序号、时间戳和会话代次。所有借用指针仅在所属回调内有效，跨线程或异步使用前须复制。
 * @see CxwlDepth_SavePointCloudPly
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetFramePointCloud(
    const CxwlDepthFrame *depth, CxwlDepthFrame *cloud);

/** @brief 将一帧板端 XYZ 点云保存为二进制 PLY。
 * @param[in] frame 实时深度回调帧、板端 XYZ 帧或调用方拥有的同帧 XYZ 副本。图像支持行填充。
 * @param[in] path_utf8 以 NUL 结尾的 UTF-8 文件路径。父目录须已存在。成功时原子替换同名文件。
 * @return OK 表示完整文件已提交，INVALID_ARGUMENT 表示帧或路径参数无效，UNAVAILABLE 表示实时帧缺少同帧点云，或文件写入、提交失败。
 * @note 同步、无需会话，可在工作线程使用拥有的帧副本。文件采用 PLY 1.0 binary_little_endian，
 * 依行列顺序写入有效点的 float32 XYZ（ToF 坐标系，米）。全缺测帧生成零顶点 PLY。
 * 注释保存宽、高、SDK 档位、会话代次、序号、纳秒时间戳和时钟域。
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_SavePointCloudPly(
    const CxwlDepthFrame *frame, const char *path_utf8);

/**
 * @brief 一次分配并返回会话设备列表的 UTF-8 JSON 快照。
 * @param[in] session 有效句柄；必须在创建句柄的所有者线程调用。
 * @param[out] json_utf8 SDK 分配的 NUL 结尾字符串；失败为 NULL。不得用 free/delete 释放。
 * @param[out] byte_count 字节数，包含 NUL；失败为 0。
 * @return OK 已取得快照；INVALID_ARGUMENT 句柄或输出指针无效；WRONG_THREAD 非所有者线程；UNAVAILABLE 查询或分配失败。公共日志重入返回 BUSY。
 * @note 该副本独立于句柄和后续查询，有效期至 CxwlDepth_FreeJson。调用方必须恰好释放一次，允许在关闭句柄后或其他线程释放；释放与读取不可并发。字段与 CxwlDepth_ListDevices 相同。
 * @see CxwlDepth_FreeJson
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListDevicesAlloc(
    CxwlDepthSession *session, char **json_utf8, uint32_t *byte_count);

/**
 * @brief 一次分配并返回会话诊断状态的 UTF-8 JSON 快照。
 * @param[in] session 有效句柄；必须在创建句柄的所有者线程调用。
 * @param[out] json_utf8 SDK 分配的 NUL 结尾字符串；失败为 NULL。不得用 free/delete 释放。
 * @param[out] byte_count 字节数，包含 NUL；失败为 0。
 * @return OK 已取得快照；INVALID_ARGUMENT 句柄或输出指针无效；WRONG_THREAD 非所有者线程；UNAVAILABLE 查询或分配失败。公共日志重入返回 BUSY。
 * @note 该副本独立于句柄和后续查询，有效期至 CxwlDepth_FreeJson。调用方必须恰好释放一次，允许在关闭句柄后或其他线程释放；释放与读取不可并发。字段与 CxwlDepth_GetState 相同。
 * @see CxwlDepth_FreeJson
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetStateAlloc(
    CxwlDepthSession *session, char **json_utf8, uint32_t *byte_count);

/**
 * @brief 一次分配并返回会话 USB 状态的 UTF-8 JSON 快照。
 * @param[in] session 有效句柄；必须在创建句柄的所有者线程调用。
 * @param[out] json_utf8 SDK 分配的 NUL 结尾字符串；失败为 NULL。不得用 free/delete 释放。
 * @param[out] byte_count 字节数，包含 NUL；失败为 0。
 * @return OK 已取得快照；INVALID_ARGUMENT 句柄或输出指针无效；WRONG_THREAD 非所有者线程；UNAVAILABLE 查询或分配失败。公共日志重入返回 BUSY。
 * @note 该副本独立于句柄和后续查询，有效期至 CxwlDepth_FreeJson。调用方必须恰好释放一次，允许在关闭句柄后或其他线程释放；释放与读取不可并发。字段与 CxwlDepth_GetUsbStatus 相同。
 * @see CxwlDepth_FreeJson
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetUsbStatusAlloc(
    CxwlDepthSession *session, char **json_utf8, uint32_t *byte_count);

/**
 * @brief 一次分配并返回已登记设备列表的 UTF-8 JSON 快照。
 * @param[in] manager 有效句柄；必须在创建句柄的所有者线程调用。
 * @param[out] json_utf8 SDK 分配的 NUL 结尾字符串；失败为 NULL。不得用 free/delete 释放。
 * @param[out] byte_count 字节数，包含 NUL；失败为 0。
 * @return OK 已取得快照；INVALID_ARGUMENT 句柄或输出指针无效；WRONG_THREAD 非所有者线程；UNAVAILABLE 查询或分配失败。公共日志重入返回 BUSY。
 * @note 该副本独立于句柄和后续查询，有效期至 CxwlDepth_FreeJson。调用方必须恰好释放一次，允许在关闭句柄后或其他线程释放；释放与读取不可并发。字段与 CxwlDepth_ListSavedDevices 相同。
 * @see CxwlDepth_FreeJson
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_ListSavedDevicesAlloc(
    CxwlDepthDeviceManager *manager, char **json_utf8, uint32_t *byte_count);

/**
 * @brief 一次分配并返回设备管理器诊断状态的 UTF-8 JSON 快照。
 * @param[in] manager 有效句柄；必须在创建句柄的所有者线程调用。
 * @param[out] json_utf8 SDK 分配的 NUL 结尾字符串；失败为 NULL。不得用 free/delete 释放。
 * @param[out] byte_count 字节数，包含 NUL；失败为 0。
 * @return OK 已取得快照；INVALID_ARGUMENT 句柄或输出指针无效；WRONG_THREAD 非所有者线程；UNAVAILABLE 查询或分配失败。公共日志重入返回 BUSY。
 * @note 该副本独立于句柄和后续查询，有效期至 CxwlDepth_FreeJson。调用方必须恰好释放一次，允许在关闭句柄后或其他线程释放；释放与读取不可并发。字段与 CxwlDepth_GetDeviceManagerState 相同。
 * @see CxwlDepth_FreeJson
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_GetDeviceManagerStateAlloc(
    CxwlDepthDeviceManager *manager, char **json_utf8, uint32_t *byte_count);

/**
 * @brief 释放任意分配型 JSON 接口返回的字符串。
 * @param[in] json_utf8 尚未释放的 SDK 分配地址，或 NULL。禁止传入回调内借用的元数据地址。
 * @return OK 已释放或参数为 NULL；INVALID_ARGUMENT 地址不属于尚未释放的 SDK JSON；BUSY 日志回调内重入；UNAVAILABLE 内部异常。
 * @note 可在任意线程、句柄关闭后调用；同一地址只能释放一次。不能与该地址的读取并发。需在卸载共享库前释放所有副本。
 * @see @ref sdk_json
 */
CXWL_DEPTH_API CxwlDepthResult CXWL_DEPTH_CALL CxwlDepth_FreeJson(char *json_utf8);

#pragma pack(pop)

#if defined(__cplusplus) && (defined(_MSC_VER) || __cplusplus >= 201103L)
#define CXWL_DEPTH_LAYOUT(name, condition) static_assert(condition, "ABI v3 layout: " #name)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define CXWL_DEPTH_LAYOUT(name, condition) _Static_assert(condition, "ABI v3 layout: " #name)
#else
#define CXWL_DEPTH_LAYOUT(name, condition) typedef char cxwl_depth_layout_##name[(condition) ? 1 : -1]
#endif
CXWL_DEPTH_LAYOUT(options, sizeof(CxwlDepthOptions) == 24 && offsetof(CxwlDepthOptions, device_store_utf8) == 16);
CXWL_DEPTH_LAYOUT(image, sizeof(CxwlDepthImage) == 32 && offsetof(CxwlDepthImage, row_stride_bytes) == 24);
CXWL_DEPTH_LAYOUT(point, sizeof(CxwlDepthPoint) == 12 && offsetof(CxwlDepthPoint, z_m) == 8);
CXWL_DEPTH_LAYOUT(frame, sizeof(CxwlDepthFrame) == 104 && offsetof(CxwlDepthFrame, image) == 64);
CXWL_DEPTH_LAYOUT(connect_options, sizeof(CxwlDepthConnectOptions) == 56 && offsetof(CxwlDepthConnectOptions, runtime_directory_utf8) == 32);
CXWL_DEPTH_LAYOUT(version, sizeof(CxwlDepthVersionInfo) == 116);
CXWL_DEPTH_LAYOUT(error, sizeof(CxwlDepthErrorInfo) == 616);
CXWL_DEPTH_LAYOUT(session_status, sizeof(CxwlDepthSessionStatus) == 320);
CXWL_DEPTH_LAYOUT(manager_status, sizeof(CxwlDepthManagerStatus) == 24);
CXWL_DEPTH_LAYOUT(device, sizeof(CxwlDepthDeviceInfo) == 616);
CXWL_DEPTH_LAYOUT(capabilities, sizeof(CxwlDepthCapabilities) == 72);
CXWL_DEPTH_LAYOUT(extended_frame, sizeof(CxwlDepthFrameEx) == 176 && offsetof(CxwlDepthFrameEx, valid_mask) == 144);
#undef CXWL_DEPTH_LAYOUT

#ifdef __cplusplus
}
#endif
#endif

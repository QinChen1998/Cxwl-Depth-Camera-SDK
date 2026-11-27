cmake_minimum_required(VERSION 3.22)
project(CxwlDepthSdk LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|aarch64|arm64)$")
    message(FATAL_ERROR "Use a Linux x86-64 or AArch64 toolchain. The Windows build uses the Visual Studio solution.")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk/product.json" product)
string(JSON sdk_version GET "${product}" sdk_version)
string(JSON sdk_abi GET "${product}" abi_version)

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui Network Concurrent)
find_package(Threads REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

add_library(CxwlDepthSdk SHARED
    sdk/DepthSdk.cpp
    sdk/DepthProjection.cpp
    RuntimeDiscovery.cpp
    sensors/RvDeviceStore.cpp
    sensors/RvDtofBoardStream.cpp
    sensors/RvDtofCandidates.cpp
    sensors/RvSessionController.cpp
    sensors/RvTunerController.cpp
    sensors/RvTunerProtocol.cpp
    sdk/windows/CameraTransport.cpp
    sdk/windows/DeviceService.cpp
    sdk/windows/UsbEnrollment.cpp
    sdk/linux/CameraCredentials.cpp
    sdk/linux/CameraInterface.cpp)
add_library(CXWL::DepthSdk ALIAS CxwlDepthSdk)
target_compile_features(CxwlDepthSdk PRIVATE cxx_std_17)
target_include_directories(CxwlDepthSdk PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>" "$<INSTALL_INTERFACE:include>"
    PRIVATE third_party/ffmpeg_7_1_3)
target_compile_definitions(CxwlDepthSdk PRIVATE CXWL_DEPTH_SDK_BUILD CXWL_INSTALLER_BUILD)
target_link_libraries(CxwlDepthSdk PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Threads::Threads OpenSSL::Crypto ${CMAKE_DL_LIBS})
set(exports_map "${CMAKE_CURRENT_SOURCE_DIR}/tools/linux/exports.map")
target_link_options(CxwlDepthSdk PRIVATE "LINKER:--version-script=${exports_map}" LINKER:-z,defs)
set_target_properties(CxwlDepthSdk PROPERTIES
    AUTOMOC ON VERSION "${sdk_version}" SOVERSION "${sdk_abi}" EXPORT_NAME DepthSdk
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON LINK_DEPENDS "${exports_map}"
    INSTALL_RPATH "$ORIGIN/cxwl")

include(CMakePackageConfigHelpers)
install(TARGETS CxwlDepthSdk EXPORT CxwlDepthSdkTargets LIBRARY DESTINATION lib)
install(EXPORT CxwlDepthSdkTargets NAMESPACE CXWL:: DESTINATION lib/cmake/CxwlDepthSdk)
install(FILES sdk/DepthSdk.h sdk/DepthSdk.hpp sdk/DepthSdkVersion.h DESTINATION include)
configure_package_config_file(tools/linux/CxwlDepthSdkConfig.cmake.in CxwlDepthSdkConfig.cmake
    INSTALL_DESTINATION lib/cmake/CxwlDepthSdk NO_CHECK_REQUIRED_COMPONENTS_MACRO)
write_basic_package_version_file(CxwlDepthSdkConfigVersion.cmake
    VERSION "${sdk_version}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CxwlDepthSdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CxwlDepthSdkConfigVersion.cmake" DESTINATION lib/cmake/CxwlDepthSdk)
configure_file(tools/linux/cxwl-depth-sdk.pc.in cxwl-depth-sdk.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cxwl-depth-sdk.pc" DESTINATION lib/pkgconfig)

include(CTest)
if(BUILD_TESTING)
    add_executable(cxwl_cpp_tests tests/test_sdk_cpp_helpers.cpp)
    target_compile_features(cxwl_cpp_tests PRIVATE cxx_std_17)
    target_compile_options(cxwl_cpp_tests PRIVATE -UNDEBUG)
    target_link_libraries(cxwl_cpp_tests PRIVATE CXWL::DepthSdk Threads::Threads)
    add_test(NAME cpp_helpers COMMAND cxwl_cpp_tests)

    add_executable(cxwl_linux_credentials_tests tests/LinuxServiceCredentialsTests.cpp
        sdk/linux/CameraCredentials.cpp RuntimeDiscovery.cpp)
    target_compile_features(cxwl_linux_credentials_tests PRIVATE cxx_std_17)
    target_compile_options(cxwl_linux_credentials_tests PRIVATE -UNDEBUG)
    target_link_libraries(cxwl_linux_credentials_tests PRIVATE Qt6::Core Qt6::Network OpenSSL::Crypto)
    add_test(NAME linux_service_credentials COMMAND cxwl_linux_credentials_tests)
    set_tests_properties(cpp_helpers linux_service_credentials PROPERTIES ENVIRONMENT "RV_TEST_OFFLINE_ONLY=1")
endif()

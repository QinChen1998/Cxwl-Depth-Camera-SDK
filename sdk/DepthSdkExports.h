#pragma once

// C++ symbols used by development diagnostics.
#if defined(_WIN32) && defined(CXWL_DEPTH_SDK_BUILD) && !defined(CXWL_INSTALLER_BUILD)
#define CXWL_DEPTH_INTERNAL __declspec(dllexport)
#elif defined(_WIN32) && defined(CXWL_DEPTH_SDK_LINK)
#define CXWL_DEPTH_INTERNAL __declspec(dllimport)
#else
#define CXWL_DEPTH_INTERNAL
#endif

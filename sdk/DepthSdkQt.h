#pragma once

#include "DepthSdkExports.h"

class RvSessionController;
struct CxwlDepthDeviceManager;
struct CxwlDepthSession;

namespace DepthSdkQt {
// Development diagnostics exercise the transport beneath the public API.
CXWL_DEPTH_INTERNAL RvSessionController *validationController(CxwlDepthDeviceManager *manager);
// Offline-only fixture; never exported by the customer build.
CXWL_DEPTH_INTERNAL CxwlDepthSession *validationSession(CxwlDepthDeviceManager *manager, bool managed);
}

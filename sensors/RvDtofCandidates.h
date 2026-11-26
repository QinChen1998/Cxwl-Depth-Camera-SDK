#pragma once
#include "../sdk/DepthSdkExports.h"
#include "RvTofFrame.h"
#include "../tools/rv_capture/Contract.h"

namespace RvDtofCandidates {
constexpr int Native48=RvCaptureContract::Native48;
inline bool supports(int mode){return RvCaptureContract::supportsProductMode(mode);}

CXWL_DEPTH_INTERNAL QString name(int mode);
CXWL_DEPTH_INTERNAL int modeForScheme(const QString &scheme);
}

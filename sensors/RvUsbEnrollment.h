#pragma once

#include "../sdk/DepthSdkExports.h"
#include <QtCore/QJsonObject>
#include <functional>

class RvSessionController;
class RvUsbEnrollment;

RvUsbEnrollment *createRvUsbEnrollment(RvSessionController *session, std::function<void()> changed);
bool connectRvDevice(RvUsbEnrollment *operation, bool allowSavedDevice, bool offerRgbLsc = false);
bool checkRvUsbDevice(RvUsbEnrollment *operation);
bool enrollCheckedRvUsbDevice(RvUsbEnrollment *operation, quint64 checkId, bool startAfter,
                              bool readOnlyIfReady = false, bool offerRgbLsc = false);
bool rvUsbOperationBusy(const RvUsbEnrollment *operation);
QJsonObject rvUsbOperationStatus(const RvUsbEnrollment *operation);
CXWL_DEPTH_INTERNAL QJsonObject checkRvUsbEnrollmentCore();

#pragma once

#include "../sensors/RvDeviceStore.h"

inline RvDeviceProfile offlineRvDevice()
{
    RvDeviceProfile fixture;
    fixture.id = fixture.serial = "1234567890abcdef";
    fixture.name = "Offline CXWL Depth Camera";
    fixture.host = "172.32.0.93";
    fixture.publicKey = "AAAAC3NzaC1lZDI1NTE5AAAAIAEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB";
    fixture.calibration = QLatin1String(RvCalibration::Id);
    return fixture;
}

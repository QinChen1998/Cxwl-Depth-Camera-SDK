#pragma once
#include "sdk/DepthSdkExports.h"
#include <QtCore/QString>

class QSettings;

// Sensor-only runtime discovery. All default paths belong to this application.
namespace RuntimeDiscovery {
CXWL_DEPTH_INTERNAL QString applicationRoot();
CXWL_DEPTH_INTERNAL QString runtimeDirectory();
CXWL_DEPTH_INTERNAL QString managedConfigurationDirectory();
void setSdkPaths(const QString &runtimeDirectory, const QString &settingsFile);
CXWL_DEPTH_INTERNAL QSettings depthSettings();
}

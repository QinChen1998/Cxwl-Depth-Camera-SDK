#include "RuntimeDiscovery.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

namespace RuntimeDiscovery {
namespace {
QString sdkRuntimeDirectory, sdkSettingsFile;

}
void setSdkPaths(const QString &directory, const QString &settingsFile) {
    sdkRuntimeDirectory=directory;
    sdkSettingsFile=settingsFile;
}
QSettings depthSettings() {
    if (sdkSettingsFile.isEmpty()) return QSettings();
    return QSettings(sdkSettingsFile,QSettings::IniFormat);
}
QString runtimeDirectory() {
    return sdkRuntimeDirectory.isEmpty()?QCoreApplication::applicationDirPath():sdkRuntimeDirectory;
}
QString managedConfigurationDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("DepthCameraAndTactileSensor/config");
}
QString applicationRoot() {
    const auto runtime = runtimeDirectory();
#ifndef CXWL_INSTALLER_BUILD
    QDir directory(runtime);
    for (int i = 0; i < 4; ++i) {
        if (directory.exists("Cxwl Depth Camera.vcxproj") && directory.exists("DCTS_APP.json"))
            return directory.absolutePath();
        if (!directory.cdUp()) break;
    }
#endif
    return runtime;
}
}

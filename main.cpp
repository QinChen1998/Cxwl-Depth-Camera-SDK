#include "SensorMainWindow.h"
#include "UiStyle.h"
#include "sdk/DepthSdkVersion.h"
#ifdef CXWL_DESKTOP_VALIDATION
#include "sensors/RvDeviceStore.h"
#endif
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#ifdef CXWL_DESKTOP_VALIDATION
#include <QTemporaryDir>
#include <memory>
#endif
#include <QStandardPaths>

#ifdef CXWL_DESKTOP_VALIDATION
int runPortSmokeTests(SensorMainWindow &,const QString &);
int runRvTunerTests(const QString &);
int runLiveValidation(SensorMainWindow &,const QString &);
int runImagingSwitchValidation(SensorMainWindow&,const QString&);
int runUsbEnrollmentLiveValidation(SensorMainWindow &,const QString &);
QJsonObject checkRvUsbEnrollment();
#endif

namespace {
#ifndef CXWL_INSTALLER_BUILD
QString applicationRoot()
{
    QDir directory(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4; ++i) {
        if (directory.exists("DCTS_APP.json")) return directory.absolutePath();
        if (!directory.cdUp()) break;
    }
    return QCoreApplication::applicationDirPath();
}
#endif

QString configurationDirectory()
{
#ifdef CXWL_INSTALLER_BUILD
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("DepthCameraAndTactileSensor/config");
#else
    return applicationRoot() + "/config";
#endif
}

void configureSettings(const QString &directory,bool seedDefaults)
{
    QDir().mkpath(directory);
    QCoreApplication::setOrganizationName("DepthCameraAndTactileSensor");
    QCoreApplication::setApplicationName("DepthCameraAndTactileSensor");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory+"/preferences");
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,directory+"/preferences");
    QSettings current;
    if(!current.value("application/imported",false).toBool()){
        if(seedDefaults){
            const QSettings defaults(":/config/first-run-settings.ini",QSettings::IniFormat);
            for(const auto &key:defaults.allKeys())
                if(!current.contains(key))current.setValue(key,defaults.value(key));
        }
        current.setValue("application/imported",true);current.sync();
    }
}
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);UiStyle::install(app);
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    QTranslator qtChinese;
    if (qtChinese.load(QStringLiteral("qtbase_zh_CN"), app.applicationDirPath() + "/translations")
        || qtChinese.load(QStringLiteral("qtbase_zh_CN"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtChinese);
    QTranslator chinese;
    if (!chinese.load(QStringLiteral(":/i18n/hand_zh_CN.qm"))) return 3;
    app.installTranslator(&chinese);
    app.setApplicationDisplayName(QStringLiteral("Cxwl Depth Camera"));
    app.setApplicationVersion(QString::fromLatin1(CXWL_DEPTH_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/cx.ico")));
#ifdef CXWL_DESKTOP_VALIDATION
    const auto arguments=app.arguments();const QString flag=arguments.value(1),report=arguments.value(2);
    const bool usbEnrollmentLive=flag=="--usb-enrollment-live-validation";
    const bool firstUsbEnrollment=usbEnrollmentLive&&qEnvironmentVariable("DCTS_USB_TEST_NEW")=="1";
    const bool imagingSwitchLive=flag=="--imaging-switch-validation";
    const bool live=usbEnrollmentLive||imagingSwitchLive||flag=="--live-validation";
    if(live&&(qEnvironmentVariable("DCTS_LIVE_ENABLE")!="1"||qEnvironmentVariable("DCTS_LIVE_SERIAL").isEmpty()
        ||(!usbEnrollmentLive&&qEnvironmentVariable("DCTS_ALLOW_SAVED_TOF_SWITCH")!="1")||qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")=="1"))return 3;
    const bool testing=!flag.isEmpty();std::unique_ptr<QTemporaryDir> temporary;
    if(testing){
        const auto parent=applicationRoot()+"/x64/validation";
        QDir().mkpath(parent);
        temporary=std::make_unique<QTemporaryDir>(parent+"/settings-XXXXXX");
        if(!temporary->isValid())return 3;
    }
    if(live&&!firstUsbEnrollment){
        // Explicitly gated live tests may read an existing development profile
        // while keeping installed user settings and credentials untouched.
        const auto source=qEnvironmentVariable("DCTS_LIVE_CONFIG_SOURCE",configurationDirectory());
        const auto preferences="/preferences/DepthCameraAndTactileSensor/DepthCameraAndTactileSensor.ini";
        QDir().mkpath(temporary->path()+"/preferences/DepthCameraAndTactileSensor");
        if(!QFile::copy(source+"/devices.json",temporary->path()+"/devices.json")
            ||!QFile::copy(source+preferences,temporary->path()+preferences))return 3;
        if(!usbEnrollmentLive){
            RvDeviceStore store(temporary->path()+"/devices.json");QString selected;
            for(const auto &device:store.profiles())if(device.serial==qEnvironmentVariable("DCTS_LIVE_SERIAL"))selected=device.id;
            if(selected.isEmpty())return 3;store.save(store.profiles(),selected);
        }
    }
    const auto settingsDirectory = testing ? temporary->path() : configurationDirectory();
    configureSettings(settingsDirectory, !testing || firstUsbEnrollment || flag == "--port-ui-smoke-test");
    if(testing) qputenv("DCTS_DEVICE_STORE_PATH", QFile::encodeName(settingsDirectory + "/devices.json"));
    if(usbEnrollmentLive&&!firstUsbEnrollment)QSettings().setValue("depth/imagingScheme","board_native_tof48");
    if (flag == "--port-ui-smoke-test") QSettings().setValue("ui/language", "en");
    if(flag=="--rv-tuner-test")return runRvTunerTests(report);
    if(flag=="--rv-usb-enrollment-test"){
        const auto checks=checkRvUsbEnrollment();
        QFile output(report);
        if(!output.open(QIODevice::WriteOnly|QIODevice::Text))return 3;
        bool passed=true;
        for(auto it=checks.constBegin();it!=checks.constEnd();++it){
            const bool ok=it.value().toBool();
            output.write(QByteArray(ok?"PASS ":"FAIL ")+it.key().toUtf8()+"\n");
            passed &= ok;
        }
        return passed?0:1;
    }
    if(testing&&flag!="--port-ui-smoke-test"&&!live)return 3;
    SensorMainWindow window(settingsDirectory + "/devices.json");
    window.show();
    if(usbEnrollmentLive)return runUsbEnrollmentLiveValidation(window,report);
    if(imagingSwitchLive)return runImagingSwitchValidation(window,report);
    if(live)return runLiveValidation(window,report);
    if(flag=="--port-ui-smoke-test"){
        const int result=runPortSmokeTests(window,report);
        QSettings user;
        user.setValue("depth/nativeTofRotationDegrees", 37);
        user.setValue("depth/colorMode", "fixed");
        user.setValue("pointCloud/colorMode", "fixed");
        user.setValue("imageDisplay/rotationDegrees",90);
        user.sync();
        configureSettings(temporary->path(),true);
        QSettings reopened;
        const bool preserved=reopened.value("depth/nativeTofRotationDegrees").toInt() == 37
            &&reopened.value("imageDisplay/rotationDegrees").toInt()==90
            &&reopened.value("depth/colorMode").toString()=="fixed"
            &&reopened.value("pointCloud/colorMode").toString()=="fixed";
        QFile output(report);
        if(output.open(QIODevice::Append|QIODevice::Text))
            output.write(preserved?"FIRST_RUN_DEFAULTS_DO_NOT_OVERWRITE_USER PASS\n":"FIRST_RUN_DEFAULTS_DO_NOT_OVERWRITE_USER FAIL\n");
        return result?result:(preserved?0:1);
    }
#else
    configureSettings(configurationDirectory(), true);
#ifdef CXWL_INSTALLER_BUILD
    SensorMainWindow window;
#else
    SensorMainWindow window(configurationDirectory() + "/devices.json");
#endif
    window.show();
#endif
    app.setQuitOnLastWindowClosed(true);return app.exec();
}

// MIT. Standalone viewer: public SDK + Qt Widgets, with no internal test entry points.
#include "SensorMainWindow.h"
#include "UiStyle.h"
#include "sdk/DepthSdkVersion.h"
#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("CXWL"));
    app.setApplicationName(QStringLiteral("DepthSdkViewer"));
    app.setApplicationDisplayName(QStringLiteral("CXWL 深度相机 SDK Viewer"));
    app.setApplicationVersion(QString::fromLatin1(CXWL_DEPTH_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/cx.ico")));
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    UiStyle::install(app);
    QTranslator qtChinese, chinese;
    if (qtChinese.load(QStringLiteral("qtbase_zh_CN"), app.applicationDirPath() + "/translations")
        || qtChinese.load(QStringLiteral("qtbase_zh_CN"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtChinese);
    if (!chinese.load(QStringLiteral(":/i18n/hand_zh_CN.qm"))) return 3;
    app.installTranslator(&chinese);
    // UI preferences are private to this example. The SDK owns device registration.
    const auto settings = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(settings);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings);
    QSettings current;
    if (!current.value("application/imported", false).toBool()) {
        const QSettings defaults(":/config/first-run-settings.ini", QSettings::IniFormat);
        for (const auto &key : defaults.allKeys())
            if (!current.contains(key)) current.setValue(key, defaults.value(key));
        current.setValue("application/imported", true);
        current.sync();
    }
    SensorMainWindow window;
    window.setWindowTitle(app.applicationDisplayName() + " " + app.applicationVersion());
    window.show();
    return app.exec();
}

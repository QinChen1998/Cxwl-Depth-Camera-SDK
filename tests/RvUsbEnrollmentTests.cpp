#include "../sensors/RvUsbEnrollment.h"
#include "../sensors/RvImportDiagnostics.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

QJsonObject checkRvUsbEnrollment()
{
    auto checks = checkRvUsbEnrollmentCore();
    const QString serial = "1234567890abcdef";
    QTemporaryDir temporary;
    const auto report = RvImportDiagnostics::writeReport(temporary.filePath("diagnostics"), "test_usb_setup", "test failure",
        {{"phase", "adb_identity_and_delivery_probe"}, {"serial", serial}});
    QFile reportFile(report);
    const auto diagnostic = reportFile.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(reportFile.readAll()).object() : QJsonObject();
    checks["USB_IMPORT_DIAGNOSTIC_REPORT_WRITTEN"] = temporary.isValid() && diagnostic["operation"] == "test_usb_setup"
        && diagnostic["reason"] == "test failure" && diagnostic["details"].toObject()["serial"] == serial
        && diagnostic["details"].toObject()["phase"] == "adb_identity_and_delivery_probe";
    checks["USB_IMPORT_DIAGNOSTIC_REPORT_USES_PRODUCT_BRAND"] = QFileInfo(report).fileName().startsWith("cxwl-depth-camera-import-")
        && diagnostic["schema"] == "cxwl-depth-camera-import-failure-v1";
    checks["USB_IMPORT_DIAGNOSTIC_WRITE_FAILURE_HANDLED"] = temporary.isValid()
        && RvImportDiagnostics::writeReport(report, "test_usb_setup", "test failure", {}).isEmpty();
    return checks;
}

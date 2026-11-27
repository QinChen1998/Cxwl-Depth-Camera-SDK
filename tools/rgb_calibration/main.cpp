#include "Calibration.h"
#include "../../RuntimeDiscovery.h"
#include "../../sdk/windows/DeviceService.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFutureWatcher>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <cstdio>
#include <stdexcept>

namespace {
QJsonObject run(const QString &path, const QString &serial, bool write) {
    try {
        const auto profiles = RvDeviceStore(path).profiles();
        for (const auto &profile : profiles) {
            if (profile.serial != serial || profile.serviceInstance.isEmpty()) continue;
            auto result = rgbCalibration(profile, write).value("rgb_lsc").toObject();
            if (result["state"].toString().isEmpty()) throw std::runtime_error("设备校正状态回执缺失");
            return result;
        }
        throw std::runtime_error("请先在相机设备管理中登记所选相机，或选择登记时使用的设备档案。");
    } catch (const std::exception &error) {
        return {{"error", QString::fromUtf8(error.what())}};
    }
}

class Window final : public QWidget {
    QString store;
    QComboBox *cameras = new QComboBox(this);
    QPushButton *refresh = new QPushButton("刷新相机", this);
    QPushButton *chooseStore = new QPushButton("选择设备档案…", this);
    QPushButton *check = new QPushButton("查看校正状态", this);
    QPushButton *apply = new QPushButton("采集并写入校正", this);
    QLabel *status = new QLabel(this);
    QFutureWatcher<QJsonObject> operation;
    void controls(bool busy) {
        cameras->setEnabled(!busy);
        refresh->setEnabled(!busy);
        chooseStore->setEnabled(!busy);
        check->setEnabled(!busy && cameras->count());
        apply->setEnabled(check->isEnabled());
    }
    void scan() {
        const auto previous = cameras->currentText();
        cameras->clear();
        try {
            for (const auto &ep : DeviceService::attached()) cameras->addItem(ep.serial);
            const int index = cameras->findText(previous);
            if (index >= 0) cameras->setCurrentIndex(index);
            status->setText(cameras->count() ? "请选择相机并查看校正状态。" : "尚未检测到相机 USB 网卡。");
        } catch (const std::exception &error) { status->setText(QString::fromUtf8(error.what())); }
        controls(false);
    }
    void start(bool write) {
        const auto serial = cameras->currentText();
        if (serial.isEmpty() || operation.isRunning()) return;
        if (write && QMessageBox::question(this, "确认写入 RGB 校正",
                QString("请停止相机 %1 的采集，将均匀照明的平整白板充满画面，并在标定期间保持连接。现在采集并写入该相机？").arg(serial),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        controls(true);
        status->setText(write ? "正在采集标定帧并核验写入结果…" : "正在读取板端状态…");
        operation.setFuture(QtConcurrent::run(run, store, serial, write));
    }
    void closeEvent(QCloseEvent *event) override {
        if (operation.isRunning()) event->ignore();
        else QWidget::closeEvent(event);
    }
public:
    explicit Window(QString path) : store(std::move(path)) {
        setWindowTitle("Cxwl RGB 校正");
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("相机 USB 序列号", this));
        for (QWidget *widget : QList<QWidget *>{cameras, refresh, chooseStore, check, apply, status}) layout->addWidget(widget);
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        connect(refresh, &QPushButton::clicked, this, [this]{ scan(); });
        connect(chooseStore, &QPushButton::clicked, this, [this]{
            const auto path = QFileDialog::getOpenFileName(this, "打开相机登记档案", store, "JSON (*.json)");
            if (!path.isEmpty()) { store = path; scan(); }
        });
        connect(check, &QPushButton::clicked, this, [this]{ start(false); });
        connect(apply, &QPushButton::clicked, this, [this]{ start(true); });
        connect(cameras, &QComboBox::currentTextChanged, status, [this]{ status->clear(); });
        connect(&operation, &QFutureWatcher<QJsonObject>::finished, this, [this]{
            const auto r = operation.result();
            const auto result = r.value("result").toString(r.value("state").toString());
            static const QMap<QString,QString> labels{{"installed","新校正已写入并通过读回核验。"},
                {"already_deployed","现有校正已通过核验。"},{"deployed","已安装校正网格。"},
                {"active","已部署的校正网格正在使用。"},{"missing","尚未安装校正网格。"},{"unsupported","该相机的 ISP 配置不支持此校正。"},
                {"unverified","现有校正记录无法核验。"},{"insufficient_raw_samples","有效样本不足，请检查白板和照明后重试。"}};
            status->setText(r.contains("error") ? r["error"].toString() : labels.value(result, result));
            controls(false);
        });
        resize(490, 220);
        scan();
    }
};
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"device-store", "使用指定的已登记设备档案。", "path"});
    parser.addOption({"check", "只读查询指定相机的 RGB 校正状态并输出 JSON。"});
    parser.addOption({"serial", "USB 相机序列号。", "serial"});
    parser.process(app);
    auto store = parser.value("device-store");
    if (store.isEmpty()) {
        store = QDir(RuntimeDiscovery::managedConfigurationDirectory()).filePath("devices.json");
        const auto development = QDir(RuntimeDiscovery::applicationRoot()).filePath("config/devices.json");
        if (!QFileInfo::exists(store) && QFileInfo(development).isFile()) store = development;
    }
    if (parser.isSet("check")) {
        if (parser.value("serial").isEmpty()) parser.showHelp(2);
        const auto result = run(store, parser.value("serial"), false);
        std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        return result.contains("error") ? 1 : 0;
    }
    if (parser.isSet("serial")) parser.showHelp(2);
    Window window(store);
    window.show();
    return app.exec();
}

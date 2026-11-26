#include "SensorMainWindow.h"
#include "demo/DepthSdkClient.h"
#include "sensors/DepthMapWidget.h"
#include "sensors/RtspCameraPage.h"
#include "sensors/RvDeviceManagerDialog.h"
#include "sensors/RvPointCloudWidget.h"
#include "sensors/RvImportDiagnostics.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDockWidget>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
SensorMainWindow::SensorMainWindow(const QString &deviceStore, QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName("sensorMainWindow");
    setWindowTitle(QApplication::applicationDisplayName() + QStringLiteral("（内测版）"));
    setDockNestingEnabled(true);
    setDockOptions(AllowNestedDocks | AllowTabbedDocks | AnimatedDocks);
    auto *center=new QWidget(this);center->setMaximumSize(0,0);setCentralWidget(center);
    m_session = new DepthSdkClient(deviceStore, this);
    connect(m_session, &DepthSdkClient::boardFilesRequired, this, [this](const QString &id) {
        const auto revision = m_session->stopRevision();
        QTimer::singleShot(0, this, [this, id, revision] { offerBoardImport(id, revision); });
    });
    connect(m_session, &DepthSdkClient::usbDiscovered, this, [this](const QJsonObject &status) {
        const auto revision = m_session->stopRevision();
        QTimer::singleShot(0, this, [this, status, revision] {
            if (m_session->stopRevision() == revision) offerUsbSetup(status);
        });
    });
    connect(m_session, &DepthSdkClient::captureError, this, &SensorMainWindow::showCaptureError, Qt::QueuedConnection);
    connect(m_session, &DepthSdkClient::closeReady, this, [this] { if (m_closing) close(); });
    auto *usbTimer = new QTimer(this);
    usbTimer->setInterval(2500);
    connect(usbTimer, &QTimer::timeout, this, [this] {
        if (isVisible() && !m_closing && !QApplication::activeModalWidget() && !(m_devices && m_devices->isVisible()))
            m_session->scanUsb();
    });
    usbTimer->start();
    m_depthDock=new QDockWidget(tr("Depth Map"), this);m_depthDock->setObjectName("depthMapDock");
    m_depthDock->setMinimumSize(380,220);
    m_depth=new DepthMapWidget(m_depthDock);m_depth->setCaptureClient(m_session);
    auto *scroll=new QScrollArea(m_depthDock);scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(m_depth);m_depthDock->setWidget(scroll);
    m_cameraDock=new QDockWidget(tr("Camera raw video"), this);m_cameraDock->setObjectName("rtspCameraDock");
    m_camera=new RtspCameraPage(m_cameraDock);m_camera->setCaptureClient(m_session);
    m_cameraDock->setWidget(m_camera);
    m_pointCloudDock = new QDockWidget(tr("Point Cloud"), this);
    m_pointCloudDock->setObjectName("pointCloudDock");
    auto *pointCloud = new RvPointCloudWidget(m_session, m_pointCloudDock);
    m_pointCloudDock->setWidget(pointCloud);
    connect(m_pointCloudDock, &QDockWidget::visibilityChanged, pointCloud, &RvPointCloudWidget::setRenderingActive);
    m_docks={m_depthDock,m_pointCloudDock,m_cameraDock};
    for(auto *dock:m_docks)dock->setAllowedAreas(Qt::AllDockWidgetAreas);

    auto *fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->setObjectName("fileMenu");
    fileMenu->addAction(tr("Exit"), this, &QWidget::close);
    auto *viewMenu = menuBar()->addMenu(tr("View"));
    viewMenu->setObjectName("viewMenu");
    for (auto *dock : m_docks) viewMenu->addAction(dock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Reset layout"), this, &SensorMainWindow::resetLayout)->setObjectName("resetLayoutAction");
    m_presets = viewMenu->addMenu(tr("Layout Presets"));
    m_presets->setObjectName("layoutPresetMenu");
    connect(m_presets, &QMenu::aboutToShow, this, &SensorMainWindow::refreshPresets);
    viewMenu->addAction(tr("Save layout..."), this, &SensorMainWindow::savePreset);
    m_deletePreset = viewMenu->addAction(tr("Delete layout..."));
    connect(m_deletePreset,&QAction::triggered,this,[this]{
        QSettings settings;settings.beginGroup("workspace/presets");const auto names=settings.childGroups();
        bool ok=false;const auto name=QInputDialog::getItem(this,tr("Delete layout"),tr("Layout name:"),names,0,false,&ok);
        if(ok&&!name.isEmpty())settings.remove(name);
    });
    auto *settingsMenu = menuBar()->addMenu(tr("Settings"));
    settingsMenu->setObjectName("settingsMenu");
    settingsMenu->addAction(tr("Device Manager..."), this, [this] { showDeviceManager(); })
        ->setObjectName("rvDeviceManagerAction");
    menuBar()->addAction(tr("About"), this, &SensorMainWindow::showAbout)->setObjectName("aboutAction");
    auto *diagnostics = new QPushButton(tr("Copy diagnostics"), this);
    diagnostics->setObjectName("copyDiagnosticsButton");
    statusBar()->addPermanentWidget(diagnostics);
    connect(diagnostics, &QPushButton::clicked, this, [this] {
        const auto json = QJsonDocument(m_session->captureDiagnostics()).toJson();
        QApplication::clipboard()->setText(QString::fromUtf8(json));
        statusBar()->showMessage(tr("Diagnostics copied to the clipboard."), 4000);
    });
    resize(1680,1040);resetLayout();
    QSettings settings;
    if(settings.contains("workspace/geometry"))restoreGeometry(settings.value("workspace/geometry").toByteArray());
    if(settings.contains("workspace/state"))restoreState(settings.value("workspace/state").toByteArray(),1);
}

void SensorMainWindow::showAbout()
{
    auto *dialog = new QDialog(this, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("About"));

    auto *logo = new QLabel(dialog);
    logo->setPixmap(windowIcon().pixmap(QSize(112, 112), dialog->devicePixelRatioF()));
    auto *name = new QLabel(QApplication::applicationDisplayName(), dialog);
    auto *version = new QLabel(QStringLiteral("v%1 · 内测版").arg(QApplication::applicationVersion()), dialog);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->addWidget(logo, 0, Qt::AlignHCenter);
    layout->addWidget(name, 0, Qt::AlignHCenter);
    layout->addWidget(version, 0, Qt::AlignHCenter);
    dialog->open();
}

RvDeviceManagerDialog *SensorMainWindow::showDeviceManager(bool modal)
{
    if (!m_devices) {
        m_devices = new RvDeviceManagerDialog(m_session, this);
        m_devices->setAttribute(Qt::WA_DeleteOnClose);
    }
    if (modal && !m_devices->isModal()) {
        m_devices->hide();
        m_devices->setWindowModality(Qt::WindowModal);
    }
    m_devices->show();
    m_devices->raise();
    m_devices->activateWindow();
    return m_devices;
}

void SensorMainWindow::offerBoardImport(const QString &id, quint64 stopRevision)
{
    const auto stillSelected = [this, &id, stopRevision] {
        return !m_closing && m_session->userStopped() && m_session->canManageDevices()
            && m_session->stopRevision() == stopRevision && m_session->activeDeviceId() == id;
    };
    if (!stillSelected()) return;
    if (QMessageBox::question(this, tr("Install required board files"),
            tr("The device needs board files before capture can start. Check and repair them now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes
        || !stillSelected()) return;
    showDeviceManager(true)->importBoardFilesForStart(id);
}

void SensorMainWindow::offerUsbSetup(const QJsonObject &status)
{
    if (m_closing || !m_session->userStopped() || QApplication::activeModalWidget()) return;
    const auto state = status["state"];
    const auto checkId = quint64(status["check_id"].toInteger());
    if (state == "ready") {
        m_session->connectViaUsb(checkId);
        return;
    }
    const auto text = state == "registration_required"
        ? tr("A new CXWL Depth Camera was detected. Go to Settings to add it automatically?")
        : state == "network_required" ? tr("The CXWL Depth Camera connection needs setup. Go to Settings to complete it automatically?")
                                       : tr("CXWL Depth Camera board files need updating. Go to Settings to update them automatically?");
    const auto revision = m_session->stopRevision();
    QMessageBox prompt(QMessageBox::Question, tr("CXWL Depth Camera connection"), text, QMessageBox::Yes | QMessageBox::No, this);
    prompt.setObjectName("rvUsbSetupPrompt");
    prompt.setDefaultButton(QMessageBox::No);
    if (prompt.exec() == QMessageBox::Yes && !m_closing && m_session->userStopped()
        && m_session->stopRevision() == revision) showDeviceManager()->connectViaUsb(checkId);
}

void SensorMainWindow::showCaptureError(const QString &reason, const QJsonObject &usb)
{
    if (m_closing) return;
    if (reason == "usb_setup_failed" && m_devices && m_devices->isVisible()) {
        RvImportDiagnostics::showFailure(m_devices, "usb_enrollment",
            usb["error"].toString(tr("USB connection setup ended.")), usb);
    } else if (reason == "unavailable") {
        statusBar()->showMessage(tr("Capture is unavailable."), 10000);
    } else {
        statusBar()->showMessage(tr("Could not start capture. Check the device connection and try again."), 10000);
    }
}

SensorMainWindow::~SensorMainWindow()
{
    m_closing=true;
    m_session->stop();
    // Destroy subscribers while the session is still alive.
    for(auto *dock:m_docks)delete dock;
    if(m_devices)delete m_devices;
    delete m_session;
}

void SensorMainWindow::resetLayout()
{
    for (auto *dock : m_docks) {
        removeDockWidget(dock);
        dock->setFloating(false);
    }
    addDockWidget(Qt::LeftDockWidgetArea, m_depthDock);
    splitDockWidget(m_depthDock, m_cameraDock, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, m_pointCloudDock);
    for (auto *dock : m_docks) dock->show();
    resizeDocks({m_depthDock, m_pointCloudDock}, {720, 960}, Qt::Horizontal);
    resizeDocks({m_depthDock, m_cameraDock}, {475, 530}, Qt::Vertical);
}

void SensorMainWindow::saveLayout()
{
    QSettings settings;settings.setValue("workspace/geometry",saveGeometry());settings.setValue("workspace/state",saveState(1));settings.sync();
}

void SensorMainWindow::refreshPresets()
{
    m_presets->clear();QSettings settings;settings.beginGroup("workspace/presets");
    const auto names=settings.childGroups();m_deletePreset->setEnabled(!names.isEmpty());
    for(const auto &name:names){
        const auto state=settings.value(name+"/state").toByteArray();
        auto *action=m_presets->addAction(name);connect(action,&QAction::triggered,this,[this,state]{restoreState(state,1);});
    }
    if(names.isEmpty())m_presets->addAction(tr("No saved layouts"))->setEnabled(false);
}

void SensorMainWindow::savePreset()
{
    bool ok=false;const auto name=QInputDialog::getText(this,tr("Save layout"),tr("Layout name:"),QLineEdit::Normal,{},&ok).trimmed();
    if(ok&&!name.isEmpty()&&!name.contains('/')&&!name.contains('\\')){
        QSettings settings;settings.setValue("workspace/presets/"+name+"/state",saveState(1));
    }
}

void SensorMainWindow::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    if (!m_session->shutdown()) {
        event->ignore();
        return;
    }
    saveLayout();
    event->accept();
}

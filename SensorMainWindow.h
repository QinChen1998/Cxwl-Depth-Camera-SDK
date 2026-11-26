#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QVector>
#include <QJsonObject>

class DepthMapWidget;
class RtspCameraPage;
class DepthSdkClient;
class RvDeviceManagerDialog;
class QMenu;
class QAction;

class SensorMainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit SensorMainWindow(const QString &deviceStore = {}, QWidget *parent = nullptr);
    ~SensorMainWindow() override;
    DepthSdkClient *session() const { return m_session; }
    DepthMapWidget *depth() const { return m_depth; }
    void resetLayout();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    friend int runPortSmokeTests(SensorMainWindow &, const QString &);
    void showAbout();
    void saveLayout();
    void refreshPresets();
    void savePreset();
    RvDeviceManagerDialog *showDeviceManager(bool modal = false);
    void offerBoardImport(const QString &id, quint64 stopRevision);
    void offerUsbSetup(const QJsonObject &status);
    void showCaptureError(const QString &reason, const QJsonObject &usb);
    DepthSdkClient *m_session;
    DepthMapWidget *m_depth;
    RtspCameraPage *m_camera;
    QDockWidget *m_depthDock, *m_cameraDock, *m_pointCloudDock;
    QPointer<RvDeviceManagerDialog> m_devices;
    QVector<QDockWidget *> m_docks;
    QMenu *m_presets;
    QAction *m_deletePreset;
    bool m_closing = false;
};

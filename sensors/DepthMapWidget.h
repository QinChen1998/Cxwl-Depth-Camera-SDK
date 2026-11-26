#pragma once

#include "DepthSnapshot.h"
#include "ImageDisplaySettings.h"
#include "../demo/DepthSdkClient.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QSharedPointer>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

class QPushButton;
class QCheckBox;
class RvDepthView;
class QJsonObject;
class QComboBox;
class QLabel;
class QTimer;
class QProgressBar;

class DepthMapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DepthMapWidget(QWidget *parent = nullptr);
    ~DepthMapWidget() override;
    void setCaptureClient(DepthSdkClient *controller);
    DepthSdkClient *captureClient() const;
    void acceptRvDepth(const QByteArray &depth,const QJsonObject &metadata);
    void clearRvDepth(const QString &reason);
    DepthSnapshot snapshot() const;
    int selectedWidth() const;
    int selectedHeight() const;
    int displayRotationDegrees() const;
    void setDisplayRotationDegrees(int degrees);
    int nativeTofRotationDegrees() const { return m_nativeTofRotation; }

protected:

private:
    QSize selectedGrid() const;
    void beginImagingSwitchUi(const QString &scheme,int estimatedMs);
    void finishImagingSwitchUi();
    void cancelImagingSwitchUi();
    void advanceImagingSwitchUi();
    void syncCaptureProgressUi();
    bool eventFilter(QObject *watched,QEvent *event) override;
    void saveCurrentFrame();
    void editNativeTofRotation();
    void syncNativeTofRotation();
    QPushButton *m_nativeTofAngle = nullptr;
    int m_nativeTofRotation = 90;
    QPushButton *m_saveButton = nullptr;
    QComboBox *m_colorMode = nullptr;
    QCheckBox *m_blackOutsideRange = nullptr;
    QLabel *m_colorMinimumLabel = nullptr, *m_colorMaximumLabel = nullptr;
    QLabel *m_fpsLabel = nullptr;
    QPushButton *m_applyColorRange = nullptr;
    DepthSnapshot m_snapshot;
    void initializeLabels();
    void syncSessionControls();
    void syncDisplayColors();
    void updateRvPresentation();

    QSharedPointer<ImageDisplaySettings> m_imageSettings;
    RvDepthView *m_rvCanvas = nullptr;
    QWidget *m_viewport = nullptr;
    QWidget *m_progressOverlay=nullptr;
    QLabel *m_progressTitle=nullptr,*m_progressDetail=nullptr;
    QProgressBar *m_progressBar=nullptr;
    QTimer *m_progressUiTimer=nullptr;
    QElapsedTimer m_switchUiClock,m_switchFinishClock;
    int m_switchEstimatedMs=10000,m_switchFinishFrom=0;
    bool m_switchUiActive=false,m_switchUiFinishing=false;
    QString m_switchUiScheme;
    QPointer<DepthSdkClient> m_session;
    QWidget *m_rvControls = nullptr;
    QLabel *m_rvModelCaption = nullptr;
    QComboBox *m_rvModel = nullptr;
    QPushButton *m_rvStart = nullptr, *m_rvStop = nullptr, *m_rvReconnect = nullptr;


};

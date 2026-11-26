#pragma once
#include "ImageDisplaySettings.h"
#include "../demo/DepthSdkClient.h"

#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

class QLabel;
class QComboBox;
class QPushButton;
class QProgressBar;
class CameraImageCanvas;
class DepthSdkClient;

// Camera presentation and camera requests. The shared session owns all I/O.
class RtspCameraPage final : public QWidget
{
    Q_OBJECT
public:
    explicit RtspCameraPage(QWidget *parent = nullptr);
    ~RtspCameraPage() override;
    void setCaptureClient(DepthSdkClient *session);
    DepthSdkClient *captureClient() const { return m_session.data(); }
    void stop();
    QImage currentFrameImage() const;
    int displayRotationDegrees() const;
    void setDisplayRotationDegrees(int degrees);

protected:

private:
    void updateSessionUi();
    void initializeLabels();

    QPointer<DepthSdkClient> m_session;
    QSharedPointer<ImageDisplaySettings> m_imageSettings;
    CameraImageCanvas *m_video = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_reconnectButton = nullptr;
    QLabel *m_videoInfo = nullptr;
    QLabel *m_rotationLabel = nullptr;
    QWidget *m_captureOverlay = nullptr;
    QLabel *m_captureMessage = nullptr;
    QProgressBar *m_captureProgress = nullptr;
    QComboBox *m_rotation = nullptr;
};

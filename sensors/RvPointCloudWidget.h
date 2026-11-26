#pragma once

#include <DepthSdk.h>
#include "PointCloudColorScale.h"
#include "RvPointCloudCanvas.h"
#include <QWidget>
#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QJsonObject;
class QLabel;
class QPushButton;
class QVBoxLayout;
class DepthSdkClient;

class RvPointCloudWidget final : public QWidget {
    Q_OBJECT
public:
    explicit RvPointCloudWidget(DepthSdkClient *session, QWidget *parent = nullptr);
    ~RvPointCloudWidget() override;
    void setRenderingActive(bool active);

private:
    using Point = RvPointCloudCanvas::Point;
    struct Frame {
        QVector<Point> points;
        QVector<quint32> indices;
    };

    void acceptPoints(const QByteArray &xyz, const QJsonObject &metadata);
    static Frame makeFrame(const CxwlDepthPoint *tofPoints, const QSize &size, int angleDegrees);
    void rebuildFrame();
    void colorFrame(Frame &frame) const;
    void clearFrame();
    void present();
    void setupColorControls(QVBoxLayout &layout);
    void editInputAngle();
    void syncInputAngle();
    void applyColorRange();
    void refreshColors();
    void updateLegend();
    void updateStatus();
    void toggleRecording();
    void initializeLabels();

    DepthSdkClient *const m_session;
    RvPointCloudCanvas *const m_view;
    QLabel *const m_status;
    QComboBox *const m_displayMode;
    QComboBox *const m_colorMode;
    QPushButton *const m_angleButton;
    QPushButton *const m_reset;
    QPushButton *const m_record;
    QCheckBox *const m_recordDepth;
    QCheckBox *const m_recordRgb;
    QLabel *const m_recordStatus;
    QWidget *const m_rangeControls;
    QLabel *const m_rangeLabel;
    QLabel *const m_legend;
    QLabel *const m_legendMinimum;
    QLabel *const m_legendMaximum;
    QDoubleSpinBox *const m_minimum;
    QDoubleSpinBox *const m_maximum;
    QPushButton *const m_applyRange;
    std::array<CxwlDepthPoint, CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT> m_tofPoints{};
    QSize m_grid;
    Frame m_latest;
    int m_inputAngleDegrees = 90;
    double m_colorMinimum = PointCloudColorScale::DefaultMinimumM;
    double m_colorMaximum = PointCloudColorScale::DefaultMaximumM;
    bool m_active = false;
};

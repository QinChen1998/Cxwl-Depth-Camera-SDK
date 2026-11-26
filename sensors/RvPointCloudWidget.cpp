#include "RvPointCloudWidget.h"
#include "../demo/DepthSdkClient.h"
#include "ImagePresentation.h"
#include "../UiStyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <cstring>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTransform>
#include <QVBoxLayout>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>

namespace {
constexpr float MmPerMetre = 1000.f;
constexpr auto InputAngleKey = "pointCloud/inputAngleDegrees";
constexpr auto ColorModeKey = "pointCloud/colorMode";
constexpr auto ColorMinimumKey = "pointCloud/colorMinimumM";
constexpr auto ColorMaximumKey = "pointCloud/colorMaximumM";
constexpr quint32 Missing = std::numeric_limits<quint32>::max();
enum { Points, Mesh };
enum { FixedColor, DynamicColor };


}

RvPointCloudWidget::RvPointCloudWidget(DepthSdkClient *session, QWidget *parent)
    : QWidget(parent), m_session(session), m_view(new RvPointCloudCanvas(this)),
      m_status(new QLabel(this)), m_displayMode(new QComboBox(this)), m_colorMode(new QComboBox(this)),
      m_angleButton(new QPushButton(this)), m_reset(new QPushButton(this)), m_record(new QPushButton(this)),
      m_recordDepth(new QCheckBox(this)), m_recordRgb(new QCheckBox(this)),
      m_recordStatus(new QLabel(this)), m_rangeControls(new QWidget(this)),
      m_rangeLabel(new QLabel(m_rangeControls)), m_legend(new QLabel(this)),
      m_legendMinimum(new QLabel(this)), m_legendMaximum(new QLabel(this)),
      m_minimum(new QDoubleSpinBox(m_rangeControls)), m_maximum(new QDoubleSpinBox(m_rangeControls)),
      m_applyRange(new QPushButton(m_rangeControls))
{
    setObjectName("rvPointCloudWidget");
    m_view->setObjectName("rvPointCloudView");
    m_status->setObjectName("rvPointCloudStatus");
    m_status->setWordWrap(true);
    m_displayMode->setObjectName("rvPointCloudDisplayMode");
    m_displayMode->addItems({QString(), QString()});
    bool angleValid = false;
    const int savedAngle = QSettings().value(InputAngleKey, m_inputAngleDegrees).toInt(&angleValid);
    if (angleValid && savedAngle >= 0 && savedAngle < 360) m_inputAngleDegrees = savedAngle;
    m_angleButton->setObjectName("rvPointCloudAngle");
    m_reset->setObjectName("rvPointCloudReset");
    m_record->setObjectName("rvPointCloudRecord");
    m_recordDepth->setObjectName("rvPointCloudRecordDepth");
    m_recordRgb->setObjectName("rvPointCloudRecordRgb");
    m_recordStatus->setObjectName("rvPointCloudRecordStatus");
    m_recordStatus->setWordWrap(true);
    m_recordStatus->setTextFormat(Qt::PlainText);
    m_recordStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_recordStatus->hide();
    auto *toolbar = new QWidget(this);
    auto *tools = UiStyle::wrappingRow(toolbar, ImagePresentation::Spacing);
    tools->addWidget(m_displayMode);
    tools->addWidget(m_angleButton);
    tools->addWidget(m_reset);
    tools->addWidget(m_record);
    tools->addWidget(m_recordDepth);
    tools->addWidget(m_recordRgb);
    tools->addWidget(m_status);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(toolbar);
    layout->addWidget(m_recordStatus);
    layout->addWidget(m_view, 1);
    setupColorControls(*layout);

    connect(m_displayMode, &QComboBox::currentIndexChanged, this, &RvPointCloudWidget::present);
    connect(m_angleButton, &QPushButton::clicked, this, &RvPointCloudWidget::editInputAngle);
    connect(m_session, &DepthSdkClient::pointCloudFrameReady, this, &RvPointCloudWidget::acceptPoints);
    connect(m_session, &DepthSdkClient::depthStreamCleared, this, &RvPointCloudWidget::clearFrame);
    connect(m_session, &DepthSdkClient::stateChanged, this, &RvPointCloudWidget::updateStatus);
    connect(m_reset, &QPushButton::clicked, m_view, &RvPointCloudCanvas::resetView);
    connect(m_record, &QPushButton::clicked, this, &RvPointCloudWidget::toggleRecording);
    auto &recorder = m_session->pointCloudRecorder();
    connect(&recorder, &PointCloudRecorder::stateChanged, this, &RvPointCloudWidget::updateStatus);
    connect(&recorder, &PointCloudRecorder::completed, this, [this](quint64 count, quint64 rgb, const QString &folder, const QString &error) {
        const auto path = QDir::toNativeSeparators(folder);
        QString result;
        if (folder.isEmpty()) result = tr("No frames were received during recording.");
        else if (m_recordRgb->isChecked()) result = tr("Saved %1 point cloud frames and %2 RGB frames to %3").arg(count).arg(rgb).arg(path);
        else result = tr("Saved %1 frames to %2").arg(count).arg(path);
        m_recordStatus->setText(error.isEmpty() ? result : error + " " + result);
        m_recordStatus->show();
    });
    initializeLabels();
}

RvPointCloudWidget::~RvPointCloudWidget() = default;

void RvPointCloudWidget::toggleRecording()
{
    auto &recorder = m_session->pointCloudRecorder();
    if (recorder.state() == PointCloudRecorder::State::Recording) {
        recorder.stopRecording();
        return;
    }
    if (recorder.state() != PointCloudRecorder::State::Idle || !m_grid.isValid()) return;
    const auto key = QStringLiteral("pointCloud/recordDirectory");
    const auto previous = QSettings().value(key, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    auto *dialog = new QFileDialog(this, tr("Select point cloud recording folder"), previous);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly);
    dialog->setSupportedSchemes({QStringLiteral("file")});
    connect(dialog, &QDialog::accepted, this, [this, dialog, key] {
        const auto folder = dialog->selectedFiles().value(0);
        if (folder.isEmpty() || !m_grid.isValid() || m_session->stopping()) return;
        const auto error = m_session->pointCloudRecorder().startRecording(folder, m_recordDepth->isChecked(), m_recordRgb->isChecked());
        m_recordStatus->setText(error);
        m_recordStatus->setVisible(!error.isEmpty());
        if (error.isEmpty()) QSettings().setValue(key, folder);
    });
    // Keep the capture event loop running while the Windows folder picker is open.
    dialog->open();
}

void RvPointCloudWidget::setupColorControls(QVBoxLayout &layout)
{
    m_colorMode->setObjectName("rvPointCloudColorMode");
    m_colorMode->addItems({QString(), QString()});
    m_minimum->setObjectName("rvPointCloudColorMinimum");
    m_maximum->setObjectName("rvPointCloudColorMaximum");
    m_applyRange->setObjectName("rvPointCloudColorApply");
    m_maximum->setSuffix(QStringLiteral(" m"));
    m_rangeLabel->setBuddy(m_minimum);
    for (auto *field : {m_minimum, m_maximum}) {
        field->setDecimals(2);
        field->setRange(PointCloudColorScale::DefaultMinimumM, 11.0);
        field->setSingleStep(0.1);
        field->setKeyboardTracking(false);
    }
    const QSettings settings;
    m_colorMode->setCurrentIndex(settings.value(ColorModeKey).toString() == "fixed" ? FixedColor : DynamicColor);
    m_rangeControls->setVisible(m_colorMode->currentIndex() == FixedColor);
    const double low = settings.value(ColorMinimumKey, m_colorMinimum).toDouble();
    const double high = settings.value(ColorMaximumKey, m_colorMaximum).toDouble();
    if (low >= m_minimum->minimum() && high <= m_maximum->maximum() && low < high) {
        m_colorMinimum = low;
        m_colorMaximum = high;
    }
    m_minimum->setValue(m_colorMinimum);
    m_maximum->setValue(m_colorMaximum);

    auto *range = new QHBoxLayout(m_rangeControls);
    range->setContentsMargins(0, 0, 0, 0);
    range->addWidget(m_rangeLabel);
    range->addWidget(m_minimum);
    range->addWidget(new QLabel(QStringLiteral("–"), m_rangeControls));
    range->addWidget(m_maximum);
    range->addWidget(m_applyRange);
    auto *row = new QWidget(this);
    auto *controls = UiStyle::wrappingRow(row, ImagePresentation::Spacing);
    controls->addWidget(m_colorMode);
    controls->addWidget(m_rangeControls);
    auto *legend = new QWidget(row);
    auto *labels = new QHBoxLayout(legend);
    labels->setContentsMargins(0, 0, 0, 0);
    m_legend->setFixedSize(96, 7);
    m_legend->setScaledContents(true);
    labels->addWidget(m_legendMinimum);
    labels->addWidget(m_legend);
    labels->addWidget(m_legendMaximum);
    controls->addWidget(legend);
    layout.addWidget(row);

    connect(m_colorMode, &QComboBox::currentIndexChanged, this, [this](int mode) {
        QSettings().setValue(ColorModeKey, mode == DynamicColor ? "dynamic" : "fixed");
        m_rangeControls->setVisible(mode == FixedColor);
        refreshColors();
    });
    const auto checkRange = [this] { m_applyRange->setEnabled(m_minimum->value() < m_maximum->value()); };
    connect(m_minimum, &QDoubleSpinBox::valueChanged, this, checkRange);
    connect(m_maximum, &QDoubleSpinBox::valueChanged, this, checkRange);
    connect(m_applyRange, &QPushButton::clicked, this, &RvPointCloudWidget::applyColorRange);
}

void RvPointCloudWidget::applyColorRange()
{
    if (m_minimum->value() >= m_maximum->value()) return;
    m_colorMinimum = m_minimum->value();
    m_colorMaximum = m_maximum->value();
    QSettings settings;
    settings.setValue(ColorMinimumKey, m_colorMinimum);
    settings.setValue(ColorMaximumKey, m_colorMaximum);
    refreshColors();
}

void RvPointCloudWidget::refreshColors()
{
    colorFrame(m_latest);
    updateLegend();
    present();
}

void RvPointCloudWidget::updateLegend()
{
    const bool dynamic = m_colorMode->currentIndex() == DynamicColor;
    m_legendMinimum->setText(dynamic ? tr("Near") : QString::number(m_colorMinimum, 'f', 2));
    m_legendMaximum->setText(dynamic ? tr("Far") : tr("%1 m").arg(m_colorMaximum, 0, 'f', 2));
    QImage image(96, 1, QImage::Format_RGB32);
    for (int x = 0; x < image.width(); ++x) {
        const double fraction = double(x) / (image.width() - 1);
        image.setPixel(x, 0, dynamic ? PointCloudColorScale::DynamicHistogram::colorAtFraction(fraction)
                                     : PointCloudColorScale::jet(fraction));
    }
    m_legend->setPixmap(QPixmap::fromImage(image));
}

void RvPointCloudWidget::setRenderingActive(bool active)
{
    m_active = active;
    if (active) present();
}

void RvPointCloudWidget::editInputAngle()
{
    QInputDialog dialog(this);
    dialog.setObjectName("rvPointCloudAngleDialog");
    dialog.setWindowTitle(tr("Point cloud angle"));
    dialog.setInputMode(QInputDialog::IntInput);
    dialog.setLabelText(tr("Clockwise angle (0–359°):"));
    dialog.setIntRange(0, 359);
    dialog.setIntValue(m_inputAngleDegrees);
    dialog.setOkButtonText(tr("Apply"));
    dialog.setCancelButtonText(tr("Cancel"));
    if (dialog.exec() != QDialog::Accepted) return;

    m_inputAngleDegrees = dialog.intValue();
    QSettings().setValue(InputAngleKey, m_inputAngleDegrees);
    syncInputAngle();
    if (m_grid.isValid()) rebuildFrame();
}

void RvPointCloudWidget::syncInputAngle()
{
    m_angleButton->setText(tr("Input angle (%1°)").arg(m_inputAngleDegrees));
    m_view->setProperty("inputAngleDegrees", m_inputAngleDegrees);
}

void RvPointCloudWidget::acceptPoints(const QByteArray &xyz, const QJsonObject &metadata)
{
    const uint32_t width = metadata["width"].toInt();
    const uint32_t height = metadata["height"].toInt();
    const uint64_t count = uint64_t(width) * height;
    if (!count || count > m_tofPoints.size() || xyz.size() != qsizetype(count * sizeof(CxwlDepthPoint))) {
        clearFrame();
        return;
    }
    std::memcpy(m_tofPoints.data(), xyz.constData(), xyz.size());
    m_grid = QSize(int(width), int(height));
    rebuildFrame();
}

void RvPointCloudWidget::rebuildFrame()
{
    m_latest = makeFrame(m_tofPoints.data(), m_grid, m_inputAngleDegrees);
    colorFrame(m_latest);
    present();
}

RvPointCloudWidget::Frame RvPointCloudWidget::makeFrame(const CxwlDepthPoint *tofPoints, const QSize &size, int angleDegrees)
{
    QTransform rotation;
    rotation.rotate(angleDegrees);
    Frame frame;
    const int columns = size.width(), rows = size.height(), pointCount = columns * rows;
    std::array<quint32, CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT> grid;
    grid.fill(Missing);
    frame.points.reserve(pointCount);
    for (int i = 0; i < pointCount; ++i) {
        const auto &point = tofPoints[i];
        if (point.z_m <= 0) continue;
        // Rotate in the ToF image plane, then map to the Qt scene with +Y up and -Z forward.
        const auto xy = rotation.map(QPointF(point.x_m, point.y_m));
        const auto position = QVector3D(float(xy.x()), -float(xy.y()), -point.z_m) * MmPerMetre;
        const float diameter = std::max(3.5f, -position.z() * .012f);
        grid[i] = quint32(frame.points.size());
        frame.points.append(Point{position, {}, diameter});
    }
    frame.indices.reserve(6 * (columns - 1) * (rows - 1));
    const auto triangle = [&](quint32 a, quint32 b, quint32 c) {
        if (a == Missing || b == Missing || c == Missing) return;
        const auto z = [&](quint32 i) { return -frame.points.at(i).position.z(); };
        const auto [low, high] = std::minmax({z(a), z(b), z(c)});
        if (high - low <= std::max(30.f, low * .05f)) frame.indices.append({a, b, c});
    };
    for (int row = 0; row + 1 < rows; ++row) {
        for (int column = 0; column + 1 < columns; ++column) {
            const int i = row * columns + column;
            triangle(grid[i], grid[i + columns], grid[i + 1]);
            triangle(grid[i + 1], grid[i + columns], grid[i + columns + 1]);
        }
    }
    return frame;
}

void RvPointCloudWidget::colorFrame(Frame &frame) const
{
    if (frame.points.isEmpty()) return;
    const auto depth = [](const Point &point) { return -point.position.z() / MmPerMetre; };
    std::optional<PointCloudColorScale::DynamicHistogram> histogram;
    if (m_colorMode->currentIndex() == DynamicColor) histogram.emplace(frame.points, depth);
    constexpr float ToColor = 1.f / 255.f;
    for (auto &point : frame.points) {
        const double z = depth(point);
        const auto rgb = histogram ? histogram->color(z) : PointCloudColorScale::color(z, m_colorMinimum, m_colorMaximum);
        point.color = {qRed(rgb) * ToColor, qGreen(rgb) * ToColor, qBlue(rgb) * ToColor, 1.f};
    }
}

void RvPointCloudWidget::clearFrame()
{
    m_grid = {};
    m_latest = {};
    m_view->clearFrame();
    updateStatus();
}

void RvPointCloudWidget::present()
{
    if (!m_active) return;
    m_view->setFrame(m_latest.points, m_latest.indices, m_displayMode->currentIndex() == Mesh, m_inputAngleDegrees);
    updateStatus();
}

void RvPointCloudWidget::updateStatus()
{
    const auto recording = m_session->pointCloudRecorder().state();
    m_record->setText(recording == PointCloudRecorder::State::Saving ? tr("Saving recording…")
                      : recording == PointCloudRecorder::State::Recording ? tr("Stop recording and save")
                                                                          : tr("Record point cloud"));
    m_record->setEnabled(recording == PointCloudRecorder::State::Recording ||
                         (recording == PointCloudRecorder::State::Idle && m_grid.isValid() && !m_session->stopping()));
    m_recordDepth->setEnabled(recording == PointCloudRecorder::State::Idle);
    m_recordRgb->setEnabled(recording == PointCloudRecorder::State::Idle);
    m_displayMode->setEnabled(true);
    m_reset->setEnabled(m_grid.isValid() && !m_latest.points.isEmpty());
    QString status;
    if (!m_grid.isValid())
        status = m_session->userStopped() ? tr("Start capture in Depth Map or Camera raw video.")
                                          : tr("Waiting for a depth frame...");
    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());
}

void RvPointCloudWidget::initializeLabels()
{
    syncInputAngle();
    m_angleButton->setToolTip(tr("Rotate the point cloud clockwise around the ToF forward axis."));
    m_displayMode->setItemText(Points, tr("Points"));
    m_displayMode->setItemText(Mesh, tr("Mesh"));
    m_displayMode->setAccessibleName(tr("Point cloud display mode"));
    m_colorMode->setItemText(FixedColor, tr("Fixed Z color range"));
    m_colorMode->setItemText(DynamicColor, tr("Dynamic + histogram equalization"));
    m_colorMode->setAccessibleName(tr("Point cloud color mode"));
    m_colorMode->setItemData(DynamicColor, tr("Equalize depths from 0.10 to 10.00 m in each frame. Colors can vary across frames."), Qt::ToolTipRole);
    m_reset->setText(tr("Reset view"));
    m_recordDepth->setText(tr("Also record depth"));
    m_recordRgb->setText(tr("Also record RGB"));
    m_rangeLabel->setText(tr("Z color range"));
    m_minimum->setAccessibleName(tr("Color minimum (m)"));
    m_maximum->setAccessibleName(tr("Color maximum (m)"));
    m_applyRange->setText(tr("Apply"));
    m_view->setAccessibleName(tr("Point cloud view"));
    m_view->setAccessibleDescription(tr("Drag to rotate, middle-drag to pan, and scroll to zoom."));
    updateLegend();
    updateStatus();
}

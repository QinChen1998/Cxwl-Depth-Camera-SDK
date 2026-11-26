#include "DepthMapWidget.h"
#include "../UiStyle.h"
#include "RvDepthView.h"
#include "RvImagingSchemes.h"
#include "PointCloudColorScale.h"
#include "ImagePresentation.h"

#include <QtCore/QSettings>
#include <QtCore/QDir>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QProgressBar>
#include <QtCore/QDateTime>
#include <QtCore/QEvent>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QVBoxLayout>

#include <cmath>

namespace
{
constexpr auto ColorModeKey = "depth/colorMode";
enum { FixedColor, DynamicColor };

class ColorRangeInput final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

    QValidator::State validate(QString &text, int &position) const override
    {
        static const QRegularExpression decimal(QStringLiteral("\\A[0-9]*\\.?[0-9]*\\z"));
        return decimal.match(text).hasMatch() ? QDoubleSpinBox::validate(text, position) : QValidator::Invalid;
    }
};


}

DepthMapWidget::DepthMapWidget(QWidget *parent)
    : QWidget(parent)

{
    setObjectName(QStringLiteral("depthMapWidget"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(ImagePresentation::Margin, ImagePresentation::Margin,
                               ImagePresentation::Margin, ImagePresentation::Margin);
    layout->setSpacing(ImagePresentation::Spacing);
    auto *settingsRow = new QWidget(this);
    settingsRow->setObjectName("imageSettingsRow");
    auto *settingsLayout = new QVBoxLayout(settingsRow);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(ImagePresentation::Spacing);
    m_rvControls = new QWidget(settingsRow);
    auto *rvLayout = UiStyle::wrappingRow(m_rvControls, ImagePresentation::Spacing);
    m_rvModelCaption = new QLabel(m_rvControls);
    m_rvModelCaption->setObjectName("rvDepthModelLabel");
    m_rvModel = new QComboBox(m_rvControls);
    m_rvModel->setObjectName("rvDepthModel");
    for (const auto &scheme : RvDtofCandidates::ImagingSchemes) {
        if (DepthSdkClient::supportsImagingMode(scheme.sdkMode))
            m_rvModel->addItem(QString(), QLatin1String(scheme.name));
    }
    m_rvModel->setMinimumWidth(0);
    m_rvModel->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_rvModel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    rvLayout->addWidget(m_rvModelCaption);
    rvLayout->addWidget(m_rvModel);
    bool angleValid=false;
    const auto savedAngle=QSettings().value("depth/nativeTofRotationDegrees",90).toInt(&angleValid);
    m_nativeTofRotation=angleValid&&savedAngle>=0&&savedAngle<360?savedAngle:90;
    QSettings().setValue("depth/nativeTofRotationDegrees",m_nativeTofRotation);
    m_nativeTofAngle=new QPushButton(m_rvControls);m_nativeTofAngle->setObjectName("rvNativeTofAngle");
    connect(m_nativeTofAngle,&QPushButton::clicked,this,&DepthMapWidget::editNativeTofRotation);
    rvLayout->addWidget(m_nativeTofAngle);
    m_rvStart = new QPushButton(m_rvControls); m_rvStart->setObjectName("rvDepthStart");
    m_rvStop = new QPushButton(m_rvControls); m_rvStop->setObjectName("rvDepthStop");
    m_rvReconnect = new QPushButton(m_rvControls); m_rvReconnect->setObjectName("rvDepthReconnect");
    for (auto *button : {m_rvStart, m_rvStop, m_rvReconnect}) rvLayout->addWidget(button);
    settingsLayout->addWidget(m_rvControls, 1);
    connect(m_rvModel, &QComboBox::activated, this, [this](int) {
        if(m_session)m_session->selectImagingScheme(m_rvModel->currentData().toString());
        syncSessionControls();
    });
    layout->addWidget(settingsRow);
    m_viewport = new QWidget(this);
    m_viewport->setObjectName("rvDepthViewport");
    auto *viewportLayout = new QVBoxLayout(m_viewport);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    m_rvCanvas = new RvDepthView(m_viewport);
    m_rvCanvas->setObjectName("rvNativeDepthCanvas");
    syncNativeTofRotation();
    viewportLayout->addWidget(m_rvCanvas);
    layout->addWidget(m_viewport, 1);
    m_progressOverlay = new QWidget(m_viewport);
    m_progressOverlay->setObjectName("rvImagingSwitchOverlay");
    m_progressOverlay->setAutoFillBackground(true);
    auto progressPalette = m_progressOverlay->palette();
    progressPalette.setColor(QPalette::Window, QColor("#11161d"));
    progressPalette.setColor(QPalette::WindowText, QColor("#c9d1d9"));
    m_progressOverlay->setPalette(progressPalette);
    auto *progressLayout = new QVBoxLayout(m_progressOverlay);
    progressLayout->setContentsMargins(24, 24, 24, 24);
    progressLayout->addStretch();
    m_progressTitle = new QLabel(m_progressOverlay);
    m_progressTitle->setObjectName("rvImagingProgressTitle");
    m_progressTitle->setAlignment(Qt::AlignCenter);
    m_progressTitle->setWordWrap(true);
    m_progressTitle->setStyleSheet(ImagePresentation::CaptureProgressTextStyle);
    progressLayout->addWidget(m_progressTitle);
    auto *progressRow=new QHBoxLayout;progressRow->addStretch(1);
    m_progressBar=new QProgressBar(m_progressOverlay);m_progressBar->setObjectName("rvImagingSwitchProgress");
    m_progressBar->setFormat("%p%");m_progressBar->setMaximumWidth(400);
    m_progressBar->setFixedHeight(22);
    m_progressBar->setStyleSheet(ImagePresentation::CaptureProgressBarStyle);
    progressRow->addWidget(m_progressBar,6);progressRow->addStretch(1);progressLayout->addLayout(progressRow);
    m_progressDetail = new QLabel(m_progressOverlay);
    m_progressDetail->setObjectName("rvImagingProgressDetail");
    m_progressDetail->setAlignment(Qt::AlignCenter);
    m_progressDetail->setWordWrap(true);
    m_progressDetail->setStyleSheet("color: #a9b8c8; background: transparent;");
    progressLayout->addWidget(m_progressDetail);
    progressLayout->addStretch();
    m_progressOverlay->hide();m_viewport->installEventFilter(this);
    m_progressUiTimer=new QTimer(this);m_progressUiTimer->setInterval(33);
    connect(m_progressUiTimer, &QTimer::timeout, this, [this] {
        if (m_switchUiActive) advanceImagingSwitchUi();
        else { syncCaptureProgressUi(); updateRvPresentation(); }
    });
    m_imageSettings=ImageDisplaySettings::shared();
    connect(m_imageSettings.data(),&ImageDisplaySettings::colorRangeChanged,this,&DepthMapWidget::syncDisplayColors);
    connect(m_imageSettings.data(),&ImageDisplaySettings::blackOutsideRangeChanged,this,&DepthMapWidget::syncDisplayColors);
    auto *statusRow = new QWidget(this);
    statusRow->setObjectName("imageStatusRow");
    auto *saveControls = UiStyle::wrappingRow(statusRow, ImagePresentation::Spacing);
    m_rvCanvas->setDisplayRotationDegrees(displayRotationDegrees());
    connect(m_imageSettings.data(), &ImageDisplaySettings::rotationChanged, this, [this](int degrees) {
        m_rvCanvas->setDisplayRotationDegrees(degrees);
    });
    m_saveButton = new QPushButton(this);m_saveButton->setObjectName("depthMapSave");
    m_saveButton->setEnabled(false);
    m_fpsLabel = new QLabel(QStringLiteral("FPS: 0.0"), statusRow);
    m_fpsLabel->setObjectName("depthFpsLabel");
    m_fpsLabel->setMinimumWidth(75);
    saveControls->addWidget(m_fpsLabel);
    saveControls->addWidget(m_saveButton);
    m_colorMode = new QComboBox(statusRow);
    m_colorMode->setObjectName("rvDepthColorMode");
    m_colorMode->addItems({QString(), QString()});
    m_colorMode->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    saveControls->addWidget(m_colorMode);
    m_blackOutsideRange = new QCheckBox(statusRow);
    m_blackOutsideRange->setObjectName("depthBlackOutsideRange");
    m_blackOutsideRange->setChecked(m_imageSettings->blackOutsideRange());
    saveControls->addWidget(m_blackOutsideRange);
    connect(m_blackOutsideRange, &QCheckBox::toggled, this, [this](bool enabled){m_imageSettings->setBlackOutsideRange(enabled);});
    connect(m_imageSettings.data(), &ImageDisplaySettings::blackOutsideRangeChanged, this, [this](bool enabled){const QSignalBlocker block(m_blackOutsideRange);m_blackOutsideRange->setChecked(enabled);});

    auto *minimum = new ColorRangeInput(statusRow);
    auto *maximum = new ColorRangeInput(statusRow);
    minimum->setObjectName("rvColorMinimum");
    maximum->setObjectName("rvColorMaximum");
    auto locale = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
    for (auto *field : {minimum, maximum}) {
        field->setLocale(locale);
        field->setDecimals(3);
        field->setRange(ImageDisplaySettings::MinimumColorRangeM, ImageDisplaySettings::MaximumColorRangeM);
        field->setSingleStep(.1);
        field->setButtonSymbols(QAbstractSpinBox::NoButtons);
        field->setMinimumWidth(67);
    }
    minimum->setValue(m_imageSettings->colorMinimum());
    maximum->setValue(m_imageSettings->colorMaximum());
    m_colorMinimumLabel = new QLabel(statusRow);
    m_colorMaximumLabel = new QLabel(statusRow);
    m_colorMinimumLabel->setBuddy(minimum);
    m_colorMaximumLabel->setBuddy(maximum);
    m_applyColorRange = new QPushButton(statusRow);
    m_applyColorRange->setObjectName("rvColorRangeApply");
    QList<QWidget *> fixedControls{m_blackOutsideRange, m_applyColorRange};
    for (const auto &[label, field] : {qMakePair(m_colorMinimumLabel, minimum), qMakePair(m_colorMaximumLabel, maximum)}) {
        auto *range = new QWidget(statusRow);
        auto *pair = new QHBoxLayout(range);
        pair->setContentsMargins(0, 0, 0, 0);
        pair->setSpacing(ImagePresentation::Spacing);
        pair->addWidget(label);
        pair->addWidget(field);
        saveControls->addWidget(range);
        fixedControls.append(range);
    }
    saveControls->addWidget(m_applyColorRange);
    const auto validRange = [minimum, maximum] {
        return minimum->hasAcceptableInput() && maximum->hasAcceptableInput()
            && minimum->value() < maximum->value();
    };
    const auto updateApply = [this, validRange] { m_applyColorRange->setEnabled(validRange()); };
    for (auto *field : {minimum, maximum}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, updateApply);
        connect(field->findChild<QLineEdit *>(), &QLineEdit::textChanged, this, updateApply);
    }
    connect(m_applyColorRange, &QPushButton::clicked, this, [this, minimum, maximum, validRange] {
        if(validRange()){
            m_imageSettings->setColorRange(minimum->value(),maximum->value());
        }
    });
    updateApply();
    const auto applyColorMode = [this, fixedControls](int mode) {
        const bool dynamic = mode == DynamicColor;
        for (auto *control : fixedControls) control->setVisible(!dynamic);
        m_rvCanvas->setDynamicColoring(dynamic);
    };
    m_colorMode->setCurrentIndex(QSettings().value(ColorModeKey).toString() == "fixed" ? FixedColor : DynamicColor);
    applyColorMode(m_colorMode->currentIndex());
    connect(m_colorMode, &QComboBox::currentIndexChanged, this, [applyColorMode](int mode) {
        QSettings().setValue(ColorModeKey, mode == DynamicColor ? "dynamic" : "fixed");
        applyColorMode(mode);
    });
    auto *fpsRefresh=new QTimer(this);fpsRefresh->setInterval(250);
    connect(fpsRefresh,&QTimer::timeout,this,[this]{
        m_fpsLabel->setText(QStringLiteral("FPS: %1").arg(m_rvCanvas->displayedFps(), 0, 'f', 1));
    });
    fpsRefresh->start();
    connect(m_rvStart, &QPushButton::clicked, this, [this]{if (m_session) m_session->start();});
    connect(m_rvStop, &QPushButton::clicked, this, [this]{if (m_session) m_session->stop();});
    connect(m_rvReconnect, &QPushButton::clicked, this, [this]{if (m_session) m_session->reconnectNow();});
    layout->addWidget(statusRow);
    connect(m_saveButton,&QPushButton::clicked,this,&DepthMapWidget::saveCurrentFrame);

    syncDisplayColors();
    initializeLabels();
}

QSize DepthMapWidget::selectedGrid() const
{
    const auto *scheme = RvDtofCandidates::schemeForName(m_rvModel->currentData().toString());
    const auto mode = scheme ? scheme->boardMode : RvCaptureContract::Native48;
    return {RvCaptureContract::columns(mode), RvCaptureContract::rows(mode)};
}

int DepthMapWidget::selectedWidth() const { return selectedGrid().width(); }
int DepthMapWidget::selectedHeight() const { return selectedGrid().height(); }

int DepthMapWidget::displayRotationDegrees() const
{
    return m_imageSettings->rotationDegrees();
}

void DepthMapWidget::setDisplayRotationDegrees(int degrees)
{
    m_imageSettings->setRotationDegrees(degrees);
}

void DepthMapWidget::syncNativeTofRotation()
{
    m_nativeTofAngle->setText(tr("Input angle (%1°)").arg(m_nativeTofRotation));
    m_nativeTofAngle->setToolTip(tr("Rotate the displayed ToF grid clockwise. Raw measurements and saved images keep their orientation."));
    m_rvCanvas->setNativeDisplayRotationDegrees(m_nativeTofRotation);
}

void DepthMapWidget::editNativeTofRotation()
{
    QInputDialog dialog(this);dialog.setObjectName("rvNativeTofAngleDialog");
    dialog.setWindowTitle(tr("Native ToF angle"));dialog.setInputMode(QInputDialog::IntInput);
    dialog.setLabelText(tr("Clockwise angle (0–359°):"));dialog.setIntRange(0,359);dialog.setIntValue(m_nativeTofRotation);
    dialog.setOkButtonText(tr("Apply"));dialog.setCancelButtonText(tr("Cancel"));
    if(dialog.exec()!=QDialog::Accepted)return;
    m_nativeTofRotation=dialog.intValue();QSettings().setValue("depth/nativeTofRotationDegrees",m_nativeTofRotation);
    syncNativeTofRotation();
}

void DepthMapWidget::initializeLabels()
{
    syncNativeTofRotation();
    m_colorMode->setItemText(FixedColor, tr("Fixed color range"));
    m_colorMode->setItemText(DynamicColor, tr("Dynamic + histogram equalization"));
    m_colorMode->setAccessibleName(tr("Depth map color mode"));
    m_colorMode->setItemData(DynamicColor, tr("Equalize depths from 0.10 to 10.00 m in each frame. Colors can vary across frames."), Qt::ToolTipRole);
    m_blackOutsideRange->setText(tr("Black outside range"));
    m_colorMinimumLabel->setText(tr("Minimum (m)"));
    m_colorMaximumLabel->setText(tr("Maximum (m)"));
    m_applyColorRange->setText(tr("Apply"));
    m_saveButton->setText(tr("Save depth map"));
    m_rvModelCaption->setText(tr("Imaging scheme:"));
    m_rvStart->setText(tr("Start"));
    m_rvStop->setText(tr("Stop"));
    m_rvReconnect->setText(tr("Reconnect now"));
    for (auto *button : {m_rvStart, m_rvStop, m_rvReconnect})
        button->setToolTip(tr("Start, Stop and Reconnect apply to the shared camera + depth stream."));
    for (const auto &scheme : RvDtofCandidates::ImagingSchemes) {
        const int index = m_rvModel->findData(QLatin1String(scheme.name));
        if (index < 0) continue;
        const auto label = scheme.highAccuracy ? tr("%1×%2 high accuracy") : tr("%1×%2 standard");
        m_rvModel->setItemText(index,
            label.arg(RvCaptureContract::columns(scheme.boardMode)).arg(RvCaptureContract::rows(scheme.boardMode)));
    }
    m_rvModel->setAccessibleName(tr("Imaging scheme:"));
    m_rvModel->setToolTip(tr("Select standard or high accuracy for 48×32 depth."));
    syncSessionControls();
}

bool DepthMapWidget::eventFilter(QObject *watched,QEvent *event)
{
    if(watched==m_viewport&&m_progressOverlay&&(event->type()==QEvent::Resize||event->type()==QEvent::Show)){
        m_progressOverlay->setGeometry(m_viewport->rect());if(!m_progressOverlay->isHidden())m_progressOverlay->raise();
    }
    return QWidget::eventFilter(watched,event);
}

void DepthMapWidget::beginImagingSwitchUi(const QString &scheme,int estimatedMs)
{
    m_switchUiActive=true;m_switchUiFinishing=false;m_switchUiScheme=scheme;
    m_switchEstimatedMs=std::max(1,estimatedMs);m_switchUiClock.restart();m_switchFinishClock.invalidate();
    m_progressBar->setRange(0,1000);m_progressBar->setValue(0);m_progressBar->setTextVisible(true);
    m_progressBar->show();
    m_progressDetail->show();
    m_progressBar->setAccessibleDescription(QString());
    m_snapshot = {};
    m_rvCanvas->clear(tr("Switching imaging scheme..."));
    m_progressOverlay->setGeometry(m_viewport->rect());m_progressOverlay->show();m_progressOverlay->raise();
    m_progressUiTimer->start();advanceImagingSwitchUi();syncSessionControls();
}

void DepthMapWidget::finishImagingSwitchUi()
{
    if(!m_switchUiActive)return;
    // Completion is signalled only by an admitted depth result. Coloring stays
    // asynchronous under the overlay; do not expose an empty or old viewport.
    m_switchUiFinishing=true;m_switchFinishFrom=m_progressBar->value();m_switchFinishClock.invalidate();
    advanceImagingSwitchUi();
}

void DepthMapWidget::cancelImagingSwitchUi()
{
    if(!m_switchUiActive)return;
    m_switchUiActive=false;m_switchUiFinishing=false;m_progressUiTimer->stop();
    m_progressBar->setValue(0);m_progressOverlay->hide();
    setProperty("imagingSwitchVisible",false);setProperty("imagingSwitchProgress",0);
    syncSessionControls();updateRvPresentation();
}

void DepthMapWidget::advanceImagingSwitchUi()
{
    if(!m_switchUiActive)return;
    if(!m_session||m_session->userStopped()){cancelImagingSwitchUi();return;}
    const auto *view=m_rvCanvas;
    const bool rendered=view&&view->hasRenderableFrame();
    const int waiting=std::min(950,int(960.*(1.-std::exp(-2.*m_switchUiClock.elapsed()/m_switchEstimatedMs))));
    if(m_switchUiFinishing&&rendered&&!m_switchFinishClock.isValid()){
        m_switchFinishFrom=m_progressBar->value();m_switchFinishClock.start();
    }
    int value=std::max(m_progressBar->value(),waiting);
    if(m_switchUiFinishing&&m_switchFinishClock.isValid())
        value=m_switchFinishFrom+int((1000-m_switchFinishFrom)*std::min(1.,m_switchFinishClock.elapsed()/180.));
    m_progressBar->setValue(value);m_progressBar->setAccessibleName(tr("Imaging scheme switch progress"));
    m_progressBar->setToolTip(tr("Estimated progress. Completion waits for a valid new depth image."));
    m_progressTitle->setText(m_switchUiFinishing?(m_switchFinishClock.isValid()?tr("Switch complete"):tr("Waiting for the new depth image...")):tr("Switching imaging scheme..."));
    m_progressDetail->setText(m_rvModel->itemText(m_rvModel->findData(m_switchUiScheme)));
    setProperty("imagingSwitchVisible",true);setProperty("imagingSwitchProgress",value);
    m_progressOverlay->raise();
    if(m_switchUiFinishing&&m_switchFinishClock.isValid()&&m_switchFinishClock.elapsed()>=260&&rendered){
        m_progressBar->setValue(1000);setProperty("imagingSwitchProgress",1000);
        m_switchUiActive=false;m_switchUiFinishing=false;m_progressUiTimer->stop();m_progressOverlay->hide();
        setProperty("imagingSwitchVisible",false);syncSessionControls();updateRvPresentation();
    }
}

void DepthMapWidget::syncCaptureProgressUi()
{
    if (m_switchUiActive) return;
    using Status = DepthSdkClient::CaptureStatus;
    const auto status = m_session ? m_session->captureStatus() : Status::Idle;
    const auto title = m_session ? m_session->captureStatusText() : QString();
    if (title.isEmpty()) {
        if (m_progressOverlay->isHidden()) return;
        // Keep the overlay during asynchronous coloring of an admitted frame.
        const auto *view = m_rvCanvas;
        if (status == Status::Running && view->isVisible() && view->hasFrame()
            && !view->frameHeld() && !view->hasRenderableFrame()) {
            if (!m_progressUiTimer->isActive()) m_progressUiTimer->start();
            return;
        }
        m_progressUiTimer->stop();
        m_progressOverlay->hide();
        return;
    }

    QString detail;
    if (status == Status::Starting) {
        switch (m_session->connectionStage()) {
        case CXWL_DEPTH_STAGE_CHECKING: detail = tr("Checking device and board files..."); break;
        case CXWL_DEPTH_STAGE_STARTING: detail = tr("Starting camera and depth capture..."); break;
        default: detail = tr("Waiting for depth and video frames..."); break;
        }
    }
    m_progressUiTimer->stop();
    m_progressTitle->setText(title);
    m_progressDetail->setText(detail);
    m_progressDetail->setVisible(!detail.isEmpty());
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setAccessibleName(title);
    m_progressBar->setAccessibleDescription(detail);
    m_progressBar->setToolTip(QString());
    m_progressBar->setVisible(m_session && m_session->captureTransitionPending());
    if (m_progressOverlay->isHidden()) {
        m_progressOverlay->setGeometry(m_viewport->rect());
        m_progressOverlay->show();
        m_progressOverlay->raise();
    }
}

void DepthMapWidget::setCaptureClient(DepthSdkClient *controller)
{
    if (m_session == controller) return;
    cancelImagingSwitchUi();
    if(m_session){
        disconnect(m_session,nullptr,this,nullptr);
    }
    // A recovery window belongs to one source. Rebinding never carries the
    // previous source's displayed pixels into the new session.
    m_rvCanvas->clear();
    m_session = controller;
    clearRvDepth(tr("RV session changed"));
    if (m_session) {
        connect(m_session, &DepthSdkClient::stateChanged,
                this, &DepthMapWidget::syncSessionControls);
        connect(m_session, &DepthSdkClient::depthFrameReady,
                this, &DepthMapWidget::acceptRvDepth);
        connect(m_session, &DepthSdkClient::depthStreamCleared,
                this, &DepthMapWidget::clearRvDepth);
        connect(m_session,&DepthSdkClient::imagingSwitchStarted,this,&DepthMapWidget::beginImagingSwitchUi);
        connect(m_session,&DepthSdkClient::imagingSwitchFinished,this,[this]{finishImagingSwitchUi();});
        connect(m_session,&DepthSdkClient::imagingSwitchAborted,this,[this]{cancelImagingSwitchUi();});
        connect(m_session,&DepthSdkClient::recoveryDisplayExpired,this,[this] {
            if(m_rvCanvas->frameHeld())clearRvDepth(tr("No fresh dToF data"));
        });
        connect(m_session, &QObject::destroyed, this, [this] {
            m_session = nullptr;
            cancelImagingSwitchUi();
            clearRvDepth(tr("RV session unavailable"));
            syncSessionControls();
        });
    }
    syncSessionControls();
}

DepthSdkClient *DepthMapWidget::captureClient() const
{
    return m_session.data();
}

void DepthMapWidget::syncSessionControls()
{
    const bool available = m_session && m_session->available();
    const bool canChangeScheme = ImagePresentation::updateCaptureButtons(
        m_session.data(), *m_rvStart, *m_rvStop, *m_rvReconnect);
    if(m_switchUiActive&&(!m_session||m_session->stopping()||m_session->userStopped()))cancelImagingSwitchUi();
    const auto scheme = available ? m_session->currentImagingScheme() : QString();
    {
        const QSignalBlocker blocker(m_rvModel);
        m_rvModel->setCurrentIndex(m_rvModel->findData(scheme));
    }
    m_rvModel->setEnabled(canChangeScheme && !m_switchUiActive);
    syncCaptureProgressUi();
    updateRvPresentation();
}
void DepthMapWidget::syncDisplayColors()
{
    m_rvCanvas->setDisplayRange(m_imageSettings->colorMinimum(), m_imageSettings->colorMaximum());
    m_rvCanvas->setBlackOutsideRange(m_imageSettings->blackOutsideRange());
    if(auto *field=findChild<QDoubleSpinBox*>("rvColorMinimum"))field->setValue(m_imageSettings->colorMinimum());
    if(auto *field=findChild<QDoubleSpinBox*>("rvColorMaximum"))field->setValue(m_imageSettings->colorMaximum());
    if(m_blackOutsideRange){const QSignalBlocker block(m_blackOutsideRange);m_blackOutsideRange->setChecked(m_imageSettings->blackOutsideRange());}
    if(m_colorMinimumLabel)m_colorMinimumLabel->setText(tr("Minimum (m)"));
    if(m_colorMaximumLabel)m_colorMaximumLabel->setText(tr("Maximum (m)"));
    // Expired pixels cannot be recolored after their metric samples are revoked.
}
void DepthMapWidget::acceptRvDepth(const QByteArray& depth,const QJsonObject& m)
{
    if(!m_rvCanvas->setDepth(depth,m)){clearRvDepth(tr("Invalid depth packet"));return;}
    m_snapshot = {depth, {}, m, QDateTime::currentDateTimeUtc()};
    updateRvPresentation();
}
void DepthMapWidget::clearRvDepth(const QString& reason)
{
    m_snapshot={};
    const bool holding=m_session&&m_session->retainingRecoveryDisplay();
    if(holding)m_rvCanvas->holdFrame();else m_rvCanvas->clear(reason);
    m_fpsLabel->setText(QStringLiteral("FPS: 0.0"));
    updateRvPresentation();
}

void DepthMapWidget::updateRvPresentation()
{
    if (!m_saveButton) return;
    m_saveButton->setEnabled(m_progressOverlay->isHidden()
        && !(m_session && m_session->retainingRecoveryDisplay()) && !m_snapshot.metresFloat32LE.isEmpty());
}

DepthSnapshot DepthMapWidget::snapshot() const
{
    if (m_snapshot.metresFloat32LE.isEmpty()) return {};
    auto result = m_snapshot;
    result.depthImage = m_rvCanvas->snapshotImage();
    const bool dynamic = m_rvCanvas->dynamicColoring();
    using Histogram = PointCloudColorScale::DynamicHistogram;
    result.metadata["display_color_mode"] = dynamic ? "dynamic" : "fixed";
    result.metadata["display_minimum_m"] = dynamic ? Histogram::MinimumM : m_rvCanvas->displayMinimum();
    result.metadata["display_maximum_m"] = dynamic ? Histogram::MaximumM : m_rvCanvas->displayMaximum();
    result.metadata["display_out_of_range_black"] = !dynamic && m_rvCanvas->blackOutsideRange();
    result.metadata["native_tof_display_rotation_degrees"] = m_nativeTofRotation;
    result.metadata["native_tof_display_mirror_horizontal"] = true;
    result.metadata["display_rotation_degrees"] = (displayRotationDegrees() + m_nativeTofRotation) % 360;
    return result;
}

void DepthMapWidget::saveCurrentFrame()
{
    const auto frozen=snapshot(); // Freeze before the dialog can process another packet.
    if (frozen.depthImage.isNull()) {
        QMessageBox::warning(this,tr("Save depth map"),tr("No current depth frame. Start capture and wait for a frame."));return;
    }
    QSettings settings;
    const auto directory=QFileDialog::getExistingDirectory(this,tr("Select capture folder"),settings.value("depthMap/saveDirectory",QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString());
    if(directory.isEmpty())return;
    QString result;
    if(!frozen.save(directory, &result)){QMessageBox::warning(this,tr("Save depth map"),tr("Save failed: %1").arg(result));return;}
    settings.setValue("depthMap/saveDirectory",directory);
    QMessageBox::information(this,tr("Save depth map"),tr("Saved to %1").arg(QDir::toNativeSeparators(result)));
}

DepthMapWidget::~DepthMapWidget(){
    if(m_session){
        disconnect(m_session,nullptr,this,nullptr);
    }
}

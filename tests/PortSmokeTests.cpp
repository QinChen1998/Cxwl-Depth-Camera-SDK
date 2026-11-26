#include "../SensorMainWindow.h"
#include "../RuntimeDiscovery.h"
#include "../demo/DepthSdkClient.h"
#include "../sdk/DepthSdkQt.h"
#include "../sensors/RvSessionController.h"
#include "RvDeviceFixture.h"
#include "../sensors/DepthMapWidget.h"
#include "../sensors/RtspCameraPage.h"
#include "../sensors/RvDepthView.h"
#include "../sensors/RvPointCloudWidget.h"
#include "../sensors/RvDeviceManagerDialog.h"
#include "../sensors/RvCaptureCalibration.h"
#include "../sensors/RvHdVideoStream.h"
#include "../sensors/RvTunerWidget.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>
#include <QTimer>
#include <QToolBar>
#include <utility>

namespace {
void settle(int milliseconds){QElapsedTimer timer;timer.start();while(timer.elapsed()<milliseconds){QApplication::processEvents();QThread::msleep(5);}}

bool chooseInputAngle(QPushButton *button, const char *dialogName, int degrees, bool accept)
{
    if (!button) return false;
    bool opened = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, button, [&] {
        if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            opened = dialog->objectName() == dialogName && dialog->intMinimum() == 0 && dialog->intMaximum() == 359;
            dialog->setIntValue(degrees);
            dialog->done(accept ? QDialog::Accepted : QDialog::Rejected);
        }
    });
    timer.start(0);
    button->click();
    return opened;
}
}

int runPortSmokeTests(SensorMainWindow &window,const QString &path)
{
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text))return 3;
    QTextStream out(&file);bool pass=true;
    const auto check=[&](const QString &name,bool ok){out<<name<<' '<<(ok?"PASS":"FAIL")<<Qt::endl;pass&=ok;};
    check("NATIVE_FFMPEG_RUNTIME", RvHdVideoStream::runtimeError().isEmpty());
    {
        // Compare with the original public Qt conversion, especially around
        // every hue rounding boundary and the user range/invalid-value rules.
        bool hues=true,ranges=true;quint32 random=0x2834af17;
        for(int i=0;i<=24000;++i)for(float offset:{-.501f,-.5f,-.499f,0.f,.499f,.5f,.501f}){
            const float hue=std::clamp((float(i)+offset)/36000.f,0.f,2.f/3);
            hues&=DepthColorScale::hueColor(hue)==QColor::fromHsvF(hue,1,1).rgb();
        }
        const auto reference=[](float metres,double lo,double hi,bool valid,bool black){
            if(!valid||!std::isfinite(metres)||metres<=0||!std::isfinite(lo)||!std::isfinite(hi)||hi<=lo)return qRgb(0,0,0);
            if(black&&(metres<float(lo)||metres>float(hi)))return qRgb(0,0,0);
            return QColor::fromHsvF((1-std::clamp((double(metres)-lo)/(hi-lo),0.,1.))*2/3,1,1).rgb();
        };
        for(const auto range:{std::pair<double,double>{.01,3},{.01,11},{.37,2.19},{1,1},{3,1}}){
            for(int i=0;i<100000;++i){
                random=random*1664525u+1013904223u;
                const float metres=(int(random>>8)-0x400000)/float(0x800000)*12;
                for(bool black:{false,true})ranges&=DepthColorScale::color(metres,range.first,range.second,true,black)==reference(metres,range.first,range.second,true,black);
            }
            for(float metres:{0.f,-1.f,float(range.first),float(range.second),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
                for(bool valid:{false,true})for(bool black:{false,true})
                    ranges&=DepthColorScale::color(metres,range.first,range.second,valid,black)==reference(metres,range.first,range.second,valid,black);
        }
        check("PALETTE_ALL_HUE_BOUNDARIES_MATCH_QT",hues);
        check("PALETTE_RANGE_AND_INVALID_PIXELS_MATCH_QT",ranges);
    }
    settle(900);
    const auto docks=window.findChildren<QDockWidget *>(QString(),Qt::FindDirectChildrenOnly);
    check("EXACTLY_THREE_MODULES", docks.size() == 3);
    check("RETIRED_MODULES_ABSENT", !window.findChild<QWidget *>("tactileDock")
        && !window.findChild<QAction *>("inferenceDeviceAction") && !window.findChild<QMenu *>("renderModeMenu")
        && !window.findChild<QWidget *>("rvAssistDepthCanvas") && !window.findChild<QWidget *>("depthMapSaveCamera"));
    check("NO_DTOF_TUNER_UI",!window.findChild<RvTunerWidget *>());
    check("NO_INACTIVE_DEPTH_PAGES",!window.depth()->findChild<QWidget *>("depthMapCanvas")
        &&!window.depth()->findChild<QWidget *>("rvLiteSrDepthCanvas"));
    for(const auto *name:{"depthSourceControls","depthMapResolutionCombo","depthMapDisplayCombo",
                         "rvDepthResolution","rvDepthOnBoard","rvDepthMethod","rvColorOutput",
                         "rvLiteSrWhiteCounts","rvLiteSrInputRotation","depthMonocularConfidence"})
        check(QString("NO_OBSOLETE_CONTROL_")+name,!window.depth()->findChild<QWidget *>(name));
    const auto *toolbar=window.findChild<QToolBar *>("sensorToolBar");
    const auto *viewMenu=window.findChild<QMenu *>("viewMenu");
    bool viewToggles=viewMenu;
    for(auto *dock:docks)viewToggles&=viewMenu&&viewMenu->actions().contains(dock->toggleViewAction());
    check("MODULES_AVAILABLE_ONLY_IN_VIEW_MENU",!toolbar&&viewToggles);
    for(const auto *name:{"depthMapDock","rtspCameraDock","pointCloudDock"})
        check(QString("DOCK_")+name,window.findChild<QDockWidget *>(name)!=nullptr);
    auto *depthDock = window.findChild<QDockWidget *>("depthMapDock");
    auto *cameraDock = window.findChild<QDockWidget *>("rtspCameraDock");
    auto *cloudDock = window.findChild<QDockWidget *>("pointCloudDock");
    const auto usesDefaultLayout = [&] {
        if (!depthDock || !cameraDock || !cloudDock) return false;
        const auto depth = depthDock->geometry();
        const auto camera = cameraDock->geometry();
        const auto cloud = cloudDock->geometry();
        return depthDock->isVisible() && cameraDock->isVisible() && cloudDock->isVisible()
            && !depthDock->isFloating() && !cameraDock->isFloating() && !cloudDock->isFloating()
            && depth.left() == camera.left() && depth.right() == camera.right()
            && depth.bottom() < camera.top() && depth.right() < cloud.left()
            && depth.top() == cloud.top() && camera.bottom() == cloud.bottom()
            && depth.width() < cloud.width();
    };
    check("FIRST_RUN_DEPTH_AND_VIDEO_LEFT_POINT_CLOUD_RIGHT", usesDefaultLayout());
    check("FIRST_RUN_LANGUAGE_IS_SIMPLIFIED_CHINESE",
        QLocale().language() == QLocale::Chinese && QSettings().value("ui/language") == "en"
        && !window.findChild<QMenu *>("languageMenu")
        && depthDock && depthDock->windowTitle() == QStringLiteral("深度图")
        && cloudDock && cloudDock->windowTitle() == QStringLiteral("点云"));
    auto *depthColorMode = window.findChild<QComboBox *>("rvDepthColorMode");
    auto *cloudColorMode = window.findChild<QComboBox *>("rvPointCloudColorMode");
    check("FIRST_RUN_BOTH_DISPLAYS_USE_DYNAMIC_HISTOGRAM",
        depthColorMode && cloudColorMode
        && depthColorMode->currentText() == QStringLiteral("动态＋直方图均衡")
        && cloudColorMode->currentText() == depthColorMode->currentText());
    check("FIRST_RUN_LAYOUT_IMAGE_SAVED", window.grab().save(path+".png"));
    auto *models=window.findChild<QComboBox *>("rvDepthModel");
    const QString ordinary = "board_native_tof48";
    const QString highAccuracy = "board_native_tof48_high_accuracy";
    auto *session = window.session();
    const auto submitDepth = [&](QByteArray samples, const QJsonObject &metadata) {
        CxwlDepthFrame frame{};
        frame.size = sizeof(frame);
        frame.stream = CXWL_DEPTH_STREAM_DEPTH;
        frame.coordinate = CXWL_DEPTH_COORDINATE_SENSOR_RADIAL;
        constexpr uint32_t rowBytes = CXWL_DEPTH_WIDTH * sizeof(float);
        constexpr uint32_t stride = rowBytes + 8;
        QByteArray padded(CXWL_DEPTH_HEIGHT * stride - 8, char(0xff));
        for (uint32_t row = 0; row < CXWL_DEPTH_HEIGHT; ++row)
            std::memcpy(padded.data() + row * stride, samples.constData() + row * rowBytes, rowBytes);
        frame.image = {padded.constData(), uint64_t(padded.size()), CXWL_DEPTH_WIDTH, CXWL_DEPTH_HEIGHT,
                       stride, CXWL_DEPTH_PIXEL_F32_LE};
        const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
        frame.metadata_json_utf8 = json.constData();
        DepthSdkClient::receiveFrame(session, &frame);
        if (!session->m_depth.isEmpty()) {
            std::array<CxwlDepthPoint, CXWL_DEPTH_WIDTH * CXWL_DEPTH_HEIGHT> points{};
            // A planar XYZ fixture supplied independently of the radial distance values.
            for (int row = 0; row < CXWL_DEPTH_HEIGHT; ++row) {
                for (int col = 0; col < CXWL_DEPTH_WIDTH; ++col) {
                    const int index = row * CXWL_DEPTH_WIDTH + col;
                    if (qFromLittleEndian<quint32>(samples.constData() + 4 * index))
                        points[index] = {(col - 24) / 36.f, (row - 16) / 32.f, 1.f};
                }
            }
            auto cloud = frame;
            cloud.stream = CXWL_DEPTH_STREAM_POINT_CLOUD;
            cloud.coordinate = CXWL_DEPTH_COORDINATE_SENSOR_XYZ;
            cloud.image = {points.data(), sizeof(points), CXWL_DEPTH_WIDTH, CXWL_DEPTH_HEIGHT,
                           CXWL_DEPTH_WIDTH * sizeof(CxwlDepthPoint), CXWL_DEPTH_PIXEL_XYZ_F32_LE};
            session->acceptPointCloud(cloud);
        }
        padded.fill(0);
        session->dispatch();
    };
    const auto submitVideo = [&](const QImage &source) {
        CxwlDepthFrame frame{};
        frame.size = sizeof(frame);
        frame.stream = CXWL_DEPTH_STREAM_VIDEO;
        QImage rgb = source.convertToFormat(QImage::Format_RGB888);
        frame.image = {rgb.constBits(), uint64_t(rgb.sizeInBytes()), uint32_t(rgb.width()), uint32_t(rgb.height()),
                       uint32_t(rgb.bytesPerLine()), CXWL_DEPTH_PIXEL_RGB8};
        DepthSdkClient::receiveFrame(session, &frame);
        rgb.fill(Qt::white);
        session->dispatch();
    };
    check("EXACT_IMAGING_SCHEMES", models && models->count() == 2
        && models->itemData(0) == ordinary && models->itemData(1) == highAccuracy);
    check("FIRST_RUN_ORDINARY_NATIVE_MODE", session->currentImagingScheme() == ordinary
        && models && models->currentData() == ordinary && session->m_mode == CXWL_DEPTH_BOARD);
    const auto initialPresentation = ImageDisplaySettings::shared();
    check("FIRST_RUN_PROFILE", initialPresentation->rotationDegrees() == 0 && initialPresentation->blackOutsideRange()
        && initialPresentation->colorMinimum() == .01 && initialPresentation->colorMaximum() == 3);
    check("STARTUP_CAPTURE_STOPPED",session->userStopped());
    RvDeviceStore initialDevices;
    check("FIRST_RUN_HAS_NO_TRUSTED_OR_ACTIVE_DEVICE",initialDevices.profiles().isEmpty()
        &&initialDevices.activeId().isEmpty() && session->savedDevices().isEmpty());
    session->scanUsb();
    check("FIRST_RUN_NO_HISTORICAL_SSH_PROBE",session->userStopped()&&!session->busy());
    // The remaining tests exercise the controls for an explicitly enrolled device.
    const auto fixture=offlineRvDevice();
    initialDevices.save({fixture},fixture.id);
    session->selectDevice(session->savedDevices().first().toObject()["id"].toString());
    session->dispatch();
    for (const auto *retired : {"host_unidepth_rgb672_vga_fp16_tof48_b", "host_unidepth_rgb672_vga_fp16_tof48_g",
        "host_unidepth_rgb672_vga_fp16_tof16", "board_litesr_near_384x256_host_contour_hd", "board_tiny_nv12_128x80", "unknown"}) {
        check(QString("RETIRED_SCHEME_ABSENT_") + retired, models->findData(retired) == -1);
        session->selectImagingScheme(retired);
        check(QString("RETIRED_SCHEME_API_REJECTED_") + retired,
            session->currentImagingScheme() == ordinary && session->userStopped());
        QSettings().setValue("depth/imagingScheme", retired);
        DepthSdkClient reopened(initialDevices.path());
        check(QString("RETIRED_SCHEME_MIGRATES_TO_ORDINARY_") + retired,
            reopened.currentImagingScheme() == ordinary && reopened.userStopped()
                && QSettings().value("depth/imagingScheme") == ordinary);
    }
    for (const auto &scheme : {highAccuracy, ordinary}) {
        session->selectImagingScheme(scheme);
        DepthSdkClient reopened(initialDevices.path());
        check("NATIVE_MODE_SELECTS_AND_PERSISTS_" + scheme, session->userStopped() && !session->busy()
            && session->currentImagingScheme() == scheme && models->currentData() == scheme
            && reopened.currentImagingScheme() == scheme && reopened.userStopped()
            && window.depth()->selectedWidth() == 48 && window.depth()->selectedHeight() == 32);
    }
    const std::pair<QString, QString> sdkOnlySchemes[] = {
        {"board_native_tof8", ordinary}, {"board_native_tof8_high_accuracy", highAccuracy},
        {"board_native_tof16", ordinary}, {"board_native_tof16_high_accuracy", highAccuracy},
    };
    for (const auto &[saved, expected] : sdkOnlySchemes) {
        session->selectImagingScheme(saved);
        check("SDK_ONLY_MODE_UNAVAILABLE_IN_VIEWER_" + saved,
            models && models->findData(saved) == -1 && session->currentImagingScheme() == ordinary
            && session->userStopped());
        QSettings().setValue("depth/imagingScheme", saved);
        DepthSdkClient reopened(initialDevices.path());
        check("SAVED_VIEWER_MODE_KEEPS_ACCURACY_" + saved, reopened.currentImagingScheme() == expected
            && QSettings().value("depth/imagingScheme") == expected && reopened.userStopped());
    }
    QSettings().setValue("depth/imagingScheme", ordinary);
    {
        auto *fixedRange = window.findChild<QPushButton *>("rvColorRangeApply");
        const auto values = std::vector<float>{0.f, .5f, 1.f};
        const PointCloudColorScale::DynamicHistogram histogram(values, [](float v) { return v; });
        QByteArray raw(48 * 32 * 4, '\0');
        qToLittleEndian<quint32>(0x3f000000, raw.data() + 4);
        qToLittleEndian<quint32>(0x3f800000, raw.data() + 8);
        window.depth()->acceptRvDepth(raw, {{"width",48},{"height",32},{"depth_unit","sensor_radial_m_float32"},
                                         {"frame_id",7},{"generation",3}});
        if (depthColorMode) depthColorMode->setCurrentIndex(0);
        const auto fixed = window.depth()->snapshot();
        if (depthColorMode) depthColorMode->setCurrentIndex(1);
        const auto dynamic = window.depth()->snapshot();
        check("DYNAMIC_DEPTH_COLOR_IS_PERSISTED_AND_PRESERVES_METRES", depthColorMode && depthColorMode->count() == 2
            && QSettings().value("depth/colorMode").toString() == "dynamic" && fixedRange && fixedRange->isHidden()
            && !dynamic.depthImage.isNull() && dynamic.depthImage.pixel(0, 0) == qRgb(0, 0, 0)
            && dynamic.depthImage.pixel(1, 0) == histogram.color(.5) && dynamic.depthImage.pixel(2, 0) == histogram.color(1.)
            && fixed.depthImage != dynamic.depthImage && dynamic.metresFloat32LE == raw
            && dynamic.metadata["display_color_mode"] == "dynamic");
        if (depthColorMode) depthColorMode->setCurrentIndex(0);
        DepthMapWidget reopened;
        const auto *reopenedColor = reopened.findChild<QComboBox *>("rvDepthColorMode");
        check("DEPTH_COLOR_MODE_RESTORES_EXPLICIT_FIXED_SELECTION", reopenedColor && reopenedColor->currentIndex() == 0);
        window.depth()->clearRvDepth(QString());
    }
    {
        if (cloudDock) {
            auto *view = cloudDock->findChild<RvPointCloudCanvas *>("rvPointCloudView");
            auto *points = view;
            auto *geometry = view;
            auto *mode = cloudDock->findChild<QComboBox *>("rvPointCloudDisplayMode");
            auto *angle = cloudDock->findChild<QPushButton *>("rvPointCloudAngle");
            auto *status = cloudDock->findChild<QLabel *>("rvPointCloudStatus");
            check("POINT_CLOUD_SCENE_LOADS", view && view->isVisible() && points && geometry && mode);
            check("POINT_CLOUD_ACCUMULATION_CONTROL_REMOVED", !cloudDock->findChild<QPushButton *>("rvPointCloudAccumulate"));
            if (cloudColorMode) {
                cloudColorMode->setCurrentIndex(0);
                RvPointCloudWidget reopened(session);
                const auto *reopenedColor = reopened.findChild<QComboBox *>("rvPointCloudColorMode");
                check("POINT_CLOUD_COLOR_MODE_RESTORES_EXPLICIT_FIXED_SELECTION", reopenedColor && reopenedColor->currentIndex() == 0);
                cloudColorMode->setCurrentIndex(1);
            }
            if (view && view && points && geometry && mode) {
                auto *scene = view;
                check("POINT_CLOUD_ANGLE_DEFAULT_CLOCKWISE_90", angle && angle->text() == QStringLiteral("输入角度（90°）")
                    && scene->property("inputAngleDegrees").toInt() == 90);
                QByteArray depth(48 * 32 * 4, '\0');
                for (int i = 0; i < 48 * 32 - 1; ++i) qToLittleEndian<quint32>(0x3f800000, depth.data() + i * 4);
                const QString capture(32, 'a');
                QJsonObject metadata{{"width",48},{"height",32},{"depth_unit","sensor_radial_m_float32"},
                    {"source_generation",capture},{"generation",1},{"frame_id",1},
                    {"tof_calibration",QJsonObject{{"schema",1},{"run_id","dtof_"+capture},
                        {"sensor_fov_correction",6},{"processing_fov_correction",6},{"temporary_calibration",false}}}};
                for (const auto &scheme : {ordinary, highAccuracy}) {
                    session->selectImagingScheme(scheme);
                    metadata["candidate_mode"] = scheme == ordinary ? 24 : 25;
                    mode->setCurrentIndex(0);
                    submitDepth(depth, metadata);
                    const QVector3D corner = QVector3D(500.f, 2000.f / 3.f, -1000.f);
                    constexpr int opticalAxis = 16 * 48 + 24;
                    check("POINT_CLOUD_BOARD_XYZ_DISPLAY_" + scheme, points->pointCount() == 1535
                        && (points->pointPosition(0) - corner).length() < .01f
                        && points->pointPosition(opticalAxis) == QVector3D(0.f, 0.f, -1000.f));
                    check("POINT_CLOUD_ACTIVE_TOOLBAR_OMITS_COUNT_" + scheme, status && status->isHidden() && status->text().isEmpty());
                    if (scheme == ordinary) view->grabFramebuffer().save(path + ".pointcloud-points.png");
                    mode->setCurrentIndex(1);
                    check("POINT_CLOUD_MESH_" + scheme, points->pointCount() == 0
                        && geometry->meshIndexCount() > 0 && view->property("meshVisible").toBool());
                    if (scheme == ordinary) view->grabFramebuffer().save(path + ".pointcloud-mesh.png");
                    session->stop();
                    check("POINT_CLOUD_FRAME_REVOKED_" + scheme, geometry->meshIndexCount() == 0 && points->pointCount() == 0);
                }
                mode->setCurrentIndex(0);
                submitDepth(depth, metadata);
                depth.fill('\0');
                for (int row = 11; row <= 13; ++row)
                    for (int col = 29; col <= 31; ++col)
                        qToLittleEndian<quint32>(0x3f800000, depth.data() + (row * 48 + col) * 4);
                metadata["frame_id"] = 2;
                submitDepth(depth, metadata);
                check("POINT_CLOUD_NEW_FRAME_REPLACES_PREVIOUS_POINTS", points->pointCount() == 9);

                // Render the same off-axis patch twice without submitting another depth frame.
                const auto previousYaw = scene->property("orbitYaw");
                const auto previousPitch = scene->property("orbitPitch");
                scene->setProperty("orbitYaw", 0.);
                scene->setProperty("orbitPitch", 0.);
                const float cameraZ = scene->property("cameraTarget").value<QVector3D>().z()
                    + scene->property("cameraDistance").toFloat();
                const float centreZ = 1000.f;
                const QVector3D unturned(centreZ / 6.f, centreZ / 8.f, -centreZ);
                const QVector3D turned(unturned.y(), -unturned.x(), unturned.z());
                const auto hasPatch = [cameraZ](const QImage &frame, const QVector3D &position) {
                    if (frame.isNull()) return false;
                    const double verticalScale = 2. * std::tan(std::acos(-1.) / 8.) * (cameraZ - position.z());
                    const double pixelsPerMm = frame.height() / verticalScale;
                    const QPoint projected(qRound(frame.width() / 2. + position.x() * pixelsPerMm),
                                           qRound(frame.height() / 2. - position.y() * pixelsPerMm));
                    const QRect area = QRect(projected - QPoint(3, 3), QSize(7, 7)).intersected(frame.rect());
                    const auto background = frame.pixel(0, 0);
                    for (int y = area.top(); y <= area.bottom(); ++y)
                        for (int x = area.left(); x <= area.right(); ++x) {
                            const auto pixel = frame.pixel(x, y);
                            if (std::max({std::abs(qRed(pixel) - qRed(background)),
                                          std::abs(qGreen(pixel) - qGreen(background)),
                                          std::abs(qBlue(pixel) - qBlue(background))}) > 32)
                                return true;
                        }
                    return false;
                };
                const auto checkRenderedPatch = [&](int drawMode, int degrees, const QVector3D &expected, const QVector3D &previous) {
                    const bool applied = chooseInputAngle(angle, "rvPointCloudAngleDialog", degrees, true);
                    settle(40);
                    const auto frame = view->grabFramebuffer();
                    check(QString("POINT_CLOUD_PIXELS_%1_%2").arg(drawMode == 0 ? "POINTS" : "MESH").arg(degrees),
                        applied && hasPatch(frame, expected) && !hasPatch(frame, previous));
                };
                for (int drawMode : {0, 1}) {
                    mode->setCurrentIndex(drawMode);
                    checkRenderedPatch(drawMode, 0, unturned, turned);
                    checkRenderedPatch(drawMode, 90, turned, unturned);
                }
                scene->setProperty("orbitYaw", previousYaw);
                scene->setProperty("orbitPitch", previousPitch);
                mode->setCurrentIndex(0);
                const int depthAngle = window.depth()->nativeTofRotationDegrees();
                const int cameraAngle = window.depth()->displayRotationDegrees();
                const double radians = 137. * std::acos(-1.) / 180.;
                const float cosine = float(std::cos(radians));
                const float sine = float(std::sin(radians));
                const QVector3D clockwise137(unturned.x() * cosine + unturned.y() * sine,
                    unturned.y() * cosine - unturned.x() * sine, unturned.z());
                check("POINT_CLOUD_ANGLE_UPDATES_CURRENT_FRAME", chooseInputAngle(angle, "rvPointCloudAngleDialog", 137, true)
                    && angle->text() == QStringLiteral("输入角度（137°）") && points->pointCount() == 9
                    && (points->pointPosition(4) - clockwise137).length() < .01f);
                check("POINT_CLOUD_ANGLE_LEAVES_DEPTH_AND_VIDEO_ANGLES", window.depth()->nativeTofRotationDegrees() == depthAngle
                    && window.depth()->displayRotationDegrees() == cameraAngle);
                check("POINT_CLOUD_ANGLE_CANCEL_KEEPS_VALUE", chooseInputAngle(angle, "rvPointCloudAngleDialog", 0, false)
                    && scene->property("inputAngleDegrees").toInt() == 137);
                {
                    RvPointCloudWidget reopened(session);
                    const auto *reopenedAngle = reopened.findChild<QPushButton *>("rvPointCloudAngle");
                    check("POINT_CLOUD_ANGLE_RESTORES_ON_REOPEN", reopenedAngle
                        && reopenedAngle->text() == QStringLiteral("输入角度（137°）")
                        && QSettings().value("pointCloud/inputAngleDegrees").toInt() == 137);
                }
                check("POINT_CLOUD_RASTER_FRAMEBUFFER", !view->grabFramebuffer().isNull());
                view->setProperty("orbitYaw", 32.0);
                cloudDock->setFloating(true);
                settle(140);
                cloudDock->setFloating(false);
                settle(140);
                check("POINT_CLOUD_VIEW_SURVIVES_REDOCK", view
                    && view->property("orbitYaw").toDouble() == 32.0
                    && view->property("inputAngleDegrees").toInt() == 137 && points->pointCount() == 9);
                const auto yawBefore = view->property("orbitYaw").toDouble();
                const QPointF local(100, 100), moved(120, 115);
                const auto sendDrag = [&](Qt::MouseButton button) {
                    QMouseEvent down(QEvent::MouseButtonPress, local, view->mapToGlobal(local.toPoint()), button, button, Qt::NoModifier);
                    QMouseEvent move(QEvent::MouseMove, moved, view->mapToGlobal(moved.toPoint()), Qt::NoButton, button, Qt::NoModifier);
                    QMouseEvent up(QEvent::MouseButtonRelease, moved, view->mapToGlobal(moved.toPoint()), button, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(view, &down); QApplication::sendEvent(view, &move); QApplication::sendEvent(view, &up);
                };
                sendDrag(Qt::LeftButton);
                check("POINT_CLOUD_MOUSE_ORBIT", view->property("orbitYaw").toDouble() != yawBefore);
                const auto targetBefore = view->property("cameraTarget").value<QVector3D>();
                sendDrag(Qt::MiddleButton);
                check("POINT_CLOUD_MOUSE_PAN", view->property("cameraTarget").value<QVector3D>() != targetBefore);
                const double distanceBefore = view->property("cameraDistance").toDouble();
                QWheelEvent wheel(local, view->mapToGlobal(local.toPoint()), QPoint(), QPoint(0,120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(view, &wheel);
                check("POINT_CLOUD_MOUSE_ZOOM", view->property("cameraDistance").toDouble() < distanceBefore);
                view->resetView();
                check("POINT_CLOUD_RESET_VIEW", view->property("cameraTarget").value<QVector3D>() == QVector3D(0,0,-600)
                    && view->property("cameraDistance").toDouble() == 900 && view->property("orbitYaw").toDouble() == -25
                    && view->property("orbitPitch").toDouble() == -18);
                auto *record = cloudDock->findChild<QPushButton *>("rvPointCloudRecord");
                auto *recordStatus = cloudDock->findChild<QLabel *>("rvPointCloudRecordStatus");
                auto *withDepth = cloudDock->findChild<QCheckBox *>("rvPointCloudRecordDepth");
                auto *withRgb = cloudDock->findChild<QCheckBox *>("rvPointCloudRecordRgb");
                auto &recorder = session->pointCloudRecorder();
                check("POINT_CLOUD_RECORD_AVAILABLE", record && record->isEnabled() && recordStatus && withDepth && withRgb
                    && !withDepth->isChecked() && !withRgb->isChecked());
                if (record && recordStatus && withDepth && withRgb) {
                    // Drive the directory picker through Qt in the unattended test.
                    const bool widgetsOnly = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
                    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
                    const auto directoryKey = QStringLiteral("pointCloud/recordDirectory");
                    const auto previousDirectory = QSettings().value(directoryKey);
                    record->click();
                    auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                    check("POINT_CLOUD_RECORD_USES_PLATFORM_FOLDER_PICKER", picker && picker->fileMode() == QFileDialog::Directory
                        && picker->testOption(QFileDialog::ShowDirsOnly) && !picker->testOption(QFileDialog::DontUseNativeDialog));
                    if (picker) {
                        picker->selectFile(QDir::tempPath());
                        picker->reject();
                    }
                    check("POINT_CLOUD_RECORD_CANCEL", picker && recorder.state() == PointCloudRecorder::State::Idle
                        && QSettings().value(directoryKey) == previousDirectory);
                    QImage video(17, 13, QImage::Format_RGB888);
                    video.fill(Qt::red);
                    for (int options = 0; options < 4; ++options) {
                        QTemporaryDir recordings;
                        const bool saveDepth = options & 1, saveRgb = options & 2;
                        withDepth->setChecked(saveDepth);
                        withRgb->setChecked(saveRgb);
                        record->click();
                        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                            dialog->selectFile(recordings.path());
                            static_cast<QDialog *>(dialog)->accept();
                        }
                        const auto prefix = QStringLiteral("POINT_CLOUD_RECORD_%1_").arg(options);
                        const bool started = recorder.state() == PointCloudRecorder::State::Recording;
                        check(prefix + "STARTS", started && !withDepth->isEnabled() && !withRgb->isEnabled()
                            && QSettings().value(directoryKey).toString() == recordings.path());
                        if (!started) continue;
                        const QByteArray emptyGrid(depth.size(), '\0');
                        submitDepth(depth, metadata);
                        submitVideo(video);
                        submitVideo(video);
                        submitDepth(emptyGrid, metadata);
                        submitVideo(video);
                        record->click();
                        QElapsedTimer timer; timer.start();
                        while (recorder.state() != PointCloudRecorder::State::Idle && timer.elapsed() < 5000) settle(10);
                        const auto folders = QDir(recordings.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                        const QDir take(recordings.filePath(folders.value(0)));
                        const auto contents = [&](const QString &name) {
                            QFile file(take.filePath(name));
                            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
                        };
                        check(prefix + "SAVES", recordings.isValid() && folders.size() == 1 && !folders.first().endsWith(".partial")
                            && take.entryList({"*.ply"}, QDir::Files).size() == 2 && withDepth->isEnabled() && withRgb->isEnabled()
                            && recordStatus->text().contains(QDir::toNativeSeparators(take.path()))
                            && contents("frame-00000002.ply").contains("element vertex 0\n"));
                        check(prefix + "DEPTH", take.exists("depth") == saveDepth && (!saveDepth ||
                            (contents("depth/frame-00000001.f32") == depth && contents("depth/frame-00000002.f32") == emptyGrid)));
                        const auto rows = contents("frames.csv").trimmed().split('\n');
                        QImage saved;
                        if (saveRgb) saved.load(take.filePath("rgb/frame-00000003.jpg"));
                        check(prefix + "RGB", take.exists("rgb") == saveRgb && take.exists("frames.csv") == saveRgb
                            && (!saveRgb || (saved.size() == video.size() && saved.pixelColor(0, 0).red() > 240
                                && saved.pixelColor(0, 0).blue() < 16 && rows.size() == 6
                                && rows[1].startsWith("frame-00000001.ply,") && rows[2].startsWith("rgb/frame-00000001.jpg,")
                                && rows[4].startsWith("frame-00000002.ply,") && rows[5].startsWith("rgb/frame-00000003.jpg,"))));
                    }
                    QTemporaryDir empty;
                    const bool started = recorder.startRecording(empty.path(), true, true).isEmpty();
                    recorder.stopRecording();
                    QElapsedTimer timer; timer.start();
                    while (recorder.state() != PointCloudRecorder::State::Idle && timer.elapsed() < 5000) settle(10);
                    check("POINT_CLOUD_RECORD_EMPTY", started && QDir(empty.path()).isEmpty());
                    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, widgetsOnly);
                }
                session->stop();
                check("POINT_CLOUD_FRAME_REVOKED_ON_STOP", points->pointCount() == 0);
                check("POINT_CLOUD_RECORD_DISABLED_WHEN_STOPPED", record && !record->isEnabled());
                session->selectImagingScheme(ordinary);
            }
        }
    }
    auto *depthStart=window.findChild<QPushButton *>("rvDepthStart");
    auto *depthStop=window.findChild<QPushButton *>("rvDepthStop");
    auto *depthReconnect=window.findChild<QPushButton *>("rvDepthReconnect");
    auto *cameraStart=window.findChild<QPushButton *>("rtspCameraStart");
    auto *cameraStop=window.findChild<QPushButton *>("rtspCameraStop");
    auto *cameraReconnect=window.findChild<QPushButton *>("rtspCameraReconnect");
    const bool controlsPresent=depthStart&&depthStop&&depthReconnect&&cameraStart&&cameraStop&&cameraReconnect;
    check("SHARED_CONTROLS_IDLE",controlsPresent&&depthStart->isEnabled()&&cameraStart->isEnabled()
        &&!depthStop->isEnabled()&&!cameraStop->isEnabled()&&depthReconnect->isEnabled()&&cameraReconnect->isEnabled());
    if(controlsPresent){
        if(qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")=="1"){
            using Status = DepthSdkClient::CaptureStatus;
            auto *overlay = window.findChild<QWidget *>("rvImagingSwitchOverlay");
            auto *progress = window.findChild<QProgressBar *>("rvImagingSwitchProgress");
            auto *progressTitle = window.findChild<QLabel *>("rvImagingProgressTitle");
            auto *progressDetail = window.findChild<QLabel *>("rvImagingProgressDetail");
            auto *cameraOverlay = window.findChild<QWidget *>("rtspCameraCaptureOverlay");
            auto *cameraProgress = window.findChild<QProgressBar *>("rtspCameraCaptureProgress");
            auto *cameraMessage = window.findChild<QLabel *>("rtspCameraCaptureMessage");
            auto *fps = window.findChild<QLabel *>("depthFpsLabel");
            check("CAPTURE_PROGRESS_HIDDEN_WHEN_IDLE",overlay && overlay->isHidden()
                && cameraOverlay && cameraOverlay->isHidden());
            depthStart->click();
            cameraReconnect->click();
            session->scanUsb();
            check("CAPTURE_AND_USB_CHECK_BLOCKED_OFFLINE",session->userStopped()&&!session->busy()&&!session->automaticConnecting());
            check("CAPTURE_FAILURE_REPLACES_PROGRESS", session->captureStatus() == Status::Failed
                && overlay && overlay->isVisible() && progress && progress->isHidden()
                && cameraOverlay && cameraOverlay->isVisible() && cameraProgress && cameraProgress->isHidden());
            const auto *copyDiagnostics = window.findChild<QPushButton *>("copyDiagnosticsButton");
            const auto diagnostics = session->captureDiagnostics();
            const auto failure = diagnostics.value("last_capture_failure").toObject();
            check("COPY_CAPTURE_DIAGNOSTICS", copyDiagnostics && copyDiagnostics->isVisible()
                && copyDiagnostics->text() == QStringLiteral("复制诊断")
                && diagnostics["sdk_version"] == CXWL_DEPTH_VERSION
                && diagnostics["abi_version"].toInt() == CXWL_DEPTH_ABI_VERSION
                && diagnostics["device_id"] == session->activeDeviceId()
                && failure["result"].toInt() == CXWL_DEPTH_UNAVAILABLE && failure["connection_state"] == "stopped");
            const auto setStage = [&](int stage) {
                session->m_status = Status::Starting;
                session->m_capture = {{"connection_stage", stage}, {"connection_state", "connecting"}, {"busy", true}};
                emit session->stateChanged();
            };
            if (overlay && progress && progressTitle && progressDetail && cameraOverlay && cameraProgress && cameraMessage) {
                setStage(CXWL_DEPTH_STAGE_CHECKING);
                check("STARTING_CAPTURE_SHARED_CONTROLS",overlay->isVisible() && cameraOverlay->isVisible()
                    && progress->minimum()==0 && progress->maximum()==0 && !progress->isTextVisible()
                    && cameraProgress->minimum()==0 && cameraProgress->maximum()==0 && cameraProgress->isVisible()
                    && progressTitle->text()==QStringLiteral("正在启动采集…") && progressTitle->text()==cameraMessage->text()
                    && !depthStart->isEnabled() && !cameraStart->isEnabled()
                    && !depthReconnect->isEnabled() && !cameraReconnect->isEnabled()
                    && depthStop->isEnabled() && cameraStop->isEnabled());
                const auto checkingRevision = session->stopRevision();
                if (fps) fps->setText("FPS: 15.0");
                cameraStop->click();
                check("STOP_IN_PROGRESS_BEFORE_COMPLETION", session->stopping() && !session->userStopped()
                    && overlay->isVisible() && progress->isVisible() && progressTitle->text()==QStringLiteral("正在停止…")
                    && cameraOverlay->isVisible() && cameraProgress->isVisible() && cameraMessage->text()==progressTitle->text()
                    && !depthStop->isEnabled() && !cameraStop->isEnabled()
                    && !depthStart->isEnabled() && !cameraStart->isEnabled());
                check("STOP_CLEARS_STALE_DEPTH_FPS_IMMEDIATELY", fps && fps->text()=="FPS: 0.0");
                settle(80);
                check("STOP_CONFIRMED_AFTER_CANCELLING_PENDING_START", session->stopRevision()>checkingRevision
                    && session->userStopped() && !session->busy()
                    && progress->isHidden() && progressTitle->text()==QStringLiteral("已停止采集") && overlay->isVisible()
                    && cameraProgress->isHidden() && cameraMessage->text()==progressTitle->text() && cameraOverlay->isVisible()
                    && depthStart->isEnabled() && cameraStart->isEnabled());

                // Manual start may have no USB stage while it waits for the first frames.
                setStage(CXWL_DEPTH_STAGE_IDLE);
                settle(40);
                check("MANUAL_START_PROGRESS_CHINESE_WITHOUT_USB_STAGE", overlay->isVisible()
                    && progressTitle->text()==QStringLiteral("正在启动采集…")
                    && progressDetail->text()==QStringLiteral("正在等待深度图和原视频帧…")
                    && cameraMessage->text()==progressTitle->text());
                const QJsonObject metadata{{"generation", 1},
                    {"frame_id", 1}, {"width", 48}, {"height", 32}, {"depth_unit", "sensor_radial_m_float32"}};
                QByteArray depth(48 * 32 * 4, '\0');
                qToLittleEndian<quint32>(0x3f800000, depth.data());
                QImage video(32, 24, QImage::Format_RGB888);
                video.fill(Qt::black);
                submitVideo(video);
                check("START_WAITS_FOR_BOTH_STREAMS", session->captureStatus()==Status::Starting
                    && overlay->isVisible() && cameraOverlay->isVisible());
                overlay->grab().save(path + ".auto-connect.png");
                submitDepth(depth, metadata);
                session->m_capture = {{"connection_state", "connected"}, {"busy", false}};
                session->dispatch();
                auto *canvas = static_cast<RvDepthView *>(window.findChild<QWidget *>("rvNativeDepthCanvas"));
                QElapsedTimer renderWait;
                renderWait.start();
                while (!overlay->isHidden() && renderWait.elapsed()<900) settle(10);
                check("CAPTURE_READY_AFTER_SDK_CONNECTS_AND_RENDERS", session->captureStatus()==Status::Running
                    && overlay->isHidden() && cameraOverlay->isHidden() && canvas && canvas->hasRenderableFrame()
                    && depthReconnect->isEnabled() && cameraReconnect->isEnabled());
                session->stop();
                settle(80);
                check("STOP_CONFIRMED_IN_CHINESE", progressTitle->text()==QStringLiteral("已停止采集")
                    && cameraMessage->text()==progressTitle->text());
                session->m_status = Status::Idle;
                emit session->stateChanged();
            }
        }
    }
    bool idle=true;for(auto *process:window.findChildren<QProcess *>())idle&=process->state()==QProcess::NotRunning;
    check("NO_WORKER_OR_HARDWARE_STARTED",idle);
    check("NO_EXTERNAL_CAMERA_CONTROL_PROCESS",window.findChildren<QProcess *>("rvTunerControlProcess").empty());
    check("CHINESE_WINDOW",window.windowTitle()==QStringLiteral("Cxwl Depth Camera（内测版）"));
    auto *about = window.menuBar()->findChild<QAction *>("aboutAction");
    check("CHINESE_ABOUT_MENU",about && window.menuBar()->actions().contains(about)
        && about->text()==QStringLiteral("关于"));
    if(about)about->trigger();
    auto *aboutDialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    bool aboutName=false, aboutVersion=false, aboutLogo=false;
    if(aboutDialog){
        for(auto *label:aboutDialog->findChildren<QLabel *>()){
            aboutName |= label->text()==QStringLiteral("Cxwl Depth Camera");
            aboutVersion |= label->text()==QStringLiteral("v%1 · 内测版").arg(QCoreApplication::applicationVersion());
            aboutLogo |= !label->pixmap().isNull();
        }
    }
    check("ABOUT_BRANDING",aboutDialog && aboutDialog->windowTitle()==QStringLiteral("关于")
        && aboutName && aboutVersion && aboutLogo && !window.windowIcon().isNull());
    if(aboutDialog)aboutDialog->reject();
    check("CHINESE_IMAGING_SCHEMES", models && models->count() == 2
        && models->itemText(0) == QStringLiteral("48×32 普通")
        && models->itemText(1) == QStringLiteral("48×32 高精度"));
    check("CHINESE_SHARED_CONTROL_LABELS",controlsPresent&&depthStart->text()==cameraStart->text()
        &&depthStop->text()==cameraStop->text()&&depthReconnect->text()==cameraReconnect->text());
    check("CHINESE_IMAGING_SCHEME_TOOLTIP",models&&models->toolTip()==QStringLiteral("为 48×32 深度图选择普通或高精度档。"));
    check("REMOVED_DEPTH_UI",!window.findChild<QPushButton *>("rvUniDepthCorrectionPoints")
        && !window.findChild<QPushButton *>("rvUniDepthDiagnostic")
        && !window.findChild<QPushButton *>("rvUniDepthDiagnosticSequence")
        && !window.findChild<QLabel *>("rvDepthInfo"));
    const auto *depthFps=window.findChild<QLabel *>("depthFpsLabel");
    const auto *depthSave=window.findChild<QPushButton *>("depthMapSave");
    check("DEPTH_FPS_AND_SAVE_PRESENT",depthFps&&depthSave&&depthSave->isVisible()
        && depthSave->parentWidget()==depthFps->parentWidget()
        && depthSave->text()==QString::fromUtf8("保存深度图"));
    check("REMOVED_IMAGING_ACTIONS",!window.findChild<QAction *>("rvDepthCoverageAction")
        && !window.findChild<QAction *>("rvStreamAddressAction")
        && !window.findChild<QAction *>("rvBlackOutsideRangeAction")
        && !window.findChild<QAction *>("rvTofProjectionAction") && window.menuBar()->actions().size()==4);
    auto *blackOutside=window.findChild<QCheckBox *>("depthBlackOutsideRange");
    check("BLACK_OUTSIDE_AT_RIGHT_OF_DEPTH_FOOTER",blackOutside&&blackOutside->isVisible()&&depthSave
        && blackOutside->parentWidget()==depthSave->parentWidget()&&blackOutside->x()>depthSave->x()
        && blackOutside->text()==QString::fromUtf8("超范围黑色"));
    if(blackOutside){
        const auto settings=ImageDisplaySettings::shared();
        const bool priorBlack=settings->blackOutsideRange();
        blackOutside->click();
        check("BLACK_OUTSIDE_CONTROL_UPDATES_SHARED_PALETTE",settings->blackOutsideRange()!=priorBlack
            && QSettings().value("imageDisplay/blackOutsideRange").toBool()==settings->blackOutsideRange());
        settings->setBlackOutsideRange(priorBlack);
    }
    auto *cameraInfo=window.findChild<QLabel *>("rtspCameraVideoInfo");
    auto *rotation=window.findChild<QComboBox *>("rvDisplayRotation");
    auto *cameraCanvas=window.findChild<QWidget *>("rtspCameraLatestFrame");
    check("ROTATION_ON_CAMERA_FOOTER_RIGHT",rotation&&cameraInfo
        && rotation->parentWidget()==cameraInfo->parentWidget()&&rotation->x()>cameraInfo->x()
        && rotation->count()==4&&!window.findChild<QMenu *>("rvImagingSettingsMenu"));
    if(rotation&&cameraCanvas){
        const int priorRotation=window.depth()->displayRotationDegrees();
        const int selected=(priorRotation+90)%360;
        rotation->setCurrentIndex(rotation->findData(selected));
        check("ROTATION_STILL_LINKS_DEPTH_AND_CAMERA",window.depth()->displayRotationDegrees()==selected
            && cameraCanvas->property("displayRotationDegrees").toInt()==selected);
        rotation->setCurrentIndex(rotation->findData(priorRotation));
    }
    {
        auto *angleButton=window.depth()->findChild<QPushButton*>("rvNativeTofAngle");
        auto *native=static_cast<RvDepthView*>(window.depth()->findChild<QWidget*>("rvNativeDepthCanvas"));
        const auto priorScheme=session->currentImagingScheme();
        const auto priorGlobal=window.depth()->displayRotationDegrees();
        check("NATIVE_ANGLE_DEFAULT_90",angleButton&&native&&!angleButton->isHidden()
            &&window.depth()->nativeTofRotationDegrees()==90&&native->nativeDisplayRotationDegrees()==90);
        session->selectImagingScheme("board_native_tof48");settle(20);
        check("NATIVE_ANGLE_VISIBLE_FOR_ORDINARY",angleButton&&!angleButton->isHidden());
        QByteArray raw(48*32*4,0);
        for(int i=0;i<48*32;++i){float value=.2f+.001f*i;quint32 bits;std::memcpy(&bits,&value,4);qToLittleEndian(bits,raw.data()+4*i);}
        window.depth()->acceptRvDepth(raw,{{"width",48},{"height",32},{"depth_unit","sensor_radial_m_float32"},{"frame_id",123},{"generation",456}});
        settle(80);
        const auto frozen=window.depth()->snapshot();
        const auto chooseAngle=[&](int degrees,bool accept){
            return chooseInputAngle(angleButton, "rvNativeTofAngleDialog", degrees, accept);
        };
        bool geometry=true,readout=true;
        const auto checkMirroredView=[&] {
            const auto rendered=native->grab().toImage();
            const double radians=((window.depth()->nativeTofRotationDegrees()+window.depth()->displayRotationDegrees())%360)*std::acos(-1.)/180.;
            const double c=std::cos(radians),s=std::sin(radians);
            const auto area=QRectF(native->rect()).adjusted(8,8,-8,-8);
            const double scale=std::min(area.width()/(48*std::abs(c)+32*std::abs(s)),area.height()/(48*std::abs(s)+32*std::abs(c)));
            // Check asymmetric source locations after screen-horizontal mirroring.
            for(const QPoint index:{QPoint(5,7),QPoint(39,21)}){
                const QPointF source(index.x()+.5-24,index.y()+.5-16);
                const QPointF point=area.center()+QPointF(-(c*source.x()-s*source.y()),s*source.x()+c*source.y())*scale;
                geometry&=rendered.pixelColor((point*rendered.devicePixelRatio()).toPoint())==QColor(native->colorAt(index.x(),index.y()));
                QMouseEvent hover(QEvent::MouseMove,point,native->mapToGlobal(point.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(native,&hover);
                const float expected=.2f+.001f*(index.y()*48+index.x());
                readout&=native->property("lastHoverText").toString()==QStringLiteral("（%1，%2）径向距离 %3 米")
                    .arg(index.x()).arg(index.y()).arg(expected,0,'f',4);
            }
        };
        for(int angle:{90,37,180,270,0}){
            check(QString("NATIVE_ANGLE_DIALOG_ACCEPTS_%1").arg(angle),chooseAngle(angle,true));
            for(int global:{0,90,180,270}){
                window.depth()->setDisplayRotationDegrees(global);checkMirroredView();
            }
            window.depth()->setDisplayRotationDegrees(priorGlobal);
            const auto current=window.depth()->snapshot();
            check(QString("NATIVE_ANGLE_PRESERVES_DATA_%1").arg(angle),current.metresFloat32LE==raw
                &&current.depthImage==frozen.depthImage&&current.metadata["native_tof_display_rotation_degrees"]==angle
                &&current.metadata["native_tof_display_mirror_horizontal"]==true
                &&window.depth()->displayRotationDegrees()==priorGlobal);
        }
        check("NATIVE_MIRROR_AFTER_ROTATION_PIXEL_GEOMETRY",geometry);
        check("NATIVE_MIRROR_HOVER_RETURNS_ORIGINAL_SAMPLE",readout);
        check("NATIVE_ANGLE_CANCEL_KEEPS_VALUE",chooseAngle(123,false)&&window.depth()->nativeTofRotationDegrees()==0);
        chooseAngle(137,true);
        session->selectImagingScheme("board_native_tof48_high_accuracy");settle(20);
        check("NATIVE_ANGLE_SHARED_BY_BOTH_MODES",!angleButton->isHidden()&&native->nativeDisplayRotationDegrees()==137);
        {DepthMapWidget reopened;check("NATIVE_ANGLE_SAVED_ON_REOPEN",reopened.nativeTofRotationDegrees()==137
            &&QSettings().value("depth/nativeTofRotationDegrees").toInt()==137);}
        chooseAngle(90,true);
        const auto *cloudAngleButton = window.findChild<QPushButton *>("rvPointCloudAngle");
        check("DEPTH_ANGLE_LEAVES_POINT_CLOUD_ANGLE", cloudAngleButton
            && cloudAngleButton->text() == QStringLiteral("输入角度（137°）")
            && QSettings().value("pointCloud/inputAngleDegrees").toInt() == 137);
        window.depth()->acceptRvDepth(raw,{{"width",48},{"height",32},{"depth_unit","sensor_radial_m_float32"},{"frame_id",124},{"generation",457}});
        geometry=true;readout=true;
        for(int global:{0,90,180,270}){window.depth()->setDisplayRotationDegrees(global);checkMirroredView();}
        window.depth()->setDisplayRotationDegrees(priorGlobal);
        const auto highSnapshot=window.depth()->snapshot();
        check("NATIVE_HIGH_ACCURACY_MIRROR_AND_RAW_SNAPSHOT",geometry&&readout
            &&highSnapshot.metresFloat32LE==raw&&highSnapshot.depthImage==frozen.depthImage
            &&highSnapshot.metadata["native_tof_display_mirror_horizontal"]==true);
        settle(80);window.depth()->grab().save(path+".native-tof-angle.png");
        window.depth()->clearRvDepth(QString());session->selectImagingScheme(priorScheme);
    }
    auto *minimum=window.findChild<QDoubleSpinBox *>("rvColorMinimum");
    auto *maximum=window.findChild<QDoubleSpinBox *>("rvColorMaximum");
    auto *applyRange=window.findChild<QPushButton *>("rvColorRangeApply");
    check("COLOR_RANGE_ON_DEPTH_FOOTER",minimum&&maximum&&applyRange&&depthFps&&blackOutside
        && depthFps->parentWidget()->isAncestorOf(minimum)&&depthFps->parentWidget()->isAncestorOf(maximum)
        && applyRange->parentWidget()==depthFps->parentWidget()
        && applyRange->text()==QString::fromUtf8("应用"));
    if(minimum&&maximum&&applyRange){
        const auto settings=ImageDisplaySettings::shared();
        const double oldMin=settings->colorMinimum(),oldMax=settings->colorMaximum();
        QString invalidText="1e1";
        int cursor=invalidText.size();
        check("COLOR_RANGE_REJECTS_NON_DECIMAL_AND_OUTSIDE_LIMITS",minimum->validate(invalidText,cursor)==QValidator::Invalid
            && minimum->minimum()==.01&&maximum->maximum()==11
            && !settings->setColorRange(0,11)&&!settings->setColorRange(.01,11.01));
        maximum->setValue(1);
        minimum->setValue(2);
        check("COLOR_RANGE_DISALLOWS_INVERTED_VALUES",!applyRange->isEnabled()
            && settings->colorMinimum()==oldMin&&settings->colorMaximum()==oldMax);
        minimum->setValue(.01);
        maximum->setValue(11);
        check("COLOR_RANGE_EDIT_WAITS_FOR_APPLY",settings->colorMinimum()==oldMin&&settings->colorMaximum()==oldMax);
        applyRange->click();
        check("COLOR_RANGE_APPLIES_LIMITS",settings->colorMinimum()==.01&&settings->colorMaximum()==11);
        minimum->setValue(oldMin);
        maximum->setValue(oldMax);
        applyRange->click();
    }
    check("CAMERA_ONLY_RESOLUTION_AND_FPS",cameraInfo&&cameraInfo->x()==0
        && cameraInfo->text()==QString::fromUtf8("分辨率：- | FPS：-")
        && !window.findChild<QComboBox *>("rvCameraResolution")
        && !window.findChild<QLabel *>("rtspCameraState")
        && !window.findChild<QLabel *>("rtspCameraDetail"));
    QImage syntheticVideo(2304,1296,QImage::Format_RGB888);
    syntheticVideo.fill(Qt::black);
    submitVideo(syntheticVideo);
    check("CAMERA_RESOLUTION_USES_RECEIVED_FRAME",cameraInfo&&cameraInfo->text().contains(QStringLiteral("2304×1296")));
    {
        auto *page=window.findChild<RtspCameraPage*>();auto *canvas=window.findChild<QWidget*>("rtspCameraLatestFrame");
        bool geometry=page&&canvas;const int priorRotation=page?page->displayRotationDegrees():0;
        if(geometry)for(int degrees:{0,90,180,270}){
            page->setDisplayRotationDegrees(degrees);canvas->grab();
            const auto rect=canvas->property("presentationRect").toRectF();
            const double expected=(degrees==90||degrees==270)?1296./2304:2304./1296;
            geometry&=rect.height()>0&&std::abs(rect.width()/rect.height()-expected)<1e-6&&page->currentFrameImage()==syntheticVideo;
        }
        check("CAMERA_ASPECT_ROTATION_AND_RAW_PIXELS",geometry);
        if(page)page->setDisplayRotationDegrees(priorRotation);
    }
    DepthSdkClient::captureEvent(session, CXWL_DEPTH_EVENT_VIDEO_CLEARED);
    session->dispatch();
    {
        QByteArray raw(48 * 32 * 4, '\0');
        qToLittleEndian<quint32>(0x3f800000, raw.data());
        const QJsonObject metadata{{"width",48},{"height",32},{"depth_unit","sensor_radial_m_float32"},
            {"frame_id",7},{"generation",3},{"interpolation",false}};
        submitDepth(raw, metadata);
        const auto snapshot = window.depth()->snapshot();
        QTemporaryDir output;
        QString destination;
        const bool saved = output.isValid() && snapshot.save(output.path(), &destination);
        check("NATIVE_DEPTH_SAVE_PRESERVES_GRID_AND_METRES", saved && snapshot.depthImage.size() == QSize(48,32)
            && snapshot.metresFloat32LE == raw && snapshot.metadata["interpolation"] == false
            && snapshot.depthImage.pixelColor(1,0) == QColor(Qt::black));
        auto wrong = metadata;
        wrong["depth_unit"] = "camera-Z_m_float32";
        window.depth()->acceptRvDepth(raw, wrong);
        check("NATIVE_VIEW_REJECTS_RETIRED_DEPTH_UNIT", window.depth()->snapshot().metresFloat32LE.isEmpty());
        window.depth()->acceptRvDepth(raw, metadata);
        session->m_capture = {{"retaining_display", true}};
        DepthSdkClient::captureEvent(session, CXWL_DEPTH_EVENT_DEPTH_CLEARED);
        session->dispatch();
        check("RECOVERY_DISPLAYS_PIXELS_WITHOUT_SAVABLE_DEPTH", window.depth()->snapshot().metresFloat32LE.isEmpty());
        session->stop();
        settle(80);
    }
    const auto device=RvDeviceStore().active();
    auto temporary=device;temporary.temporaryFovCalibration=true;
    check("TEMPORARY_CALIBRATION_EXPLICIT_PROFILE_ROUNDTRIP",!device.temporaryFovCalibration
        &&!RvDeviceProfile::fromJson(temporary.json()).temporaryFovCalibration);
    const QString calibrationGeneration(32,'a');
    const QJsonObject calibration{{"schema",1},{"run_id","dtof_"+calibrationGeneration},
        {"sensor_fov_correction",6},{"processing_fov_correction",1},{"temporary_calibration",true}};
    check("TEMPORARY_CALIBRATION_REQUIRES_OPT_IN",!RvCaptureCalibration::valid(calibration,calibrationGeneration,1,false)
        &&RvCaptureCalibration::valid(calibration,calibrationGeneration,1,true));
    check("TEMPORARY_CALIBRATION_REJECTS_STALE_GENERATION",!RvCaptureCalibration::valid(calibration,QString(32,'b'),1,true));
    auto wrongCalibration=calibration;wrongCalibration["temporary_calibration"]=false;
    check("TEMPORARY_CALIBRATION_REJECTS_HIDDEN_MISMATCH",!RvCaptureCalibration::valid(wrongCalibration,calibrationGeneration,1,true));
    check("TEMPORARY_CALIBRATION_REJECTS_OTHER_GEOMETRY",!RvCaptureCalibration::valid(calibration,calibrationGeneration,2,true));
    QJsonObject annotated;RvCaptureCalibration::annotate(annotated,{{"tof_calibration",calibration}});
    check("TEMPORARY_CALIBRATION_RETAINS_SENSOR_IDENTITY",annotated["sensor_fov_correction"]==6
        &&annotated["processing_fov_correction"]==1&&annotated["temporary_fov_calibration"]==true);
    auto legacy = device.json();
    legacy["input_rotation"] = -1;
    const auto migrated = RvDeviceProfile::fromJson(legacy);
    check("LEGACY_DEVICE_WITHOUT_INFERENCE_ORIENTATION_IS_READY",
        migrated.ready() && migrated.json() == device.json() && !migrated.json().contains("input_rotation"));
    {
        RvDeviceManagerDialog devices(session);
        auto *list = devices.findChild<QListWidget *>("rvDeviceList");
        bool hidesDeviceIdentity = true;
        for (const auto *name : {"rvDeviceRotation", "rvDeviceCameraMount", "rvDeviceHost",
                                 "rvDevicePort", "rvDeviceSerial", "rvDeviceUsername", "rvDeviceKey",
                                 "rvDeviceFingerprint", "rvDeviceCalibration", "rvDeviceConfirm"}) {
            hidesDeviceIdentity &= !devices.findChild<QWidget *>(name);
        }
        check("DEVICE_MANAGER_SHOWS_SAVED_NAMES_WITHOUT_CONNECTION_FIELDS", hidesDeviceIdentity
            && list && list->count() == 1 && list->item(0)->text().contains(device.name)
            && !list->item(0)->text().contains(device.serial));
        auto *maintain = devices.findChild<QPushButton *>("rvDeviceMaintainCapture");
        auto *grid = devices.findChild<QPushButton *>("rvDeviceCheckRgbLsc");
        check("RGB_CALIBRATION_IS_STANDALONE", grid == nullptr);
        auto *recover = devices.findChild<QPushButton *>("rvDeviceRecover");
        auto *connectUsb = devices.findChild<QPushButton *>("rvDeviceConnectUsb");
        auto *status = devices.findChild<QLabel *>("rvDeviceStatus");
        check("USB_CONNECT_ACTION", connectUsb && connectUsb->isEnabled()
            && connectUsb->text() == QStringLiteral("USB 一键连接"));
        check("BOARD_FILE_MAINTENANCE_ACTION", maintain && maintain->isEnabled()
            && maintain->text() == QStringLiteral("检查板端文件"));
        check("DEVICE_RECOVERY_ACTION", recover && recover->isEnabled()
            && recover->text() == QStringLiteral("恢复设备"));
        if (connectUsb && maintain && recover && status && qEnvironmentVariable("RV_TEST_OFFLINE_ONLY") == "1") {
            connectUsb->click();
            check("USB_CONNECT_OFFLINE_GUARD", !devices.operationPending() && session->userStopped()
                && status->text() == QStringLiteral("离线模式下无法通过 USB 连接设备。"));
            maintain->click();
            bool idle = true;
            for (const auto *worker : devices.findChildren<QProcess *>()) idle &= worker->state() == QProcess::NotRunning;
            check("BOARD_FILE_MAINTENANCE_OFFLINE_GUARD", idle
                && status->text() == QStringLiteral("离线模式下无法维护板端文件。"));
            recover->click();
            check("DEVICE_RECOVERY_OFFLINE_GUARD", !devices.operationPending() && session->userStopped()
                && status->text() == QStringLiteral("离线模式下无法恢复设备。"));
        }
        auto *controller = DepthSdkQt::validationController(session->m_manager);
        for (bool managed : {false, true}) {
            auto *capture = DepthSdkQt::validationSession(session->m_manager, managed);
            CxwlDepthSessionStatus state{}; state.size = sizeof(state);
            bool unified = capture && CxwlDepth_GetSessionStatus(capture, &state) == CXWL_DEPTH_OK
                && state.state == CXWL_DEPTH_STATE_STOPPED && CxwlDepth_Stop(capture) == CXWL_DEPTH_OK;
            uint32_t bytes = 0;
            unified &= CxwlDepth_ListDevices(capture, nullptr, &bytes) == CXWL_DEPTH_OK && bytes > 0;
            unified &= CxwlDepth_GetUsbStatus(capture, nullptr, &bytes) == CXWL_DEPTH_OK;
            unified &= CxwlDepth_SetMode(capture, CXWL_DEPTH_BOARD) == CXWL_DEPTH_OK;
            struct Observation { int frames = 0; bool quality = false, boardCloud = false; CxwlDepthSession *handle = nullptr; } observation;
            observation.handle = capture;
            auto receive = [](void *context, const CxwlDepthFrameEx *frame) {
                auto &result = *static_cast<Observation *>(context); ++result.frames;
                CxwlDepthFrame cloud{};
                cloud.size = sizeof(cloud);
                const auto status = CxwlDepth_GetFramePointCloud(&frame->frame, &cloud);
                if (result.frames == 1) {
                    CxwlDepthPoint first{};
                    if (status == CXWL_DEPTH_OK) std::memcpy(&first, cloud.image.data, sizeof(first));
                    result.boardCloud = status == CXWL_DEPTH_OK && first.x_m == 0.375f
                        && cloud.sequence == frame->frame.sequence && cloud.timestamp_ns == frame->frame.timestamp_ns
                        && cloud.stream == CXWL_DEPTH_STREAM_POINT_CLOUD && cloud.image.width == frame->frame.image.width
                        && cloud.image.pixel_format == CXWL_DEPTH_PIXEL_XYZ_F32_LE;
                } else result.boardCloud &= status == CXWL_DEPTH_UNAVAILABLE;
                result.quality = frame->snr.data && frame->valid_mask.data
                    && frame->snr.width == 48 && frame->snr.height == 32
                    && static_cast<const unsigned char *>(frame->snr.data)[0] == 37
                    && static_cast<const unsigned char *>(frame->valid_mask.data)[0] == 1
                    && CxwlDepth_Stop(result.handle) == CXWL_DEPTH_BUSY;
            };
            unified &= CxwlDepth_SubscribeEx(capture, receive, nullptr, &observation) == CXWL_DEPTH_OK;
            RvTofFrame raw;
            raw.samples = QByteArray(48 * 32 * 4, 0);
            for (int i = 0; i < 48 * 32; ++i) {
                qToLittleEndian<quint16>(4000, raw.samples.data() + i * 4);
                raw.samples[i * 4 + 2] = 37; raw.samples[i * 4 + 3] = 1;
            }
            raw.metadata = {{"dtof_width",48},{"dtof_height",32},{"dtof_format","range_uq2_u16le_snr_u8_valid_u8"},
                {"dtof_range_unit","0.25_mm"},{"dtof_source","tmf8829_ipc_first_peak"},{"dtof_bytes",48*32*4},
                {"dtof_coordinate","sensor_radial_native"},{"frame_id",1},{"work_mode","dtof_candidate"},
                {"protocol","LSG2"},{"candidate_mode",24},{"engine","native-hd-capture-v1"},{"model_sha256",""},
                {"capture_only",true},{"inference_location","none"},{"depth_present",false},{"photon_enabled",false},
                {"width",256},{"height",256},{"fresh_ms",1000},{"generation",QString(32,'a')},
                {"boot_id","11111111-2222-3333-4444-555555555555"},{"tof_sequence",1},{"tof_timestamp_ns",1000000},{"tof_reused",false}};
            raw.boardPoints = QByteArray(48 * 32 * sizeof(CxwlDepthPoint), 0);
            const CxwlDepthPoint sentinel{0.375f, 0.25f, 1.0f};
            std::memcpy(raw.boardPoints.data(), &sentinel, sizeof(sentinel));
            unified &= raw.isValid();
            controller->m_userStopped = false; // Synthetic input only; never launch a worker.
            emit controller->tofFrameReady(raw);
            auto metadata = raw.metadata;
            metadata["width"] = 48; metadata["height"] = 32; metadata["depth_unit"] = "sensor_radial_m_float32";
            metadata["source_generation"] = raw.metadata["generation"];
            emit controller->depthFrameReady(raw.radialMetres(), metadata);
            unified &= observation.frames == 1 && observation.quality;
            metadata["source_generation"] = QString(32, 'b');
            emit controller->depthFrameReady(raw.radialMetres(), metadata);
            unified &= observation.frames == 2 && !observation.quality; // Never attach stale quality to a new generation.
            check(managed ? "SDK_MANAGED_MATCHED_BOARD_XYZ" : "SDK_EXPLICIT_MATCHED_BOARD_XYZ", observation.boardCloud);
            controller->m_userStopped = true;
            unified &= CxwlDepth_Stop(capture) == CXWL_DEPTH_OK;
            unified &= CxwlDepth_Disconnect(capture) == CXWL_DEPTH_OK;
            check(managed ? "SDK_MANAGED_STOP_AND_QUALITY" : "SDK_EXPLICIT_STOP_AND_QUALITY", unified);
        }
        const bool exclusive = controller && controller->beginBoardFileOperation();
        session->dispatch();
        check("BOARD_FILE_MAINTENANCE_EXCLUDES_OTHER_DEVICE_OPERATIONS", exclusive && session->busy()
            && maintain && !maintain->isEnabled() && connectUsb && !connectUsb->isEnabled()
            && recover && !recover->isEnabled());
        if (exclusive) controller->endBoardFileOperation();
        session->dispatch();
        check("DEVICE_MANAGER_PRESERVES_TRUST_AND_CAMERA_METADATA", RvDeviceStore().active().json() == device.json()
            && session->userStopped());
    }
    {
        QSettings settings;
        const auto oldUrl=settings.value("camera/streamUrlV4");
        settings.setValue("camera/streamUrlV4","rtsp://127.0.0.1/obsolete");
        RvSessionController reopened;
        check("DEVICE_PROFILE_OVERRIDES_OLD_CUSTOM_URL",reopened.controlledSource()
            && reopened.deviceProfile().streamUrl() == fixture.streamUrl());
        if(oldUrl.isValid())settings.setValue("camera/streamUrlV4",oldUrl);
        else settings.remove("camera/streamUrlV4");
    }
    if (qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")=="1") {
        const auto answerImport=[&](QMessageBox::StandardButton answer){
            bool prompted=false;
            QTimer responder;
            QObject::connect(&responder,&QTimer::timeout,&window,[&]{
                auto *prompt=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if(!prompt||prompt->windowTitle()!=QStringLiteral("安装板端所需文件"))return;
                prompted=true;
                prompt->button(answer)->click();
            });
            responder.start(10);
            emit session->boardFilesRequired(session->activeDeviceId());
            settle(80);
            return prompted;
        };
        check("MISSING_FILES_DECLINE_STAYS_STOPPED",answerImport(QMessageBox::No)&&session->userStopped()
            &&!window.findChild<RvDeviceManagerDialog *>());
        const bool accepted=answerImport(QMessageBox::Yes);
        auto *devices=window.findChild<RvDeviceManagerDialog *>();
        const auto *status=devices?devices->findChild<QLabel *>("rvDeviceStatus"):nullptr;
        check("MISSING_FILES_CONFIRM_REUSES_IMPORT",accepted&&devices&&devices->isModal()&&status
            &&status->text()==QStringLiteral("离线模式下无法维护板端文件。")&&session->userStopped());
        if(devices)devices->close();
        settle(30);
        const auto answerUsbSetup=[&](bool existing,bool network,QMessageBox::StandardButton answer,bool cancelPending=false){
            bool prompted=false,noParameters=false;
            QTimer responder;
            QObject::connect(&responder,&QTimer::timeout,&window,[&]{
                auto *prompt=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if(!prompt||prompt->objectName()!="rvUsbSetupPrompt")return;
                prompted=true;
                noParameters=prompt->findChildren<QComboBox *>().isEmpty()
                    && prompt->findChildren<QSpinBox *>().isEmpty();
                const auto expected=!existing
                    ? "A new CXWL Depth Camera was detected. Go to Settings to add it automatically?"
                    : network ? "The CXWL Depth Camera connection needs setup. Go to Settings to complete it automatically?"
                              : "CXWL Depth Camera board files need updating. Go to Settings to update them automatically?";
                noParameters &= prompt->text()==QCoreApplication::translate("SensorMainWindow",expected);
                if(cancelPending)session->stop();
                prompt->button(answer)->click();
            });
            responder.start(10);
            window.offerUsbSetup({{"state", !existing ? "registration_required" : network ? "network_required" : "update_required"}, {"check_id", 1}});
            settle(50);
            return prompted&&noParameters;
        };
        check("USB_NEW_DEVICE_DECLINE_STAYS_STOPPED",answerUsbSetup(false,false,QMessageBox::No)
            && !window.findChild<RvDeviceManagerDialog *>() && session->userStopped());
        {
            const bool prompted=answerUsbSetup(false,false,QMessageBox::Yes,true);
            auto *manager=window.findChild<RvDeviceManagerDialog *>();
            const auto *usbStatus=manager?manager->findChild<QLabel *>("rvDeviceStatus"):nullptr;
            check("USB_SETUP_STOP_BEFORE_DISPATCH_CANCELS",prompted && !manager && !usbStatus && session->userStopped());
            if(manager)manager->close();
            settle(30);
        }
        for(int scenario=0;scenario<3;++scenario) {
            const bool prompted=answerUsbSetup(scenario!=0,scenario==2,QMessageBox::Yes);
            auto *manager=window.findChild<RvDeviceManagerDialog *>();
            const auto *usbStatus=manager?manager->findChild<QLabel *>("rvDeviceStatus"):nullptr;
            bool noProcess=true;
            if(manager)for(auto *process:manager->findChildren<QProcess *>())noProcess&=process->state()==QProcess::NotRunning;
            check(QString("USB_SETUP_CONFIRM_AUTOMATICALLY_DISPATCHES_")+QString::number(scenario),prompted
                && manager && manager->isVisible() && usbStatus && noProcess && session->userStopped()
                && usbStatus->text()==QStringLiteral("离线模式下无法通过 USB 连接设备。"));
            if(manager)manager->close();
            settle(30);
        }
    }
    check("DEPTH_SNAPSHOT_EMPTY_BEFORE_INPUT",window.depth()->snapshot().depthImage.isNull());
    if (depthDock && cloudDock) {
        window.tabifyDockWidget(depthDock, cloudDock);
        const auto savedLayout = window.saveState(1);
        window.resetLayout();
        check("SAVED_TABBED_LAYOUT_CAN_STILL_BE_RESTORED", window.restoreState(savedLayout, 1)
            && window.tabifiedDockWidgets(depthDock).contains(cloudDock));
    }
    window.resetLayout();settle(100);
    check("RESET_LAYOUT_CAPTURE_STOPPED",session->userStopped());
    check("RESET_LAYOUT_RESTORES_DEFAULT_COLUMNS", usesDefaultLayout());
    window.close();settle(100);
    check("CLOSE_CAPTURE_STOPPED",session->userStopped());
    out<<"RESULT "<<(pass?"PASS":"FAIL")<<Qt::endl;return pass?0:1;
}

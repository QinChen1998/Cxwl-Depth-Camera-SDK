#include "../sensors/RvCaptureCalibration.h"
#include <QtWidgets/QDockWidget>
#include "../SensorMainWindow.h"
#include "../demo/DepthSdkClient.h"
#include "../sdk/DepthSdkQt.h"
#include "../sensors/RvSessionController.h"
#include "../sensors/DepthMapWidget.h"
#include "../sensors/RvDepthView.h"
#include "../sensors/RvDtofBoardStream.h"
#include "../sensors/RvTunerController.h"
#include "../sensors/RvDeviceManagerDialog.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QtEndian>
#include <cstring>
#include <QDateTime>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
bool waitFor(const std::function<bool()> &done,int ms){
    QEventLoop loop;QTimer pulse,timeout;pulse.setInterval(20);timeout.setSingleShot(true);
    QObject::connect(&pulse,&QTimer::timeout,&loop,[&]{if(done())loop.quit();});
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    if(!done()){pulse.start();timeout.start(ms);loop.exec();}return done();
}
bool save(const QString &path,const QJsonObject &value){QSaveFile file(path);const auto bytes=QJsonDocument(value).toJson();return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();}
}

// Explicitly gated real GUI test. All frame products remain in memory. Only
// scalar counters, timings, settings and capture identities enter the reports.
int runImagingSwitchValidation(SensorMainWindow &,const QString &);
int runLiveValidation(SensorMainWindow &window,const QString &path){
    return runImagingSwitchValidation(window,path);
}

struct RvDtofBoardStreamTestAccess {
    static void disconnectReceiver(RvDtofBoardStream &stream) { stream.socket.disconnectFromHost(); }
};

// Explicitly authorized real USB onboarding through the ordinary UI/worker.
// Device registration is isolated by main.cpp.
int runUsbEnrollmentLiveValidation(SensorMainWindow &window,const QString &path){
    auto *client = window.session();
    auto *session = DepthSdkQt::validationController(client->m_manager);
    if (!session) return 3;
    const auto serial=qEnvironmentVariable("DCTS_LIVE_SERIAL");
    if(qEnvironmentVariable("DCTS_LIVE_ENABLE")!="1"||serial.isEmpty()
        ||qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")=="1")return 3;
    const bool firstRun=qEnvironmentVariable("DCTS_USB_TEST_NEW")=="1";
    RvDeviceStore initialStore(session->deviceStorePath());bool previouslyRegistered=false;
    for(const auto &profile:initialStore.profiles())previouslyRegistered|=profile.serial==serial;
    const bool emptyRegistry=initialStore.profiles().isEmpty()&&!QFile::exists(initialStore.path());
    QJsonObject report{{"passed",false},{"empty_registry_before_import",emptyRegistry},{"images_saved",false}};
    if(firstRun&&!emptyRegistry){save(path,report);return 3;}
    report["previously_registered"]=previouslyRegistered;
    auto *action=window.findChild<QAction*>("rvDeviceManagerAction");if(!action)return 3;
    action->trigger();auto *manager=window.findChild<RvDeviceManagerDialog*>();if(!manager)return 3;
    auto *connectButton=manager->findChild<QPushButton*>("rvDeviceConnectUsb");
    if(!connectButton)return 3;
    auto *depthCanvas=static_cast<RvDepthView*>(window.depth()->findChild<QWidget*>("rvNativeDepthCanvas"));
    if(!depthCanvas)return 3;
    QObject depthObserver;int depthFrames=0,positiveFrames=0,paintedFrames=0;bool invalidDepth=false;
    QString generation; qint64 lastFrame=-1;QElapsedTimer depthClock;QJsonObject lastDepth;
    const auto acceptDepth=[&](const QByteArray &bytes,const QJsonObject &metadata){
        const int width=metadata["width"].toInt(),height=metadata["height"].toInt();
        const auto current=metadata["generation"].toVariant().toString();const auto id=metadata["frame_id"].toInteger();
        if(width!=48||height!=32||metadata["depth_unit"]!="sensor_radial_m_float32"||bytes.size()!=width*height*4||(current==generation&&id<=lastFrame)){invalidDepth=true;return;}
        int positive=0;
        for(int i=0;i<width*height;++i){const auto bits=qFromLittleEndian<quint32>(bytes.constData()+4*i);float z;std::memcpy(&z,&bits,4);if(!std::isfinite(z)||z<0){invalidDepth=true;return;}positive+=z>0;}
        if(!depthClock.isValid())depthClock.start();
        generation=current;lastFrame=id;lastDepth=metadata;++depthFrames;positiveFrames+=positive>0;
    };
    QObject::connect(client, &DepthSdkClient::depthFrameReady, &depthObserver, acceptDepth);
    QTimer render;
    render.setInterval(50);
    QObject::connect(&render, &QTimer::timeout, &depthObserver, [&] {
        depthCanvas->grab();
        paintedFrames = depthCanvas->property("paintedFrames").toInt();
    });
    render.start();
    bool failed=false;QObject context;QTimer responder;
    QObject::connect(&responder,&QTimer::timeout,&context,[&]{
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)return;
        if(auto *message=qobject_cast<QMessageBox*>(dialog)){
            report["error"]=message->text()+"\n"+message->informativeText();
            failed=true;message->done(QMessageBox::Close);
        }
    });
    responder.start(20);connectButton->click();
    const bool registered=waitFor([&]{return failed||(!manager->operationPending()
        &&manager->property("rvUsbRuntimeReplaced").isValid()&&session->deviceProfile().serial==serial);},240000)&&!failed
        &&session->deviceProfile().serial==serial;
    report["parameters_requested"]=false;report["registered"]=registered;
    report["first_runtime_replaced"]=manager->property("rvUsbRuntimeReplaced").toBool();
    const auto stopped=[&]{return client->userStopped()&&!client->automaticConnecting();};
    const bool firstDepth=registered&&waitFor([&]{return invalidDepth||stopped()||(positiveFrames>=30&&paintedFrames>=20);},95000)
        &&!invalidDepth&&!client->userStopped()&&positiveFrames>=30&&paintedFrames>=20;
    report["first_depth_received_and_painted"]=firstDepth;
    report["first_depth_frames"]=depthFrames;report["first_painted_frames"]=paintedFrames;
    if(firstRun&&firstDepth)
        report["images_saved"]=depthCanvas->grab().save(path+".depth.png");
    const bool testRecovery=qEnvironmentVariable("DCTS_USB_TEST_STREAM_RECOVERY")=="1";
    bool recovered=!testRecovery;
    if(firstDepth&&testRecovery&&session->m_dtofStream){
        const auto previous=session->m_dtofReceipt["generation"].toString();
        const auto boot=session->m_dtofReceipt["boot_id"];
        const int beforeRecovery=positiveFrames,beforePaint=paintedFrames;
        const auto resumed=[&]{return lastDepth["source_generation"].toString()!=previous
            &&positiveFrames-beforeRecovery>=30&&paintedFrames-beforePaint>=20;};
        RvDtofBoardStreamTestAccess::disconnectReceiver(*session->m_dtofStream);
        recovered=waitFor([&]{return invalidDepth||stopped()||resumed();},75000)
            &&!invalidDepth&&!client->userStopped()&&resumed()
            &&session->m_dtofReceipt["boot_id"]==boot
            &&session->m_dtofReceipt["candidate_stream_recovery"]==QLatin1String(RvCaptureContract::CandidateRecoveryProtocol);
        report["stream_recovery_passed"]=recovered;
        report["stream_recovery_new_generation"]=session->m_dtofReceipt["generation"].toString()!=previous;
        if(!recovered)report["stream_recovery_detail"]=session->detailText();
    }
    if(registered){
        const RvDeviceStore registry(session->deviceStorePath());
        report["device_saved"]=QFile::exists(registry.path())&&registry.activeId()==session->deviceProfile().id;
    }
    const auto stopAndInspect=[&]{
        waitFor([&]{return !client->busy();},60000);
        client->stop();if(!waitFor([&]{return !client->busy();},35000))return false;
        auto *tuner=session->tuner();QElapsedTimer timer;timer.start();
        do {
            tuner->read();
            if(!waitFor([&]{return !tuner->pending();},95000))return false;
            if(tuner->state()==RvTunerController::Idle&&tuner->inspection()["owner"]=="idle"){
                report["board_idle_after_stop"]=true;return true;
            }
            waitFor([]{return false;},200);
        }while(timer.elapsed()<35000);
        report["error"]=tuner->detail();return false;
    };
    bool idle=registered&&stopAndInspect();
    bool reconnected=false;
    if(idle){
        const auto firstProfile=session->deviceProfile().json();
        const auto count=RvDeviceStore(session->deviceStorePath()).profiles().size();
        const int beforeFrames=positiveFrames,beforePainted=paintedFrames;
        connectButton->click();
        reconnected=waitFor([&]{return failed||(!manager->operationPending()
            &&manager->property("rvUsbRuntimeReplaced").isValid());},240000)&&!failed
            &&session->deviceProfile().json()==firstProfile
            &&RvDeviceStore(session->deviceStorePath()).profiles().size()==count
            &&!manager->property("rvUsbRuntimeReplaced").toBool();
        report["repeat_import_selected_existing_device"]=reconnected;
        report["repeat_runtime_replaced"]=manager->property("rvUsbRuntimeReplaced").toBool();
        const bool repeatDepth=reconnected&&waitFor([&]{return invalidDepth||client->userStopped()||(positiveFrames-beforeFrames>=30&&paintedFrames-beforePainted>=20);},95000)
            &&!invalidDepth&&!client->userStopped()&&positiveFrames-beforeFrames>=30&&paintedFrames-beforePainted>=20;
        report["repeat_depth_received_and_painted"]=repeatDepth;reconnected&=repeatDepth;
        idle=stopAndInspect();
    }
    responder.stop();
    report["normal_stop"]=idle;
    const bool expectedRepair=qEnvironmentVariable("DCTS_USB_EXPECT_REPAIR")!="1"||report["first_runtime_replaced"].toBool();
    report["depth_frames"]=depthFrames;report["positive_depth_frames"]=positiveFrames;report["painted_frames"]=paintedFrames;
    report["depth_width"]=lastDepth["width"];report["depth_height"]=lastDepth["height"];report["depth_unit"]=lastDepth["depth_unit"];
    report["model_sha256"]=lastDepth["model_sha256"];
    report["passed"]=registered&&firstDepth&&idle&&reconnected&&expectedRepair&&recovered
        &&report["device_saved"].toBool();
    if(!registered&&!report.contains("error"))report["error"]="USB registration did not complete";
    save(path,report);manager->close();window.close();return report["passed"].toBool()?0:2;
}

int runImagingSwitchValidation(SensorMainWindow &window,const QString &path){
    auto *client = window.session();
    auto *session = DepthSdkQt::validationController(client->m_manager);
    if (!session) return 3;
    if(qEnvironmentVariable("DCTS_LIVE_ENABLE")!="1"||qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")=="1"
       ||session->deviceProfile().serial!=qEnvironmentVariable("DCTS_LIVE_SERIAL"))return 3;
    auto *menu=window.findChild<QComboBox*>("rvDepthModel");
    auto *native=static_cast<RvDepthView*>(window.depth()->findChild<QWidget*>("rvNativeDepthCanvas"));
    if(!menu||!native)return 3;
    const QString ordinary="board_native_tof48",high="board_native_tof48_high_accuracy";
    QJsonObject report{{"passed",false},{"calibration_release",RvCalibration::Release},{"rendering","offscreen_widget_grab"}};
    QJsonArray stages;bool all=true;QString priorGeneration;
    for(const auto &scheme:QStringList{ordinary,high,ordinary,high}){
        QObject scope;int frames=0,painted=0,videos=0,rawFrames=0,matchingRawFrames=0;bool invalid=false,measuring=false;
        QMap<qint64, QByteArray> radialBySequence;
        QJsonObject last;QString generation;
        auto *canvas = native;
        const int initialPainted = canvas->property("paintedFrames").toInt();
        // A hidden validation process must still exercise actual widget paints.
        QTimer render;render.setInterval(50);
        QObject::connect(&render,&QTimer::timeout,&scope,[&]{if(measuring)canvas->grab();});render.start();
        QObject::connect(client,&DepthSdkClient::cameraFrameReady,&scope,[&](const QImage&){if(measuring)++videos;});
        QObject::connect(session,&RvSessionController::tofFrameReady,&scope,[&](const RvTofFrame &f){
            if(!measuring)return;
            ++rawFrames;
            radialBySequence.insert(f.metadata["tof_sequence"].toInteger(), f.radialMetres());
            while (radialBySequence.size() > 8) radialBySequence.erase(radialBySequence.begin());
            invalid|=f.columns()!=48||f.rows()!=32||f.metadata["dtof_fov_correction"]!=6;
        });
        const auto accept=[&](const QByteArray &bytes,const QJsonObject &m){
            if(!measuring)return;++frames;last=m;generation=m["source_generation"].toString();
            if(generation.isEmpty())generation=m["generation"].toVariant().toString();
            invalid|=bytes.size()!=48*32*4;
            const auto rawDepth = radialBySequence.take(m["tof_sequence"].toInteger());
            invalid|=rawDepth.isEmpty() || m["depth_unit"]!="sensor_radial_m_float32"||m["native_order"]!=true
                    ||m["sensor_mode"]!=(scheme==ordinary?11:12);
            matchingRawFrames+=rawDepth==bytes;
            invalid|=rawDepth!=bytes||m["interpolation"]!=false;
        };
        QObject::connect(client, &DepthSdkClient::depthFrameReady, &scope, accept);
        const int index=menu->findData(scheme);if(index<0){all=false;break;}
        menu->setCurrentIndex(index);QMetaObject::invokeMethod(menu,"activated",Qt::DirectConnection,Q_ARG(int,index));
        if(client->userStopped())client->start();
        const bool ready=waitFor([&]{return client->userStopped()||(!client->busy()&&!session->imagingSwitchActive());},95000)
            &&!client->userStopped()&&client->currentImagingScheme()==scheme;
        client->dispatch();
        measuring=true;QElapsedTimer clock;clock.start();
        if(ready)waitFor([&]{return invalid||client->userStopped();},12000);
        measuring=false;
        painted = canvas->property("paintedFrames").toInt() - initialPainted;
        const auto snap=window.depth()->snapshot();
        const bool valid=ready&&!invalid&&!client->userStopped()&&frames>=10&&painted>=10&&rawFrames>=10&&videos>=20
            &&generation!=priorGeneration&&snap.metresFloat32LE.size()==48*32*4;
        QJsonObject result{{"scheme",scheme},{"passed",valid},{"ready",ready},{"invalid_data",invalid},{"frames",frames},
            {"painted",painted},{"raw_frames",rawFrames},{"matching_raw_frames",matchingRawFrames},{"video_frames",videos},{"seconds",clock.elapsed()/1000.},
            {"width",last["width"]},{"height",last["height"]},{"depth_unit",last["depth_unit"]},{"generation",generation},
            {"sensor_mode",last["sensor_mode"]},{"state",session->stateText()},
            {"detail",session->detailText()},{"switch",session->imagingSwitchDiagnostics()}};
        stages.append(result);report["stages"]=stages;save(path,report);
        if(!valid){all=false;break;}
        canvas->grab().save(path+"."+scheme+".png");
        priorGeneration=generation;
    }
    QObject stopScope;int late=0;
    QObject::connect(client,&DepthSdkClient::depthFrameReady,&stopScope,[&]{++late;});
    client->stop();const bool stopped=waitFor([&]{return !client->busy();},35000);waitFor([]{return false;},1500);
    report["normal_stop"]=stopped;report["late_depth_results"]=late;report["passed"]=all&&stages.size()==4&&stopped&&late==0;
    save(path,report);window.close();return report["passed"].toBool()?0:2;
}

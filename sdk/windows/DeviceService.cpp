#include "DeviceService.h"
#include "../../sensors/RvTunerProtocol.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QSaveFile>
#include <QtCore/QScopeGuard>
#include <QtCore/QStandardPaths>
#include "../../sensors/RvCaptureCalibration.h"
#include "../../sensors/RvDtofBoardStream.h"
#include <QtCore/QDeadlineTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QTcpServer>
#include <algorithm>
#include <stdexcept>
#include <vector>

#include "CameraTransport.h"
namespace DeviceService {
using namespace Transport;
namespace {
void archive(const Identity &i,const QByteArray &secret,QJsonObject &receipt) {
    const auto candidates=receipt.take("rollbacks").toArray();
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/camera-rollbacks/"+i.endpoint.serial);
    int count=0;QJsonArray archives;
    for(auto item:candidates){
        const auto token=item.toString().toLatin1();require(QRegularExpression("^[a-f0-9]{32}$").match(QString::fromLatin1(token)).hasMatch(),QStringLiteral("维护归档编号无效"));
        require(dir.mkpath("."),QStringLiteral("无法创建本地归档目录"));
        const auto name=dir.filePath(i.boot+"-"+QString::fromLatin1(token)+".tar");QSaveFile file(name);QCryptographicHash hash(QCryptographicHash::Sha256);
        require(file.open(QIODevice::WriteOnly),QStringLiteral("无法写入本地归档"));
        download(i,secret,"ARCHIVE",{token},96*1024*1024,[&](const auto &part){require(file.write(part)==part.size(),QStringLiteral("本地归档磁盘写入失败"));hash.addData(part);});
        require(file.size()>0 && file.commit(),QStringLiteral("无法保存本地归档"));
        QFile local(name);require(local.open(QIODevice::ReadOnly),QStringLiteral("无法复核本地归档"));QCryptographicHash readback(QCryptographicHash::Sha256);
        require(readback.addData(&local) && readback.result()==hash.result(),QStringLiteral("本地归档读回不一致"));
        request(i,secret,"ARCHIVED",{token,readback.result().toHex()},90000);++count;archives.append(name);
    }
    receipt["archived_rollbacks"]=count;receipt["archives"]=archives;
}
}
RvDeviceProfile profile(const Identity &i,const QVector<RvDeviceProfile> &known) {
    RvDeviceProfile result;result.serial=i.endpoint.serial;result.id=result.serial;result.name=result.defaultName();
    for(auto &p:known) {
        if(p.serial!=result.serial) continue;
        require(p.serviceInstance==i.instance,
                QStringLiteral("设备实例与原档案不同。请核对实物，移除原登记并重新登记。"));
        result=p;break;
    }
    result.serviceInstance=i.instance;result.host=i.endpoint.remote.toString();result.validate();
    return result;
}
void enroll(const Identity &i) {
    auto secret=credential(i,true);
    require(request(i,secret,"PAIR")["paired"]==true,QStringLiteral("设备登记回执无效"));
}
QString hostAuthorizationId(const Identity &i) {
    return QString::fromLatin1(sha(hostPublicKey(credential(i,false))));
}
Capture start(const RvDeviceProfile &device,int mode,const QString &generation,const QString &expectedBoot) {
    auto i=verified(device);
    require(i.modes.contains(mode), QStringLiteral("相机服务未提供所选深度档位"));
    require(expectedBoot.isEmpty() || i.boot==expectedBoot,QStringLiteral("设备已重启，请重新连接"));
    auto secret=credential(i,false);
    auto sync=qEnvironmentVariable("CXWL_DEPTH_RGB_TOF_SYNC","off").toLatin1();
    auto response=request(i,secret,"START",{i.epoch.toLatin1(),generation.toLatin1(),QByteArray::number(mode),sync},55000);
    auto data=response["capture"].toObject();auto receipt=data["receipt"].toObject();
    auto calibration=data["calibration"].toObject();
    try {
        require(dtofBoardReceipt(receipt,mode,generation,"1,1") && receipt["boot_id"]==i.boot
             && RvCaptureCalibration::valid(calibration,generation,device.fov(),true)
             && QRegularExpression("^[0-9a-f]{32}$").match(response["grant"].toString()).hasMatch(),QStringLiteral("采集代次或 F6 标定回执不匹配"));
    } catch(...) {request(i,secret,"STOP",{generation.toLatin1()},22000);throw;}
    receipt["tof_calibration"]=calibration;
    receipt["rgb_tof_sync_mode"]=QString::fromLatin1(sync=="phase" && mode!=24?QByteArray("int"):sync);
    receipt["candidate_stream_recovery"]=QLatin1String(RvCaptureContract::CandidateRecoveryProtocol);
    return {i,secret,response["grant"].toString().toLatin1(),generation,receipt};
}
void stop(const Capture &c) {
    try {
        require(request(c.identity,c.credential,"STOP",{c.generation.toLatin1()},25000)["stopped"]==true,QStringLiteral("未收到设备停止回执"));
    } catch(const std::exception &) {
        auto current=inspect(c.identity.endpoint.serial);
        require(current.instance==c.identity.instance && (current.boot!=c.identity.boot || !current.active),QStringLiteral("本次采集的释放状态无法确认"));
    }
}
void heartbeat(const Capture &c) {request(c.identity,c.credential,"PING",{c.generation.toLatin1()});}
QJsonObject manage(const RvDeviceProfile &device,const QByteArray &op) {
    auto i=verified(device);
    auto secret=credential(i,false);
    require(op=="CHECK" || op=="MAINTAIN" || op=="REPAIR" || op=="RECOVER",QStringLiteral("未知的设备维护操作"));
    auto response=request(i,secret,op,{},90000);
    if(op=="MAINTAIN")archive(i,secret,response);
    return response;
}
QJsonObject tune(const RvDeviceProfile &device,const QString &action,const QJsonObject &args,std::shared_ptr<MediaPipe> &pipe) {
    const auto i=verified(device);const auto secret=credential(i,false);
    require(action=="inspect" || args["boot_id"]==i.boot,QStringLiteral("工程调参会话所属的设备已重启"));
    if(action=="inspect"){
        auto r=request(i,secret,"TINSPECT");
        r["settings"]=RvTuner::scriptSettings(r.take("native_line").toString().toLatin1()).json();
        auto line=r.take("capture_line").toString().toLatin1();
        r["capture_settings"]=RvTuner::scriptSettings(line+"\\nsignal=-g; rate=4; distance=11;",true).json();
        return r;
    }
    auto token=args["token"].toString().toLatin1(),snapshot=args["snapshot"].toString().toLatin1();
    if(action=="stop" || action=="heartbeat")return request(i,secret,action=="stop"?"TSTOP":"TPING",{token},35000);
    const auto settings=RvTuner::Settings::fromJson(args["settings"].toObject());
    auto values=settings.arguments().toLatin1().split(' ');
    if(action=="persist")return request(i,secret,"TPERSIST",QList<QByteArray>{token,snapshot}+values,75000);
    require(action=="start" || action=="switch",QStringLiteral("工程调参操作无效"));
    auto old=args[action=="switch"?"old_token":"owner_generation"].toString().toLatin1();
    if(old.isEmpty())old="-";
    const auto result=request(i,secret,"TSTART",QList<QByteArray>{i.epoch.toLatin1(),snapshot,token,old}+values,75000);
    try {
        require(result["settings"].toObject()==settings.json() && RvTuner::scriptSettings(result["sensor_line"].toString().toLatin1())==settings,QStringLiteral("传感器参数读回不一致"));
        Capture c{i,secret,result["grant"].toString().toLatin1(),QString::fromLatin1(token),{}};
        pipe=std::make_shared<MediaPipe>(c,QByteArray("TUNER"));
        auto port=pipe->listenPort();require(port!=0,QStringLiteral("无法打开调参测量通道"));
        auto receipt=result;receipt.remove("grant");receipt.remove("sensor_line");receipt["local_port"]=port;return receipt;
    } catch(...){request(i,secret,"TSTOP",{token},35000);throw;}
}
MediaPipe::MediaPipe(const Capture &c,bool color):MediaPipe(c,QByteArray(color?"RGB":"DEPTH")) {}
MediaPipe::MediaPipe(const Capture &c,QByteArray name):capture(c),stream(std::move(name)) {}
quint16 MediaPipe::listenPort() {
    start();ready.acquire();return port;
}
MediaPipe::~MediaPipe() {requestInterruption();quit();wait();}
void MediaPipe::run() {
    QTcpServer server;
    server.setProxy(QNetworkProxy::NoProxy);
    if(!server.listen(QHostAddress::LocalHost,0)){ready.release();return;}
    port=server.serverPort();ready.release();
    connect(&server,&QTcpServer::newConnection,&server,[&] {
        auto *local=server.nextPendingConnection();
        if(!local)return;
        try {
            auto channel=openChannel(capture.identity.endpoint,capture.identity.instance,capture.identity.boot);
            exchange(channel,"DATA",capture.credential,{capture.generation.toLatin1(),capture.grant,stream});
            auto *remote=channel.socket.release();remote->setParent(local);
            local->setReadBufferSize(256*1024);remote->setReadBufferSize(256*1024);
            local->setSocketOption(QAbstractSocket::LowDelayOption,1);
            remote->setSocketOption(QAbstractSocket::LowDelayOption,1);
            auto forward=[](QAbstractSocket *src,QAbstractSocket *dst) {
                while(src->bytesAvailable() && dst->bytesToWrite()<256*1024) {
                    const auto bytes=src->read(32768);if(bytes.isEmpty())break;
                    if(dst->write(bytes)!=bytes.size()){src->abort();dst->abort();break;}
                }
            };
            connect(local,&QTcpSocket::readyRead,local,[=]{forward(local,remote);});
            connect(remote,&QTcpSocket::readyRead,local,[=]{forward(remote,local);});
            connect(local,&QTcpSocket::bytesWritten,local,[=]{forward(remote,local);});
            connect(remote,&QTcpSocket::bytesWritten,local,[=]{forward(local,remote);});
            connect(remote,&QTcpSocket::disconnected,local,&QTcpSocket::disconnectFromHost);
            connect(local,&QTcpSocket::disconnected,local,&QObject::deleteLater);
            forward(local,remote);forward(remote,local);
        } catch(const std::exception&) {local->abort();local->deleteLater();}
    });
    if(!isInterruptionRequested())exec();
}
}

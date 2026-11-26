#include "Calibration.h"
#include "../../sdk/windows/CameraTransport.h"
#include "Estimate.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QScopeGuard>
#include <QtCore/QRegularExpression>
#include <QtCore/QtEndian>

namespace {
using namespace DeviceService;
using namespace DeviceService::Transport;
void upload(const Identity &i,const QByteArray &secret,const QByteArray &token,const QByteArray &field,const QByteArray &bytes) {
    require(!bytes.isEmpty() && bytes.size()<=1048576,QStringLiteral("标定资料大小无效"));
    auto t=transfer(i,secret,"CALPUT",{token,field,QByteArray::number(bytes.size()),sha(bytes)});
    require(t.socket->write(bytes)==bytes.size(),QStringLiteral("标定数据写入失败"));
    while(t.socket->bytesToWrite())require(t.socket->waitForBytesWritten(20000),QStringLiteral("标定数据传输中断"));
    QDeadlineTimer deadline(25000);transferred(t,i,deadline);
}
QJsonObject calibrate(const Identity &i,const QByteArray &secret) {
    auto initial=request(i,secret,"LSC").value("rgb_lsc").toObject();
    if(initial["state"]=="unsupported"){initial["result"]="unsupported";return {{"rgb_lsc",initial}};}
    auto begin=request(i,secret,"CALBEGIN");
    auto snapshot=begin["rgb_lsc"].toObject();auto token=begin["token"].toString().toLatin1();
    require(QRegularExpression("^[a-f0-9]{16}$").match(QString::fromLatin1(token)).hasMatch(),QStringLiteral("标定会话标识无效"));
    const auto guard=qScopeGuard([&]{try{request(i,secret,"CALEND",{token});}catch(const std::exception&){}});
    const auto get=[&](QByteArray field,QByteArray expected){
        QByteArray bytes;download(i,secret,"CALREAD",{token,field},1048576,[&](const auto &part){bytes+=part;});
        require(sha(bytes)==expected,QStringLiteral("标定资料校验失败"));return bytes;
    };
    const auto prior=snapshot["iq_sha256"].toString().toLatin1();
    const auto bytes=get("current",prior);const auto current=QJsonDocument::fromJson(bytes).object();
    require(RvRgbLsc::compatible(current),QStringLiteral("当前 ISP 配置未提供受支持的双绿 LSC 表"));
    QJsonArray reference;
    auto origin=snapshot["factory_sha256"].toString().toLatin1();if(origin.isEmpty())origin=prior;
    if(!snapshot["reference"].toString().isEmpty()){
        auto record=get("record",snapshot["record"].toString().toLatin1());
        auto body=record.mid(record.indexOf('\n')+1);
        require(sha(body)==snapshot["reference"].toString().toLatin1(),QStringLiteral("相机原始绿通道参考校验失败"));
        reference=QJsonDocument::fromJson(body).array();
    } else if(snapshot["factory_backup_present"].toBool()) {
        reference=RvRgbLsc::greenReference(QJsonDocument::fromJson(get("original",snapshot["backup"].toString().toLatin1())).object());
    } else {
        require(snapshot["record"]=="-",QStringLiteral("相机原始绿通道参考缺失"));
        reference=RvRgbLsc::greenReference(current);
    }
    require(!reference.isEmpty(),QStringLiteral("相机绿通道参考数据无效"));
    QByteArray pending;RvRgbLsc::Bracket bracket;int count=0;quint64 time=0;quint32 seq=0;
    constexpr int frameBytes=24+RvRgbLsc::RawBytes;
    download(i,secret,"CALREAD",{token,"raw"},qint64(frameBytes)*RvRgbLsc::CaptureFrames,[&](const auto &part){
        pending+=part;
        if(pending.size()<frameBytes)return;
        const auto *head=pending.constData();auto stamp=qFromLittleEndian<quint64>(head+8);auto index=qFromLittleEndian<quint32>(head+16);
        auto exposure=RvRgbLsc::Exposures.at(count/RvRgbLsc::FramesPerExposure);
        require(QByteArrayView(head,4)=="LSC0" && qFromLittleEndian<quint32>(head+4)==RvRgbLsc::RawBytes
              && qFromLittleEndian<quint32>(head+20)==unsigned(exposure) && stamp>time
              && (!(count%RvRgbLsc::FramesPerExposure) || index>seq),QStringLiteral("传感器原始帧或曝光次序无效"));
        bracket[exposure].push_back(RvRgbLsc::measure(QByteArrayView(head+24,RvRgbLsc::RawBytes)));
        time=stamp;seq=index;++count;pending.remove(0,frameBytes);
    });
    require(count==RvRgbLsc::CaptureFrames && pending.isEmpty(),QStringLiteral("传感器标定帧不足"));
    auto fit=RvRgbLsc::estimate(bracket);snapshot["frames"]=count;
    snapshot["fit"]=QJsonObject{{"training_tiles",fit.trainingTiles},{"validation_tiles",fit.validationTiles},{"nodes_no_evidence",fit.nodesWithoutEvidence},{"before_log_ratio",fit.before},{"after_log_ratio",fit.after}};
    if(!fit.trainingTiles || !fit.validationTiles || fit.nodesWithoutEvidence==RvRgbLsc::GridNodes){
        snapshot["result"]="insufficient_raw_samples";return {{"rgb_lsc",snapshot}};
    }
    const auto corrected=RvRgbLsc::apply(reference,current,fit);
    const auto candidate=corrected==current?bytes:QJsonDocument(corrected).toJson(QJsonDocument::Compact);
    auto digest=sha(candidate);upload(i,secret,token,"iq",candidate);request(i,secret,"CALTRIAL",{token,digest},55000);
    auto body=QJsonDocument(reference).toJson(QJsonDocument::Compact);
    auto record="2 "+i.endpoint.serial.toLatin1()+' '+origin+' '+digest+' '+sha(body)+'\n'+body;
    upload(i,secret,token,"record",record);
    auto receipt=request(i,secret,"CALCOMMIT",{token,digest,sha(record)},55000)["rgb_lsc"].toObject();
    require(receipt["state"]=="deployed" && receipt["iq_sha256"].toString().toLatin1()==digest && receipt["boot_id"]==i.boot
         && receipt["record"].toString().toLatin1()==sha(record) && receipt["factory_backup_present"]==false,QStringLiteral("标定写入尚未通过读回核验"));
    receipt["frames"]=count;receipt["fit"]=snapshot["fit"];receipt["trial_loaded"]=true;receipt["iq_replaced"]=digest!=prior;
    receipt["result"]=digest!=prior?"installed":"already_deployed";return {{"rgb_lsc",receipt}};
}
}
QJsonObject rgbCalibration(const RvDeviceProfile &device, bool write) {
    using namespace DeviceService::Transport;
    const auto identity = verified(device);
    const auto secret = credential(identity, false);
    return write ? calibrate(identity, secret) : request(identity, secret, "LSC");
}

#include "../../sensors/RvUsbEnrollment.h"
#include "../../sensors/RvSessionController.h"
#include "DeviceService.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QFutureWatcher>
#include <QtCore/QDateTime>
#include <QtCore/QUuid>
#include <optional>

namespace {
struct Result {
    DeviceService::Identity identity;
    QString error;
    QString hostId;
    bool absent=false;
    bool needsRepair=false;
};
bool sameAttachment(const DeviceService::Identity &a,const DeviceService::Identity &b) {
    return a.endpoint.serial==b.endpoint.serial && a.endpoint.adapter==b.endpoint.adapter
        && a.endpoint.index==b.endpoint.index && a.instance==b.instance && a.boot==b.boot;
}
}
class RvUsbEnrollment final:public QObject {
public:
    RvUsbEnrollment(RvSessionController *s,std::function<void()> callback):QObject(s),session(s),changed(std::move(callback)){}
    RvSessionController *session;
    std::function<void()> changed;
    DeviceService::Identity checked;
    bool active=false,known=false,absent=false;
    QString hostId;
    quint64 id=0,next=0,checkedStop=0;
    QString revision,state="idle",error,deviceId;
    bool run(bool checkOnly,bool start,bool confirmed,bool defaultSelection) {
        if(active || !session->userStopped() || session->busy()) return false;
        const auto stop=session->stopRevision();
        RvDeviceStore store(session->deviceStorePath());
        if(confirmed && (!id || stop!=checkedStop || store.revision()!=revision)) return false;
        const auto previous=checked;
        const auto saved=store.profiles();
        auto selected=confirmed?previous.endpoint.serial:qEnvironmentVariable("CXWL_DEPTH_USB_SERIAL").trimmed().toLower();
        if(selected.isEmpty() && defaultSelection && DeviceService::attached().size()>1)selected=session->deviceProfile().serial;
        id=0;error.clear();deviceId.clear();hostId.clear();checked={};absent=false;known=false;active=true;
        state=checkOnly?"checking":"enrolling";
        const auto snapshot=store.revision();
        auto *watch=new QFutureWatcher<Result>(this);
        connect(watch,&QFutureWatcher<Result>::finished,this,[=] {
            const auto r=watch->result();watch->deleteLater();active=false;
            try {
                if(session->stopRevision()!=stop){state="cancelled";changed();return;}
                RvDeviceStore current(session->deviceStorePath());
                if(current.revision()!=snapshot)throw std::runtime_error("设备档案已变化，请重新检查");
                absent=r.absent;
                if(!r.error.isEmpty())throw std::runtime_error(r.error.toUtf8().constData());
                checked=r.identity;
                hostId=r.hostId;
                auto p=DeviceService::profile(checked,current.profiles());
                auto list=current.profiles();
                auto existing=std::find_if(list.begin(),list.end(),[&](auto &e){return e.id==p.id;});
                known=existing!=list.end() && existing->serviceInstance==checked.instance;
                if(checkOnly) {
                    id=++next;checkedStop=stop;revision=snapshot;
                    state=known?"ready":"registration_required";deviceId=known?p.id:QString();
                } else {
                    if(existing==list.end())list.append(p);else *existing=p;
                    if(current.activeId()!=p.id || current.profiles().size()!=list.size() || current.byId(p.id).json()!=p.json())current.save(list,p.id);
                    deviceId=p.id;state="enrolled";
                    session->reloadDeviceProfiles();
                    if(r.needsRepair) {
                        state="error";error=QStringLiteral("设备身份已登记，采集文件需要维护");session->boardFilesRequired(p);
                    } else if(start && session->stopRevision()==stop+1 && session->deviceProfile().id==p.id)session->startAutomatically(checked.boot);
                }
            } catch(const std::exception &e){state="error";error=QString::fromUtf8(e.what());}
            changed();
        });
        watch->setFuture(QtConcurrent::run([=] {
            Result r;
            try {
                if(DeviceService::attached().isEmpty()){r.absent=true;throw std::runtime_error("未发现已就绪的相机 USB 网卡");}
                r.identity=DeviceService::inspect(selected);
                if(confirmed && !sameAttachment(previous,r.identity))throw std::runtime_error("检查后相机已更换或重启，请重新确认");
                auto p=DeviceService::profile(r.identity,saved);
                const auto existing=std::find_if(saved.begin(),saved.end(),[&](auto &d){return d.serial==p.serial;});
                if(!checkOnly) {
                    if(existing==saved.end() && !saved.isEmpty() && !confirmed)throw std::runtime_error("检测到新相机，请在设备管理中核对并确认登记");
                    DeviceService::enroll(r.identity);
                    r.hostId=DeviceService::hostAuthorizationId(r.identity);
                    try {r.needsRepair=DeviceService::manage(p,"CHECK")["capture_runtime"]!="ready";}
                    catch(const std::exception &e) {
                        if(!QString::fromUtf8(e.what()).startsWith("camera:"))throw;
                        r.needsRepair=true;
                    }
                } else if(existing!=saved.end() && existing->serviceInstance==r.identity.instance) {
                    try {r.hostId=DeviceService::hostAuthorizationId(r.identity);} catch(const std::exception &) {}
                }
            }catch(const std::exception &e){r.error=QString::fromUtf8(e.what());}
            return r;
        }));
        changed();return true;
    }
};
RvUsbEnrollment *createRvUsbEnrollment(RvSessionController *s,std::function<void()> callback) {
    return s && qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")!="1"?new RvUsbEnrollment(s,std::move(callback)):nullptr;
}
bool checkRvUsbDevice(RvUsbEnrollment *u){return u && u->run(true,false,false,false);}
bool connectRvDevice(RvUsbEnrollment *u,bool allowSaved,bool){return u && u->run(false,true,false,allowSaved);}
bool enrollCheckedRvUsbDevice(RvUsbEnrollment *u,quint64 id,bool start,bool,bool){return u && id && u->id==id && u->run(false,start,true,false);}
bool rvUsbOperationBusy(const RvUsbEnrollment *u){return u && u->active;}
QJsonObject rvUsbOperationStatus(const RvUsbEnrollment *u) {
    if(!u)return {{"state","idle"},{"check_id",0}};
    return {{"state",u->state},{"phase","device_service"},{"error",u->error},{"check_id",qint64(u->id)},
        {"known",u->known},{"absent",u->absent},{"device_id",u->deviceId},{"runtime_replaced",false},
        {"attachment_id",u->checked.endpoint.adapter},{"host_authorization_id",u->hostId}};
}
QJsonObject checkRvUsbEnrollmentCore() {
    DeviceService::Identity a,b;
    a.endpoint.serial="0123456789abcdef";a.endpoint.adapter="test-rndis";a.instance="a";a.boot="first";b=a;
    QJsonObject checks{{"USB_SAME_ATTACHMENT",sameAttachment(a,b)}};
    b.instance="b";checks["USB_INSTANCE_CHANGE_REJECTED"]=!sameAttachment(a,b);
    b=a;b.boot="next";checks["USB_BOOT_CHANGE_REJECTED"]=!sameAttachment(a,b);
    b=a;b.endpoint.adapter="other";checks["USB_INTERFACE_CHANGE_REJECTED"]=!sameAttachment(a,b);
    a.instance=QString(32,'a');a.endpoint.remote=QHostAddress("169.254.77.1");
    RvDeviceProfile old;
    old.id="previous-record";old.name="Saved camera name";old.serial=a.endpoint.serial;
    old.host="172.32.0.93";old.cameraMountRotation=180;
    old.servicePin=QString(64,'b');old.publicKey="tls-sha256:"+old.servicePin;
    const auto migrated=DeviceService::profile(a,{old});
    checks["USB_MIGRATES_SAME_SERIAL_LEGACY_PROFILE"]=migrated.serviceInstance==a.instance
        && migrated.servicePin.isEmpty() && migrated.publicKey.isEmpty() && old.serviceInstance.isEmpty()
        && migrated.id==old.id && migrated.name==old.name && migrated.cameraMountRotation==180;
    b=a;b.instance=QString(32,'c');
    const auto reflashed=DeviceService::profile(b,{migrated});
    checks["USB_MIGRATES_SAME_SERIAL_AFTER_ERASE"]=reflashed.id==migrated.id && reflashed.serviceInstance==b.instance;
    b.endpoint.serial="fedcba9876543210";
    const auto other=DeviceService::profile(b,{migrated});
    checks["USB_MIGRATION_NEVER_REUSES_OTHER_SERIAL_PROFILE"]=other.id==b.endpoint.serial && other.id!=migrated.id;
    return checks;
}

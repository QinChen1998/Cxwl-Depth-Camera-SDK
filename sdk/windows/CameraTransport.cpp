#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <wincred.h>
#include <bcrypt.h>
#include "DeviceService.h"
#include "DeviceTrust.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QSaveFile>
#include <QtCore/QScopeGuard>
#include <QtCore/QStandardPaths>
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
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslKey>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QTcpServer>
#include <algorithm>
#include <stdexcept>
#include <vector>

#include "CameraTransport.h"
#include "../../tools/rv_capture/Contract.h"
namespace DeviceService {
namespace Transport {
void require(bool valid,const QString &why) {
    if(!valid) throw std::runtime_error(why.toUtf8().constData());
}
QMap<QString,QString> usbParents() {
    QMap<QString,QString> results;
    HDEVINFO devices=SetupDiGetClassDevsW(&GUID_DEVCLASS_NET,nullptr,nullptr,DIGCF_PRESENT);
    require(devices!=INVALID_HANDLE_VALUE,QStringLiteral("无法枚举 Windows 网络设备"));
    SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
    for(DWORD i=0;SetupDiEnumDeviceInfo(devices,i,&device);++i) {
        DEVINST node=device.DevInst;
        QString serial;
        for(int parent=0;parent<10;++parent) {
            WCHAR instance[MAX_DEVICE_ID_LEN]{};
            if(CM_Get_Device_IDW(node,instance,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS) break;
            auto id=QString::fromWCharArray(instance).toLower();
            const QString prefix=QStringLiteral("usb\\vid_2207&pid_0019\\");
            if(id.startsWith(prefix)) {
                serial=id.mid(prefix.size());break;
            }
            DEVINST next;
            if(CM_Get_Parent(&next,node,0)!=CR_SUCCESS) break;
            node=next;
        }
        if(!QRegularExpression(QStringLiteral("^[0-9a-f]{16}$")).match(serial).hasMatch()) continue;
        HKEY key=SetupDiOpenDevRegKey(devices,&device,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_QUERY_VALUE);
        if(key==INVALID_HANDLE_VALUE) continue;
        WCHAR guid[64]{};DWORD size=sizeof(guid);
        if(RegGetValueW(key,nullptr,L"NetCfgInstanceId",RRF_RT_REG_SZ,nullptr,guid,&size)==ERROR_SUCCESS)
            results.insert(QString::fromWCharArray(guid).toLower(),serial);
        RegCloseKey(key);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return results;
}
QString pinOf(const QSslCertificate &cert) {
    return QString::fromLatin1(QCryptographicHash::hash(cert.publicKey().toDer(),QCryptographicHash::Sha256).toHex());
}
std::unique_ptr<QSslSocket> openChannel(const Endpoint &e,const QString &expected) {
    require(qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")!="1",QStringLiteral("离线测试禁止连接相机"));
    auto s=std::make_unique<QSslSocket>();
    s->setProxy(QNetworkProxy::NoProxy);
    auto conf=QSslConfiguration::defaultConfiguration();
    conf.setCaCertificates(QSslCertificate::fromData(cameraTrust));
    conf.setProtocol(QSsl::TlsV1_2OrLater);
    conf.setPeerVerifyMode(QSslSocket::VerifyPeer);
    s->setSslConfiguration(conf);
    require(s->bind(e.local),QStringLiteral("无法绑定已核验的 USB 网卡"));
    const DWORD index=htonl(e.index);
    require(setsockopt(SOCKET(s->socketDescriptor()),IPPROTO_IP,IP_UNICAST_IF,
                       reinterpret_cast<const char*>(&index),sizeof(index))==0,QStringLiteral("USB 网卡已变化"));
    s->setPeerVerifyName(e.serial+".cxwl.camera");
    s->connectToHostEncrypted(e.remote.toString(),8943);
    require(s->waitForEncrypted(4500),QStringLiteral("相机服务未就绪或凭据验证失败。请检查配套固件，旧固件可使用稳定版 SDK。 ")+s->errorString());
    const auto leaf=s->peerCertificate();
    require(!leaf.isNull() && (expected.isEmpty() || pinOf(leaf)==expected),
            QStringLiteral("相机公钥与登记记录不匹配，请在设备管理中确认设备替换。"));
    return s;
}
QByteArray take(QSslSocket &s,int count,QDeadlineTimer &deadline) {
    QByteArray bytes;
    while(bytes.size()<count) {
        if(!s.bytesAvailable()) {
            const auto ms=deadline.remainingTime();
            require(ms>0 && s.waitForReadyRead(int(ms)),QStringLiteral("等待设备服务回执超时。 ")+s.errorString());
        }
        bytes+=s.read(count-bytes.size());
    }
    return bytes;
}
QJsonObject exchange(QSslSocket &s,const Endpoint &ep,const QByteArray &op,const QString &boot,const QByteArray &credential,const QList<QByteArray> &args,int ms) {
    const auto id=QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    QByteArray request="CX1 "+id+' '+op+' '+(boot.isEmpty()?QByteArray("-"):boot.toLatin1())+' '+(credential.isEmpty()?QByteArray("-"):credential);
    for(const auto &arg:args) {
        require(!arg.isEmpty() && !arg.contains(' ') && !arg.contains('\n') && !arg.contains('\r'),QStringLiteral("无效的设备参数"));
        request+=' ';request+=arg;
    }
    request+='\n';
    require(request.size()<512 && s.write(request)==request.size(),QStringLiteral("无法发送设备请求"));
    s.flush();
    QDeadlineTimer deadline(ms);
    const auto header=take(s,4,deadline);
    auto length=qFromBigEndian<quint32>(header.constData());
    require(length>0 && length<=128*1024,QStringLiteral("无效的服务回执长度"));
    auto doc=QJsonDocument::fromJson(take(s,length,deadline)).object();
    require(doc["v"]==1 && doc["id"].toString().toLatin1()==id && doc["serial"]==ep.serial
        && QRegularExpression("^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$").match(doc["boot"].toString()).hasMatch()
        && (boot.isEmpty() || doc["boot"]==boot) && doc["body"].isObject(),QStringLiteral("设备服务回执身份不匹配"));
    require(doc["code"]=="ok",doc["code"].toString()+": "+doc["body"].toObject()["error"].toString());
    return doc;
}
QByteArray credential(const Identity &i,bool create) {
    const auto target=(QStringLiteral("CXWL Device Service/")+i.endpoint.serial+"/"+i.pin).toStdWString();
    PCREDENTIALW existing=nullptr;
    if(CredReadW(target.c_str(),CRED_TYPE_GENERIC,0,&existing)) {
        QByteArray bytes(reinterpret_cast<char*>(existing->CredentialBlob),int(existing->CredentialBlobSize));
        CredFree(existing);
        require(bytes.size()==32,QStringLiteral("本机设备登记损坏"));
        return bytes.toHex();
    }
    require(GetLastError()==ERROR_NOT_FOUND && create,QStringLiteral("请先通过设备管理登记这台相机"));
    unsigned char secret[32]{};
    require(BCryptGenRandom(nullptr,secret,sizeof(secret),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,QStringLiteral("无法生成主机身份"));
    CREDENTIALW item{};item.Type=CRED_TYPE_GENERIC;item.TargetName=const_cast<wchar_t*>(target.c_str());
    item.CredentialBlobSize=sizeof(secret);item.CredentialBlob=secret;item.Persist=CRED_PERSIST_LOCAL_MACHINE;
    require(CredWriteW(&item,0),QStringLiteral("Windows 无法保存本机的设备登记"));
    auto out=QByteArray(reinterpret_cast<char*>(secret),sizeof(secret)).toHex();
    SecureZeroMemory(secret,sizeof(secret));return out;
}
QJsonObject request(const Identity &i,const QByteArray &secret,const QByteArray &op,const QList<QByteArray> &args,int ms) {
    auto socket=openChannel(i.endpoint,i.pin);
    return exchange(*socket,i.endpoint,op,i.boot,secret,args,ms)["body"].toObject();
}
Identity verified(const RvDeviceProfile &device) {
    auto i=inspect(device.serial);
    require(!device.servicePin.isEmpty() && i.pin==device.servicePin,QStringLiteral("请在设备管理中核验此相机身份后再连接"));
    return i;
}
Transfer transfer(const Identity &i,const QByteArray &secret,const QByteArray &op,const QList<QByteArray> &args) {
    auto socket=openChannel(i.endpoint,i.pin);
    const auto initial=exchange(*socket,i.endpoint,op,i.boot,secret,args,30000);
    return {std::move(socket),initial["id"].toString().toLatin1()};
}
void transferred(Transfer &t,const Identity &i,QDeadlineTimer &deadline) {
    const auto size=qFromBigEndian<quint32>(take(*t.socket,4,deadline).constData());
    require(size>0 && size<=16384,QStringLiteral("设备传输结果长度无效"));
    const auto j=QJsonDocument::fromJson(take(*t.socket,int(size),deadline)).object();
    require(j["v"]==1 && j["id"].toString().toLatin1()==t.id && j["serial"]==i.endpoint.serial && j["boot"]==i.boot && j["code"]=="ok",
            QStringLiteral("设备传输未确认。")+j["body"].toObject()["error"].toString());
}
void download(const Identity &i,const QByteArray &secret,const QByteArray &op,const QList<QByteArray> &args,
              qint64 maximum,const std::function<void(const QByteArray&)> &consume) {
    auto t=transfer(i,secret,op,args);QDeadlineTimer deadline(245000);qint64 total=0;
    for(;;) {
        const auto size=qFromBigEndian<quint32>(take(*t.socket,4,deadline).constData());
        if(!size)break;
        require(size<=16384 && (total+=size)<=maximum,QStringLiteral("设备传输超出操作范围"));
        consume(take(*t.socket,int(size),deadline));
    }
    transferred(t,i,deadline);
}
QByteArray sha(const QByteArray &data){return QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex();}
} // namespace Transport
using namespace Transport;
QList<Endpoint> attached() {
    const auto parents=usbParents();
    ULONG size=16384;
    std::vector<BYTE> bytes(size);
    DWORD rc;
    for(int n=0;n<3;++n) {
        rc=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data()),&size);
        if(rc!=ERROR_BUFFER_OVERFLOW) break;
        bytes.resize(size);
    }
    if(rc==ERROR_NO_DATA)return {};
    require(rc==NO_ERROR,QStringLiteral("无法查询 USB 网卡地址"));
    QList<Endpoint> result;
    for(auto *a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());a;a=a->Next) {
        const auto adapter=QString::fromLatin1(a->AdapterName).toLower();
        if(!parents.contains(adapter) || a->OperStatus!=IfOperStatusUp) continue;
        for(auto *u=a->FirstUnicastAddress;u;u=u->Next) {
            if(u->Address.lpSockaddr->sa_family!=AF_INET) continue;
            const auto host=ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            QHostAddress local(host), remote;
            if((host>>16)==0xa9fe) remote=QHostAddress("169.254.77.1");
            else if((host>>16)==0xac20) remote=QHostAddress("172.32.0.93");
            else continue;
            result.append({parents[adapter],adapter,local,remote,a->IfIndex});break;
        }
    }
    return result;
}
Identity inspect(const QString &selected) {
    auto choices=attached();
    if(!selected.isEmpty()) choices.erase(std::remove_if(choices.begin(),choices.end(),[&](auto &e){return e.serial!=selected;}),choices.end());
    require(choices.size()==1,choices.isEmpty()?QStringLiteral("没有找到目标 USB 相机。请检查数据线和 RNDIS 网卡，等待其取得 IPv4 地址。")
        :QStringLiteral("检测到多台相机，请指定已登记设备或单独接入待登记相机。"));
    auto e=choices.first();auto s=openChannel(e,{});
    auto result=exchange(*s,e,"HELLO",{},{},{});
    const auto body=result["body"].toObject();
    QList<int> modes;
    for (const auto &item : body["modes"].toArray()) {
        const int mode = item.toInt(-1);
        if (RvCaptureContract::supportsProductMode(mode) && !modes.contains(mode)) modes.append(mode);
    }
    require(!modes.isEmpty() && QRegularExpression("^[0-9a-f]{32}$").match(body["epoch"].toString()).hasMatch(),
            QStringLiteral("相机服务版本不兼容"));
    return {e,pinOf(s->peerCertificate()),result["boot"].toString(),body["epoch"].toString(),body["legacy_public_key"].toString(),body["active"].toBool(),modes};
}
} // namespace DeviceService

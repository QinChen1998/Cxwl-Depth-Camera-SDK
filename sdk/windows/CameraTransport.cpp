#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <wincred.h>
#include <bcrypt.h>
#endif
#include "../../RuntimeDiscovery.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QLockFile>
#include <QtCore/QScopeGuard>
#include <QtCore/QDeadlineTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QUuid>
#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkProxy>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "CameraTransport.h"
#include "../../tools/rv_capture/Contract.h"
namespace DeviceService {
namespace Transport {
void require(bool valid,const QString &why) {
    if(!valid) throw std::runtime_error(why.toUtf8().constData());
}
QHostAddress cameraAddress(quint32 local) {
    constexpr quint32 cameraIpv4 = 0xac20005d; // 172.32.0.93
    constexpr quint32 netmask = 0xffff0000;
    if (local == cameraIpv4 || (local & netmask) != (cameraIpv4 & netmask)) return {};
    return QHostAddress(cameraIpv4);
}
#ifdef _WIN32
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
void bindInterface(QTcpSocket &socket,const Endpoint &endpoint) {
    require(usbParents().value(endpoint.adapter)==endpoint.serial,QStringLiteral("USB 网卡已变化"));
    const DWORD index=htonl(endpoint.index);
    require(setsockopt(SOCKET(socket.socketDescriptor()),IPPROTO_IP,IP_UNICAST_IF,
                       reinterpret_cast<const char*>(&index),sizeof(index))==0,QStringLiteral("USB 网卡已变化"));
}
#endif
QByteArray take(QTcpSocket &s,int count,QDeadlineTimer &deadline) {
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
Channel openChannel(const Endpoint &e,const QString &instance,const QString &boot) {
    require(qEnvironmentVariable("RV_TEST_OFFLINE_ONLY")!="1",QStringLiteral("离线测试禁止连接相机"));
    auto socket=std::make_unique<QTcpSocket>();
    socket->setProxy(QNetworkProxy::NoProxy);
    require(socket->bind(e.local),QStringLiteral("无法绑定已核验的 USB 网卡"));
    bindInterface(*socket,e);
    socket->connectToHost(e.remote,8943);
    const auto incompatible=QStringLiteral("相机 USB 直连接口未就绪。请检查数据线及 SDK 与固件的配套版本。");
    QDeadlineTimer deadline(4500);
    require(socket->waitForConnected(int(deadline.remainingTime())),incompatible+socket->errorString());
    socket->setSocketOption(QAbstractSocket::LowDelayOption,1);
    QByteArray preface;
    try { preface=take(*socket,124,deadline); }
    catch(const std::runtime_error &) { throw std::runtime_error(incompatible.toUtf8().constData()); }
    static const QRegularExpression pattern(QStringLiteral(
        "\\ACX2 ([0-9a-f]{16}) ([0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}) ([0-9a-f]{32}) [0-9a-f]{32}\\n\\z"));
    const auto matched=pattern.match(QString::fromLatin1(preface));
    require(matched.hasMatch() && matched.captured(1)==e.serial,QStringLiteral("相机 USB 协议版本或序列号不匹配"));
    require(instance.isEmpty() || matched.captured(3)==instance,QStringLiteral("设备实例与登记记录不匹配，请通过 USB 重新连接并登记"));
    require(boot.isEmpty() || matched.captured(2)==boot,QStringLiteral("设备已重启，请重新连接"));
    return {std::move(socket),e.serial,matched.captured(2),matched.captured(3),preface};
}
QJsonObject exchange(Channel &channel,const QByteArray &op,const QByteArray &credential,const QList<QByteArray> &args,int ms) {
    auto &s=*channel.socket;
    const auto id=QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    require(!credential.isEmpty() || op=="HELLO",QStringLiteral("请先通过设备管理登记这台相机"));
    QByteArray owner="-";
    if(!credential.isEmpty()) {
        const auto publicKey=hostPublicKey(credential);
        owner=op=="PAIR"?publicKey.toHex():sha(publicKey);
    }
    QByteArray request="CX2 "+id+' '+op+' '+channel.boot.toLatin1()+' '+owner;
    for(const auto &arg:args) {
        require(!arg.isEmpty() && std::all_of(arg.cbegin(),arg.cend(),[](unsigned char c){return c>32 && c<127;}),
                QStringLiteral("无效的设备参数"));
        request+=' ';request+=arg;
    }
    const auto proof=credential.isEmpty()?QByteArray("-"):signHostRequest(credential,channel.preface+request).toHex();
    request+=' ';request+=proof;request+='\n';
    require(request.size()<=768 && s.write(request)==request.size(),QStringLiteral("无法发送设备请求"));
    s.flush();
    QDeadlineTimer deadline(ms);
    const auto header=take(s,4,deadline);
    auto length=qFromBigEndian<quint32>(header.constData());
    require(length>0 && length<=128*1024,QStringLiteral("无效的服务回执长度"));
    auto doc=QJsonDocument::fromJson(take(s,length,deadline)).object();
    require(doc["v"]==2 && doc["id"].toString().toLatin1()==id && doc["serial"]==channel.serial
        && doc["boot"]==channel.boot && doc["instance"]==channel.instance && doc["body"].isObject(),
        QStringLiteral("设备服务回执身份不匹配"));
    require(doc["code"]=="ok",doc["code"].toString()+": "+doc["body"].toObject()["error"].toString());
    return doc;
}
#ifdef _WIN32
namespace {
constexpr int curveBytes=32;
constexpr int credentialBytes=sizeof(BCRYPT_ECCKEY_BLOB)+3*curveBytes;
const auto invalidKey=QStringLiteral("本机设备登记损坏");

void validateHostKey(const QByteArray &key) {
    BCRYPT_ECCKEY_BLOB header{};
    require(key.size()==credentialBytes,invalidKey);
    std::memcpy(&header,key.constData(),sizeof(header));
    require(header.dwMagic==BCRYPT_ECDSA_PRIVATE_P256_MAGIC && header.cbKey==curveBytes,invalidKey);
}

BCRYPT_ALG_HANDLE openHostAlgorithm() {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)==0,
            QStringLiteral("无法初始化主机签名算法"));
    return algorithm;
}

QByteArray generateHostKey() {
    const auto algorithm=openHostAlgorithm();
    const auto close=qScopeGuard([=]{BCryptCloseAlgorithmProvider(algorithm,0);});
    BCRYPT_KEY_HANDLE key=nullptr;
    const auto error=QStringLiteral("无法生成主机身份");
    require(BCryptGenerateKeyPair(algorithm,&key,256,0)==0,error);
    const auto destroy=qScopeGuard([=]{BCryptDestroyKey(key);});
    require(BCryptFinalizeKeyPair(key,0)==0,error);
    QByteArray blob(credentialBytes,Qt::Uninitialized);
    ULONG size=0;
    require(BCryptExportKey(key,nullptr,BCRYPT_ECCPRIVATE_BLOB,reinterpret_cast<PUCHAR>(blob.data()),
                            ULONG(blob.size()),&size,0)==0 && size==ULONG(blob.size()),error);
    return blob;
}
}

QByteArray credential(const Identity &i,bool create) {
    const auto target=(QStringLiteral("CXWL USB Direct/")+i.endpoint.serial+"/"+i.instance).toStdWString();
    const auto directory=RuntimeDiscovery::managedConfigurationDirectory();
    QLockFile lock(QDir(directory).filePath(QStringLiteral("usb-host-")+i.endpoint.serial+"-"+i.instance+".lock"));
    if(create) require(QDir().mkpath(directory) && lock.tryLock(2000),QStringLiteral("本机设备登记不可写或正在使用"));
    PCREDENTIALW existing=nullptr;
    if(CredReadW(target.c_str(),CRED_TYPE_GENERIC,0,&existing)) {
        QByteArray bytes(reinterpret_cast<char*>(existing->CredentialBlob),int(existing->CredentialBlobSize));
        SecureZeroMemory(existing->CredentialBlob,existing->CredentialBlobSize);
        CredFree(existing);
        validateHostKey(bytes);
        return bytes;
    }
    require(GetLastError()==ERROR_NOT_FOUND && create,QStringLiteral("请先通过设备管理登记这台相机"));
    auto secret=generateHostKey();
    CREDENTIALW item{};item.Type=CRED_TYPE_GENERIC;item.TargetName=const_cast<wchar_t*>(target.c_str());
    item.CredentialBlobSize=DWORD(secret.size());item.CredentialBlob=reinterpret_cast<LPBYTE>(secret.data());item.Persist=CRED_PERSIST_LOCAL_MACHINE;
    require(CredWriteW(&item,0),QStringLiteral("Windows 无法保存本机的设备登记"));
    return secret;
}
QByteArray hostPublicKey(const QByteArray &key) {
    validateHostKey(key);
    return QByteArray(1,'\x04')+key.mid(sizeof(BCRYPT_ECCKEY_BLOB),2*curveBytes);
}
QByteArray signHostRequest(const QByteArray &key,const QByteArray &message) {
    validateHostKey(key);
    const auto algorithm=openHostAlgorithm();
    const auto close=qScopeGuard([=]{BCryptCloseAlgorithmProvider(algorithm,0);});
    BCRYPT_KEY_HANDLE imported=nullptr;
    require(BCryptImportKeyPair(algorithm,nullptr,BCRYPT_ECCPRIVATE_BLOB,&imported,
                                reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),ULONG(key.size()),0)==0,invalidKey);
    const auto destroy=qScopeGuard([=]{BCryptDestroyKey(imported);});
    auto digest=QCryptographicHash::hash(message,QCryptographicHash::Sha256);
    QByteArray proof(2*curveBytes,Qt::Uninitialized);
    ULONG size=0;
    require(BCryptSignHash(imported,nullptr,reinterpret_cast<PUCHAR>(digest.data()),ULONG(digest.size()),
                           reinterpret_cast<PUCHAR>(proof.data()),ULONG(proof.size()),&size,0)==0 && size==ULONG(proof.size()),
            QStringLiteral("无法为设备操作生成主机签名"));
    return proof;
}
#endif
QJsonObject request(const Identity &i,const QByteArray &secret,const QByteArray &op,const QList<QByteArray> &args,int ms) {
    auto channel=openChannel(i.endpoint,i.instance,i.boot);
    return exchange(channel,op,secret,args,ms)["body"].toObject();
}
Identity verified(const RvDeviceProfile &device) {
    require(!device.serviceInstance.isEmpty(),QStringLiteral("请通过 USB 连接以迁移旧版设备档案"));
    auto i=inspect(device.serial);
    require(i.instance==device.serviceInstance,QStringLiteral("设备实例与登记记录不匹配，请通过 USB 重新连接并登记"));
    return i;
}
Transfer transfer(const Identity &i,const QByteArray &secret,const QByteArray &op,const QList<QByteArray> &args) {
    auto channel=openChannel(i.endpoint,i.instance,i.boot);
    const auto initial=exchange(channel,op,secret,args,30000);
    return {std::move(channel.socket),initial["id"].toString().toLatin1()};
}
void transferred(Transfer &t,const Identity &i,QDeadlineTimer &deadline) {
    const auto size=qFromBigEndian<quint32>(take(*t.socket,4,deadline).constData());
    require(size>0 && size<=16384,QStringLiteral("设备传输结果长度无效"));
    const auto j=QJsonDocument::fromJson(take(*t.socket,int(size),deadline)).object();
    require(j["v"]==2 && j["id"].toString().toLatin1()==t.id && j["serial"]==i.endpoint.serial && j["boot"]==i.boot
            && j["instance"]==i.instance && j["code"]=="ok",
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
#ifdef _WIN32
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
            const auto remote=cameraAddress(host);
            if(remote.isNull()) continue;
            result.append({parents[adapter],adapter,QHostAddress(host),remote,a->IfIndex});break;
        }
    }
    return result;
}
#endif
Identity inspect(const QString &selected) {
    auto choices=attached();
    if(!selected.isEmpty()) choices.erase(std::remove_if(choices.begin(),choices.end(),[&](auto &e){return e.serial!=selected;}),choices.end());
    require(choices.size()==1,choices.isEmpty()?QStringLiteral("没有找到目标 USB 相机。请检查数据线，并为相机 RNDIS 网卡配置 172.32 网段的主机地址。")
        :QStringLiteral("检测到多台相机，请指定已登记设备或单独接入待登记相机。"));
    const auto e=choices.first();auto channel=openChannel(e);
    const auto result=exchange(channel,"HELLO",{},{});
    const auto body=result["body"].toObject();
    QList<int> modes;
    for (const auto &item : body["modes"].toArray()) {
        const int mode = item.toInt(-1);
        if (RvCaptureContract::supportsProductMode(mode) && !modes.contains(mode)) modes.append(mode);
    }
    require(!modes.isEmpty() && QRegularExpression("^[0-9a-f]{32}$").match(body["epoch"].toString()).hasMatch(),
            QStringLiteral("相机服务版本不兼容"));
    return {e,channel.instance,channel.boot,body["epoch"].toString(),body["active"].toBool(),modes};
}
} // namespace DeviceService

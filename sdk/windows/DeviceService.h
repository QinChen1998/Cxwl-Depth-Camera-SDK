#pragma once
#include "../../sensors/RvDeviceStore.h"
#include <QtCore/QJsonObject>
#include <QtCore/QThread>
#include <QtCore/QSemaphore>
#include <QtNetwork/QHostAddress>
#include <memory>

namespace DeviceService {
struct Endpoint {
    QString serial, adapter;
    QHostAddress local, remote;
    quint32 index=0;
};
struct Identity {
    Endpoint endpoint;
    QString pin, boot, epoch, legacyKey;
    bool active=false;
    QList<int> modes;
};
struct Capture {
    Identity identity;
    QByteArray credential, grant;
    QString generation;
    QJsonObject receipt;
};
QList<Endpoint> attached();
Identity inspect(const QString &serial={});
RvDeviceProfile profile(const Identity &,const QVector<RvDeviceProfile> &);
void enroll(const Identity &);
QString hostAuthorizationId(const Identity &);
Capture start(const RvDeviceProfile &,int mode,const QString &generation,const QString &expectedBoot);
void stop(const Capture &);
void heartbeat(const Capture &);
QJsonObject manage(const RvDeviceProfile &,const QByteArray &operation);

class MediaPipe;
QJsonObject tune(const RvDeviceProfile &,const QString &,const QJsonObject &,std::shared_ptr<MediaPipe> &);

class MediaPipe final:public QThread {
public:
    MediaPipe(const Capture &,bool color);
    MediaPipe(const Capture &,QByteArray stream);
    ~MediaPipe() override;
    quint16 listenPort();
protected:
    void run() override;
private:
    Capture capture;
    QByteArray stream;
    QSemaphore ready;
    quint16 port=0;
};
}

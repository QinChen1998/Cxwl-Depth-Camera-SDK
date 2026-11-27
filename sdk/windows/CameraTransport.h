#pragma once
#include "DeviceService.h"
#include <QtCore/QDeadlineTimer>
#include <QtNetwork/QTcpSocket>
#include <functional>

namespace DeviceService::Transport {
void require(bool valid, const QString &why);
QHostAddress cameraAddress(quint32 local);
void bindInterface(QTcpSocket &socket, const Endpoint &endpoint);
Identity verified(const RvDeviceProfile &device);
QByteArray credential(const Identity &identity, bool create);
QByteArray hostPublicKey(const QByteArray &key);
QByteArray signHostRequest(const QByteArray &key, const QByteArray &message);
struct Channel {
    std::unique_ptr<QTcpSocket> socket;
    Endpoint endpoint;
    QString boot, instance;
    QByteArray preface;
};
Channel openChannel(const Endpoint &endpoint, const QString &instance = {}, const QString &boot = {});
QJsonObject exchange(Channel &, const QByteArray &op, const QByteArray &credential,
                     const QList<QByteArray> &args, int ms = 12000);
QJsonObject request(const Identity &, const QByteArray &secret, const QByteArray &op,
                    const QList<QByteArray> &args = {}, int ms = 12000);
struct Transfer {
    std::unique_ptr<QTcpSocket> socket;
    QByteArray id;
};
Transfer transfer(const Identity &, const QByteArray &secret, const QByteArray &op, const QList<QByteArray> &args);
void transferred(Transfer &, const Identity &, QDeadlineTimer &);
void download(const Identity &, const QByteArray &secret, const QByteArray &op, const QList<QByteArray> &args,
              qint64 maximum, const std::function<void(const QByteArray &)> &consume);
QByteArray sha(const QByteArray &data);
}

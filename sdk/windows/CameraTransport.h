#pragma once
#include "DeviceService.h"
#include <QtCore/QDeadlineTimer>
#include <QtNetwork/QSslSocket>
#include <functional>

namespace DeviceService::Transport {
void require(bool valid, const QString &why);
QHostAddress cameraAddress(quint32 local);
void bindInterface(QSslSocket &socket, const Endpoint &endpoint);
Identity verified(const RvDeviceProfile &device);
QByteArray credential(const Identity &identity, bool create);
std::unique_ptr<QSslSocket> openChannel(const Endpoint &endpoint, const QString &expected);
QJsonObject exchange(QSslSocket &, const Endpoint &, const QByteArray &op, const QString &boot,
                     const QByteArray &credential, const QList<QByteArray> &args, int ms = 12000);
QJsonObject request(const Identity &, const QByteArray &secret, const QByteArray &op,
                    const QList<QByteArray> &args = {}, int ms = 12000);
struct Transfer {
    std::unique_ptr<QSslSocket> socket;
    QByteArray id;
};
Transfer transfer(const Identity &, const QByteArray &secret, const QByteArray &op, const QList<QByteArray> &args);
void transferred(Transfer &, const Identity &, QDeadlineTimer &);
void download(const Identity &, const QByteArray &secret, const QByteArray &op, const QList<QByteArray> &args,
              qint64 maximum, const std::function<void(const QByteArray &)> &consume);
QByteArray sha(const QByteArray &data);
}

#include "../windows/CameraTransport.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkInterface>
#include <cstring>
#include <net/if.h>
#include <sys/socket.h>

namespace DeviceService {
namespace {
QByteArray attribute(const QDir &device, const QString &name)
{
    QFile file(device.filePath(name));
    return file.open(QIODevice::ReadOnly) ? file.read(128).trimmed() : QByteArray{};
}

QString cameraSerial(const QString &interface)
{
    const auto path = QFileInfo(QStringLiteral("/sys/class/net/") + interface + "/device").canonicalFilePath();
    if (!path.startsWith(QStringLiteral("/sys/devices/"))) return {};
    QDir device(path);
    do {
        const auto vendor = attribute(device, "idVendor");
        const auto product = attribute(device, "idProduct");
        if (vendor.isEmpty() || product.isEmpty()) continue;
        if (vendor != "2207" || product != "0019") return {};
        const auto serial = QString::fromLatin1(attribute(device, "serial")).toLower();
        static const QRegularExpression valid(QStringLiteral("^[0-9a-f]{16}$"));
        return valid.match(serial).hasMatch() ? serial : QString{};
    } while (device.cdUp() && device.absolutePath().startsWith(QStringLiteral("/sys/devices/")));
    return {};
}
}

QList<Endpoint> attached()
{
    QList<Endpoint> result;
    for (const auto &interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || interface.index() <= 0) continue;
        const auto serial = cameraSerial(interface.name());
        if (serial.isEmpty()) continue;
        for (const auto &address : interface.addressEntries()) {
            if (address.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const auto remote = Transport::cameraAddress(address.ip().toIPv4Address());
            if (remote.isNull() || remote == address.ip()) continue;
            result.append({serial, interface.name(), address.ip(), remote, quint32(interface.index())});
            break;
        }
    }
    return result;
}

void Transport::bindInterface(QTcpSocket &socket, const Endpoint &endpoint)
{
    char name[IF_NAMESIZE]{};
    require(if_indextoname(endpoint.index, name) && QString::fromLocal8Bit(name) == endpoint.adapter
            && cameraSerial(endpoint.adapter) == endpoint.serial,
            QStringLiteral("USB 网卡已变化"));
    require(setsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_BINDTODEVICE, name,
                       socklen_t(std::strlen(name) + 1)) == 0,
            QStringLiteral("无法将连接限制在已核验的 USB 网卡上"));
}
}

#include "../windows/CameraTransport.h"
#include "../../RuntimeDiscovery.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QScopeGuard>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace DeviceService::Transport {
QByteArray credential(const Identity &identity, bool create)
{
    static const QRegularExpression serialPattern(QStringLiteral("^[0-9a-f]{16}$"));
    static const QRegularExpression pinPattern(QStringLiteral("^[0-9a-f]{64}$"));
    require(serialPattern.match(identity.endpoint.serial).hasMatch() && pinPattern.match(identity.pin).hasMatch(),
            QStringLiteral("无效的设备登记身份"));

    const auto parent = RuntimeDiscovery::managedConfigurationDirectory();
    const auto directory = QDir(parent).filePath(QStringLiteral("host-authorizations"));
    const auto nativeDirectory = QFile::encodeName(directory);
    const auto unavailable = QStringLiteral("请先通过设备管理登记这台相机");
    const auto invalid = QStringLiteral("本机设备登记权限或数据无效");
    if (create) {
        require(QDir().mkpath(parent), invalid);
        require(mkdir(nativeDirectory.constData(), 0700) == 0 || errno == EEXIST, invalid);
    }
    const int dir = open(nativeDirectory.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    require(dir >= 0, errno == ENOENT ? unavailable : invalid);
    const auto closeDirectory = qScopeGuard([dir] { close(dir); });
    struct stat permissions{};
    require(fstat(dir, &permissions) == 0 && permissions.st_uid == geteuid()
            && (permissions.st_mode & 0777) == 0700 && flock(dir, LOCK_EX) == 0, invalid);

    const auto name = identity.endpoint.serial + '-' + identity.pin + ".key";
    const auto nativeName = QFile::encodeName(name);
    const int fd = openat(dir, nativeName.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd >= 0) {
        const auto closeKey = qScopeGuard([fd] { close(fd); });
        require(fstat(fd, &permissions) == 0 && S_ISREG(permissions.st_mode) && permissions.st_uid == geteuid()
                && (permissions.st_mode & 0777) == 0600 && permissions.st_size == 32, invalid);
        QFile file;
        require(file.open(fd, QIODevice::ReadOnly), invalid);
        auto bytes = file.read(33);
        const auto clear = qScopeGuard([&bytes] { explicit_bzero(bytes.data(), size_t(bytes.size())); });
        require(bytes.size() == 32, invalid);
        return bytes.toHex();
    }
    require(errno == ENOENT, invalid);
    require(create, unavailable);

    QByteArray bytes(32, Qt::Uninitialized);
    const auto clear = qScopeGuard([&bytes] { explicit_bzero(bytes.data(), size_t(bytes.size())); });
    require(getentropy(bytes.data(), size_t(bytes.size())) == 0, QStringLiteral("无法生成主机身份"));
    QSaveFile file(QDir(directory).filePath(name));
    require(file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            && file.write(bytes) == bytes.size() && file.commit(), QStringLiteral("无法保存本机的设备登记"));
    return bytes.toHex();
}
}

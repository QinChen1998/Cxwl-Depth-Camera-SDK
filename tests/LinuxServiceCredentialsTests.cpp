#include "../sdk/windows/CameraTransport.h"
#include "../RuntimeDiscovery.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <cassert>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

void DeviceService::Transport::require(bool valid, const QString &message)
{
    if (!valid) throw std::runtime_error(message.toStdString());
}

int main(int argc, char **argv)
{
    QTemporaryDir temporary;
    assert(temporary.isValid());
    qputenv("XDG_DATA_HOME", QFile::encodeName(temporary.path()));
    QCoreApplication app(argc, argv);
    DeviceService::Identity identity;
    identity.endpoint.serial = QString(16, '1');
    identity.pin = QString(64, '2');
    auto rejected = [&] {
        try { DeviceService::Transport::credential(identity, false); }
        catch (const std::runtime_error &) { return true; }
        return false;
    };
    assert(rejected());
    const auto key = DeviceService::Transport::credential(identity, true);
    assert(key.size() == 64);
    assert(DeviceService::Transport::credential(identity, false) == key);
    assert(DeviceService::Transport::credential(identity, true) == key);

    const auto directory = QDir(RuntimeDiscovery::managedConfigurationDirectory()).filePath("host-authorizations");
    const auto file = QDir(directory).filePath(identity.endpoint.serial + '-' + identity.pin + ".key");
    const auto path = QFile::encodeName(file);
    assert(chmod(path.constData(), 0644) == 0);
    assert(rejected());
    assert(chmod(path.constData(), 0600) == 0);
    const auto other = file + ".saved";
    assert(QFile::rename(file, other));
    assert(symlink(QFile::encodeName(other).constData(), path.constData()) == 0);
    assert(rejected());
    assert(QFile::remove(file));
    assert(QFile::rename(other, file));
    assert(chmod(QFile::encodeName(directory).constData(), 0750) == 0);
    assert(rejected());
    assert(chmod(QFile::encodeName(directory).constData(), 0700) == 0);
    assert(DeviceService::Transport::credential(identity, false) == key);
}

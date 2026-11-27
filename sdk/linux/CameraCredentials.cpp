#include "../windows/CameraTransport.h"
#include "../../RuntimeDiscovery.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QScopeGuard>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace DeviceService::Transport {
namespace {
constexpr int maximumKeyBytes = 256;
const auto invalidKey = QStringLiteral("本机设备登记权限或数据无效");
using PKey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

PKey decodeHostKey(const QByteArray &key)
{
    const auto *current = reinterpret_cast<const unsigned char *>(key.constData());
    const auto *end = current + key.size();
    PKey decoded(d2i_AutoPrivateKey(nullptr, &current, key.size()), EVP_PKEY_free);
    char curve[32]{};
    require(decoded && current == end && EVP_PKEY_is_a(decoded.get(), "EC")
            && EVP_PKEY_get_group_name(decoded.get(), curve, sizeof(curve), nullptr) == 1
            && std::strcmp(curve, "prime256v1") == 0, invalidKey);
    return decoded;
}

QByteArray generateHostKey()
{
    PKey key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
    const auto error = QStringLiteral("无法生成主机身份");
    require(bool(key), error);
    const int size = i2d_PrivateKey(key.get(), nullptr);
    require(size > 0 && size <= maximumKeyBytes, error);
    QByteArray bytes(size, Qt::Uninitialized);
    auto *out = reinterpret_cast<unsigned char *>(bytes.data());
    require(i2d_PrivateKey(key.get(), &out) == size, error);
    return bytes;
}
}

QByteArray credential(const Identity &identity, bool create)
{
    static const QRegularExpression serialPattern(QStringLiteral("^[0-9a-f]{16}$"));
    static const QRegularExpression instancePattern(QStringLiteral("^[0-9a-f]{32}$"));
    require(serialPattern.match(identity.endpoint.serial).hasMatch() && instancePattern.match(identity.instance).hasMatch(),
            QStringLiteral("无效的设备登记身份"));

    const auto parent = RuntimeDiscovery::managedConfigurationDirectory();
    const auto directory = QDir(parent).filePath(QStringLiteral("usb-host-authorizations"));
    const auto nativeDirectory = QFile::encodeName(directory);
    const auto unavailable = QStringLiteral("请先通过设备管理登记这台相机");
    const auto &invalid = invalidKey;
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

    const auto name = identity.endpoint.serial + '-' + identity.instance + ".key";
    const auto nativeName = QFile::encodeName(name);
    const int fd = openat(dir, nativeName.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd >= 0) {
        const auto closeKey = qScopeGuard([fd] { close(fd); });
        require(fstat(fd, &permissions) == 0 && S_ISREG(permissions.st_mode) && permissions.st_uid == geteuid()
                && (permissions.st_mode & 0777) == 0600 && permissions.st_size > 0
                && permissions.st_size <= maximumKeyBytes, invalid);
        QFile file;
        require(file.open(fd, QIODevice::ReadOnly), invalid);
        const auto bytes = file.read(maximumKeyBytes + 1);
        require(bytes.size() == permissions.st_size, invalid);
        decodeHostKey(bytes);
        return bytes;
    }
    require(errno == ENOENT, invalid);
    require(create, unavailable);

    const auto bytes = generateHostKey();
    QSaveFile file(QDir(directory).filePath(name));
    require(file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            && file.write(bytes) == bytes.size() && file.commit(), QStringLiteral("无法保存本机的设备登记"));
    return bytes;
}

QByteArray hostPublicKey(const QByteArray &key)
{
    const auto decoded = decodeHostKey(key);
    QByteArray publicKey(65, Qt::Uninitialized);
    // Export affine coordinates so OpenSSL's point compression default cannot change the host ID.
    auto *out = reinterpret_cast<unsigned char *>(publicKey.data());
    *out++ = 4;
    const char *coordinates[] = {OSSL_PKEY_PARAM_EC_PUB_X, OSSL_PKEY_PARAM_EC_PUB_Y};
    for (const auto *coordinate : coordinates) {
        BIGNUM *value = nullptr;
        const int extracted = EVP_PKEY_get_bn_param(decoded.get(), coordinate, &value);
        const std::unique_ptr<BIGNUM, decltype(&BN_free)> owned(value, BN_free);
        require(extracted == 1 && BN_bn2binpad(value, out, 32) == 32, invalidKey);
        out += 32;
    }
    return publicKey;
}

QByteArray signHostRequest(const QByteArray &key, const QByteArray &message)
{
    const auto decoded = decodeHostKey(key);
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    const auto error = QStringLiteral("无法为设备操作生成主机签名");
    unsigned char der[72];
    size_t size = sizeof(der);
    require(ctx && EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, decoded.get()) == 1
            && EVP_DigestSign(ctx.get(), der, &size, reinterpret_cast<const unsigned char *>(message.constData()),
                              size_t(message.size())) == 1, error);
    const unsigned char *encoded = der;
    const std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(d2i_ECDSA_SIG(nullptr, &encoded, size), ECDSA_SIG_free);
    require(signature && encoded == der + size, error);
    QByteArray proof(64, Qt::Uninitialized);
    auto *out = reinterpret_cast<unsigned char *>(proof.data());
    require(BN_bn2binpad(ECDSA_SIG_get0_r(signature.get()), out, 32) == 32
            && BN_bn2binpad(ECDSA_SIG_get0_s(signature.get()), out + 32, 32) == 32, error);
    return proof;
}
}

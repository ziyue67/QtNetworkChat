#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace E2ECodecSupport {
inline constexpr qsizetype MaxKeyIdLength = 128;
inline constexpr qsizetype MaxAadLength = 512;
inline constexpr qsizetype MinNonceBytes = 12;
inline constexpr qsizetype MaxNonceBytes = 24;
inline constexpr qsizetype MinTagBytes = 16;
inline constexpr qsizetype MaxTagBytes = 32;
inline constexpr qsizetype MaxPublicKeyBytes = 4096;
inline constexpr qsizetype FingerprintHexLength = 64;
inline constexpr qsizetype MaxSignatureBytes = 4096;

inline QString trimmed(QString value) {
    return value.trimmed();
}

inline QByteArray base64Field(const QJsonObject& obj, const char* name) {
    return QByteArray::fromBase64(obj.value(QString::fromLatin1(name)).toString().toLatin1(),
                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

inline QString toBase64Url(const QByteArray& value) {
    return QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

inline bool fail(QString* reason, const QString& value) {
    if (reason) {
        *reason = value;
    }
    return false;
}

inline bool validIdentity(const QString& value) {
    return !value.trimmed().isEmpty() && value.size() <= 128;
}

inline bool validOptionalFingerprint(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) return true;
    if (normalized.size() != FingerprintHexLength) return false;
    for (const QChar ch : normalized) {
        const ushort code = ch.unicode();
        const bool digit = code >= '0' && code <= '9';
        const bool lowerHex = code >= 'a' && code <= 'f';
        if (!digit && !lowerHex) return false;
    }
    return true;
}
} // namespace E2ECodecSupport

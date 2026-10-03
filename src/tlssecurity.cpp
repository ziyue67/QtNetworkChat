#include "tlssecurity.h"

#include <QCryptographicHash>

QString normalizedSha256Fingerprint(QString fingerprint) {
    fingerprint = fingerprint.trimmed().toLower();
    fingerprint.remove(QLatin1Char(':'));
    fingerprint.remove(QLatin1Char('-'));
    fingerprint.remove(QLatin1Char(' '));
    if (fingerprint.startsWith(QStringLiteral("sha256="))) {
        fingerprint = fingerprint.mid(7);
    }
    if (fingerprint.startsWith(QStringLiteral("sha256/"))) {
        fingerprint = fingerprint.mid(7);
    }
    return fingerprint;
}

QString certificateSha256Fingerprint(const QSslCertificate& certificate) {
    if (certificate.isNull()) {
        return QString();
    }
    return QString::fromLatin1(QCryptographicHash::hash(certificate.toDer(), QCryptographicHash::Sha256).toHex());
}

bool pinnedCertificateFingerprintMatches(const QSslCertificate& certificate,
                                         const QString& expectedFingerprint,
                                         QString* actualFingerprint) {
    const QString actual = certificateSha256Fingerprint(certificate);
    if (actualFingerprint) {
        *actualFingerprint = actual;
    }
    const QString expected = normalizedSha256Fingerprint(expectedFingerprint);
    return !actual.isEmpty()
        && expected.size() == 64
        && actual.compare(expected, Qt::CaseInsensitive) == 0;
}

QString configuredPinnedTlsFingerprint() {
    const QString value = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_PINNED_SHA256")).trimmed();
    if (!value.isEmpty()) {
        return value;
    }
    return QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_TLS_PINNED_FINGERPRINT_SHA256")).trimmed();
}

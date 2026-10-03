#ifndef TLSSECURITY_H
#define TLSSECURITY_H

#include <QSslCertificate>
#include <QString>

QString normalizedSha256Fingerprint(QString fingerprint);
QString certificateSha256Fingerprint(const QSslCertificate& certificate);
bool pinnedCertificateFingerprintMatches(const QSslCertificate& certificate,
                                         const QString& expectedFingerprint,
                                         QString* actualFingerprint = nullptr);
QString configuredPinnedTlsFingerprint();

#endif // TLSSECURITY_H

#include "tlssecurity.h"

#include <QCoreApplication>
#include <QSslCertificate>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;
    ok = expect(normalizedSha256Fingerprint("SHA256=AA:BB-cc dd") == QStringLiteral("aabbccdd"),
                "fingerprint normalization should remove prefixes and separators") && ok;
    ok = expect(normalizedSha256Fingerprint("sha256/0123") == QStringLiteral("0123"),
                "fingerprint normalization should accept sha256 slash prefix") && ok;
    ok = expect(!pinnedCertificateFingerprintMatches(QSslCertificate(), QString(64, QLatin1Char('a'))),
                "null certificate should not match a pinned fingerprint") && ok;

    // This self-signed certificate is test data, not a production trust anchor.
    const QSslCertificate testCertificate(QByteArrayLiteral(R"PEM(
-----BEGIN CERTIFICATE-----
MIIDCTCCAfGgAwIBAgIUCH+X4myKn0+clfETMBVd3dCk4cMwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MTAwMzEzNTYxOFoXDTM2MDkz
MDEzNTYxOFowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAy2qEgGv2TYsU/ITfcv1n5w2b5vRpAX+ecrKCSuEQraz+
jaQv1voZ2o+QqqGKZam2wjWkgzqzrsjo2LlvuWyKPjU+dWvKkv6bCwPQRSJ2grJx
OOXaTTgQcl/RibyjTCnaNfmAdP2oAN1MjBgO2kFb98QWVffpABy3HOllVzAjT/aR
zEGQA8szqgV3kSFIIQMOnaidOAVhPST9LhogavpVfGiVYl9vXg/mcsUyjwRas8Kw
qB8RpAvzp58Ew2yJMSfmbPAelq3LLgqvUKKyWtwUG2xx1JIyoc0kH0l+EPMAEUaK
LNGCN5E4ZmJmg5yx0IN4Vm7WFgu7PLe6Uz90zmpKoQIDAQABo1MwUTAdBgNVHQ4E
FgQUVgxiwt3nYAcaL2zuLlCx1tai/38wHwYDVR0jBBgwFoAUVgxiwt3nYAcaL2zu
LlCx1tai/38wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEABfKa
GupgFfTKGsZc29k465AhUbpzk88QWxiylB1tstOlLRMIUwVjjmow5S2bYzW320W8
qgJBi1AvhSLO+DYsgkEBwZ4r09PMroaa77pHs9L5thDdzlVYfrtUPkOJc3ehZvWc
Uwhu3jsWlKe/8rxY1F43qG9mU6kgUUmc0sRXW2Pd6fMkOBKjJ/VYEet5ew8Kc+jF
2XFZAa1rxV4TdH4Hdxw+Ey7KHKrQ1S8h7uA2ErfObiG2JhtMWbVCWGp+w/C+hlko
t69ufCPtsgCow+i3o8nR0YueriEXCIDaw/BD2xG189d6QRniijFEu1r9Hp/bn07a
cffUj2IB5RjonMA23A==
-----END CERTIFICATE-----
)PEM"));
    const QString testFingerprint = certificateSha256Fingerprint(testCertificate);
    ok = expect(!testCertificate.isNull() && testFingerprint.size() == 64,
                "test certificate should produce a SHA-256 fingerprint") && ok;
    ok = expect(pinnedCertificateFingerprintMatches(testCertificate, testFingerprint.toUpper()),
                "the exact certificate pin should match regardless of hex case") && ok;
    ok = expect(!pinnedCertificateFingerprintMatches(testCertificate, QString(64, QLatin1Char('a'))),
                "a different certificate pin should be rejected") && ok;

    qputenv("QTNETWORKCHAT_TLS_PINNED_SHA256", "AA:BB");
    ok = expect(configuredPinnedTlsFingerprint() == QStringLiteral("AA:BB"),
                "primary TLS pin environment variable should be read") && ok;
    qunsetenv("QTNETWORKCHAT_TLS_PINNED_SHA256");
    qputenv("QTNETWORKCHAT_TLS_PINNED_FINGERPRINT_SHA256", "CC:DD");
    ok = expect(configuredPinnedTlsFingerprint() == QStringLiteral("CC:DD"),
                "compat TLS pin environment variable should be read") && ok;
    qunsetenv("QTNETWORKCHAT_TLS_PINNED_FINGERPRINT_SHA256");

    return ok ? 0 : 1;
}

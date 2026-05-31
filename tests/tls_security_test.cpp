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

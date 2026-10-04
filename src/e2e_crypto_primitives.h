#pragma once

#include "e2eenvelope.h"

namespace E2ECryptoPrimitives {
inline constexpr quint32 DraftDhPrime = 2147483647u;
inline constexpr char ProductionSessionDerivePrimaryDomain[] =
    "qtnetworkchat-e2e-authenticated-production-v1|";
inline constexpr char ProductionSessionDeriveSecondaryDomain[] =
    "qtnetworkchat-e2e-production-session-public-material-v1|";

QByteArray randomBytes(qsizetype size);
quint32 readDhValue(const QByteArray& value, const char* prefix);
QByteArray writeDhValue(const char* prefix, quint32 value);
quint32 modPow(quint32 base, quint32 exponent);
QByteArray hmacSha256(const QByteArray& key, const QByteArray& data);
bool constantTimeEqual(const QByteArray& left, const QByteArray& right);
QByteArray streamXor(const QByteArray& sessionKey, const QByteArray& nonce,
                     const QString& aad, const QByteArray& input);
QByteArray envelopeTagData(const E2EEnvelope& envelope);
QByteArray agreementSignatureData(const E2EKeyAgreement& agreement);
QByteArray agreementTranscriptData(const E2EKeyAgreement& left,
                                   const E2EKeyAgreement& right,
                                   const QByteArray& sharedSecret);
QByteArray productionSessionDerivePrimary(const E2EKeyAgreement& left,
                                          const E2EKeyAgreement& right);
QByteArray productionSessionDeriveSecondary(const E2EKeyAgreement& left,
                                            const E2EKeyAgreement& right);
QByteArray productionSessionDeriveAad(const E2EKeyAgreement& left,
                                      const E2EKeyAgreement& right);
} // namespace E2ECryptoPrimitives

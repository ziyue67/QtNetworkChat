#include "e2e_crypto_primitives.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

namespace E2ECryptoPrimitives {
QByteArray randomBytes(qsizetype size) {
    QByteArray value;
    value.resize(size);
    for (qsizetype i = 0; i < size; ++i) {
        value[static_cast<int>(i)] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return value;
}

quint32 readDhValue(const QByteArray& value, const char* prefix) {
    const QByteArray marker(prefix);
    if (!value.startsWith(marker) || value.size() != marker.size() + 4) {
        return 0;
    }
    quint32 result = 0;
    for (int i = marker.size(); i < value.size(); ++i) {
        result = (result << 8) | static_cast<unsigned char>(value.at(i));
    }
    return result;
}

QByteArray writeDhValue(const char* prefix, quint32 value) {
    QByteArray result(prefix);
    result.append(static_cast<char>((value >> 24) & 0xff));
    result.append(static_cast<char>((value >> 16) & 0xff));
    result.append(static_cast<char>((value >> 8) & 0xff));
    result.append(static_cast<char>(value & 0xff));
    return result;
}

quint32 modMul(quint32 a, quint32 b) {
    return static_cast<quint32>((static_cast<quint64>(a) * static_cast<quint64>(b)) % DraftDhPrime);
}

quint32 modPow(quint32 base, quint32 exponent) {
    quint32 result = 1;
    quint32 factor = base % DraftDhPrime;
    quint32 power = exponent;
    while (power > 0) {
        if ((power & 1u) != 0u) {
            result = modMul(result, factor);
        }
        factor = modMul(factor, factor);
        power >>= 1;
    }
    return result;
}

QByteArray hmacSha256(const QByteArray& key, const QByteArray& data) {
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}

bool constantTimeEqual(const QByteArray& left, const QByteArray& right) {
    if (left.size() != right.size()) {
        return false;
    }

    volatile unsigned char difference = 0;
    for (qsizetype i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left.at(i))
            ^ static_cast<unsigned char>(right.at(i));
    }
    return difference == 0;
}

QByteArray streamXor(const QByteArray& sessionKey,
                     const QByteArray& nonce,
                     const QString& aad,
                     const QByteArray& input) {
    QByteArray output;
    output.resize(input.size());
    qsizetype offset = 0;
    quint32 counter = 0;
    while (offset < input.size()) {
        QByteArray seed;
        seed.reserve(sessionKey.size() + nonce.size() + aad.toUtf8().size() + 8);
        seed.append(sessionKey);
        seed.append(nonce);
        seed.append(aad.toUtf8());
        seed.append(static_cast<char>((counter >> 24) & 0xff));
        seed.append(static_cast<char>((counter >> 16) & 0xff));
        seed.append(static_cast<char>((counter >> 8) & 0xff));
        seed.append(static_cast<char>(counter & 0xff));
        const QByteArray block = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
        for (qsizetype i = 0; i < block.size() && offset < input.size(); ++i, ++offset) {
            output[static_cast<int>(offset)] = static_cast<char>(input[static_cast<int>(offset)] ^ block[static_cast<int>(i)]);
        }
        ++counter;
    }
    return output;
}

QByteArray envelopeTagData(const E2EEnvelope& envelope) {
    QByteArray data;
    data.append(normalizedE2EProtocol(envelope.protocol).toUtf8());
    data.append('|');
    data.append(normalizedE2ESuite(envelope.suite).toUtf8());
    data.append('|');
    data.append(envelope.senderId.trimmed().toUtf8());
    data.append('|');
    data.append(envelope.receiverId.trimmed().toUtf8());
    data.append('|');
    data.append(envelope.keyId.trimmed().toUtf8());
    data.append('|');
    data.append(envelope.aad.toUtf8());
    data.append('|');
    data.append(envelope.nonce);
    data.append('|');
    data.append(envelope.ciphertext);
    return data;
}

QByteArray agreementSignatureData(const E2EKeyAgreement& agreement) {
    QByteArray data;
    data.append("qtnetworkchat-e2e-agreement-signature-v1|");
    data.append(normalizedE2EProtocol(agreement.protocol).toUtf8());
    data.append('|');
    data.append(normalizedE2ESuite(agreement.suite).toUtf8());
    data.append('|');
    data.append(agreement.senderId.trimmed().toUtf8());
    data.append('|');
    data.append(agreement.receiverId.trimmed().toUtf8());
    data.append('|');
    data.append(agreement.keyId.trimmed().toUtf8());
    data.append('|');
    data.append(e2eFingerprint(agreement.publicKey).toUtf8());
    data.append('|');
    data.append(agreement.senderIdentityFingerprint.trimmed().toLower().toUtf8());
    data.append('|');
    data.append(agreement.receiverIdentityFingerprint.trimmed().toLower().toUtf8());
    return data;
}

QByteArray agreementTranscriptData(const E2EKeyAgreement& left,
                                   const E2EKeyAgreement& right,
                                   const QByteArray& sharedSecret) {
    const E2EKeyAgreement* first = &left;
    const E2EKeyAgreement* second = &right;
    if (left.senderId > right.senderId
        || (left.senderId == right.senderId && left.keyId > right.keyId)) {
        first = &right;
        second = &left;
    }

    QByteArray data;
    data.append("qtnetworkchat-e2e-authenticated-draft-v1|");
    data.append(sharedSecret.toHex());
    data.append('|');
    for (const E2EKeyAgreement* agreement : {first, second}) {
        data.append(normalizedE2EProtocol(agreement->protocol).toUtf8());
        data.append('|');
        data.append(normalizedE2ESuite(agreement->suite).toUtf8());
        data.append('|');
        data.append(agreement->senderId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->receiverId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->keyId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->senderIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
        data.append(agreement->receiverIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
        data.append(e2eFingerprint(agreement->publicKey).toUtf8());
        data.append('|');
    }
    return data;
}

QByteArray productionSessionDerivePrimary(const E2EKeyAgreement& left,
                                          const E2EKeyAgreement& right) {
    QByteArray transcript =
        agreementTranscriptData(left,
                                right,
                                QByteArrayLiteral("production-provider-session-shared-v1"));
    const QByteArray draftDomain = QByteArrayLiteral("qtnetworkchat-e2e-authenticated-draft-v1|");
    if (transcript.startsWith(draftDomain)) {
        transcript.remove(0, draftDomain.size());
    }
    return QByteArray(ProductionSessionDerivePrimaryDomain) + transcript;
}

QByteArray productionSessionDeriveSecondary(const E2EKeyAgreement& left,
                                            const E2EKeyAgreement& right) {
    const E2EKeyAgreement* first = &left;
    const E2EKeyAgreement* second = &right;
    if (left.senderId > right.senderId
        || (left.senderId == right.senderId && left.keyId > right.keyId)) {
        first = &right;
        second = &left;
    }

    QByteArray data;
    data.append(ProductionSessionDeriveSecondaryDomain);
    for (const E2EKeyAgreement* agreement : {first, second}) {
        data.append(agreement->senderId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->receiverId.trimmed().toUtf8());
        data.append('|');
        data.append(agreement->keyId.trimmed().toUtf8());
        data.append('|');
        data.append(e2eFingerprint(agreement->publicKey).toUtf8());
        data.append('|');
        data.append(agreement->senderIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
        data.append(agreement->receiverIdentityFingerprint.trimmed().toLower().toUtf8());
        data.append('|');
    }
    return data;
}

QByteArray productionSessionDeriveAad(const E2EKeyAgreement& left,
                                      const E2EKeyAgreement& right) {
    const E2EKeyAgreement* first = &left;
    const E2EKeyAgreement* second = &right;
    if (left.senderId > right.senderId
        || (left.senderId == right.senderId && left.keyId > right.keyId)) {
        first = &right;
        second = &left;
    }
    QByteArray data;
    data.append("qtnetworkchat-e2e-production-session-context-v1|");
    data.append(first->senderId.trimmed().toUtf8());
    data.append('|');
    data.append(second->senderId.trimmed().toUtf8());
    data.append('|');
    data.append(first->keyId.trimmed().toUtf8());
    data.append('|');
    data.append(second->keyId.trimmed().toUtf8());
    return data;
}

} // namespace E2ECryptoPrimitives

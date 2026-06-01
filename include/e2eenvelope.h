#ifndef E2EENVELOPE_H
#define E2EENVELOPE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

struct E2EKeyAgreement {
    QString protocol;
    QString suite;
    QString senderId;
    QString receiverId;
    QString keyId;
    QByteArray publicKey;
    QString senderIdentityFingerprint;
    QString receiverIdentityFingerprint;
    QByteArray signature;

    bool isValid(QString* reason = nullptr) const;
    QJsonObject toJson() const;
    static E2EKeyAgreement fromJson(const QJsonObject& obj);
};

struct E2EEnvelope {
    QString protocol;
    QString suite;
    QString senderId;
    QString receiverId;
    QString keyId;
    QByteArray nonce;
    QByteArray ciphertext;
    QByteArray tag;
    QString aad;

    bool isValid(QString* reason = nullptr) const;
    QJsonObject toJson() const;
    static E2EEnvelope fromJson(const QJsonObject& obj);
};

QString normalizedE2EProtocol(QString protocol);
QString normalizedE2ESuite(QString suite);
QString e2eFingerprint(const QByteArray& value);
bool isSupportedE2EProtocol(const QString& protocol);
bool isSupportedE2ESuite(const QString& suite);
QByteArray generateE2ESessionKey();
E2EEnvelope encryptE2EText(const QString& senderId,
                           const QString& receiverId,
                           const QString& keyId,
                           const QByteArray& sessionKey,
                           const QString& plaintext,
                           QString* reason = nullptr);
bool decryptE2EText(const E2EEnvelope& envelope,
                    const QByteArray& sessionKey,
                    QString* plaintext,
                    QString* reason = nullptr);

#endif // E2EENVELOPE_H

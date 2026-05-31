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

#endif // E2EENVELOPE_H

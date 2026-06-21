#ifndef MESSAGE_H
#define MESSAGE_H

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include "e2eenvelope.h"

enum class MessageType {
    Text,
    File,
    Image,
    System,
    Login,
    Logout,
    UserList,
    Private
};

struct Message {
    QString senderId;
    QString senderName;
    QString senderAvatar;
    QString receiverId;       // empty means broadcast
    QString content;
    QByteArray fileData;
    QString fileName;
    QString transferId;
    QString clientMessageId;
    qint64 fileSize;
    QString fileHash;
    qint64 chunkSize;
    qint64 chunkCount;
    MessageType type;
    QDateTime timestamp;
    E2EEnvelope e2eEnvelope;
    QJsonObject e2eEnvelopeHeader;
    E2EKeyAgreement e2eKeyAgreement;
    QString e2eFileKeyId;
    QString e2eFileKeyFingerprint;
    qint64 e2eFilePlainSize = 0;
    QString e2eFilePlainHash;
    bool e2eFileEncrypted = false;

    Message() : fileSize(0), chunkSize(0), chunkCount(0), type(MessageType::Text), timestamp(QDateTime::currentDateTime()) {}

    QByteArray toJson() const;
    static Message fromJson(const QByteArray& json);

    bool isPrivate() const { return !receiverId.isEmpty(); }
};

#endif // MESSAGE_H

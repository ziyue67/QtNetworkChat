#ifndef MESSAGE_H
#define MESSAGE_H

#include <QString>
#include <QDateTime>
#include <QJsonObject>

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
    QString receiverId;       // empty means broadcast
    QString content;
    QByteArray fileData;
    QString fileName;
    MessageType type;
    QDateTime timestamp;

    Message() : type(MessageType::Text), timestamp(QDateTime::currentDateTime()) {}

    QByteArray toJson() const;
    static Message fromJson(const QByteArray& json);

    bool isPrivate() const { return !receiverId.isEmpty(); }
};

#endif // MESSAGE_H

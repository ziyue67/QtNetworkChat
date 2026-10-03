#ifndef CHATUSER_H
#define CHATUSER_H

#include <QString>
#include <QHostAddress>
#include <QDateTime>

struct ChatUser {
    QString id;
    QString name;
    QString avatar;
    QHostAddress address;
    quint16 port;
    bool isOnline;
    QDateTime lastActive;

    ChatUser() : port(0), isOnline(false) {}

    bool operator==(const ChatUser& other) const {
        return id == other.id;
    }
};

#endif // CHATUSER_H

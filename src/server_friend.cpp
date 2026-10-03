#include "server.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

void Server::handleFriendEvent(const QJsonObject& obj, QTcpSocket* socket) {
    QString type = obj["type"].toString();
    if (type == "friend_search") {
        QString account = obj["account"].toString().trimmed();
        ChatUser* requester = findUserBySocket(socket);
        QJsonObject response;
        response["type"] = "friend_search_result";
        response["account"] = account;
        response["exactMatch"] = false;
        response["matchCount"] = 0;
        response["matchReason"] = "未找到匹配资料";

        QJsonObject accounts = loadAccountsFromSqlite();
        if (!account.isEmpty() && accounts.contains(account)) {
            QJsonObject accountObj = accounts[account].toObject();
            response["found"] = true;
            response["userId"] = account;
            response["userName"] = accountObj["userName"].toString(account);
            bool online = false;
            response["online"] = isRedisUserOnline(account, &online) && online;
            response["exactMatch"] = true;
            response["matchCount"] = 1;
            response["matchReason"] = "QQ号精确匹配";
        } else {
            QString matchedId;
            QString matchedName;
            int matchCount = 0;
            for (auto it = accounts.begin(); it != accounts.end(); ++it) {
                const QString candidateId = it.key();
                const QJsonObject accountObj = it.value().toObject();
                const QString candidateName = accountObj["userName"].toString(candidateId);
                const bool idMatched = candidateId.contains(account, Qt::CaseInsensitive);
                const bool nameMatched = candidateName.contains(account, Qt::CaseInsensitive);
                if (!account.isEmpty() && (idMatched || nameMatched)) {
                    ++matchCount;
                    if (matchedId.isEmpty()) {
                        matchedId = candidateId;
                        matchedName = candidateName;
                        response["matchReason"] = idMatched ? "QQ号模糊匹配" : "昵称模糊匹配";
                    }
                }
            }

            response["matchCount"] = matchCount;
            if (!matchedId.isEmpty()) {
                response["found"] = true;
                response["userId"] = matchedId;
                response["userName"] = matchedName.isEmpty() ? matchedId : matchedName;
                bool online = false;
                response["online"] = isRedisUserOnline(matchedId, &online) && online;
            } else {
                response["found"] = false;
                response["online"] = false;
            }
        }
        const QString searchState = response["found"].toBool()
            ? QString("%1_%2").arg(response["exactMatch"].toBool() ? "found_exact" : "found_fuzzy",
                                   response["online"].toBool() ? "online" : "offline")
            : "not_found";
        saveFriendEventToSqlite(type,
                                requester ? requester->id : QString(),
                                requester ? requester->name : QString(),
                                response["userId"].toString(),
                                account,
                                searchState);

        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
        }
        return;
    }

    QString receiverId = obj["receiverId"].toString().trimmed();
    ChatUser* authenticatedSender = findUserBySocket(socket);
    QString senderId = authenticatedSender ? authenticatedSender->id : obj["senderId"].toString().trimmed();
    QString senderName = authenticatedSender ? authenticatedSender->name : obj["senderName"].toString().trimmed();
    if (senderName.trimmed().isEmpty()) {
        senderName = senderId;
    }

    QJsonObject forwarded = obj;
    forwarded["senderId"] = senderId;
    forwarded["senderName"] = senderName;
    forwarded["receiverId"] = receiverId;
    const bool accepted = forwarded["accepted"].toBool(false);
    QTcpSocket* targetSocket = m_userSockets.value(receiverId);

    if (type == "friend_response" && accepted && !senderId.isEmpty() && !receiverId.isEmpty() && senderId != receiverId) {
        QString receiverName = receiverId;
        if (ChatUser* receiverUser = targetSocket ? findUserBySocket(targetSocket) : nullptr) {
            receiverName = receiverUser->name.trimmed().isEmpty() ? receiverId : receiverUser->name;
        } else {
            const QJsonObject accounts = loadAccountsFromSqlite();
            const QJsonObject receiverAccount = accounts.value(receiverId).toObject();
            receiverName = receiverAccount.value("userName").toString(receiverName).trimmed();
            if (senderName.trimmed().isEmpty() || senderName == senderId) {
                const QJsonObject senderAccount = accounts.value(senderId).toObject();
                senderName = senderAccount.value("userName").toString(senderName).trimmed();
                forwarded["senderName"] = senderName.isEmpty() ? senderId : senderName;
            }
        }
        saveAcceptedFriendshipToSqlite(senderId,
                                      senderName.isEmpty() ? senderId : senderName,
                                      receiverId,
                                      receiverName.isEmpty() ? receiverId : receiverName);
    }

    if (!targetSocket || targetSocket->state() != QAbstractSocket::ConnectedState) {
        saveFriendEventToSqlite(type, senderId, senderName, receiverId, QString(), "target_offline", accepted);
        if (type == "friend_response" && accepted && socket && socket->state() == QAbstractSocket::ConnectedState) {
            sendFriendListSnapshot(senderId, socket);
        }
        if (type == "friend_request" && socket && socket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject response;
            response["type"] = "friend_request_sent";
            response["receiverId"] = receiverId;
            response["delivered"] = false;
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
        }
        return;
    }
    saveFriendEventToSqlite(type, senderId, senderName, receiverId, QString(), "delivered", accepted);

    QByteArray data = QJsonDocument(forwarded).toJson(QJsonDocument::Compact);
    targetSocket->write(data);
    targetSocket->write("\n");
    targetSocket->flush();

    if (type == "friend_request" && socket && socket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject response;
        response["type"] = "friend_request_sent";
        response["receiverId"] = receiverId;
        response["delivered"] = true;
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
    }

    if (type == "friend_response" && accepted) {
        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            sendFriendListSnapshot(senderId, socket);
        }
        sendFriendListSnapshot(receiverId, targetSocket);
    }
}

void Server::sendFriendListSnapshot(const QString& userId, QTcpSocket* socket) const {
    const QString normalizedUserId = userId.trimmed();
    if (!socket || socket->state() != QAbstractSocket::ConnectedState || normalizedUserId.isEmpty()) {
        return;
    }

    QJsonArray friendsArray;
    const QVector<ChatUser> friends = loadFriendListFromSqlite(normalizedUserId);
    for (const ChatUser& friendUser : friends) {
        const QString friendId = friendUser.id.trimmed();
        if (friendId.isEmpty() || friendId == normalizedUserId) {
            continue;
        }

        bool online = m_userSockets.contains(friendId);
        if (!online) {
            bool redisOnline = false;
            online = isRedisUserOnline(friendId, &redisOnline) && redisOnline;
        }

        QJsonObject friendObj;
        friendObj["id"] = friendId;
        friendObj["name"] = friendUser.name.trimmed().isEmpty() ? friendId : friendUser.name.trimmed();
        friendObj["avatar"] = friendUser.avatar;
        friendObj["online"] = online;
        if (friendUser.lastActive.isValid()) {
            friendObj["lastActive"] = friendUser.lastActive.toUTC().toString(Qt::ISODateWithMs);
        }
        friendsArray.append(friendObj);
    }

    QJsonObject obj;
    obj["type"] = "friend_list";
    obj["friends"] = friendsArray;
    obj["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

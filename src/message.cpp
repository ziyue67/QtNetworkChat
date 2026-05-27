#include "message.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDataStream>

QByteArray Message::toJson() const {
    QJsonObject obj;
    obj["senderId"] = senderId;
    obj["senderName"] = senderName;
    obj["receiverId"] = receiverId;
    obj["content"] = content;
    obj["type"] = static_cast<int>(type);
    obj["timestamp"] = timestamp.toString(Qt::ISODate);
    obj["fileName"] = fileName;
    obj["transferId"] = transferId;
    obj["fileSize"] = QString::number(fileSize);
    obj["fileHash"] = fileHash;
    obj["chunkSize"] = QString::number(chunkSize);
    obj["chunkCount"] = QString::number(chunkCount);

    if (!fileData.isEmpty()) {
        obj["fileData"] = QString::fromLatin1(fileData.toBase64());
        obj["hasFile"] = true;
    }

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

Message Message::fromJson(const QByteArray& json) {
    Message msg;
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isNull()) return msg;

    QJsonObject obj = doc.object();
    msg.senderId = obj["senderId"].toString();
    msg.senderName = obj["senderName"].toString();
    msg.receiverId = obj["receiverId"].toString();
    msg.content = obj["content"].toString();
    msg.type = static_cast<MessageType>(obj["type"].toInt());
    msg.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
    msg.fileName = obj["fileName"].toString();
    msg.transferId = obj["transferId"].toString();
    msg.fileSize = obj["fileSize"].toVariant().toLongLong();
    msg.fileHash = obj["fileHash"].toString();
    msg.chunkSize = obj["chunkSize"].toVariant().toLongLong();
    msg.chunkCount = obj["chunkCount"].toVariant().toLongLong();

    if (obj.contains("hasFile") && obj["hasFile"].toBool()) {
        msg.fileData = QByteArray::fromBase64(obj["fileData"].toString().toLatin1());
    }

    if (msg.timestamp.isNull()) {
        msg.timestamp = QDateTime::currentDateTime();
    }

    return msg;
}

#pragma once

#include <QJsonObject>
#include <QString>

class QTcpSocket;
struct Message;
struct E2EEnvelope;

// Internal wire-format and delivery helpers shared by server responsibilities.
namespace ServerDeliverySupport {
constexpr qint64 kForwardChunkBytes = 256LL * 1024;
constexpr int kChunkSendMaxAttempts = 3;
constexpr qint64 kRedisPubSubFileMaxBytes = 1LL * 1024 * 1024;

bool looksLikeSha256Hex(const QString& value);
void appendE2EFields(QJsonObject* obj, const Message& msg);
QJsonObject e2eEnvelopeHeaderJson(const E2EEnvelope& envelope);
bool e2eEnvelopeHeaderLooksSafe(const QJsonObject& header);
void appendE2EFileFields(QJsonObject* target, const QJsonObject& source, bool includeCiphertext = false);
void sendSystemNotice(QTcpSocket* socket, const QString& content);
void logRedisLargeFileRouteEvent(const QString& eventName, const QString& result,
                                const QJsonObject& metadata, const QString& reason = QString(),
                                qint64 bytes = -1);
QJsonObject largeFileRouteLogMetadata(QJsonObject metadata, const QString& storeType,
                                    const QString& operation);
}

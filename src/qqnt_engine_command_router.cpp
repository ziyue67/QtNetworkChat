#include "qqnt_engine_command_router.h"

#include "qqnt_client_bridge.h"

#include <QJsonValue>

QQNTEngineCommandRouter::QQNTEngineCommandRouter(QQNTClientBridge* bridge)
    : QObject(bridge)
    , m_bridge(bridge)
    , m_hasAccountInfo(false)
{
}

void QQNTEngineCommandRouter::route(const QJsonObject& command) {
    const QString op = command.value(QStringLiteral("op")).toString().trimmed();
    const QString reqId = command.value(QStringLiteral("reqId")).toString().trimmed();
    const QJsonObject payload = command.value(QStringLiteral("payload")).toObject();

    if (op.isEmpty()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("missing_op"), QStringLiteral("Command op is required."));
        return;
    }
    if (reqId.isEmpty()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("missing_req_id"), QStringLiteral("Command reqId is required."));
        return;
    }

    if (op == QLatin1String("ready")) {
        handleReady(op, reqId);
    } else if (op == QLatin1String("login")) {
        handleLogin(op, reqId, payload, false);
    } else if (op == QLatin1String("register")) {
        handleLogin(op, reqId, payload, true);
    } else if (op == QLatin1String("connect")) {
        handleConnect(op, reqId, payload);
    } else if (op == QLatin1String("disconnect") || op == QLatin1String("logout")) {
        handleDisconnect(op, reqId);
    } else if (op == QLatin1String("set_user_info")) {
        handleSetUserInfo(op, reqId, payload);
    } else if (op == QLatin1String("send_private_message")) {
        handleSendPrivateMessage(op, reqId, payload);
    } else {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("unknown_op"),
                               QStringLiteral("Unsupported command: %1").arg(op));
    }
}

void QQNTEngineCommandRouter::handleReady(const QString& op, const QString& reqId) {
    m_bridge->sendAck(op, reqId, m_bridge->readyPayload());
}

void QQNTEngineCommandRouter::handleLogin(const QString& op, const QString& reqId, const QJsonObject& payload, bool registerMode) {
    QString account;
    QString password;
    if (!requireString(payload, QStringLiteral("account"), &account, op, reqId)
        || !requireString(payload, QStringLiteral("password"), &password, op, reqId)) {
        return;
    }

    if (registerMode) {
        const QString userName = payload.value(QStringLiteral("userName")).toString().trimmed();
        if (!userName.isEmpty()) {
            m_bridge->client()->setUserInfo(QString(), userName);
        }
    }

    m_account = account;
    m_password = password;
    m_hasAccountInfo = true;
    m_bridge->client()->setAccountInfo(account, password, registerMode);

    QJsonObject response;
    response[QStringLiteral("accepted")] = true;
    response[QStringLiteral("requiresConnect")] = !m_bridge->client()->isConnected();
    response[QStringLiteral("mode")] = registerMode ? QStringLiteral("register") : QStringLiteral("login");
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleConnect(const QString& op, const QString& reqId, const QJsonObject& payload) {
    if (!m_hasAccountInfo) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("login_required"),
                               QStringLiteral("Send login or register credentials before connect."));
        return;
    }

    const QString host = payload.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1")).trimmed();
    const int portValue = payload.value(QStringLiteral("port")).toInt(8888);
    if (host.isEmpty() || portValue <= 0 || portValue > 65535) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("invalid_target"), QStringLiteral("connect requires a valid host and port."));
        return;
    }

    m_bridge->setConnectionTarget(host, static_cast<quint16>(portValue));
    const bool connected = m_bridge->client()->connectToServer(host, static_cast<quint16>(portValue));
    if (!connected) {
        const QString reason = m_bridge->client()->lastLoginError().isEmpty()
            ? QStringLiteral("Unable to connect to server.")
            : m_bridge->client()->lastLoginError();
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("connect_failed"), reason);
        return;
    }

    QJsonObject response;
    response[QStringLiteral("connected")] = true;
    response[QStringLiteral("host")] = host;
    response[QStringLiteral("port")] = portValue;
    m_bridge->sendAck(op, reqId, response);
}

void QQNTEngineCommandRouter::handleDisconnect(const QString& op, const QString& reqId) {
    m_bridge->client()->disconnectFromServer();
    m_bridge->sendAck(op, reqId);
}

void QQNTEngineCommandRouter::handleSetUserInfo(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString userId;
    QString userName;
    if (!requireString(payload, QStringLiteral("userId"), &userId, op, reqId)
        || !requireString(payload, QStringLiteral("userName"), &userName, op, reqId)) {
        return;
    }
    m_bridge->client()->setUserInfo(userId, userName);
    m_bridge->sendAck(op, reqId);
}

void QQNTEngineCommandRouter::handleSendPrivateMessage(const QString& op, const QString& reqId, const QJsonObject& payload) {
    QString receiverId;
    QString content;
    if (!requireString(payload, QStringLiteral("receiverId"), &receiverId, op, reqId)
        || !requireString(payload, QStringLiteral("content"), &content, op, reqId)) {
        return;
    }

    if (!m_bridge->client()->isConnected()) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("not_connected"), QStringLiteral("Engine is not connected to QQNTServer."));
        return;
    }

    if (!m_bridge->client()->sendPrivateMessage(receiverId, content)) {
        m_bridge->sendErrorAck(op, reqId, QStringLiteral("send_failed"), QStringLiteral("Client rejected private message send."));
        return;
    }

    QJsonObject response;
    response[QStringLiteral("receiverId")] = receiverId;
    m_bridge->sendAck(op, reqId, response);
}

bool QQNTEngineCommandRouter::requireString(const QJsonObject& payload,
                                            const QString& field,
                                            QString* value,
                                            const QString& op,
                                            const QString& reqId) const {
    const QString text = payload.value(field).toString().trimmed();
    if (text.isEmpty()) {
        m_bridge->sendErrorAck(op,
                               reqId,
                               QStringLiteral("missing_field"),
                               QStringLiteral("payload.%1 is required.").arg(field));
        return false;
    }
    *value = text;
    return true;
}

#include "server.h"
#include "test_redis_support.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>
#include <initializer_list>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        return false;
    }
    return true;
}

bool writeSmallFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload("qqnt-engine-e2e-file-payload");
    return file.write(payload) == payload.size();
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

void resetTestStorage() {
    QStringList dirs;
    const QString configuredAppData = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!configuredAppData.isEmpty()) {
        dirs << QDir::cleanPath(configuredAppData);
    }
    const QString standardAppData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!standardAppData.isEmpty()) {
        dirs << QDir::cleanPath(standardAppData);
    }

    dirs.removeDuplicates();
    for (const QString& dir : dirs) {
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
    }
}

QJsonObject makeCommand(const QString& op, const QString& reqId, const QJsonObject& payload = QJsonObject()) {
    QJsonObject command;
    command[QStringLiteral("op")] = op;
    command[QStringLiteral("reqId")] = reqId;
    command[QStringLiteral("payload")] = payload;
    return command;
}

class EngineHarness {
public:
    explicit EngineHarness(QString label)
        : m_label(std::move(label)) {
    }

    ~EngineHarness() {
        stop();
    }

    bool start(const QString& enginePath) {
        m_process.setProgram(enginePath);
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();
        return m_process.waitForStarted(5000);
    }

    bool writeCommand(const QJsonObject& command) {
        const QByteArray line = QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n';
        return m_process.write(line) == line.size() && m_process.waitForBytesWritten(1000);
    }

    void pump() {
        m_stdoutPending += m_process.readAllStandardOutput();
        m_stderr += m_process.readAllStandardError();

        while (true) {
            const int newlineIndex = m_stdoutPending.indexOf('\n');
            if (newlineIndex < 0) {
                break;
            }
            const QByteArray line = m_stdoutPending.left(newlineIndex).trimmed();
            m_stdoutPending = m_stdoutPending.mid(newlineIndex + 1);
            if (line.isEmpty()) {
                continue;
            }

            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                m_invalidStdout << QString::fromUtf8(line);
                continue;
            }

            const QJsonObject object = document.object();
            if (object.value(QStringLiteral("type")).toString() == QLatin1String("ack")) {
                m_acks[object.value(QStringLiteral("reqId")).toString()] = object;
            } else if (object.value(QStringLiteral("type")).toString() == QLatin1String("event")) {
                m_events.append(object);
                if (object.value(QStringLiteral("event")).toString() == QLatin1String("login_result")) {
                    const QJsonObject payload = object.value(QStringLiteral("payload")).toObject();
                    if (payload.value(QStringLiteral("success")).toBool(false)) {
                        m_userId = payload.value(QStringLiteral("userId")).toString();
                        m_userName = payload.value(QStringLiteral("userName")).toString();
                    }
                }
            }
        }
    }

    void stop() {
        if (m_process.state() == QProcess::NotRunning) {
            return;
        }

        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(3000)) {
            m_process.terminate();
            if (!m_process.waitForFinished(2000)) {
                m_process.kill();
                m_process.waitForFinished(2000);
            }
        }
        pump();
    }

    bool hasOkAck(const QString& reqId) const {
        const auto it = m_acks.constFind(reqId);
        return it != m_acks.constEnd()
            && it.value().value(QStringLiteral("status")).toString() == QLatin1String("ok");
    }

    bool hasStartupReadyEvent() const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() == QLatin1String("ready")) {
                return true;
            }
        }
        return false;
    }

    bool hasLoginSuccess() const {
        return !m_userId.isEmpty();
    }

    bool hasFriendRequestFrom(const QString& expectedSenderId) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("friend_event")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            if (payload.value(QStringLiteral("type")).toString() == QLatin1String("request_received")
                && payload.value(QStringLiteral("senderId")).toString() == expectedSenderId) {
                return true;
            }
        }
        return false;
    }

    bool hasFriendResponseFrom(const QString& expectedSenderId, bool expectedAccepted) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("friend_event")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            if (payload.value(QStringLiteral("type")).toString() == QLatin1String("response_received")
                && payload.value(QStringLiteral("senderId")).toString() == expectedSenderId
                && payload.value(QStringLiteral("accepted")).toBool() == expectedAccepted) {
                return true;
            }
        }
        return false;
    }

    bool hasFriendListEntry(const QString& expectedUserId, const QString& expectedName = QString()) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("friend_list")) {
                continue;
            }
            const QJsonArray friends = event.value(QStringLiteral("payload")).toObject().value(QStringLiteral("friends")).toArray();
            for (const QJsonValue& value : friends) {
                const QJsonObject friendObject = value.toObject();
                if (friendObject.value(QStringLiteral("id")).toString() != expectedUserId) {
                    continue;
                }
                return expectedName.isEmpty()
                    || friendObject.value(QStringLiteral("name")).toString() == expectedName;
            }
        }
        return false;
    }

    bool friendListAckContains(const QString& reqId, const QString& expectedUserId, const QString& expectedName = QString()) const {
        const auto it = m_acks.constFind(reqId);
        if (it == m_acks.constEnd()
            || it.value().value(QStringLiteral("status")).toString() != QLatin1String("ok")) {
            return false;
        }

        const QJsonArray friends = it.value().value(QStringLiteral("payload")).toObject().value(QStringLiteral("friends")).toArray();
        for (const QJsonValue& value : friends) {
            const QJsonObject friendObject = value.toObject();
            if (friendObject.value(QStringLiteral("id")).toString() != expectedUserId) {
                continue;
            }
            return expectedName.isEmpty()
                || friendObject.value(QStringLiteral("name")).toString() == expectedName;
        }
        return false;
    }

    bool hasPrivateMessage(const QString& expectedContent,
                           const QString& expectedSenderId,
                           const QString& expectedReceiverId) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("message")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            const QJsonObject message = payload.value(QStringLiteral("message")).toObject();
            if (message.value(QStringLiteral("content")).toString() == expectedContent
                && message.value(QStringLiteral("senderId")).toString() == expectedSenderId
                && message.value(QStringLiteral("receiverId")).toString() == expectedReceiverId
                && payload.value(QStringLiteral("sessionId")).toString() == expectedSenderId) {
                return true;
            }
        }
        return false;
    }

    bool hasGroupSnapshot(const QString& expectedGroupId) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("group_snapshot")) {
                continue;
            }
            const QJsonArray groups = event.value(QStringLiteral("payload")).toObject().value(QStringLiteral("groups")).toArray();
            for (const QJsonValue& value : groups) {
                if (value.toObject().value(QStringLiteral("groupId")).toString() == expectedGroupId) {
                    return true;
                }
            }
        }
        return false;
    }

    QString activeGroupIdByName(const QString& expectedGroupName) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("group_snapshot")) {
                continue;
            }
            const QJsonArray groups = event.value(QStringLiteral("payload")).toObject().value(QStringLiteral("groups")).toArray();
            for (const QJsonValue& value : groups) {
                const QJsonObject group = value.toObject();
                if (group.value(QStringLiteral("groupName")).toString() == expectedGroupName
                    && group.value(QStringLiteral("membershipState")).toString() == QLatin1String("active")) {
                    return group.value(QStringLiteral("groupId")).toString();
                }
            }
        }
        return QString();
    }

    bool hasGroupSnapshotContractFields(const QString& expectedGroupId) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("group_snapshot")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            if (!payload.value(QStringLiteral("hasSnapshot")).toBool(false)
                || !payload.value(QStringLiteral("groups")).isArray()
                || !payload.value(QStringLiteral("removedGroups")).isArray()) {
                continue;
            }
            const QJsonArray groups = payload.value(QStringLiteral("groups")).toArray();
            for (const QJsonValue& value : groups) {
                if (value.toObject().value(QStringLiteral("groupId")).toString() == expectedGroupId) {
                    return true;
                }
            }
        }
        return false;
    }

    bool hasRemovedGroupSnapshot(const QString& expectedGroupId) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("group_snapshot")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            if (!payload.value(QStringLiteral("hasSnapshot")).toBool(false)
                || !payload.value(QStringLiteral("groups")).isArray()
                || !payload.value(QStringLiteral("removedGroups")).isArray()) {
                continue;
            }
            bool stillActive = false;
            const QJsonArray groups = payload.value(QStringLiteral("groups")).toArray();
            for (const QJsonValue& value : groups) {
                if (value.toObject().value(QStringLiteral("groupId")).toString() == expectedGroupId) {
                    stillActive = true;
                    break;
                }
            }
            if (stillActive) {
                continue;
            }
            const QJsonArray removedGroups = payload.value(QStringLiteral("removedGroups")).toArray();
            for (const QJsonValue& value : removedGroups) {
                const QJsonObject group = value.toObject();
                if (group.value(QStringLiteral("groupId")).toString() == expectedGroupId
                    && group.value(QStringLiteral("membershipState")).toString() == QLatin1String("removed")
                    && !group.value(QStringLiteral("canSend")).toBool(true)
                    && group.value(QStringLiteral("canReadHistory")).toBool(false)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool hasGroupMessage(const QString& expectedContent,
                         const QString& expectedSenderId,
                         const QString& expectedGroupId) const {
        const QString expectedSessionId = QStringLiteral("group:%1").arg(expectedGroupId);
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("message")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            const QJsonObject message = payload.value(QStringLiteral("message")).toObject();
            if (message.value(QStringLiteral("content")).toString() == expectedContent
                && message.value(QStringLiteral("senderId")).toString() == expectedSenderId
                && message.value(QStringLiteral("receiverId")).toString() == expectedGroupId
                && message.value(QStringLiteral("contentType")).toString() == QLatin1String("text")
                && payload.value(QStringLiteral("sessionId")).toString() == expectedSessionId
                && message.value(QStringLiteral("sessionId")).toString() == expectedSessionId) {
                return true;
            }
        }
        return false;
    }

    bool hasNotification(const QString& expectedBody) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("notification")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            if (!payload.value(QStringLiteral("title")).toString().trimmed().isEmpty()
                && payload.value(QStringLiteral("body")).toString() == expectedBody) {
                return true;
            }
        }
        return false;
    }

    bool hasFileProgress(const QString& expectedFileName, const QString& expectedDirection) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("file_progress")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            if (payload.value(QStringLiteral("fileName")).toString() == expectedFileName
                && payload.value(QStringLiteral("direction")).toString() == expectedDirection
                && !payload.value(QStringLiteral("transferId")).toString().trimmed().isEmpty()) {
                return true;
            }
        }
        return false;
    }

    bool hasFileDone(const QString& expectedFileName, const QString& expectedDirection, const QString& expectedFilePath = QString()) const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("file_done")) {
                continue;
            }
            const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
            if (payload.value(QStringLiteral("fileName")).toString() != expectedFileName
                || payload.value(QStringLiteral("direction")).toString() != expectedDirection
                || payload.value(QStringLiteral("transferId")).toString().trimmed().isEmpty()
                || !payload.contains(QStringLiteral("filePath"))) {
                continue;
            }
            if (!expectedFilePath.isEmpty()
                && payload.value(QStringLiteral("filePath")).toString() != expectedFilePath) {
                continue;
            }
            return true;
        }
        return false;
    }

    bool hasFileError() const {
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() == QLatin1String("file_error")) {
                return true;
            }
        }
        return false;
    }

    QString fileEventSummary() const {
        QStringList lines;
        for (const QJsonObject& event : m_events) {
            const QString eventName = event.value(QStringLiteral("event")).toString();
            if (eventName != QLatin1String("file_progress")
                && eventName != QLatin1String("file_done")
                && eventName != QLatin1String("file_error")) {
                continue;
            }
            lines << QStringLiteral("%1 %2")
                .arg(eventName,
                     QString::fromUtf8(QJsonDocument(event.value(QStringLiteral("payload")).toObject()).toJson(QJsonDocument::Compact)));
        }
        return lines.join(QLatin1Char('\n'));
    }

    QString notificationEventSummary() const {
        QStringList lines;
        for (const QJsonObject& event : m_events) {
            if (event.value(QStringLiteral("event")).toString() != QLatin1String("notification")) {
                continue;
            }
            lines << QString::fromUtf8(QJsonDocument(event.value(QStringLiteral("payload")).toObject()).toJson(QJsonDocument::Compact));
        }
        return lines.join(QLatin1Char('\n'));
    }

    QString userId() const { return m_userId; }
    QString label() const { return m_label; }
    QString stderrText() const { return QString::fromLocal8Bit(m_stderr); }
    QStringList invalidStdout() const { return m_invalidStdout; }

private:
    QString m_label;
    QProcess m_process;
    QByteArray m_stdoutPending;
    QByteArray m_stderr;
    QMap<QString, QJsonObject> m_acks;
    QVector<QJsonObject> m_events;
    QStringList m_invalidStdout;
    QString m_userId;
    QString m_userName;
};

bool waitFor(const std::function<bool()>& predicate,
             std::initializer_list<EngineHarness*> engines,
             int timeoutMs = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        for (EngineHarness* engine : engines) {
            engine->pump();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        for (EngineHarness* engine : engines) {
            engine->pump();
        }
        if (predicate()) {
            return true;
        }
        QThread::msleep(10);
    }

    for (EngineHarness* engine : engines) {
        engine->pump();
    }
    return predicate();
}

QJsonObject registerPayload(const QString& account, const QString& userName) {
    QJsonObject payload;
    payload[QStringLiteral("account")] = account;
    payload[QStringLiteral("password")] = QStringLiteral("secret");
    payload[QStringLiteral("userName")] = userName;
    return payload;
}

QJsonObject connectPayload(quint16 port) {
    QJsonObject payload;
    payload[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    payload[QStringLiteral("port")] = static_cast<int>(port);
    return payload;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QtNetworkChatTests"));
    QCoreApplication::setApplicationName(QStringLiteral("qqnt_engine_end_to_end_test"));
    QStandardPaths::setTestModeEnabled(true);

    const QStringList arguments = app.arguments();
    if (arguments.size() < 2) {
        std::fprintf(stderr, "usage: %s <QQNTEngine executable>\n", argv[0]);
        return 2;
    }

    resetTestStorage();

    TestRedisServerEnvironment redisEnv(QStringLiteral("qtchat-qqnt-engine-e2e-test"));
    QString redisError;
    bool ok = expect(redisEnv.start(&redisError), "fake Redis server should start") && redisError.isEmpty();
    ok = expect(ok, "fake Redis server should not report a startup error") && ok;
    if (!ok) return 1;
    redisEnv.applyEnvironment();

    const quint16 serverPort = freeLocalPort();
    ok = expect(serverPort != 0, "chat server port should be available") && ok;
    if (!ok) return 1;

    Server server;
    ok = expect(server.start(serverPort), "QQNT server should start with Redis enabled") && ok;
    ok = expect(server.isServiceReady(), "QQNT server should report Redis-ready") && ok;
    if (!ok) return 1;

    const QString enginePath = arguments.at(1);
    EngineHarness alice(QStringLiteral("alice"));
    EngineHarness bob(QStringLiteral("bob"));
    ok = expect(alice.start(enginePath), "alice QQNTEngine should start") && ok;
    ok = expect(bob.start(enginePath), "bob QQNTEngine should start") && ok;
    ok = expect(waitFor([&] {
        return alice.hasStartupReadyEvent() && bob.hasStartupReadyEvent();
    }, {&alice, &bob}), "both QQNTEngine processes should emit ready") && ok;

    const QString suffix = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString aliceAccount = QStringLiteral("engine-e2e-alice-%1").arg(suffix);
    const QString bobAccount = QStringLiteral("engine-e2e-bob-%1").arg(suffix);

    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("register"),
                                               QStringLiteral("alice-register"),
                                               registerPayload(aliceAccount, QStringLiteral("Alice Engine E2E")))),
                "alice register command should be written") && ok;
    ok = expect(bob.writeCommand(makeCommand(QStringLiteral("register"),
                                             QStringLiteral("bob-register"),
                                             registerPayload(bobAccount, QStringLiteral("Bob Engine E2E")))),
                "bob register command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.hasOkAck(QStringLiteral("alice-register"))
            && bob.hasOkAck(QStringLiteral("bob-register"));
    }, {&alice, &bob}), "both QQNTEngine processes should ack register") && ok;

    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("connect"),
                                               QStringLiteral("alice-connect"),
                                               connectPayload(serverPort))),
                "alice connect command should be written") && ok;
    ok = expect(bob.writeCommand(makeCommand(QStringLiteral("connect"),
                                             QStringLiteral("bob-connect"),
                                             connectPayload(serverPort))),
                "bob connect command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.hasOkAck(QStringLiteral("alice-connect"))
            && bob.hasOkAck(QStringLiteral("bob-connect"))
            && alice.hasLoginSuccess()
            && bob.hasLoginSuccess();
    }, {&alice, &bob}), "both QQNTEngine processes should connect and emit login_result") && ok;

    ok = expect(waitFor([&] {
        return alice.hasGroupSnapshot(QStringLiteral("public"))
            && bob.hasGroupSnapshot(QStringLiteral("public"));
    }, {&alice, &bob}), "both QQNTEngine processes should receive the public group snapshot") && ok;
    ok = expect(waitFor([&] {
        return alice.hasGroupSnapshotContractFields(QStringLiteral("public"))
            && bob.hasGroupSnapshotContractFields(QStringLiteral("public"));
    }, {&alice, &bob}), "group_snapshot events should include removedGroups and hasSnapshot contract fields") && ok;

    QJsonObject friendRequestPayload;
    friendRequestPayload[QStringLiteral("receiverId")] = bob.userId();
    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("send_friend_request"),
                                               QStringLiteral("alice-send-friend-request"),
                                               friendRequestPayload)),
                "alice friend request command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.hasOkAck(QStringLiteral("alice-send-friend-request"))
            && bob.hasFriendRequestFrom(alice.userId());
    }, {&alice, &bob}), "bob QQNTEngine should receive alice friend request") && ok;

    QJsonObject friendResponsePayload;
    friendResponsePayload[QStringLiteral("senderId")] = alice.userId();
    friendResponsePayload[QStringLiteral("accepted")] = true;
    ok = expect(bob.writeCommand(makeCommand(QStringLiteral("respond_friend_request"),
                                             QStringLiteral("bob-accept-friend-request"),
                                             friendResponsePayload)),
                "bob friend response command should be written") && ok;
    ok = expect(waitFor([&] {
        return bob.hasOkAck(QStringLiteral("bob-accept-friend-request"))
            && alice.hasFriendResponseFrom(bob.userId(), true)
            && alice.hasFriendListEntry(bob.userId(), QStringLiteral("Bob Engine E2E"))
            && bob.hasFriendListEntry(alice.userId(), QStringLiteral("Alice Engine E2E"));
    }, {&alice, &bob}), "both QQNTEngine processes should update friend list after acceptance") && ok;

    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("get_friend_list"), QStringLiteral("alice-get-friends"))),
                "alice get_friend_list command should be written") && ok;
    ok = expect(bob.writeCommand(makeCommand(QStringLiteral("get_friend_list"), QStringLiteral("bob-get-friends"))),
                "bob get_friend_list command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.friendListAckContains(QStringLiteral("alice-get-friends"), bob.userId(), QStringLiteral("Bob Engine E2E"))
            && bob.friendListAckContains(QStringLiteral("bob-get-friends"), alice.userId(), QStringLiteral("Alice Engine E2E"));
    }, {&alice, &bob}), "get_friend_list should return accepted friends") && ok;

    const QString privateContent = QStringLiteral("qqnt engine e2e private message");
    QJsonObject messagePayload;
    messagePayload[QStringLiteral("receiverId")] = bob.userId();
    messagePayload[QStringLiteral("content")] = privateContent;
    ok = expect(!bob.userId().isEmpty(), "bob user id should be known before private send") && ok;
    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("send_private_message"),
                                               QStringLiteral("alice-private-message"),
                                               messagePayload)),
                "alice private message command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.hasOkAck(QStringLiteral("alice-private-message"))
            && bob.hasPrivateMessage(privateContent, alice.userId(), bob.userId())
            && bob.hasNotification(privateContent);
    }, {&alice, &bob}), "bob QQNTEngine should emit the private message event") && ok;

    const QString groupId = QStringLiteral("public");
    const QString groupContent = QStringLiteral("qqnt engine e2e group message");
    QJsonObject groupPayload;
    groupPayload[QStringLiteral("groupId")] = groupId;
    groupPayload[QStringLiteral("content")] = groupContent;
    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("send_group_message"),
                                               QStringLiteral("alice-group-message"),
                                               groupPayload)),
                "alice group message command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.hasOkAck(QStringLiteral("alice-group-message"))
            && bob.hasGroupMessage(groupContent, alice.userId(), groupId)
            && bob.hasNotification(groupContent);
    }, {&alice, &bob}), "bob QQNTEngine should emit the group message event with a group session id") && ok;

    QTemporaryDir tempDir;
    ok = expect(tempDir.isValid(), "temporary directory should be available for group file send") && ok;
    const QString groupFileName = QStringLiteral("qqnt-engine-e2e-file.txt");
    const QString groupFilePath = tempDir.filePath(groupFileName);
    ok = expect(writeSmallFile(groupFilePath), "group file payload should be created") && ok;
    QJsonObject filePayload;
    filePayload[QStringLiteral("groupId")] = groupId;
    filePayload[QStringLiteral("filePath")] = groupFilePath;
    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("send_file"),
                                               QStringLiteral("alice-group-file"),
                                               filePayload)),
                "alice group file command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.hasOkAck(QStringLiteral("alice-group-file"))
            && alice.hasFileProgress(groupFileName, QStringLiteral("outgoing"))
            && bob.hasFileProgress(groupFileName, QStringLiteral("incoming"))
            && alice.hasFileDone(groupFileName,
                                 QStringLiteral("outgoing"),
                                 QFileInfo(groupFilePath).absoluteFilePath())
            && bob.hasFileDone(groupFileName, QStringLiteral("incoming"));
    }, {&alice, &bob}), "QQNTEngine should emit completed group file IPC events with transfer ids") && ok;
    ok = expect(!alice.hasFileError(), "alice QQNTEngine should not emit file_error for a completed group file") && ok;
    ok = expect(!bob.hasFileError(), "bob QQNTEngine should not emit file_error for a completed group file") && ok;

    const QString privateGroupName = QStringLiteral("Engine E2E private %1").arg(suffix);
    QJsonObject createGroupPayload;
    createGroupPayload[QStringLiteral("groupName")] = privateGroupName;
    createGroupPayload[QStringLiteral("announcement")] = QStringLiteral("Engine E2E private group");
    QJsonArray privateGroupMembers;
    privateGroupMembers.append(bob.userId());
    createGroupPayload[QStringLiteral("members")] = privateGroupMembers;
    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("create_group"),
                                               QStringLiteral("alice-create-private-group"),
                                               createGroupPayload)),
                "alice private group create command should be written") && ok;
    QString privateGroupId;
    ok = expect(waitFor([&] {
        const QString alicePrivateGroupId = alice.activeGroupIdByName(privateGroupName);
        const QString bobPrivateGroupId = bob.activeGroupIdByName(privateGroupName);
        if (alicePrivateGroupId.isEmpty() || alicePrivateGroupId != bobPrivateGroupId) {
            return false;
        }
        privateGroupId = alicePrivateGroupId;
        return alice.hasOkAck(QStringLiteral("alice-create-private-group"))
            && alice.hasGroupSnapshotContractFields(privateGroupId)
            && bob.hasGroupSnapshotContractFields(privateGroupId);
    }, {&alice, &bob}), "private group snapshots should include contract fields for owner and invited member") && ok;

    QJsonObject removeMemberPayload;
    removeMemberPayload[QStringLiteral("groupId")] = privateGroupId;
    removeMemberPayload[QStringLiteral("memberId")] = bob.userId();
    removeMemberPayload[QStringLiteral("action")] = QStringLiteral("remove");
    ok = expect(alice.writeCommand(makeCommand(QStringLiteral("update_group_member"),
                                               QStringLiteral("alice-remove-bob-private"),
                                               removeMemberPayload)),
                "alice remove private group member command should be written") && ok;
    ok = expect(waitFor([&] {
        return alice.hasOkAck(QStringLiteral("alice-remove-bob-private"))
            && bob.hasRemovedGroupSnapshot(privateGroupId);
    }, {&alice, &bob}), "removed group member should receive removedGroups in group_snapshot event") && ok;

    alice.pump();
    bob.pump();
    ok = expect(alice.invalidStdout().isEmpty(), "alice QQNTEngine stdout should contain only JSON objects") && ok;
    ok = expect(bob.invalidStdout().isEmpty(), "bob QQNTEngine stdout should contain only JSON objects") && ok;

    if (!ok) {
        const QString aliceStderr = alice.stderrText();
        const QString bobStderr = bob.stderrText();
        if (!aliceStderr.isEmpty()) {
            std::fprintf(stderr, "alice stderr:\n%s\n", qPrintable(aliceStderr));
        }
        if (!bobStderr.isEmpty()) {
            std::fprintf(stderr, "bob stderr:\n%s\n", qPrintable(bobStderr));
        }
        const QString aliceFileEvents = alice.fileEventSummary();
        const QString bobFileEvents = bob.fileEventSummary();
        const QString bobNotifications = bob.notificationEventSummary();
        if (!aliceFileEvents.isEmpty()) {
            std::fprintf(stderr, "alice file events:\n%s\n", qPrintable(aliceFileEvents));
        }
        if (!bobFileEvents.isEmpty()) {
            std::fprintf(stderr, "bob file events:\n%s\n", qPrintable(bobFileEvents));
        }
        if (!bobNotifications.isEmpty()) {
            std::fprintf(stderr, "bob notifications:\n%s\n", qPrintable(bobNotifications));
        }
    }

    alice.stop();
    bob.stop();
    server.stop();
    redisEnv.stop();
    return ok ? 0 : 1;
}

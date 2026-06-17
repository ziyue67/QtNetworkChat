#include "server.h"
#include "test_redis_support.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

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
            && bob.hasPrivateMessage(privateContent, alice.userId(), bob.userId());
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
            && bob.hasGroupMessage(groupContent, alice.userId(), groupId);
    }, {&alice, &bob}), "bob QQNTEngine should emit the group message event with a group session id") && ok;

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
    }

    alice.stop();
    bob.stop();
    server.stop();
    redisEnv.stop();
    return ok ? 0 : 1;
}

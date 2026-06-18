#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTextStream>
#include <QThread>

#include <cstdio>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        return false;
    }
    return true;
}

bool expect(bool condition, const QString& message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
        return false;
    }
    return true;
}

QJsonObject parseProtocolLine(const QByteArray& line, bool* ok) {
    *ok = false;
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return QJsonObject();
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonObject();
    }

    *ok = true;
    return document.object();
}

QStringList readProtocolCommands(const QString& path, bool* ok) {
    *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "protocol contract fixture should be readable: %s\n", qPrintable(file.errorString()));
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        std::fprintf(stderr, "protocol contract fixture should be a JSON object: %s\n", qPrintable(error.errorString()));
        return {};
    }

    QStringList commands;
    const QJsonArray commandArray = document.object().value(QStringLiteral("commands")).toArray();
    for (const QJsonValue& value : commandArray) {
        const QString command = value.toString().trimmed();
        if (!command.isEmpty()) {
            commands.append(command);
        }
    }

    if (commands.isEmpty()) {
        std::fprintf(stderr, "protocol contract fixture should include commands\n");
        return {};
    }

    *ok = true;
    return commands;
}

QSet<QString> missingAckReqIds(const QSet<QString>& expectedAckReqIds, const QSet<QString>& seenAckReqIds) {
    QSet<QString> missing = expectedAckReqIds;
    missing.subtract(seenAckReqIds);
    return missing;
}

bool readProcessOutput(QProcess* process,
                       QByteArray* stdoutBytes,
                       QByteArray* stderrBytes,
                       const QSet<QString>& expectedAckReqIds) {
    QSet<QString> seenAckReqIds;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < 5000 && !missingAckReqIds(expectedAckReqIds, seenAckReqIds).isEmpty()) {
        process->waitForReadyRead(100);
        *stdoutBytes += process->readAllStandardOutput();
        *stderrBytes += process->readAllStandardError();
        const QList<QByteArray> lines = stdoutBytes->split('\n');
        for (const QByteArray& line : lines) {
            bool parsed = false;
            const QJsonObject object = parseProtocolLine(line, &parsed);
            if (!parsed) {
                continue;
            }
            if (object.value(QStringLiteral("type")).toString() == QLatin1String("ack")) {
                seenAckReqIds.insert(object.value(QStringLiteral("reqId")).toString());
            }
        }
    }

    return missingAckReqIds(expectedAckReqIds, seenAckReqIds).isEmpty();
}

bool writeCommand(QProcess* process, const QByteArray& command) {
    process->write(command);
    return process->waitForBytesWritten(1000);
}

QString contractReqId(const QString& op) {
    return QStringLiteral("contract-%1").arg(op);
}

QJsonObject contractProbePayload(const QString& op) {
    QJsonObject payload;
    if (op == QLatin1String("connect")) {
        payload[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
        payload[QStringLiteral("port")] = 65535;
    } else if (op == QLatin1String("login")) {
        payload[QStringLiteral("account")] = QStringLiteral("smoke-account");
        payload[QStringLiteral("password")] = QStringLiteral("smoke-password");
    } else if (op == QLatin1String("register")) {
        payload[QStringLiteral("account")] = QStringLiteral("smoke-register");
        payload[QStringLiteral("password")] = QStringLiteral("smoke-password");
        payload[QStringLiteral("userName")] = QStringLiteral("Smoke Register");
    } else if (op == QLatin1String("set_user_info")) {
        payload[QStringLiteral("userId")] = QStringLiteral("10000");
        payload[QStringLiteral("userName")] = QStringLiteral("Smoke User");
    } else if (op == QLatin1String("search_friend")) {
        payload[QStringLiteral("account")] = QStringLiteral("10001");
    } else if (op == QLatin1String("send_friend_request")) {
        payload[QStringLiteral("receiverId")] = QStringLiteral("10001");
    } else if (op == QLatin1String("respond_friend_request")) {
        payload[QStringLiteral("senderId")] = QStringLiteral("10001");
        payload[QStringLiteral("accepted")] = false;
    } else if (op == QLatin1String("send_private_message")) {
        payload[QStringLiteral("receiverId")] = QStringLiteral("10001");
        payload[QStringLiteral("content")] = QStringLiteral("hello from smoke");
    } else if (op == QLatin1String("send_group_message")) {
        payload[QStringLiteral("groupId")] = QStringLiteral("public");
        payload[QStringLiteral("content")] = QStringLiteral("hello from smoke");
    } else if (op == QLatin1String("create_group")) {
        payload[QStringLiteral("groupName")] = QStringLiteral("Smoke Group");
        payload[QStringLiteral("members")] = QJsonArray();
        payload[QStringLiteral("announcement")] = QStringLiteral("Smoke announcement");
    } else if (op == QLatin1String("update_group_announcement")) {
        payload[QStringLiteral("groupId")] = QStringLiteral("public");
        payload[QStringLiteral("announcement")] = QStringLiteral("Smoke announcement");
    } else if (op == QLatin1String("update_group_member")) {
        payload[QStringLiteral("groupId")] = QStringLiteral("public");
        payload[QStringLiteral("memberId")] = QStringLiteral("10001");
        payload[QStringLiteral("action")] = QStringLiteral("add");
    } else if (op == QLatin1String("send_file") || op == QLatin1String("send_image")) {
        payload[QStringLiteral("filePath")] = QStringLiteral("C:/tmp/qqnt-smoke-missing.bin");
        payload[QStringLiteral("receiverId")] = QStringLiteral("10001");
    } else if (op == QLatin1String("cancel_transfer")) {
        payload[QStringLiteral("transferId")] = QStringLiteral("contract-transfer");
    } else if (op == QLatin1String("query_resume")) {
        payload[QStringLiteral("transferId")] = QStringLiteral("contract-transfer");
        payload[QStringLiteral("filePath")] = QStringLiteral("C:/tmp/qqnt-smoke-missing.bin");
    } else if (op == QLatin1String("e2e_status")) {
        payload[QStringLiteral("peerId")] = QStringLiteral("10001");
    } else if (op == QLatin1String("e2e_announce_identity") || op == QLatin1String("e2e_request_rotation")) {
        payload[QStringLiteral("peerId")] = QStringLiteral("10001");
    } else if (op == QLatin1String("e2e_pin_identity")) {
        payload[QStringLiteral("peerId")] = QStringLiteral("10001");
        payload[QStringLiteral("fingerprint")] = QStringLiteral("smoke-fingerprint");
    } else if (op == QLatin1String("profile_update")) {
        payload[QStringLiteral("userName")] = QStringLiteral("Contract Smoke User");
    } else if (op == QLatin1String("settings_sync")) {
        QJsonObject notifications;
        notifications[QStringLiteral("desktop")] = false;
        QJsonObject settings;
        settings[QStringLiteral("notifications")] = notifications;
        payload[QStringLiteral("settings")] = settings;
    }
    return payload;
}

bool expectEmptyPayload(const QString& op, const QJsonObject& payload) {
    return expect(payload.isEmpty(), QStringLiteral("%1 payload should be empty").arg(op));
}

bool expectArrayField(const QString& op, const QJsonObject& payload, const QString& field) {
    return expect(payload.value(field).isArray(),
                  QStringLiteral("%1 payload should include %2 array").arg(op, field));
}

bool expectBoolField(const QString& op, const QJsonObject& payload, const QString& field) {
    return expect(payload.value(field).isBool(),
                  QStringLiteral("%1 payload should include %2 boolean").arg(op, field));
}

bool expectTrueBoolField(const QString& op, const QJsonObject& payload, const QString& field) {
    return expect(payload.value(field).toBool(false),
                  QStringLiteral("%1 payload should include true %2").arg(op, field));
}

bool expectObjectField(const QString& op, const QJsonObject& payload, const QString& field) {
    return expect(payload.value(field).isObject(),
                  QStringLiteral("%1 payload should include %2 object").arg(op, field));
}

bool expectStringField(const QString& op, const QJsonObject& payload, const QString& field) {
    return expect(payload.value(field).isString(),
                  QStringLiteral("%1 payload should include %2 string").arg(op, field));
}

bool expectNonEmptyStringField(const QString& op, const QJsonObject& payload, const QString& field) {
    return expect(!payload.value(field).toString().isEmpty(),
                  QStringLiteral("%1 payload should include non-empty %2").arg(op, field));
}

bool expectUnsignedIntegerStringField(const QString& op, const QJsonObject& payload, const QString& field) {
    const QString text = payload.value(field).toString();
    bool converted = false;
    text.toULongLong(&converted);
    return expect(converted && !text.isEmpty(),
                  QStringLiteral("%1 payload should include unsigned integer string %2").arg(op, field));
}

bool expectUnsignedIntegerStringArrayField(const QString& op, const QJsonObject& payload, const QString& field) {
    if (!expectArrayField(op, payload, field)) {
        return false;
    }

    bool ok = true;
    const QJsonArray values = payload.value(field).toArray();
    for (const QJsonValue& value : values) {
        const QString text = value.toString();
        bool converted = false;
        text.toULongLong(&converted);
        ok = expect(converted && !text.isEmpty(),
                    QStringLiteral("%1 payload should include unsigned integer string items in %2").arg(op, field)) && ok;
    }
    return ok;
}

bool expectAcceptedPayload(const QString& op, const QJsonObject& payload) {
    return expectTrueBoolField(op, payload, QStringLiteral("accepted"));
}

bool validateOkContractAckPayload(const QString& op, const QJsonObject& payload) {
    bool ok = true;

    if (op == QLatin1String("ready")) {
        ok = expect(payload.value(QStringLiteral("protocolVersion")).toInt() == 1,
                    "ready contract payload should advertise protocol version 1") && ok;
        ok = expectNonEmptyStringField(op, payload, QStringLiteral("version")) && ok;
        ok = expectNonEmptyStringField(op, payload, QStringLiteral("qtVersion")) && ok;
        ok = expectNonEmptyStringField(op, payload, QStringLiteral("e2eStatus")) && ok;
    } else if (op == QLatin1String("connect")) {
        ok = expectTrueBoolField(op, payload, QStringLiteral("connected")) && ok;
        ok = expectNonEmptyStringField(op, payload, QStringLiteral("host")) && ok;
        const int port = payload.value(QStringLiteral("port")).toInt();
        ok = expect(port > 0 && port <= 65535, "connect contract payload should include valid port") && ok;
    } else if (op == QLatin1String("login") || op == QLatin1String("register")) {
        ok = expectAcceptedPayload(op, payload) && ok;
        ok = expectBoolField(op, payload, QStringLiteral("requiresConnect")) && ok;
        ok = expect(payload.value(QStringLiteral("mode")).toString() == op,
                    QStringLiteral("%1 contract payload should echo mode").arg(op)) && ok;
    } else if (op == QLatin1String("disconnect")
               || op == QLatin1String("logout")
               || op == QLatin1String("set_user_info")) {
        ok = expectEmptyPayload(op, payload) && ok;
    } else if (op == QLatin1String("get_user_list")) {
        ok = expectArrayField(op, payload, QStringLiteral("users")) && ok;
        ok = expect(!payload.contains(QStringLiteral("friends")),
                    "get_user_list contract payload should not alias friends") && ok;
    } else if (op == QLatin1String("get_friend_list")) {
        ok = expectArrayField(op, payload, QStringLiteral("friends")) && ok;
        ok = expect(!payload.contains(QStringLiteral("users")),
                    "get_friend_list contract payload should not alias users") && ok;
    } else if (op == QLatin1String("get_group_list")) {
        ok = expectArrayField(op, payload, QStringLiteral("groups")) && ok;
        ok = expectArrayField(op, payload, QStringLiteral("removedGroups")) && ok;
        ok = expectBoolField(op, payload, QStringLiteral("hasSnapshot")) && ok;
    } else if (op == QLatin1String("search_friend")
               || op == QLatin1String("send_friend_request")
               || op == QLatin1String("respond_friend_request")
               || op == QLatin1String("send_group_message")
               || op == QLatin1String("create_group")
               || op == QLatin1String("update_group_announcement")
               || op == QLatin1String("update_group_member")
               || op == QLatin1String("send_file")
               || op == QLatin1String("send_image")
               || op == QLatin1String("e2e_announce_identity")
               || op == QLatin1String("e2e_pin_identity")
               || op == QLatin1String("e2e_request_rotation")) {
        ok = expectAcceptedPayload(op, payload) && ok;
    } else if (op == QLatin1String("send_private_message")) {
        ok = expectNonEmptyStringField(op, payload, QStringLiteral("receiverId")) && ok;
    } else if (op == QLatin1String("cancel_transfer")) {
        ok = expectTrueBoolField(op, payload, QStringLiteral("cancelled")) && ok;
        ok = expectNonEmptyStringField(op, payload, QStringLiteral("transferId")) && ok;
    } else if (op == QLatin1String("query_resume")) {
        ok = expectTrueBoolField(op, payload, QStringLiteral("canResume")) && ok;
        ok = expectNonEmptyStringField(op, payload, QStringLiteral("transferId")) && ok;
        ok = expectUnsignedIntegerStringField(op, payload, QStringLiteral("confirmedBytes")) && ok;
        ok = expectUnsignedIntegerStringField(op, payload, QStringLiteral("nextChunkIndex")) && ok;
        ok = expectUnsignedIntegerStringField(op, payload, QStringLiteral("fileSize")) && ok;
        ok = expectUnsignedIntegerStringField(op, payload, QStringLiteral("chunkSize")) && ok;
        ok = expectUnsignedIntegerStringField(op, payload, QStringLiteral("chunkCount")) && ok;
        ok = expectStringField(op, payload, QStringLiteral("fileHash")) && ok;
        ok = expectUnsignedIntegerStringArrayField(op, payload, QStringLiteral("receivedChunks")) && ok;
        ok = expectBoolField(op, payload, QStringLiteral("resumed")) && ok;
        const QString mode = payload.value(QStringLiteral("mode")).toString();
        ok = expect(mode == QLatin1String("query") || mode == QLatin1String("resume"),
                    "query_resume contract payload should include query/resume mode") && ok;
    } else if (op == QLatin1String("e2e_status")) {
        ok = expectObjectField(op, payload, QStringLiteral("localIdentity")) && ok;
        if (payload.contains(QStringLiteral("peerId"))) {
            ok = expectNonEmptyStringField(op, payload, QStringLiteral("peerId")) && ok;
            ok = expectObjectField(op, payload, QStringLiteral("session")) && ok;
            ok = expectObjectField(op, payload, QStringLiteral("identity")) && ok;
        }
    } else if (op == QLatin1String("profile_update")) {
        ok = expectAcceptedPayload(op, payload) && ok;
        ok = expectBoolField(op, payload, QStringLiteral("avatarSent")) && ok;
        ok = expectStringField(op, payload, QStringLiteral("userName")) && ok;
    } else if (op == QLatin1String("settings_sync")) {
        ok = expectAcceptedPayload(op, payload) && ok;
        ok = expect(payload.value(QStringLiteral("revision")).toInt() >= 1,
                    "settings_sync contract payload should include revision") && ok;
        ok = expectObjectField(op, payload, QStringLiteral("settings")) && ok;
        if (payload.contains(QStringLiteral("appliedDownloadDir"))) {
            ok = expectNonEmptyStringField(op, payload, QStringLiteral("appliedDownloadDir")) && ok;
        }
    }

    return ok;
}

bool writeJsonCommand(QProcess* process, const QString& op, const QString& reqId, const QJsonObject& payload) {
    QJsonObject command;
    command[QStringLiteral("op")] = op;
    command[QStringLiteral("reqId")] = reqId;
    command[QStringLiteral("payload")] = payload;
    QByteArray line = QJsonDocument(command).toJson(QJsonDocument::Compact);
    line.push_back('\n');
    return writeCommand(process, line);
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 3) {
        std::fprintf(stderr, "usage: %s <QQNTEngine executable> <protocol contract fixture>\n", argv[0]);
        return 2;
    }

    const QString enginePath = arguments.at(1);
    bool contractReadOk = false;
    const QStringList contractCommands = readProtocolCommands(arguments.at(2), &contractReadOk);
    if (!contractReadOk) {
        return 1;
    }

    QProcess process;
    process.setProgram(enginePath);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    bool ok = true;
    bool sawSettingsSyncedEvent = false;
    ok = expect(process.waitForStarted(5000), "QQNTEngine should start") && ok;
    if (!ok) {
        return 1;
    }

    ok = expect(writeCommand(&process, "{\"op\":\"ready\",\"reqId\":\"smoke-ready\",\"payload\":{}}\n"),
                "ready command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"get_user_list\",\"reqId\":\"smoke-users\",\"payload\":{}}\n"),
                "get_user_list command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"get_friend_list\",\"reqId\":\"smoke-friends\",\"payload\":{}}\n"),
                "get_friend_list command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"get_group_list\",\"reqId\":\"smoke-groups\",\"payload\":{}}\n"),
                "get_group_list command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"e2e_status\",\"reqId\":\"smoke-e2e\",\"payload\":{}}\n"),
                "e2e_status command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"e2e_announce_identity\",\"reqId\":\"smoke-e2e-announce-missing\",\"payload\":{}}\n"),
                "e2e_announce_identity missing peer command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"cancel_transfer\",\"reqId\":\"smoke-cancel-missing\",\"payload\":{}}\n"),
                "cancel_transfer missing field command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"cancel_transfer\",\"reqId\":\"smoke-cancel-inactive\",\"payload\":{\"transferId\":\"smoke-transfer\"}}\n"),
                "cancel_transfer inactive command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"send_file\",\"reqId\":\"smoke-file-missing-target\",\"payload\":{\"filePath\":\"C:/tmp/missing.txt\"}}\n"),
                "send_file missing target command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"send_image\",\"reqId\":\"smoke-file-ambiguous-target\",\"payload\":{\"filePath\":\"C:/tmp/missing.png\",\"receiverId\":\"10001\",\"groupId\":\"public\"}}\n"),
                "send_image ambiguous target command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"query_resume\",\"reqId\":\"smoke-resume-missing-target\",\"payload\":{\"transferId\":\"resume-transfer\",\"filePath\":\"C:/tmp/missing.txt\"}}\n"),
                "query_resume missing target command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"query_resume\",\"reqId\":\"smoke-resume-ambiguous-target\",\"payload\":{\"transferId\":\"resume-transfer\",\"filePath\":\"C:/tmp/missing.txt\",\"receiverId\":\"10001\",\"groupId\":\"public\"}}\n"),
                "query_resume ambiguous target command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"profile_update\",\"reqId\":\"smoke-profile\",\"payload\":{\"userName\":\"Smoke User\"}}\n"),
                "profile_update command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"settings_sync\",\"reqId\":\"smoke-settings\",\"payload\":{\"settings\":{\"notifications\":{\"desktop\":true},\"files\":{\"autoDownload\":false}}}}\n"),
                "settings_sync command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"settings_sync\",\"reqId\":\"smoke-settings-invalid\",\"payload\":{\"settings\":\"bad\"}}\n"),
                "invalid settings_sync command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"search_friend\",\"reqId\":\"smoke-invalid-payload\",\"payload\":\"bad\"}\n"),
                "invalid payload command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"search_friend\",\"reqId\":\"smoke-missing-field\",\"payload\":{}}\n"),
                "missing field command should be written") && ok;

    QSet<QString> contractAckReqIds;
    for (const QString& command : contractCommands) {
        const QString reqId = contractReqId(command);
        contractAckReqIds.insert(reqId);
        ok = expect(writeJsonCommand(&process, command, reqId, contractProbePayload(command)),
                    QStringLiteral("protocol contract command should be written: %1").arg(command)) && ok;
    }
    process.closeWriteChannel();

    QSet<QString> expectedAckReqIds = {
        QStringLiteral("smoke-ready"),
        QStringLiteral("smoke-users"),
        QStringLiteral("smoke-friends"),
        QStringLiteral("smoke-groups"),
        QStringLiteral("smoke-e2e"),
        QStringLiteral("smoke-e2e-announce-missing"),
        QStringLiteral("smoke-cancel-missing"),
        QStringLiteral("smoke-cancel-inactive"),
        QStringLiteral("smoke-file-missing-target"),
        QStringLiteral("smoke-file-ambiguous-target"),
        QStringLiteral("smoke-resume-missing-target"),
        QStringLiteral("smoke-resume-ambiguous-target"),
        QStringLiteral("smoke-profile"),
        QStringLiteral("smoke-settings"),
        QStringLiteral("smoke-settings-invalid"),
        QStringLiteral("smoke-invalid-payload"),
        QStringLiteral("smoke-missing-field")
    };
    expectedAckReqIds.unite(contractAckReqIds);

    QByteArray stdoutBytes;
    QByteArray stderrBytes;
    ok = expect(readProcessOutput(&process, &stdoutBytes, &stderrBytes, expectedAckReqIds),
                "QQNTEngine should ack core IPC commands") && ok;
    process.waitForFinished(5000);
    stdoutBytes += process.readAllStandardOutput();
    stderrBytes += process.readAllStandardError();

    bool sawReadyEvent = false;
    QSet<QString> seenAckReqIds;
    int protocolLineCount = 0;
    const int contractReqIdPrefixLength = QStringLiteral("contract-").size();
    const QList<QByteArray> lines = stdoutBytes.split('\n');
    for (const QByteArray& line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        ++protocolLineCount;
        bool parsed = false;
        const QJsonObject object = parseProtocolLine(trimmed, &parsed);
        ok = expect(parsed, "stdout line should be a JSON object") && ok;
        if (!parsed) {
            continue;
        }

        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("event")) {
            sawReadyEvent = object.value(QStringLiteral("event")).toString() == QLatin1String("ready") || sawReadyEvent;
            if (object.value(QStringLiteral("event")).toString() == QLatin1String("settings_synced")) {
                const QJsonObject payload = object.value(QStringLiteral("payload")).toObject();
                sawSettingsSyncedEvent = sawSettingsSyncedEvent
                    || (payload.value(QStringLiteral("revision")).toInt() >= 1
                        && payload.value(QStringLiteral("settings")).toObject().value(QStringLiteral("notifications")).isObject());
            }
            continue;
        }

        if (type != QLatin1String("ack")) {
            continue;
        }

        const QString reqId = object.value(QStringLiteral("reqId")).toString();
        seenAckReqIds.insert(reqId);
        const QJsonObject payload = object.value(QStringLiteral("payload")).toObject();

        if (reqId == QLatin1String("smoke-ready")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "ready command should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("protocolVersion")).toInt() == 1,
                        "ready payload should advertise protocol version 1") && ok;
            ok = expect(!payload.value(QStringLiteral("version")).toString().isEmpty(),
                        "ready payload should include engine version") && ok;
            ok = expect(!payload.value(QStringLiteral("qtVersion")).toString().isEmpty(),
                        "ready payload should include Qt version") && ok;
            ok = expect(!payload.value(QStringLiteral("e2eStatus")).toString().isEmpty(),
                        "ready payload should include E2E status") && ok;
        } else if (reqId == QLatin1String("smoke-users")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "get_user_list should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("users")).isArray(),
                        "get_user_list payload should include users array") && ok;
            ok = expect(!payload.contains(QStringLiteral("friends")),
                        "get_user_list payload should not alias friends") && ok;
        } else if (reqId == QLatin1String("smoke-friends")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "get_friend_list should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("friends")).isArray(),
                        "get_friend_list payload should include friends array") && ok;
            ok = expect(!payload.contains(QStringLiteral("users")),
                        "get_friend_list payload should not alias users") && ok;
        } else if (reqId == QLatin1String("smoke-groups")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "get_group_list should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("groups")).isArray(),
                        "get_group_list payload should include groups array") && ok;
            ok = expect(payload.value(QStringLiteral("removedGroups")).isArray(),
                        "get_group_list payload should include removedGroups array") && ok;
            ok = expect(payload.value(QStringLiteral("hasSnapshot")).isBool(),
                        "get_group_list payload should include hasSnapshot boolean") && ok;
        } else if (reqId == QLatin1String("smoke-e2e")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "e2e_status should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("localIdentity")).isObject(),
                        "e2e_status payload should include local identity object") && ok;
        } else if (reqId == QLatin1String("smoke-e2e-announce-missing")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "e2e_announce_identity missing peerId should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("missing_field"),
                        "e2e_announce_identity missing peerId should use missing_field code") && ok;
        } else if (reqId == QLatin1String("smoke-cancel-missing")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "cancel_transfer missing transferId should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("missing_field"),
                        "cancel_transfer missing transferId should use missing_field code") && ok;
        } else if (reqId == QLatin1String("smoke-cancel-inactive")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "cancel_transfer without active transfer should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("transfer_not_active"),
                        "cancel_transfer without active transfer should use transfer_not_active code") && ok;
        } else if (reqId == QLatin1String("smoke-file-missing-target")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "send_file without target should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("missing_target"),
                        "send_file without target should use missing_target code") && ok;
        } else if (reqId == QLatin1String("smoke-file-ambiguous-target")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "send_image with two targets should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("ambiguous_target"),
                        "send_image with two targets should use ambiguous_target code") && ok;
        } else if (reqId == QLatin1String("smoke-resume-missing-target")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "query_resume without target should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("missing_target"),
                        "query_resume without target should use missing_target code") && ok;
        } else if (reqId == QLatin1String("smoke-resume-ambiguous-target")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "query_resume with two targets should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("ambiguous_target"),
                        "query_resume with two targets should use ambiguous_target code") && ok;
        } else if (reqId == QLatin1String("smoke-profile")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "profile_update should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("userName")).toString() == QLatin1String("Smoke User"),
                        "profile_update payload should echo updated user name") && ok;
        } else if (reqId == QLatin1String("smoke-settings")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "settings_sync should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("accepted")).toBool(false),
                        "settings_sync payload should be accepted") && ok;
            ok = expect(payload.value(QStringLiteral("revision")).toInt() == 1,
                        "settings_sync payload should include revision") && ok;
            ok = expect(payload.value(QStringLiteral("settings")).toObject().value(QStringLiteral("files")).isObject(),
                        "settings_sync payload should echo settings object") && ok;
        } else if (reqId == QLatin1String("smoke-settings-invalid")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "invalid settings_sync should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("invalid_settings"),
                        "invalid settings_sync should use invalid_settings code") && ok;
        } else if (reqId == QLatin1String("smoke-invalid-payload")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "non-object payload should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("invalid_payload"),
                        "non-object payload should use invalid_payload code") && ok;
        } else if (reqId == QLatin1String("smoke-missing-field")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "missing required field should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("missing_field"),
                        "missing required field should use missing_field code") && ok;
        }

        if (contractAckReqIds.contains(reqId)) {
            const QString contractOp = reqId.mid(contractReqIdPrefixLength);
            ok = expect(object.value(QStringLiteral("op")).toString() == contractOp,
                        QStringLiteral("protocol contract ack should echo op: %1").arg(contractOp)) && ok;
            if (object.value(QStringLiteral("status")).toString() == QLatin1String("ok")) {
                ok = validateOkContractAckPayload(contractOp, payload) && ok;
            } else if (object.value(QStringLiteral("status")).toString() == QLatin1String("error")) {
                const QString errorCode = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
                ok = expect(errorCode != QLatin1String("unknown_op"),
                            QStringLiteral("protocol contract command should be routed, not unknown_op: %1").arg(contractOp)) && ok;
            }
        }
    }

    ok = expect(protocolLineCount >= expectedAckReqIds.size() + 1, "QQNTEngine should emit startup event and command acks") && ok;
    ok = expect(sawReadyEvent, "QQNTEngine should emit startup ready event") && ok;
    ok = expect(sawSettingsSyncedEvent, "QQNTEngine should emit settings_synced event") && ok;
    ok = expect(missingAckReqIds(expectedAckReqIds, seenAckReqIds).isEmpty(), "QQNTEngine should ack every smoke command") && ok;
    return ok ? 0 : 1;
}

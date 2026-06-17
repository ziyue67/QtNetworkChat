#include <QCoreApplication>
#include <QElapsedTimer>
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
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2) {
        std::fprintf(stderr, "usage: %s <QQNTEngine executable>\n", argv[0]);
        return 2;
    }

    const QString enginePath = arguments.at(1);
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
    ok = expect(writeCommand(&process, "{\"op\":\"get_group_list\",\"reqId\":\"smoke-groups\",\"payload\":{}}\n"),
                "get_group_list command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"e2e_status\",\"reqId\":\"smoke-e2e\",\"payload\":{}}\n"),
                "e2e_status command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"cancel_transfer\",\"reqId\":\"smoke-cancel-missing\",\"payload\":{}}\n"),
                "cancel_transfer missing field command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"cancel_transfer\",\"reqId\":\"smoke-cancel-inactive\",\"payload\":{\"transferId\":\"smoke-transfer\"}}\n"),
                "cancel_transfer inactive command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"send_file\",\"reqId\":\"smoke-file-missing-target\",\"payload\":{\"filePath\":\"C:/tmp/missing.txt\"}}\n"),
                "send_file missing target command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"send_image\",\"reqId\":\"smoke-file-ambiguous-target\",\"payload\":{\"filePath\":\"C:/tmp/missing.png\",\"receiverId\":\"10001\",\"groupId\":\"public\"}}\n"),
                "send_image ambiguous target command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"profile_update\",\"reqId\":\"smoke-profile\",\"payload\":{\"userName\":\"Smoke User\"}}\n"),
                "profile_update command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"settings_sync\",\"reqId\":\"smoke-settings\",\"payload\":{\"settings\":{\"notifications\":{\"desktop\":true},\"files\":{\"autoDownload\":false}}}}\n"),
                "settings_sync command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"settings_sync\",\"reqId\":\"smoke-settings-invalid\",\"payload\":{\"settings\":\"bad\"}}\n"),
                "invalid settings_sync command should be written") && ok;
    ok = expect(writeCommand(&process, "{\"op\":\"search_friend\",\"reqId\":\"smoke-missing-field\",\"payload\":{}}\n"),
                "missing field command should be written") && ok;
    process.closeWriteChannel();

    const QSet<QString> expectedAckReqIds = {
        QStringLiteral("smoke-ready"),
        QStringLiteral("smoke-users"),
        QStringLiteral("smoke-groups"),
        QStringLiteral("smoke-e2e"),
        QStringLiteral("smoke-cancel-missing"),
        QStringLiteral("smoke-cancel-inactive"),
        QStringLiteral("smoke-file-missing-target"),
        QStringLiteral("smoke-file-ambiguous-target"),
        QStringLiteral("smoke-profile"),
        QStringLiteral("smoke-settings"),
        QStringLiteral("smoke-settings-invalid"),
        QStringLiteral("smoke-missing-field")
    };

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
                sawSettingsSyncedEvent = payload.value(QStringLiteral("revision")).toInt() == 1
                    && payload.value(QStringLiteral("settings")).toObject().value(QStringLiteral("notifications")).isObject();
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
        } else if (reqId == QLatin1String("smoke-groups")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "get_group_list should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("groups")).isArray(),
                        "get_group_list payload should include groups array") && ok;
        } else if (reqId == QLatin1String("smoke-e2e")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("ok"),
                        "e2e_status should return ok ack") && ok;
            ok = expect(payload.value(QStringLiteral("localIdentity")).isObject(),
                        "e2e_status payload should include local identity object") && ok;
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
        } else if (reqId == QLatin1String("smoke-missing-field")) {
            ok = expect(object.value(QStringLiteral("status")).toString() == QLatin1String("error"),
                        "missing required field should return error ack") && ok;
            ok = expect(object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("missing_field"),
                        "missing required field should use missing_field code") && ok;
        }
    }

    ok = expect(protocolLineCount >= expectedAckReqIds.size() + 1, "QQNTEngine should emit startup event and command acks") && ok;
    ok = expect(sawReadyEvent, "QQNTEngine should emit startup ready event") && ok;
    ok = expect(sawSettingsSyncedEvent, "QQNTEngine should emit settings_synced event") && ok;
    ok = expect(missingAckReqIds(expectedAckReqIds, seenAckReqIds).isEmpty(), "QQNTEngine should ack every smoke command") && ok;
    return ok ? 0 : 1;
}

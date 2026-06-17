#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
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

bool readProcessOutput(QProcess* process, QByteArray* stdoutBytes, QByteArray* stderrBytes) {
    bool sawReadyAck = false;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < 5000 && !sawReadyAck) {
        process->waitForReadyRead(100);
        *stdoutBytes += process->readAllStandardOutput();
        *stderrBytes += process->readAllStandardError();
        const QList<QByteArray> lines = stdoutBytes->split('\n');
        for (const QByteArray& line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(trimmed, &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                const QJsonObject object = document.object();
                sawReadyAck = object.value(QStringLiteral("type")).toString() == QStringLiteral("ack")
                    && object.value(QStringLiteral("op")).toString() == QStringLiteral("ready")
                    && object.value(QStringLiteral("reqId")).toString() == QStringLiteral("smoke-ready");
            }
        }
    }

    return sawReadyAck;
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
    ok = expect(process.waitForStarted(5000), "QQNTEngine should start") && ok;
    if (!ok) {
        return 1;
    }

    process.write("{\"op\":\"ready\",\"reqId\":\"smoke-ready\",\"payload\":{}}\n");
    ok = expect(process.waitForBytesWritten(1000), "ready command should be written") && ok;
    process.closeWriteChannel();

    QByteArray stdoutBytes;
    QByteArray stderrBytes;
    ok = expect(readProcessOutput(&process, &stdoutBytes, &stderrBytes), "QQNTEngine should emit ready ack") && ok;
    process.waitForFinished(5000);
    stdoutBytes += process.readAllStandardOutput();
    stderrBytes += process.readAllStandardError();

    bool sawReadyEvent = false;
    bool sawReadyAck = false;
    int protocolLineCount = 0;
    const QList<QByteArray> lines = stdoutBytes.split('\n');
    for (const QByteArray& line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        ++protocolLineCount;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(trimmed, &error);
        ok = expect(error.error == QJsonParseError::NoError && document.isObject(), "stdout line should be a JSON object") && ok;
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }

        const QJsonObject object = document.object();
        const QJsonObject payload = object.value(QStringLiteral("payload")).toObject();
        ok = expect(payload.value(QStringLiteral("protocolVersion")).toInt() == 1,
                    "ready payload should advertise protocol version 1") && ok;
        ok = expect(!payload.value(QStringLiteral("version")).toString().isEmpty(),
                    "ready payload should include engine version") && ok;
        ok = expect(!payload.value(QStringLiteral("qtVersion")).toString().isEmpty(),
                    "ready payload should include Qt version") && ok;
        ok = expect(!payload.value(QStringLiteral("e2eStatus")).toString().isEmpty(),
                    "ready payload should include E2E status") && ok;

        if (object.value(QStringLiteral("type")).toString() == QStringLiteral("event")) {
            sawReadyEvent = object.value(QStringLiteral("event")).toString() == QStringLiteral("ready") || sawReadyEvent;
        }
        if (object.value(QStringLiteral("type")).toString() == QStringLiteral("ack")) {
            sawReadyAck = object.value(QStringLiteral("op")).toString() == QStringLiteral("ready")
                && object.value(QStringLiteral("reqId")).toString() == QStringLiteral("smoke-ready")
                && object.value(QStringLiteral("status")).toString() == QStringLiteral("ok") || sawReadyAck;
        }
    }

    ok = expect(protocolLineCount >= 2, "QQNTEngine should emit startup event and ready ack") && ok;
    ok = expect(sawReadyEvent, "QQNTEngine should emit startup ready event") && ok;
    ok = expect(sawReadyAck, "QQNTEngine should ack ready command") && ok;
    return ok ? 0 : 1;
}

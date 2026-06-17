#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        return false;
    }
    return true;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2) {
        std::fprintf(stderr, "usage: %s <ready fixture>\n", argv[0]);
        return 2;
    }

    QFile fixture(arguments.at(1));
    bool ok = true;
    ok = expect(fixture.open(QIODevice::ReadOnly), "ready fixture should be readable") && ok;
    if (!ok) {
        return 1;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(fixture.readAll(), &error);
    ok = expect(error.error == QJsonParseError::NoError && document.isObject(),
                "ready fixture should be a JSON object") && ok;
    if (!ok) {
        return 1;
    }

    const QJsonObject envelope = document.object();
    const QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
    ok = expect(envelope.value(QStringLiteral("type")).toString() == QStringLiteral("event"),
                "ready fixture should use event envelope") && ok;
    ok = expect(envelope.value(QStringLiteral("event")).toString() == QStringLiteral("ready"),
                "ready fixture should describe ready event") && ok;
    ok = expect(payload.value(QStringLiteral("protocolVersion")).toInt() == 1,
                "ready fixture should pin protocol version 1") && ok;
    ok = expect(!payload.value(QStringLiteral("version")).toString().isEmpty(),
                "ready fixture should include engine version") && ok;
    ok = expect(!payload.value(QStringLiteral("qtVersion")).toString().isEmpty(),
                "ready fixture should include Qt version") && ok;
    ok = expect(!payload.value(QStringLiteral("e2eStatus")).toString().isEmpty(),
                "ready fixture should include E2E status") && ok;

    return ok ? 0 : 1;
}

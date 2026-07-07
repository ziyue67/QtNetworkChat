#include "qqnt_backend_service.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdio>

namespace {
bool expect(bool condition, const QString& message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", qPrintable(message));
        return false;
    }
    return true;
}

QSet<QString> toSet(const QJsonArray& array) {
    QSet<QString> set;
    for (const QJsonValue& value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) set.insert(text);
    }
    return set;
}

QSet<QString> toSet(const QStringList& values) {
    return QSet<QString>(values.cbegin(), values.cend());
}

bool expectSameSet(const QSet<QString>& actual, const QSet<QString>& expected, const QString& label) {
    bool ok = true;
    const QSet<QString> missing = expected - actual;
    const QSet<QString> extra = actual - expected;
    ok = expect(missing.isEmpty(), QStringLiteral("%1 missing: %2").arg(label, QStringList(missing.values()).join(", "))) && ok;
    ok = expect(extra.isEmpty(), QStringLiteral("%1 extra: %2").arg(label, QStringList(extra.values()).join(", "))) && ok;
    return ok;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() < 2) {
        std::fprintf(stderr, "usage: %s <tauri invoke commands fixture>\n", argv[0]);
        return 2;
    }

    QFile file(app.arguments().at(1));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "fixture should be readable: %s\n", qPrintable(file.errorString()));
        return 1;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject fixture = doc.object();
    bool ok = true;
    const QSet<QString> tauriInvoke = toSet(fixture.value("invokeCommands").toArray());
    const QSet<QString> localExpected = toSet(fixture.value("localBackendCommands").toArray());
    const QSet<QString> engineWrappers = toSet(fixture.value("engineCommandWrappers").toArray());
    const QSet<QString> qtLocal = toSet(QQNTBackendService::commands());

    ok = expect(tauriInvoke.size() == 69, QStringLiteral("tauri invoke command fixture should cover 69 commands")) && ok;
    ok = expect(engineWrappers.size() == 36, QStringLiteral("tauri invoke fixture should cover 36 engine wrappers")) && ok;
    QSet<QString> partition = localExpected + engineWrappers;
    partition.insert(QStringLiteral("qqnt_command"));
    ok = expect(partition == tauriInvoke,
                QStringLiteral("tauri invoke fixture should partition into generic, engine wrappers, and local backend commands")) && ok;
    ok = expectSameSet(qtLocal, localExpected, QStringLiteral("Qt local backend command parity")) && ok;

    for (const QString& op : localExpected) {
        ok = expect(QQNTBackendService::isCommand(op), QStringLiteral("QQNTBackendService should recognize %1").arg(op)) && ok;
    }
    ok = expect(!QQNTBackendService::isCommand(QStringLiteral("send_private_message")),
                QStringLiteral("engine network commands should remain in QQNTEngineCommandRouter")) && ok;

    return ok ? 0 : 1;
}

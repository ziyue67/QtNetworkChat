#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <cstdio>

namespace {
bool expect(bool condition, const QString& message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
        return false;
    }
    return true;
}

bool readJsonObject(const QString& path, QJsonObject* object, const QString& label) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "%s should be readable: %s\n", label.toUtf8().constData(), qPrintable(file.errorString()));
        return false;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        std::fprintf(stderr, "%s should be a JSON object: %s\n", label.toUtf8().constData(), qPrintable(error.errorString()));
        return false;
    }

    *object = document.object();
    return true;
}

QStringList jsonStringArray(const QJsonObject& object, const QString& key) {
    QStringList values;
    const QJsonArray array = object.value(key).toArray();
    for (const QJsonValue& value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

QSet<QString> toSet(const QStringList& values) {
    return QSet<QString>(values.cbegin(), values.cend());
}

bool expectUnique(const QStringList& values, const QString& label) {
    const QSet<QString> unique = toSet(values);
    return expect(unique.size() == values.size(), QStringLiteral("%1 should not contain duplicates").arg(label));
}

bool expectSameSet(const QStringList& actual, const QStringList& expected, const QString& label) {
    bool ok = true;
    const QSet<QString> actualSet = toSet(actual);
    const QSet<QString> expectedSet = toSet(expected);

    const QSet<QString> missing = expectedSet - actualSet;
    const QSet<QString> extra = actualSet - expectedSet;
    ok = expect(missing.isEmpty(), QStringLiteral("%1 missing: %2").arg(label, QStringList(missing.values()).join(QStringLiteral(", ")))) && ok;
    ok = expect(extra.isEmpty(), QStringLiteral("%1 extra: %2").arg(label, QStringList(extra.values()).join(QStringLiteral(", ")))) && ok;
    return ok;
}

QStringList extractMarkdownTableKeys(const QString& markdown, const QString& heading, const QString& keyColumnName) {
    const int headingIndex = markdown.indexOf(heading);
    if (headingIndex < 0) {
        return {};
    }

    const int nextHeadingIndex = markdown.indexOf(QStringLiteral("\n## "), headingIndex + heading.size());
    const QString section = nextHeadingIndex < 0 ? markdown.mid(headingIndex) : markdown.mid(headingIndex, nextHeadingIndex - headingIndex);
    const QStringList lines = section.split(QLatin1Char('\n'));
    const QRegularExpression tickedKey(QStringLiteral("`([^`]+)`"));
    QStringList values;
    bool inTable = false;

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!inTable) {
            inTable = line.startsWith(QLatin1Char('|')) && line.contains(keyColumnName);
            continue;
        }

        if (!line.startsWith(QLatin1Char('|'))) {
            if (!values.isEmpty()) {
                break;
            }
            continue;
        }
        if (line.contains(QStringLiteral("---"))) {
            continue;
        }

        const QRegularExpressionMatch match = tickedKey.match(line);
        if (match.hasMatch()) {
            values.append(match.captured(1));
        }
    }

    return values;
}

bool validateReadyFixture(const QJsonObject& envelope) {
    bool ok = true;
    const QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
    ok = expect(envelope.value(QStringLiteral("type")).toString() == QStringLiteral("event"),
                QStringLiteral("ready fixture should use event envelope")) && ok;
    ok = expect(envelope.value(QStringLiteral("event")).toString() == QStringLiteral("ready"),
                QStringLiteral("ready fixture should describe ready event")) && ok;
    ok = expect(payload.value(QStringLiteral("protocolVersion")).toInt() == 1,
                QStringLiteral("ready fixture should pin protocol version 1")) && ok;
    ok = expect(!payload.value(QStringLiteral("version")).toString().isEmpty(),
                QStringLiteral("ready fixture should include engine version")) && ok;
    ok = expect(!payload.value(QStringLiteral("qtVersion")).toString().isEmpty(),
                QStringLiteral("ready fixture should include Qt version")) && ok;
    ok = expect(!payload.value(QStringLiteral("e2eStatus")).toString().isEmpty(),
                QStringLiteral("ready fixture should include E2E status")) && ok;
    return ok;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 4) {
        std::fprintf(stderr, "usage: %s <ready fixture> <protocol doc> <protocol contract fixture>\n", argv[0]);
        return 2;
    }

    bool ok = true;
    QJsonObject readyFixture;
    QJsonObject contractFixture;
    ok = readJsonObject(arguments.at(1), &readyFixture, QStringLiteral("ready fixture")) && ok;
    ok = readJsonObject(arguments.at(3), &contractFixture, QStringLiteral("protocol contract fixture")) && ok;
    if (!ok) {
        return 1;
    }

    QFile protocolDoc(arguments.at(2));
    ok = expect(protocolDoc.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("protocol doc should be readable")) && ok;
    if (!ok) {
        return 1;
    }

    const QString markdown = QString::fromUtf8(protocolDoc.readAll());
    const QStringList documentedCommands = extractMarkdownTableKeys(markdown, QStringLiteral("## 5. 命令表"), QStringLiteral("`op`"));
    const QStringList documentedEvents = extractMarkdownTableKeys(markdown, QStringLiteral("## 6. 主动事件表"), QStringLiteral("`event`"));
    const QStringList expectedCommands = jsonStringArray(contractFixture, QStringLiteral("commands"));
    const QStringList expectedEvents = jsonStringArray(contractFixture, QStringLiteral("events"));

    ok = validateReadyFixture(readyFixture) && ok;
    ok = expect(contractFixture.value(QStringLiteral("protocolVersion")).toInt() == 1,
                QStringLiteral("protocol contract fixture should pin protocol version 1")) && ok;
    ok = expectUnique(expectedCommands, QStringLiteral("protocol contract commands")) && ok;
    ok = expectUnique(expectedEvents, QStringLiteral("protocol contract events")) && ok;
    ok = expectSameSet(documentedCommands, expectedCommands, QStringLiteral("documented commands")) && ok;
    ok = expectSameSet(documentedEvents, expectedEvents, QStringLiteral("documented events")) && ok;

    return ok ? 0 : 1;
}

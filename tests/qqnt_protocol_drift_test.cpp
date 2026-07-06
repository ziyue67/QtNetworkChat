#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
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

bool readTextFile(const QString& path, QString* text, const QString& label) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "%s should be readable: %s\n", label.toUtf8().constData(), qPrintable(file.errorString()));
        return false;
    }

    *text = QString::fromUtf8(file.readAll());
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

QMap<QString, QString> jsonStringObject(const QJsonObject& object, const QString& key) {
    QMap<QString, QString> values;
    const QJsonObject nestedObject = object.value(key).toObject();
    for (auto it = nestedObject.constBegin(); it != nestedObject.constEnd(); ++it) {
        const QString text = it.value().toString().trimmed();
        if (!it.key().trimmed().isEmpty() && !text.isEmpty()) {
            values.insert(it.key().trimmed(), text);
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

bool expectSameMap(const QMap<QString, QString>& actual, const QMap<QString, QString>& expected, const QString& label) {
    bool ok = true;
    ok = expectSameSet(actual.keys(), expected.keys(), label + QStringLiteral(" keys")) && ok;
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        if (!actual.contains(it.key())) {
            continue;
        }
        ok = expect(actual.value(it.key()) == it.value(),
                    QStringLiteral("%1 mismatch for %2: expected '%3', got '%4'")
                        .arg(label, it.key(), it.value(), actual.value(it.key()))) && ok;
    }
    return ok;
}

bool expectContains(const QString& actual, const QString& needle, const QString& label) {
    return expect(actual.contains(needle),
                  QStringLiteral("%1 should contain '%2'").arg(label, needle));
}

QStringList splitMarkdownTableRow(const QString& line) {
    QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('|'))) {
        trimmed.remove(0, 1);
    }
    if (trimmed.endsWith(QLatin1Char('|'))) {
        trimmed.chop(1);
    }

    QStringList cells;
    for (const QString& cell : trimmed.split(QLatin1Char('|'))) {
        cells.append(cell.trimmed());
    }
    return cells;
}

QString untickCell(const QString& cell) {
    QString value = cell.trimmed();
    if (value.startsWith(QLatin1Char('`')) && value.endsWith(QLatin1Char('`')) && value.size() >= 2) {
        value = value.mid(1, value.size() - 2);
    }
    return value.trimmed();
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

QMap<QString, QString> extractMarkdownPayloads(const QString& markdown, const QString& heading, const QString& keyColumnName) {
    const int headingIndex = markdown.indexOf(heading);
    if (headingIndex < 0) {
        return {};
    }

    const int nextHeadingIndex = markdown.indexOf(QStringLiteral("\n## "), headingIndex + heading.size());
    const QString section = nextHeadingIndex < 0 ? markdown.mid(headingIndex) : markdown.mid(headingIndex, nextHeadingIndex - headingIndex);
    const QStringList lines = section.split(QLatin1Char('\n'));
    QMap<QString, QString> payloads;
    int keyColumn = -1;
    int payloadColumn = -1;
    bool inTable = false;

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!inTable) {
            if (line.startsWith(QLatin1Char('|'))
                && line.contains(keyColumnName)
                && line.contains(QStringLiteral("payload"))) {
                const QStringList headers = splitMarkdownTableRow(line);
                for (int index = 0; index < headers.size(); ++index) {
                    const QString header = untickCell(headers.at(index));
                    if (header == keyColumnName) {
                        keyColumn = index;
                    } else if (header == QStringLiteral("payload")) {
                        payloadColumn = index;
                    }
                }
                inTable = keyColumn >= 0 && payloadColumn >= 0;
            }
            continue;
        }

        if (!line.startsWith(QLatin1Char('|'))) {
            if (!payloads.isEmpty()) {
                break;
            }
            continue;
        }
        if (line.contains(QStringLiteral("---"))) {
            continue;
        }

        const QStringList cells = splitMarkdownTableRow(line);
        if (keyColumn >= cells.size() || payloadColumn >= cells.size()) {
            continue;
        }
        const QString key = untickCell(cells.at(keyColumn));
        const QString payload = untickCell(cells.at(payloadColumn));
        if (!key.isEmpty() && !payload.isEmpty()) {
            payloads.insert(key, payload);
        }
    }

    return payloads;
}

QMap<QString, QString> extractMarkdownColumnByKey(const QString& markdown,
                                                  const QString& heading,
                                                  const QString& keyColumnName,
                                                  const QString& valueColumnName) {
    const int headingIndex = markdown.indexOf(heading);
    if (headingIndex < 0) {
        return {};
    }

    const int nextHeadingIndex = markdown.indexOf(QStringLiteral("\n## "), headingIndex + heading.size());
    const QString section = nextHeadingIndex < 0 ? markdown.mid(headingIndex) : markdown.mid(headingIndex, nextHeadingIndex - headingIndex);
    const QStringList lines = section.split(QLatin1Char('\n'));
    QMap<QString, QString> values;
    int keyColumn = -1;
    int valueColumn = -1;
    bool inTable = false;

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!inTable) {
            if (line.startsWith(QLatin1Char('|'))
                && line.contains(keyColumnName)
                && line.contains(valueColumnName)) {
                const QStringList headers = splitMarkdownTableRow(line);
                for (int index = 0; index < headers.size(); ++index) {
                    const QString header = untickCell(headers.at(index));
                    if (header == keyColumnName) {
                        keyColumn = index;
                    } else if (header == valueColumnName) {
                        valueColumn = index;
                    }
                }
                inTable = keyColumn >= 0 && valueColumn >= 0;
            }
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

        const QStringList cells = splitMarkdownTableRow(line);
        if (keyColumn >= cells.size() || valueColumn >= cells.size()) {
            continue;
        }
        const QString key = untickCell(cells.at(keyColumn));
        const QString value = cells.at(valueColumn).trimmed();
        if (!key.isEmpty() && !value.isEmpty()) {
            values.insert(key, value);
        }
    }

    return values;
}

QStringList extractRegexCaptures(const QString& source, const QRegularExpression& pattern) {
    QStringList values;
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(source);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const QString value = match.captured(1).trimmed();
        if (!value.isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

QStringList extractCppCommandOps(const QString& source) {
    const QRegularExpression routedOp(QStringLiteral("\\bop\\s*==\\s*QLatin1String\\(\\\"([^\\\"]+)\\\"\\)"));
    return extractRegexCaptures(source, routedOp);
}

QStringList extractCppEventNames(const QStringList& sources) {
    const QRegularExpression sentEvent(QStringLiteral("\\bsendEvent\\s*\\(\\s*QStringLiteral\\(\\\"([^\\\"]+)\\\"\\)"));
    QStringList values;
    for (const QString& source : sources) {
        values.append(extractRegexCaptures(source, sentEvent));
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

bool validateStrictCommandValidation(const QString& markdown,
                                     const QString& routerSource,
                                     const QString& rustCommandsSource) {
    bool ok = true;
    ok = expectContains(markdown,
                        QStringLiteral("命令包络是严格白名单"),
                        QStringLiteral("protocol doc command envelope validation")) && ok;
    ok = expectContains(markdown,
                        QStringLiteral("只允许 `op`、`reqId`、`payload`"),
                        QStringLiteral("protocol doc command envelope fields")) && ok;
    ok = expectContains(markdown,
                        QStringLiteral("命令 payload 也按命令表字段严格白名单"),
                        QStringLiteral("protocol doc command payload validation")) && ok;
    ok = expectContains(markdown,
                        QStringLiteral("返回 `invalid_payload`"),
                        QStringLiteral("protocol doc command payload error")) && ok;
    ok = expectContains(markdown,
                        QStringLiteral("details.targetFields"),
                        QStringLiteral("protocol doc target validation error details")) && ok;
    ok = expectContains(markdown,
                        QStringLiteral("[\"receiverId\", \"groupId\"]"),
                        QStringLiteral("protocol doc target validation field names")) && ok;

    ok = expectContains(routerSource,
                        QStringLiteral("requireOnlyFields"),
                        QStringLiteral("C++ router strict payload validator")) && ok;
    ok = expectContains(routerSource,
                        QStringLiteral("Command payload for %1 contains unsupported field: %2."),
                        QStringLiteral("C++ router unsupported payload field error")) && ok;
    ok = expectContains(routerSource,
                        QStringLiteral("targetFieldsErrorDetails"),
                        QStringLiteral("C++ router target validation error details helper")) && ok;
    ok = expectContains(routerSource,
                        QStringLiteral("targetFields"),
                        QStringLiteral("C++ router target validation field names")) && ok;

    ok = expectContains(rustCommandsSource,
                        QStringLiteral("fn require_command_envelope_fields"),
                        QStringLiteral("Rust command envelope validator")) && ok;
    ok = expectContains(rustCommandsSource,
                        QStringLiteral("[\"op\", \"reqId\", \"payload\"]"),
                        QStringLiteral("Rust command envelope whitelist")) && ok;
    ok = expectContains(rustCommandsSource,
                        QStringLiteral("fn require_command_payload_fields"),
                        QStringLiteral("Rust command payload validator")) && ok;
    ok = expectContains(rustCommandsSource,
                        QStringLiteral("Command payload for {op} contains unsupported field: {field}."),
                        QStringLiteral("Rust unsupported payload field error")) && ok;
    ok = expectContains(rustCommandsSource,
                        QStringLiteral("fn validate_target_error_details"),
                        QStringLiteral("Rust target validation error details guard")) && ok;
    ok = expectContains(rustCommandsSource,
                        QStringLiteral("details.targetFields"),
                        QStringLiteral("Rust target validation error details message")) && ok;
    return ok;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 7) {
        std::fprintf(stderr, "usage: %s <ready fixture> <protocol doc> <protocol contract fixture> <router source> <bridge source> <rust command source> [implementation plan]\n", argv[0]);
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
    QString routerSource;
    QString bridgeSource;
    QString rustCommandsSource;
    QString implementationPlan;
    ok = readTextFile(arguments.at(4), &routerSource, QStringLiteral("router source")) && ok;
    ok = readTextFile(arguments.at(5), &bridgeSource, QStringLiteral("bridge source")) && ok;
    ok = readTextFile(arguments.at(6), &rustCommandsSource, QStringLiteral("rust command source")) && ok;
    if (arguments.size() >= 8) {
        ok = readTextFile(arguments.at(7), &implementationPlan, QStringLiteral("implementation plan")) && ok;
    }
    if (!ok) {
        return 1;
    }

    const QStringList documentedCommands = extractMarkdownTableKeys(markdown, QStringLiteral("## 5. 命令表"), QStringLiteral("`op`"));
    const QStringList documentedEvents = extractMarkdownTableKeys(markdown, QStringLiteral("## 6. 主动事件表"), QStringLiteral("`event`"));
    const QMap<QString, QString> documentedCommandPayloads =
        extractMarkdownPayloads(markdown, QStringLiteral("## 5. 命令表"), QStringLiteral("op"));
    const QMap<QString, QString> documentedAckPayloads =
        extractMarkdownPayloads(markdown, QStringLiteral("### 成功 ack payload 表"), QStringLiteral("op"));
    const QMap<QString, QString> documentedEventPayloads =
        extractMarkdownPayloads(markdown, QStringLiteral("## 6. 主动事件表"), QStringLiteral("event"));
    const QMap<QString, QString> documentedCommandDescriptions =
        extractMarkdownColumnByKey(markdown, QStringLiteral("## 5. 命令表"), QStringLiteral("op"), QStringLiteral("说明"));
    const QMap<QString, QString> documentedEventDescriptions =
        extractMarkdownColumnByKey(markdown, QStringLiteral("## 6. 主动事件表"), QStringLiteral("event"), QStringLiteral("说明"));
    const QStringList expectedCommands = jsonStringArray(contractFixture, QStringLiteral("commands"));
    const QStringList expectedEvents = jsonStringArray(contractFixture, QStringLiteral("events"));
    const QMap<QString, QString> expectedCommandPayloads = jsonStringObject(contractFixture, QStringLiteral("commandPayloads"));
    const QMap<QString, QString> expectedAckPayloads = jsonStringObject(contractFixture, QStringLiteral("ackPayloads"));
    const QMap<QString, QString> expectedEventPayloads = jsonStringObject(contractFixture, QStringLiteral("eventPayloads"));
    const QStringList routedCppCommands = extractCppCommandOps(routerSource);
    const QStringList emittedCppEvents = extractCppEventNames({routerSource, bridgeSource});
    const QMap<QString, QString> implementationCommandPayloads = implementationPlan.isEmpty()
        ? QMap<QString, QString>()
        : extractMarkdownPayloads(implementationPlan, QStringLiteral("### 5.4 命令路由表 V1"), QStringLiteral("op"));
    const QMap<QString, QString> implementationEventPayloads = implementationPlan.isEmpty()
        ? QMap<QString, QString>()
        : extractMarkdownPayloads(implementationPlan, QStringLiteral("### 5.5 主动事件表 V1"), QStringLiteral("event"));

    ok = validateReadyFixture(readyFixture) && ok;
    ok = validateStrictCommandValidation(markdown, routerSource, rustCommandsSource) && ok;
    ok = expect(contractFixture.value(QStringLiteral("protocolVersion")).toInt() == 1,
                QStringLiteral("protocol contract fixture should pin protocol version 1")) && ok;
    ok = expectUnique(expectedCommands, QStringLiteral("protocol contract commands")) && ok;
    ok = expectUnique(expectedEvents, QStringLiteral("protocol contract events")) && ok;
    ok = expectSameSet(expectedCommandPayloads.keys(), expectedCommands, QStringLiteral("protocol contract command payloads")) && ok;
    ok = expectSameSet(expectedAckPayloads.keys(), expectedCommands, QStringLiteral("protocol contract ack payloads")) && ok;
    ok = expectSameSet(expectedEventPayloads.keys(), expectedEvents, QStringLiteral("protocol contract event payloads")) && ok;
    ok = expectSameSet(documentedCommands, expectedCommands, QStringLiteral("documented commands")) && ok;
    ok = expectSameMap(documentedCommandPayloads, expectedCommandPayloads, QStringLiteral("documented command payloads")) && ok;
    ok = expectSameMap(documentedAckPayloads, expectedAckPayloads, QStringLiteral("documented ack payloads")) && ok;
    ok = expectSameSet(documentedEvents, expectedEvents, QStringLiteral("documented events")) && ok;
    ok = expectSameMap(documentedEventPayloads, expectedEventPayloads, QStringLiteral("documented event payloads")) && ok;
    ok = expectSameSet(routedCppCommands, expectedCommands, QStringLiteral("C++ routed commands")) && ok;
    ok = expectSameSet(emittedCppEvents, expectedEvents, QStringLiteral("C++ emitted events")) && ok;
    ok = expectContains(documentedCommandDescriptions.value(QStringLiteral("query_resume")),
                        QStringLiteral("仅在提供 `filePath` 的恢复模式下有效"),
                        QStringLiteral("query_resume command description")) && ok;
    ok = expectContains(documentedCommandDescriptions.value(QStringLiteral("send_file")),
                        QStringLiteral("缺失返回 `missing_target`"),
                        QStringLiteral("send_file target validation command description")) && ok;
    ok = expectContains(documentedCommandDescriptions.value(QStringLiteral("send_image")),
                        QStringLiteral("同时提供返回 `ambiguous_target`"),
                        QStringLiteral("send_image target validation command description")) && ok;
    ok = expectContains(documentedCommandDescriptions.value(QStringLiteral("settings_sync")),
                        QStringLiteral("其他已提供字段仍需保持合法"),
                        QStringLiteral("settings_sync command description")) && ok;
    ok = expectContains(documentedEventDescriptions.value(QStringLiteral("file_progress")),
                        QStringLiteral("无符号整数字符串"),
                        QStringLiteral("file_progress event description")) && ok;
    ok = expectContains(documentedEventDescriptions.value(QStringLiteral("file_error")),
                        QStringLiteral("无符号整数字符串"),
                        QStringLiteral("file_error event description")) && ok;
    if (!implementationPlan.isEmpty()) {
        ok = expectSameMap(implementationCommandPayloads,
                           expectedCommandPayloads,
                           QStringLiteral("implementation plan command payloads")) && ok;
        ok = expectSameMap(implementationEventPayloads,
                           expectedEventPayloads,
                           QStringLiteral("implementation plan event payloads")) && ok;
    }
    ok = expect(!routerSource.contains(QStringLiteral("QStringLiteral(\"messageType\")")),
                QStringLiteral("C++ IPC router should use documented contentType instead of legacy messageType")) && ok;
    ok = expectContains(bridgeSource,
                        QStringLiteral("\"details\""),
                        QStringLiteral("C++ bridge error ack should emit optional details")) && ok;

    return ok ? 0 : 1;
}

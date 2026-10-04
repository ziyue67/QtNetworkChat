#include "qqnt_backend_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

namespace {
bool expect(bool condition, const QString& message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", qPrintable(message));
        return false;
    }
    return true;
}

bool call(const QString& op, const QJsonObject& payload, QJsonObject* response) {
    QString code;
    QString message;
    const bool ok = QQNTBackendService::handle(op, payload, response, &code, &message);
    if (!ok) {
        std::fprintf(stderr, "%s failed: %s %s\n", qPrintable(op), qPrintable(code), qPrintable(message));
    }
    return ok;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    bool ok = expect(temp.isValid(), QStringLiteral("temp directory should be valid"));
    qputenv("TMP", temp.path().toUtf8());
    qputenv("TEMP", temp.path().toUtf8());
    qputenv("TMPDIR", temp.path().toUtf8());

    QJsonObject message{{"id", "m1"}, {"messageId", "m1"}, {"sessionId", "s1"}, {"content", "hello"}};
    QJsonObject response;
    ok = call("favorite_local_message", {{"message", message}}, &response) && ok;
    ok = expect(response.value("saved").toBool(), QStringLiteral("favorite_local_message should save")) && ok;
    response = {};
    ok = call("toggle_local_message_favorite", {{"message", message}, {"favorite", false}}, &response) && ok;
    ok = expect(response.value("favorite").toBool(true) == false, QStringLiteral("toggle favorite should return final state")) && ok;
    response = {};
    ok = call("get_local_chat_actions", {}, &response) && ok;
    ok = expect(response.value("favoriteMessages").toArray().isEmpty(), QStringLiteral("favorite toggle false should remove favorite state")) && ok;
    response = {};
    ok = call("toggle_local_message_favorite", {{"message", message}, {"favorite", true}}, &response) && ok;
    response = {};
    ok = call("get_local_favorite_messages", {}, &response) && ok;
    ok = expect(response.value("messages").toArray().size() == 1, QStringLiteral("get_local_favorite_messages should return one message")) && ok;

    response = {};
    ok = call("clear_session_history", {{"sessionId", "s1"}}, &response) && ok;
    ok = expect(response.value("cleared").toBool(), QStringLiteral("clear_session_history should return cleared")) && ok;
    response = {};
    ok = call("delete_local_message", {{"sessionId", "s1"}, {"messageId", "m1"}}, &response) && ok;
    ok = expect(response.value("deleted").toBool(), QStringLiteral("delete_local_message should return deleted")) && ok;

    QTemporaryDir files;
    QFile png(files.filePath("avatar.png"));
    ok = expect(png.open(QIODevice::WriteOnly), QStringLiteral("png source should open")) && ok;
    png.write(QByteArray::fromHex("89504e470d0a1a0a0000000d49484452"));
    png.close();
    response = {};
    ok = call("read_image_base64", {{"filePath", png.fileName()}}, &response) && ok;
    ok = expect(response.value("dataUrl").toString().startsWith("data:image/png;base64,"), QStringLiteral("read_image_base64 should emit data URL")) && ok;

    QFile source(files.filePath("source.txt"));
    ok = expect(source.open(QIODevice::WriteOnly), QStringLiteral("source file should open")) && ok;
    source.write("hello");
    source.close();
    response = {};
    ok = call("save_file_to_directory", {{"sourcePath", source.fileName()}, {"directoryPath", files.path()}, {"fileName", "copy.txt"}}, &response) && ok;
    ok = expect(QFileInfo::exists(response.value("filePath").toString()), QStringLiteral("save_file_to_directory should create copied file")) && ok;
    response = {};
    ok = call("save_base64_file_to_directory", {{"base64", QString("aGVsbG8=")}, {"directoryPath", files.path()}, {"fileName", "decoded.txt"}}, &response) && ok;
    ok = expect(QFileInfo::exists(response.value("filePath").toString()), QStringLiteral("save_base64_file_to_directory should create decoded file")) && ok;

    return ok ? 0 : 1;
}

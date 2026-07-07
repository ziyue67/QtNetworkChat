#include "qqnt_backend_service.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMimeDatabase>
#include <QPixmap>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>

namespace {
bool fail(QString* code, QString* message, const QString& c, const QString& m) {
    if (code) *code = c;
    if (message) *message = m;
    return false;
}

QString nowMs() {
    return QString::number(QDateTime::currentMSecsSinceEpoch());
}

QString safeComponent(const QString& value) {
    QString out;
    for (const QChar ch : value.trimmed()) {
        out += (ch.isLetterOrNumber() || ch == '-' || ch == '_' || ch == '.') ? ch : QLatin1Char('_');
    }
    return out.isEmpty() ? QStringLiteral("local-message") : out;
}

QDir localActionDir() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath(QStringLiteral("tauri-qqnt-local-actions")));
}

QDir screenshotDir() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath(QStringLiteral("tauri-qqnt-screenshots")));
}

QString stringField(const QJsonObject& object, const QString& key) {
    return object.value(key).toString().trimmed();
}

bool requireField(const QJsonObject& object, const QString& key, QString* out, QString* code, QString* message) {
    *out = stringField(object, key);
    if (out->isEmpty()) {
        return fail(code, message, QStringLiteral("missing_field"), QStringLiteral("%1 is required.").arg(key));
    }
    return true;
}

QString messageId(const QJsonObject& message) {
    for (const QString& key : {QStringLiteral("id"), QStringLiteral("messageId"), QStringLiteral("clientMessageId")}) {
        const QString value = stringField(message, key);
        if (!value.isEmpty()) return value;
    }
    return QStringLiteral("local-message");
}

bool persistAction(const QString& kind, const QJsonObject& messageObject, QJsonObject* response, QString* code, QString* message) {
    const QString idPart = messageId(messageObject);
    if (idPart.trimmed().isEmpty()) {
        return fail(code, message, QStringLiteral("missing_field"), QStringLiteral("message.id is required."));
    }
    QDir dir = localActionDir();
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        return fail(code, message, QStringLiteral("local_action_write_failed"), QStringLiteral("Unable to create local action directory."));
    }
    const QString savedAt = nowMs();
    const QString id = QStringLiteral("%1-%2-%3").arg(safeComponent(kind), safeComponent(idPart), savedAt);
    QJsonObject stored;
    stored[QStringLiteral("kind")] = kind;
    stored[QStringLiteral("id")] = id;
    stored[QStringLiteral("savedAt")] = savedAt;
    stored[QStringLiteral("message")] = messageObject;
    QFile file(dir.filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(code, message, QStringLiteral("local_action_write_failed"), QStringLiteral("Unable to write local action."));
    }
    file.write(QJsonDocument(stored).toJson(QJsonDocument::Indented));
    (*response)[QStringLiteral("saved")] = true;
    (*response)[QStringLiteral("id")] = id;
    return true;
}

QJsonObject messageAction(const QJsonObject& message, qint64 savedAt) {
    const QString sessionId = stringField(message, QStringLiteral("sessionId"));
    const QString id = messageId(message);
    if (sessionId.isEmpty() || id.isEmpty()) return {};
    QJsonObject action;
    action[QStringLiteral("sessionId")] = sessionId;
    action[QStringLiteral("messageId")] = id;
    action[QStringLiteral("savedAt")] = QString::number(savedAt);
    action[QStringLiteral("value")] = message;
    return action;
}

QString actionKey(const QJsonObject& action) {
    return action.value(QStringLiteral("sessionId")).toString() + QLatin1Char('|')
        + action.value(QStringLiteral("messageId")).toString();
}

void pushAction(QJsonArray* target, const QJsonObject& message, qint64 savedAt) {
    const QJsonObject action = messageAction(message, savedAt);
    if (!action.isEmpty()) target->append(action);
}

QString imageMime(const QString& path, const QByteArray& bytes) {
    const QString name = QMimeDatabase().mimeTypeForFileNameAndData(path, bytes).name();
    if (name.startsWith(QStringLiteral("image/"))) return name;
    if (bytes.startsWith("\x89PNG\r\n\x1A\n")) return QStringLiteral("image/png");
    if (bytes.startsWith("\xFF\xD8\xFF")) return QStringLiteral("image/jpeg");
    if (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a")) return QStringLiteral("image/gif");
    if (bytes.startsWith("BM")) return QStringLiteral("image/bmp");
    if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") return QStringLiteral("image/webp");
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "svg") return QStringLiteral("image/svg+xml");
    if (suffix == "avif") return QStringLiteral("image/avif");
    if (suffix == "apng") return QStringLiteral("image/apng");
    return {};
}

QString safeLeaf(QString name) {
    name = QFileInfo(name.trimmed()).fileName();
    if (name.isEmpty() || name == "." || name == "..") name = QStringLiteral("download.bin");
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return name;
}

QString uniquePath(const QDir& dir, const QString& leaf) {
    QFileInfo info(leaf);
    const QString base = info.completeBaseName().isEmpty() ? QStringLiteral("file") : info.completeBaseName();
    const QString suffix = info.suffix();
    QString candidate = dir.filePath(leaf);
    for (int i = 1; QFileInfo::exists(candidate); ++i) {
        candidate = dir.filePath(suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(base).arg(i)
            : QStringLiteral("%1 (%2).%3").arg(base).arg(i).arg(suffix));
    }
    return candidate;
}

QMap<QString, QImage>& captureCache() {
    static QMap<QString, QImage> cache;
    return cache;
}

bool capture(QImage* image, QString* code, QString* message) {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return fail(code, message, QStringLiteral("screenshot_failed"), QStringLiteral("No display is available for screenshot capture."));
    *image = screen->grabWindow(0).toImage();
    if (image->isNull()) return fail(code, message, QStringLiteral("screenshot_failed"), QStringLiteral("Screenshot capture returned an empty image."));
    return true;
}

bool saveScreenshot(const QImage& image, const QString& prefix, QJsonObject* response, QString* code, QString* message, const QString& captureId = {}) {
    if (image.isNull()) return fail(code, message, QStringLiteral("screenshot_failed"), QStringLiteral("No display is available for screenshot capture."));
    QDir dir = screenshotDir();
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        return fail(code, message, QStringLiteral("screenshot_save_failed"), QStringLiteral("Unable to create screenshot directory."));
    }
    const QString fileName = QStringLiteral("%1-%2.png").arg(prefix, nowMs());
    const QString path = dir.filePath(fileName);
    if (!image.save(path, "PNG")) return fail(code, message, QStringLiteral("screenshot_save_failed"), QStringLiteral("Unable to save screenshot image."));
    (*response)[QStringLiteral("filePath")] = QDir::toNativeSeparators(path);
    (*response)[QStringLiteral("fileName")] = fileName;
    if (!captureId.isEmpty()) (*response)[QStringLiteral("captureId")] = captureId;
    (*response)[QStringLiteral("x")] = 0;
    (*response)[QStringLiteral("y")] = 0;
    (*response)[QStringLiteral("width")] = image.width();
    (*response)[QStringLiteral("height")] = image.height();
    return true;
}

QJsonObject rectJson(const QRect& rect) {
    return {{QStringLiteral("x"), rect.x()},
            {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()}};
}
}

QStringList QQNTBackendService::commands() {
    return {
        "clear_session_history", "delete_local_message", "favorite_local_message",
        "toggle_local_message_favorite", "get_local_favorite_messages", "add_local_emoji",
        "update_local_emoji", "multi_select_local_message", "toggle_multi_select_local_message",
        "quote_local_message", "set_essence_local_message", "recall_local_message",
        "forward_local_message", "view_local_profile", "add_local_friend", "report_local_user",
        "block_local_user", "edit_local_group_nickname", "get_local_chat_actions",
        "read_image_base64", "save_file_to_directory", "save_base64_file_to_directory",
        "get_screenshot_monitor_info", "get_screenshot_virtual_screen_info", "capture_screenshot",
        "capture_screenshot_shared_buffer", "crop_screenshot", "release_screenshot_capture",
        "set_screenshot_window_exclude_from_capture", "prepare_screenshot_window",
        "hide_main_window", "restore_main_window"
    };
}

bool QQNTBackendService::isCommand(const QString& op) {
    return commands().contains(op);
}

bool QQNTBackendService::handle(const QString& op,
                                const QJsonObject& payload,
                                QJsonObject* response,
                                QString* errorCode,
                                QString* errorMessage) {
    (*response)[QStringLiteral("accepted")] = true;
    if (op == "clear_session_history") {
        QString sessionId;
        if (!requireField(payload, "sessionId", &sessionId, errorCode, errorMessage)) return false;
        QJsonObject msg{{"id", sessionId}, {"sessionId", sessionId}};
        if (!persistAction("clear_session", msg, response, errorCode, errorMessage)) return false;
        response->remove("saved");
        (*response)["cleared"] = true;
        (*response)["sessionId"] = sessionId;
        return true;
    }
    if (op == "delete_local_message") {
        QString sessionId, msgId;
        if (!requireField(payload, "sessionId", &sessionId, errorCode, errorMessage)
            || !requireField(payload, "messageId", &msgId, errorCode, errorMessage)) return false;
        QJsonObject msg{{"id", msgId}, {"messageId", msgId}, {"sessionId", sessionId}};
        if (!persistAction("delete_message", msg, response, errorCode, errorMessage)) return false;
        response->remove("saved");
        (*response)["deleted"] = true;
        (*response)["sessionId"] = sessionId;
        (*response)["messageId"] = msgId;
        return true;
    }

    QJsonValue messageValue = payload.value("message");
    if (messageValue.isUndefined()) messageValue = payload;
    if ((op.endsWith("_local_message") || op == "add_local_emoji") && !messageValue.isObject()) {
        return fail(errorCode, errorMessage, "invalid_message", "message must be an object.");
    }
    const QJsonObject messageObject = messageValue.toObject();
    if (op == "favorite_local_message") return persistAction("favorite", messageObject, response, errorCode, errorMessage);
    if (op == "add_local_emoji") return persistAction("emoji", messageObject, response, errorCode, errorMessage);
    if (op == "multi_select_local_message") return persistAction("multi_select", messageObject, response, errorCode, errorMessage);
    if (op == "quote_local_message") return persistAction("quote", messageObject, response, errorCode, errorMessage);
    if (op == "set_essence_local_message") return persistAction("essence", messageObject, response, errorCode, errorMessage);
    if (op == "recall_local_message") return persistAction("recall", messageObject, response, errorCode, errorMessage);

    if (op == "toggle_local_message_favorite" || op == "toggle_multi_select_local_message" || op == "update_local_emoji") {
        QJsonObject msg = messageObject;
        if (op == "toggle_local_message_favorite") {
            const bool favorite = payload.value("favorite").toBool(false);
            msg["favorite"] = favorite;
            if (!persistAction("favorite_toggle", msg, response, errorCode, errorMessage)) return false;
            (*response)["favorite"] = favorite;
            return true;
        }
        if (op == "toggle_multi_select_local_message") {
            const bool selected = payload.value("selected").toBool(false);
            msg["selected"] = selected;
            if (!persistAction("multi_select_toggle", msg, response, errorCode, errorMessage)) return false;
            (*response)["selected"] = selected;
            return true;
        }
        const bool emoji = payload.value("emoji").toBool(false);
        msg["emoji"] = emoji;
        msg["pin"] = payload.value("pin").toBool(false);
        if (payload.contains("remark")) msg["emojiRemark"] = payload.value("remark").toString().trimmed();
        if (!persistAction("emoji_update", msg, response, errorCode, errorMessage)) return false;
        (*response)["emoji"] = emoji;
        return true;
    }

    if (op == "forward_local_message") {
        const QJsonObject target = payload.value("target").toObject();
        if (stringField(target, "id").isEmpty()) return fail(errorCode, errorMessage, "missing_field", "target.id is required.");
        QJsonObject msg{{"id", messageId(messageObject)}, {"message", messageObject}, {"target", target}, {"note", payload.value("note").toString()}};
        return persistAction("forward", msg, response, errorCode, errorMessage);
    }

    if (op == "view_local_profile" || op == "add_local_friend" || op == "report_local_user"
        || op == "block_local_user" || op == "edit_local_group_nickname") {
        const QJsonObject member = payload.value("member").isObject() ? payload.value("member").toObject() : payload;
        const QString memberId = stringField(member, "id");
        if (memberId.isEmpty()) return fail(errorCode, errorMessage, "missing_field", "member.id is required.");
        QString kind = op == "view_local_profile" ? "view_profile"
            : op == "add_local_friend" ? "add_friend"
            : op == "report_local_user" ? "report_user"
            : op == "block_local_user" ? "block_user" : "edit_group_nickname";
        QJsonObject msg{{"id", memberId}, {"member", member}};
        const QString sessionId = stringField(payload, "sessionId");
        if (!sessionId.isEmpty()) msg["sessionId"] = sessionId;
        if (op == "edit_local_group_nickname") {
            const QString nickname = stringField(payload, "nickname");
            if (nickname.isEmpty()) return fail(errorCode, errorMessage, "missing_field", "nickname is required.");
            msg["extra"] = QJsonObject{{"nickname", nickname}};
        }
        return persistAction(kind, msg, response, errorCode, errorMessage);
    }

    if (op == "get_local_chat_actions" || op == "get_local_favorite_messages") {
        QJsonArray cleared, deleted, quote, essence, recalled, members;
        QMap<QString, QJsonObject> favorites, emojis, selected;
        QMap<QString, qint64> favoriteTimes, emojiTimes, selectedTimes;
        const QFileInfoList entries = localActionDir().exists()
            ? localActionDir().entryInfoList({"*.json"}, QDir::Files, QDir::Name)
            : QFileInfoList();
        for (const QFileInfo& info : entries) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isObject()) continue;
            const QJsonObject stored = doc.object();
            const QString kind = stored.value("kind").toString();
            const qint64 savedAt = stored.value("savedAt").toVariant().toLongLong();
            const QJsonObject msg = stored.value("message").toObject();
            if (kind == "clear_session") {
                const QString sessionId = stringField(msg, "sessionId");
                if (!sessionId.isEmpty()) cleared.append(QJsonObject{{"sessionId", sessionId}, {"clearedAt", QString::number(savedAt)}});
            } else if (kind == "delete_message") {
                const QString sessionId = stringField(msg, "sessionId");
                const QString msgId = stringField(msg, "messageId");
                if (!sessionId.isEmpty() && !msgId.isEmpty()) deleted.append(QJsonObject{{"sessionId", sessionId}, {"messageId", msgId}});
            } else if (kind == "favorite" || kind == "favorite_toggle" || kind == "emoji" || kind == "emoji_update" || kind == "multi_select" || kind == "multi_select_toggle") {
                QJsonObject action = messageAction(msg, savedAt);
                if (action.isEmpty()) continue;
                const QString key = actionKey(action);
                QMap<QString, QJsonObject>* states = kind.startsWith("favorite") ? &favorites : (kind.startsWith("emoji") ? &emojis : &selected);
                QMap<QString, qint64>* times = kind.startsWith("favorite") ? &favoriteTimes : (kind.startsWith("emoji") ? &emojiTimes : &selectedTimes);
                if (savedAt >= times->value(key, 0)) {
                    (*times)[key] = savedAt;
                    const bool remove = (kind == "favorite_toggle" && !msg.value("favorite").toBool())
                        || (kind == "emoji_update" && !msg.value("emoji").toBool())
                        || (kind == "multi_select_toggle" && !msg.value("selected").toBool());
                    if (remove) states->remove(key); else (*states)[key] = action;
                }
            } else if (kind == "quote") pushAction(&quote, msg, savedAt);
            else if (kind == "essence") pushAction(&essence, msg, savedAt);
            else if (kind == "recall") pushAction(&recalled, msg, savedAt);
            else if (kind == "view_profile" || kind == "add_friend" || kind == "report_user" || kind == "block_user" || kind == "edit_group_nickname") {
                const QJsonObject member = msg.value("member").toObject();
                const QString memberId = stringField(member, "id");
                if (!memberId.isEmpty()) {
                    QJsonObject a{{"kind", kind}, {"memberId", memberId}, {"savedAt", QString::number(savedAt)}, {"value", msg}};
                    const QString sessionId = stringField(msg, "sessionId");
                    if (!sessionId.isEmpty()) a["sessionId"] = sessionId;
                    members.append(a);
                }
            }
        }
        QJsonArray favoriteArray, emojiArray, selectedArray;
        for (const QJsonObject& action : favorites) favoriteArray.append(action);
        for (const QJsonObject& action : emojis) emojiArray.append(action);
        for (const QJsonObject& action : selected) selectedArray.append(action);
        if (op == "get_local_favorite_messages") {
            QJsonArray messages;
            for (const QJsonValue& value : favoriteArray) {
                QJsonObject msg = value.toObject().value("value").toObject();
                msg["localFavorite"] = true;
                msg.remove("favorite");
                messages.append(msg);
            }
            (*response)["messages"] = messages;
            return true;
        }
        (*response)["clearedSessions"] = cleared;
        (*response)["deletedMessages"] = deleted;
        (*response)["favoriteMessages"] = favoriteArray;
        (*response)["emojiMessages"] = emojiArray;
        (*response)["selectedMessages"] = selectedArray;
        (*response)["quoteMessages"] = quote;
        (*response)["essenceMessages"] = essence;
        (*response)["recalledMessages"] = recalled;
        (*response)["memberActions"] = members;
        return true;
    }

    if (op == "read_image_base64") {
        QString filePath;
        if (!requireField(payload, "filePath", &filePath, errorCode, errorMessage)) return false;
        QFile file(filePath);
        if (!QFileInfo(filePath).isFile() || !file.open(QIODevice::ReadOnly)) return fail(errorCode, errorMessage, "invalid_file_path", "Selected avatar path is not a readable image file.");
        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) return fail(errorCode, errorMessage, "empty_image_file", "Selected image is empty.");
        const QString mime = imageMime(filePath, bytes);
        if (mime.isEmpty()) return fail(errorCode, errorMessage, "unsupported_image_type", "Avatar image must be a supported image type.");
        const QString b64 = QString::fromLatin1(bytes.toBase64());
        (*response)["base64"] = b64;
        (*response)["dataUrl"] = QStringLiteral("data:%1;base64,%2").arg(mime, b64);
        return true;
    }

    if (op == "save_file_to_directory" || op == "save_base64_file_to_directory") {
        QString dirPath;
        if (!requireField(payload, "directoryPath", &dirPath, errorCode, errorMessage)) return false;
        QDir dir(dirPath);
        if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return fail(errorCode, errorMessage, "save_file_failed", "Unable to create destination directory.");
        QString fileName = stringField(payload, "fileName");
        QString destination;
        if (op == "save_file_to_directory") {
            QString sourcePath;
            if (!requireField(payload, "sourcePath", &sourcePath, errorCode, errorMessage)) return false;
            QFileInfo source(sourcePath);
            if (!source.isFile()) return fail(errorCode, errorMessage, "invalid_file_path", "Source path is not a readable file.");
            if (fileName.isEmpty()) fileName = source.fileName();
            destination = uniquePath(dir, safeLeaf(fileName));
            if (!QFile::copy(source.absoluteFilePath(), destination)) return fail(errorCode, errorMessage, "save_file_failed", "Unable to copy selected file.");
        } else {
            if (fileName.isEmpty()) return fail(errorCode, errorMessage, "missing_field", "fileName is required.");
            const QByteArray data = QByteArray::fromBase64(payload.value("base64").toString().toLatin1());
            if (data.isEmpty()) return fail(errorCode, errorMessage, "invalid_base64", "base64 payload is empty or invalid.");
            destination = uniquePath(dir, safeLeaf(fileName));
            QFile out(destination);
            if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) return fail(errorCode, errorMessage, "save_file_failed", "Unable to write decoded file.");
        }
        (*response)["filePath"] = QDir::toNativeSeparators(destination);
        (*response)["fileName"] = QFileInfo(destination).fileName();
        return true;
    }

    if (op == "get_screenshot_monitor_info" || op == "get_screenshot_virtual_screen_info") {
        const QList<QScreen*> screens = QGuiApplication::screens();
        if (screens.isEmpty()) return fail(errorCode, errorMessage, "screenshot_failed", "No display is available for screenshot capture.");
        QRect rect = op == "get_screenshot_monitor_info" ? QGuiApplication::primaryScreen()->geometry() : QRect();
        if (op == "get_screenshot_virtual_screen_info") {
            for (QScreen* screen : screens) rect = rect.united(screen->geometry());
        }
        *response = rectJson(rect);
        return true;
    }
    if (op == "capture_screenshot" || op == "capture_screenshot_shared_buffer") {
        QImage image;
        if (!capture(&image, errorCode, errorMessage)) return false;
        QString captureId;
        if (op == "capture_screenshot_shared_buffer") {
            captureId = QStringLiteral("screenshot-%1").arg(nowMs());
            captureCache().insert(captureId, image);
        }
        return saveScreenshot(image, "screenshot", response, errorCode, errorMessage, captureId);
    }
    if (op == "crop_screenshot") {
        const int x = payload.value("x").toInt(-1), y = payload.value("y").toInt(-1);
        const int w = payload.value("width").toInt(0), h = payload.value("height").toInt(0);
        if (x < 0 || y < 0 || w <= 0 || h <= 0) return fail(errorCode, errorMessage, "invalid_screenshot_selection", "Screenshot selection is empty.");
        QImage source;
        const QString captureId = stringField(payload, "captureId");
        if (!captureId.isEmpty() && captureCache().contains(captureId)) source = captureCache().value(captureId);
        if (source.isNull()) {
            const QString sourcePath = stringField(payload, "sourcePath");
            if (sourcePath.isEmpty()) return fail(errorCode, errorMessage, "invalid_screenshot_source", "Screenshot source cache or file path is required.");
            if (!source.load(sourcePath)) return fail(errorCode, errorMessage, "screenshot_crop_failed", "Unable to open screenshot image.");
        }
        if (x >= source.width() || y >= source.height()) return fail(errorCode, errorMessage, "invalid_screenshot_selection", "Screenshot selection starts outside the image.");
        return saveScreenshot(source.copy(x, y, qMin(w, source.width() - x), qMin(h, source.height() - y)), "screenshot-crop", response, errorCode, errorMessage);
    }
    if (op == "release_screenshot_capture") {
        const QString captureId = stringField(payload, "captureId");
        if (!captureId.isEmpty()) captureCache().remove(captureId);
        (*response)["released"] = true;
        return true;
    }
    if (op == "set_screenshot_window_exclude_from_capture" || op == "prepare_screenshot_window") {
        (*response)["applied"] = true;
        return true;
    }
    if (op == "hide_main_window") {
        (*response)["hidden"] = true;
        return true;
    }
    if (op == "restore_main_window") {
        (*response)["restored"] = true;
        return true;
    }
    return fail(errorCode, errorMessage, "unknown_op", "Unsupported local backend command.");
}

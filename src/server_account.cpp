#include "server.h"
#include "server_database.h"
#include "server_delivery_support.h"
#include "heartbeatmonitor.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QPointer>
#include <QTimer>

namespace {
using namespace ServerDatabase;
using namespace ServerDeliverySupport;

constexpr int kPasswordKdfIterations = 120000;
constexpr int kPasswordKdfSaltBytes = 16;
constexpr int kPasswordKdfOutputBytes = 32;

QString accountArchivedId(const QString& loginAccount, int generation) {
    return QStringLiteral("%1#g%2").arg(loginAccount, QString::number(qMax(1, generation)));
}

bool archiveAccountReferences(QSqlDatabase& db, const QString& activeAccount, const QString& archivedAccount) {
    if (activeAccount.isEmpty() || archivedAccount.isEmpty() || activeAccount == archivedAccount) {
        return false;
    }

    const QList<QPair<QString, QString>> updates = {
        {QStringLiteral("UPDATE user_sessions SET user_id = ? WHERE user_id = ?"), QStringLiteral("user_sessions.user_id")},
        {QStringLiteral("UPDATE messages SET sender_id = ? WHERE sender_id = ?"), QStringLiteral("messages.sender_id")},
        {QStringLiteral("UPDATE messages SET receiver_id = ? WHERE receiver_id = ?"), QStringLiteral("messages.receiver_id")},
        {QStringLiteral("UPDATE offline_messages SET receiver_id = ? WHERE receiver_id = ?"), QStringLiteral("offline_messages.receiver_id")},
        {QStringLiteral("UPDATE friend_events SET sender_id = ? WHERE sender_id = ?"), QStringLiteral("friend_events.sender_id")},
        {QStringLiteral("UPDATE friend_events SET receiver_id = ? WHERE receiver_id = ?"), QStringLiteral("friend_events.receiver_id")},
        {QStringLiteral("UPDATE friend_events SET query_account = ? WHERE query_account = ?"), QStringLiteral("friend_events.query_account")},
        {QStringLiteral("UPDATE server_friends SET user_id = ? WHERE user_id = ?"), QStringLiteral("server_friends.user_id")},
        {QStringLiteral("UPDATE server_friends SET friend_id = ? WHERE friend_id = ?"), QStringLiteral("server_friends.friend_id")},
        {QStringLiteral("UPDATE server_groups SET owner_id = ? WHERE owner_id = ?"), QStringLiteral("server_groups.owner_id")},
        {QStringLiteral("UPDATE server_group_members SET user_id = ? WHERE user_id = ?"), QStringLiteral("server_group_members.user_id")},
        {QStringLiteral("UPDATE server_group_removed_members SET user_id = ? WHERE user_id = ?"), QStringLiteral("server_group_removed_members.user_id")},
        {QStringLiteral("UPDATE server_group_removed_members SET removed_by = ? WHERE removed_by = ?"), QStringLiteral("server_group_removed_members.removed_by")},
        {QStringLiteral("UPDATE server_group_announcements SET author_id = ? WHERE author_id = ?"), QStringLiteral("server_group_announcements.author_id")},
        {QStringLiteral("UPDATE server_group_audit_events SET actor_id = ? WHERE actor_id = ?"), QStringLiteral("server_group_audit_events.actor_id")},
        {QStringLiteral("UPDATE server_group_audit_events SET target_user_id = ? WHERE target_user_id = ?"), QStringLiteral("server_group_audit_events.target_user_id")}
    };

    bool ok = true;
    for (const auto& update : updates) {
        QSqlQuery query(db);
        query.prepare(update.first);
        query.addBindValue(archivedAccount);
        query.addBindValue(activeAccount);
        if (!query.exec()) {
            ok = false;
            qWarning() << "Failed to archive account reference"
                       << update.second
                       << activeAccount
                       << archivedAccount
                       << query.lastError().text();
        }
    }
    return ok;
}

QByteArray pbkdf2Sha256(const QByteArray& password, const QByteArray& salt, int iterations, int outputBytes) {
    if (iterations <= 0 || outputBytes <= 0) {
        return QByteArray();
    }

    QByteArray derived;
    quint32 blockIndex = 1;
    while (derived.size() < outputBytes) {
        QByteArray counter;
        counter.append(char((blockIndex >> 24) & 0xff));
        counter.append(char((blockIndex >> 16) & 0xff));
        counter.append(char((blockIndex >> 8) & 0xff));
        counter.append(char(blockIndex & 0xff));

        QByteArray u = QMessageAuthenticationCode::hash(salt + counter, password, QCryptographicHash::Sha256);
        QByteArray block = u;
        for (int i = 1; i < iterations; ++i) {
            u = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha256);
            for (int j = 0; j < block.size(); ++j) {
                block[j] = char(uchar(block.at(j)) ^ uchar(u.at(j)));
            }
        }
        derived += block;
        ++blockIndex;
    }
    return derived.left(outputBytes);
}

QByteArray randomSalt(int bytes) {
    QByteArray salt;
    salt.reserve(bytes);
    while (salt.size() < bytes) {
        const quint64 value = QRandomGenerator::global()->generate64();
        for (int shift = 0; shift < 64 && salt.size() < bytes; shift += 8) {
            salt.append(char((value >> shift) & 0xff));
        }
    }
    return salt;
}

QString legacyPasswordHash(const QString& account, const QString& password) {
    return QString::fromLatin1(QCryptographicHash::hash((account + ":" + password).toUtf8(),
                                                        QCryptographicHash::Sha256).toHex());
}

QString normalizePasswordInput(QString password) {
    password.remove(QChar(0x200B));
    password.remove(QChar(0xFEFF));
    password = password.trimmed();
    for (int i = 0; i < password.size(); ++i) {
        const ushort code = password.at(i).unicode();
        if (code >= 0xFF01 && code <= 0xFF5E) {
            password[i] = QChar(code - 0xFEE0);
        }
    }
    return password;
}

QString makePasswordKdfHash(const QString& account, const QString& password, const QByteArray& salt = QByteArray()) {
    const QByteArray actualSalt = salt.isEmpty() ? randomSalt(kPasswordKdfSaltBytes) : salt;
    const QByteArray material = (account + ":" + password).toUtf8();
    const QByteArray hash = pbkdf2Sha256(material, actualSalt, kPasswordKdfIterations, kPasswordKdfOutputBytes);
    return QStringLiteral("kdf$pbkdf2-sha256$%1$%2$%3")
        .arg(kPasswordKdfIterations)
        .arg(QString::fromLatin1(actualSalt.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
        .arg(QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
}

bool verifyPasswordKdfHash(const QString& account, const QString& password, const QString& storedHash) {
    const QStringList parts = storedHash.split(QLatin1Char('$'));
    if (parts.size() != 5
        || parts.at(0) != QLatin1String("kdf")
        || parts.at(1) != QLatin1String("pbkdf2-sha256")) {
        return false;
    }

    bool ok = false;
    const int iterations = parts.at(2).toInt(&ok);
    if (!ok || iterations <= 0 || iterations > 1000000) {
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(parts.at(3).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray expected = QByteArray::fromBase64(parts.at(4).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (salt.isEmpty() || expected.size() != kPasswordKdfOutputBytes) {
        return false;
    }

    const QByteArray material = (account + ":" + password).toUtf8();
    return pbkdf2Sha256(material, salt, iterations, expected.size()) == expected;
}

bool isKdfPasswordHash(const QString& storedHash) {
    return storedHash.startsWith(QStringLiteral("kdf$pbkdf2-sha256$"));
}

bool verifyStoredPasswordHash(const QString& account,
                              const QString& password,
                              const QString& storedHash,
                              bool* needsUpgrade = nullptr) {
    if (needsUpgrade) {
        *needsUpgrade = false;
    }
    if (isKdfPasswordHash(storedHash)) {
        return verifyPasswordKdfHash(account, password, storedHash);
    }
    if (looksLikeSha256Hex(storedHash)) {
        const bool ok = storedHash.compare(legacyPasswordHash(account, password), Qt::CaseInsensitive) == 0;
        if (ok && needsUpgrade) {
            *needsUpgrade = true;
        }
        return ok;
    }
    return false;
}

} // namespace

void Server::handleLogin(const QJsonObject& obj, QTcpSocket* socket) {
    QString mode = obj["mode"].toString("login");
    QString account = obj["account"].toString().trimmed();
    QString password = normalizePasswordInput(obj["password"].toString());
    QString userName = obj["userName"].toString().trimmed();

    qInfo().noquote() << QStringLiteral("Login received: account=%1 mode=%2 passwordLength=%3")
                            .arg(account, mode)
                            .arg(password.size());

    if (mode != "register" && account.isEmpty()) account = userName;
    if (userName.isEmpty()) userName = account.isEmpty() ? "User" : account;

    finalizeExpiredAccountDeactivations();
    QJsonObject accounts = loadAccountsFromSqlite();
    if (mode == "register") {
        if (password.isEmpty()) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "密码不能为空";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        if (account.isEmpty()) {
            account = generateAccountId(accounts);
        }
        QString passwordHash = makePasswordKdfHash(account, password);
        if (accounts.contains(account)) {
            QJsonObject existingAccount = accounts[account].toObject();
            const QString status = existingAccount.value("accountStatus").toString("active");
            const QString storedHash = existingAccount.value("passwordHash").toString();
            bool unusedUpgrade = false;
            if (status == QLatin1String("deactivation_pending")
                && verifyStoredPasswordHash(account, password, storedHash, &unusedUpgrade)) {
                QString rejectReason;
                if (!cancelAccountDeactivation(account, &rejectReason)) {
                    QJsonObject response;
                    response["type"] = "login_failed";
                    response["reason"] = rejectReason.isEmpty() ? QStringLiteral("账号恢复失败") : rejectReason;
                    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
                    socket->write("\n");
                    socket->flush();
                    return;
                }
                existingAccount["accountStatus"] = "active";
                accounts[account] = existingAccount;
                userName = existingAccount.value("userName").toString(userName);
                passwordHash = storedHash;
            } else {
                QJsonObject response;
                response["type"] = "login_failed";
                response["reason"] = status == QLatin1String("deactivation_pending")
                    ? QStringLiteral("账号正在注销冷静期内，需使用原密码重新注册才能恢复")
                    : QStringLiteral("账号已存在");
                socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
                socket->write("\n");
                socket->flush();
                return;
            }
        } else {
            QJsonObject accountObj;
            accountObj["passwordHash"] = passwordHash;
            accountObj["userName"] = userName;
            accountObj["userId"] = account;
            accountObj["avatar"] = obj["avatar"].toString();
            accountObj["accountStatus"] = "active";
            if (!insertAccountToSqlite(account,
                                       passwordHash,
                                       userName,
                                       accountObj["avatar"].toString())) {
                QJsonObject response;
                response["type"] = "login_failed";
                response["reason"] = "注册失败：账号资料保存失败";
                socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
                socket->write("\n");
                socket->flush();
                return;
            }
            accounts[account] = accountObj;
        }
    } else if (mode == "reset_password") {
        if (account.isEmpty() || !accounts.contains(account)) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "账号不存在，无法重置本地密码";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        if (password.isEmpty()) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "新密码不能为空";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        QJsonObject accountObj = accounts[account].toObject();
        const QString passwordHash = makePasswordKdfHash(account, password);
        accountObj["passwordHash"] = passwordHash;
        accountObj["accountStatus"] = "active";
        accounts[account] = accountObj;
        if (!updateAccountPasswordHashInSqlite(account, passwordHash)) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "本地密码重置保存失败";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        userName = accountObj.value("userName").toString(userName);
        qInfo().noquote() << QStringLiteral("Local password reset completed: account=%1").arg(account);
    } else if (accounts.contains(account)) {
        QJsonObject accountObj = accounts[account].toObject();
        const QString status = accountObj.value("accountStatus").toString("active");
        if (status == QLatin1String("deactivation_pending") || status == QLatin1String("deactivated")) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = status == QLatin1String("deactivation_pending")
                ? QStringLiteral("账号正在注销冷静期内，请用同账号同密码重新注册来恢复")
                : QStringLiteral("账号已注销，请重新注册新版本账号");
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        QString storedHash = accountObj["passwordHash"].toString();
        if (storedHash.isEmpty()) {
            storedHash = legacyPasswordHash(account, accountObj["password"].toString());
            accountObj.remove("password");
            accountObj["passwordHash"] = storedHash;
            accounts[account] = accountObj;
            insertAccountToSqlite(account,
                                  storedHash,
                                  accountObj["userName"].toString(userName),
                                  accountObj["avatar"].toString());
        }
        bool needsPasswordHashUpgrade = false;
        bool passwordMatches = verifyStoredPasswordHash(account, password, storedHash, &needsPasswordHashUpgrade);
        qInfo().noquote() << QStringLiteral("Password verification: account=%1 hashFormat=%2 matched=%3")
                                .arg(account,
                                     isKdfPasswordHash(storedHash) ? QStringLiteral("pbkdf2") : QStringLiteral("legacy-or-invalid"),
                                     passwordMatches ? QStringLiteral("true") : QStringLiteral("false"));
        if (!passwordMatches) {
            // Do not log password text. These stable SHA-256 prefixes distinguish
            // a GUI input mismatch from a stale/incorrect account database row.
            const QString suppliedFingerprint = QString::fromLatin1(
                QCryptographicHash::hash((account + ":" + password).toUtf8(),
                                         QCryptographicHash::Sha256).toHex().left(12));
            qWarning().noquote() << QStringLiteral("Password verification mismatch: account=%1 suppliedFingerprint=%2 storedPrefix=%3")
                                        .arg(account, suppliedFingerprint, storedHash.left(24));
        }
        if (!passwordMatches) {
            QJsonObject response;
            response["type"] = "login_failed";
            response["reason"] = "密码错误";
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            return;
        }
        if (needsPasswordHashUpgrade) {
            const QString upgradedHash = makePasswordKdfHash(account, password);
            accountObj["passwordHash"] = upgradedHash;
            accounts[account] = accountObj;
            updateAccountPasswordHashInSqlite(account, upgradedHash);
        }
        userName = accountObj["userName"].toString(userName);
    } else if (!account.isEmpty()) {
        QJsonObject response;
        response["type"] = "login_failed";
        response["reason"] = "账号不存在，请先注册";
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
        return;
    }

    if (m_usedNames.contains(userName)) {
        userName += "_" + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000);
    }

    ChatUser user;
    user.id = account.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch()) : account;
    user.name = userName;
    user.avatar = accounts.value(user.id).toObject().value("avatar").toString();
    user.address = socket->peerAddress();
    user.port = socket->peerPort();
    user.isOnline = true;
    user.lastActive = QDateTime::currentDateTime();

    m_clients[socket] = user;
    m_userSockets[user.id] = socket;
    m_usedNames.insert(userName);
    recordUserSessionToSqlite(user, "login");
    ensurePublicGroupMembership(user.id, socket);
    refreshRedisPresence(user);
    m_heartbeatMonitor->registerClient(user.id);

    QJsonObject response;
    response["type"] = "login_success";
    response["userId"] = user.id;
    response["userName"] = user.name;
    response["account"] = account;
    response["registered"] = mode == "register";
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();

    sendFavoriteMessagesSnapshot(user.id, socket);

    refreshConnectedClientViews();
    emit userJoined(user.id, user.name);
    emit clientConnected(user.id);

    Message sysMsg;
    sysMsg.type = MessageType::System;
    sysMsg.content = userName + " 加入了聊天室";
    sysMsg.timestamp = QDateTime::currentDateTime();
    broadcastMessage(sysMsg, socket);

    QPointer<QTcpSocket> socketGuard(socket);
    const QString loggedInUserId = user.id;
    QTimer::singleShot(0, this, [this, socketGuard, loggedInUserId]() {
        if (!socketGuard || socketGuard->state() != QAbstractSocket::ConnectedState) return;
        const ChatUser* currentUser = findUserBySocket(socketGuard);
        if (!currentUser || currentUser->id != loggedInUserId) return;
        sendOfflineMessages(loggedInUserId, socketGuard);
    });

    qDebug() << "User logged in:" << user.name << "id:" << user.id;
}

void Server::handleAccountDeactivationRequest(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* user = findUserBySocket(socket);
    QJsonObject response;
    response["type"] = "account_deactivation_result";

    if (!user) {
        response["accepted"] = false;
        response["reason"] = "请先登录后再申请注销";
    } else {
        QString rejectReason;
        const bool ok = markAccountDeactivationPending(user->id, obj.value("reason").toString(), &rejectReason);
        response["accepted"] = ok;
        response["account"] = user->id;
        response["status"] = ok ? QStringLiteral("deactivation_pending") : QStringLiteral("active");
        response["coolingOffDays"] = 7;
        if (!ok) {
            response["reason"] = rejectReason.isEmpty() ? QStringLiteral("账号注销申请失败") : rejectReason;
        }
    }

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::handleAccountDeactivationCancel(const QJsonObject& obj, QTcpSocket* socket) {
    Q_UNUSED(obj)
    ChatUser* user = findUserBySocket(socket);
    QJsonObject response;
    response["type"] = "account_deactivation_cancelled";

    if (!user) {
        response["accepted"] = false;
        response["reason"] = "请先登录后再取消注销";
    } else {
        QString rejectReason;
        const bool ok = cancelAccountDeactivation(user->id, &rejectReason);
        response["accepted"] = ok;
        response["account"] = user->id;
        response["status"] = ok ? QStringLiteral("active") : QStringLiteral("unknown");
        if (!ok) {
            response["reason"] = rejectReason.isEmpty() ? QStringLiteral("取消注销失败") : rejectReason;
        }
    }

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
}

void Server::handleProfileUpdate(const QJsonObject& obj, QTcpSocket* socket) {
    ChatUser* user = findUserBySocket(socket);
    if (!user) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：请先登录"));
        return;
    }

    const QString avatarBase64 = obj.value("avatar").toString().trimmed();
    if (!ensureAccountDatabase()) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：账号存储不可用"));
        return;
    }

    const QJsonObject accounts = loadAccountsFromSqlite();
    const QJsonObject existing = accounts.value(user->id).toObject();
    const QString passwordHash = existing.value("passwordHash").toString();
    const QString userName = existing.value("userName").toString(user->name);
    if (passwordHash.isEmpty()) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：账号资料缺失"));
        return;
    }

    if (!insertAccountToSqlite(user->id, passwordHash, userName, avatarBase64)) {
        sendSystemNotice(socket, QStringLiteral("头像更新失败：保存到账号资料失败"));
        return;
    }

    user->avatar = avatarBase64;
    refreshConnectedClientViews();
}


QString Server::generateAccountId(const QJsonObject& accounts) const {
    // Allocate numeric account IDs sequentially starting from 1
    // so users get short, memorable QQ-style numbers.
    qint64 maxId = 0;
    for (auto it = accounts.begin(); it != accounts.end(); ++it) {
        bool ok = false;
        const qint64 id = it.key().toLongLong(&ok);
        if (ok && id > 0 && id > maxId) {
            maxId = id;
        }
    }
    return QString::number(maxId + 1);
}

QJsonObject Server::loadAccountsFromSqlite() const {
    ensureAccountDatabase();

    QJsonObject accounts;
    QString connectionName = "accounts_read_" + QString::number(reinterpret_cast<quintptr>(this));
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            if (query.exec("SELECT account, password_hash, user_name, COALESCE(avatar, ''), "
                           "COALESCE(account_status, 'active'), COALESCE(login_account, account), "
                           "COALESCE(account_generation, 1) "
                           "FROM accounts "
                           "WHERE COALESCE(account_status, 'active') IN ('active', 'deactivation_pending')")) {
                while (query.next()) {
                    QJsonObject accountObj;
                    accountObj["passwordHash"] = query.value(1).toString();
                    accountObj["userName"] = query.value(2).toString();
                    accountObj["avatar"] = query.value(3).toString();
                    accountObj["userId"] = query.value(0).toString();
                    accountObj["accountStatus"] = query.value(4).toString();
                    accountObj["loginAccount"] = query.value(5).toString();
                    accountObj["accountGeneration"] = query.value(6).toInt();
                    accounts[query.value(0).toString()] = accountObj;
                }
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);

    if (accounts.isEmpty()) {
        QJsonObject legacyAccounts = loadAccounts();
        for (auto it = legacyAccounts.begin(); it != legacyAccounts.end(); ++it) {
            QJsonObject accountObj = it.value().toObject();
            QString passwordHash = accountObj["passwordHash"].toString();
            if (passwordHash.isEmpty() && accountObj.contains("password")) {
                passwordHash = legacyPasswordHash(it.key(), accountObj["password"].toString());
            }
            QString userName = accountObj["userName"].toString(it.key());
            if (!passwordHash.isEmpty()
                && insertAccountToSqlite(it.key(),
                                         passwordHash,
                                         userName,
                                         accountObj["avatar"].toString())) {
                QJsonObject migratedObj;
                migratedObj["passwordHash"] = passwordHash;
                migratedObj["userName"] = userName;
                migratedObj["avatar"] = accountObj["avatar"].toString();
                migratedObj["userId"] = it.key();
                accounts[it.key()] = migratedObj;
            }
        }
    }

    return accounts;
}

bool Server::insertAccountToSqlite(const QString& account,
                                   const QString& passwordHash,
                                   const QString& userName,
                                   const QString& avatarBase64) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "accounts_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            const bool inTransaction = db.transaction();
            int nextGeneration = 1;
            QSqlQuery generationQuery(db);
            generationQuery.prepare("SELECT COALESCE(MAX(account_generation), 0) + 1 FROM accounts WHERE login_account = ? OR account = ?");
            generationQuery.addBindValue(account);
            generationQuery.addBindValue(account);
            if (generationQuery.exec() && generationQuery.next()) {
                nextGeneration = qMax(1, generationQuery.value(0).toInt());
            }

            QSqlQuery archiveQuery(db);
            archiveQuery.prepare("UPDATE accounts SET account = ?, updated_at = CURRENT_TIMESTAMP "
                                 "WHERE account = ? AND COALESCE(account_status, 'active') = 'deactivated'");
            const QString archivedAccount = accountArchivedId(account, qMax(1, nextGeneration - 1));
            archiveQuery.addBindValue(archivedAccount);
            archiveQuery.addBindValue(account);
            bool archiveOk = archiveQuery.exec();
            if (archiveOk && archiveQuery.numRowsAffected() > 0) {
                archiveOk = archiveAccountReferences(db, account, archivedAccount);
            }

            if (archiveOk) {
                QSqlQuery query(db);
                query.prepare(insertReplaceSql(
                    QStringLiteral("accounts"),
                    {QStringLiteral("account"),
                     QStringLiteral("account_uid"),
                     QStringLiteral("login_account"),
                     QStringLiteral("account_generation"),
                     QStringLiteral("account_status"),
                     QStringLiteral("password_hash"),
                     QStringLiteral("user_name"),
                     QStringLiteral("avatar"),
                     QStringLiteral("updated_at")},
                    {QStringLiteral("?"),
                     accountUidDefaultSql(),
                     QStringLiteral("?"),
                     QStringLiteral("?"),
                     QStringLiteral("'active'"),
                     QStringLiteral("?"),
                     QStringLiteral("?"),
                     QStringLiteral("?"),
                     QStringLiteral("CURRENT_TIMESTAMP")},
                    {QStringLiteral("account")},
                    {QStringLiteral("account_uid = accounts.account_uid"),
                     QStringLiteral("login_account = EXCLUDED.login_account"),
                     QStringLiteral("account_generation = EXCLUDED.account_generation"),
                     QStringLiteral("account_status = 'active'"),
                     QStringLiteral("deactivation_requested_at = NULL"),
                     QStringLiteral("deactivation_effective_at = NULL"),
                     QStringLiteral("deactivated_at = NULL"),
                     QStringLiteral("deactivation_reason = NULL"),
                     QStringLiteral("password_hash = EXCLUDED.password_hash"),
                     QStringLiteral("user_name = EXCLUDED.user_name"),
                     QStringLiteral("avatar = EXCLUDED.avatar"),
                     QStringLiteral("updated_at = EXCLUDED.updated_at")}));
                query.addBindValue(account);
                query.addBindValue(account);
                query.addBindValue(nextGeneration);
                query.addBindValue(passwordHash);
                query.addBindValue(userName);
                query.addBindValue(avatarBase64);
                ok = query.exec();
                if (!ok) {
                    qWarning() << "Failed to write account:" << query.lastError().text() << account;
                }
            } else {
                qWarning() << "Failed to archive deactivated account before write:" << archiveQuery.lastError().text() << account;
            }
            if (inTransaction) {
                if (ok) {
                    db.commit();
                } else {
                    db.rollback();
                }
            } else if (!ok) {
                qWarning() << "Account write transaction unavailable:" << db.lastError().text() << account;
            }
            db.close();
        } else {
            qWarning() << "Failed to open account write database:" << db.lastError().text() << account;
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::finalizeExpiredAccountDeactivations() const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "accounts_deactivation_finalize_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("UPDATE accounts SET account_status = 'deactivated', "
                          "deactivated_at = COALESCE(deactivated_at, CURRENT_TIMESTAMP), "
                          "updated_at = CURRENT_TIMESTAMP "
                          "WHERE account_status = 'deactivation_pending' "
                          "AND deactivation_effective_at <= CURRENT_TIMESTAMP");
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::markAccountDeactivationPending(const QString& account, const QString& reason, QString* rejectReason) const {
    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("账号不能为空");
        return false;
    }
    if (!ensureAccountDatabase()) {
        if (rejectReason) *rejectReason = QStringLiteral("账号数据库不可用");
        return false;
    }

    QString connectionName = "accounts_deactivation_request_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("UPDATE accounts SET account_status = 'deactivation_pending', "
                                         "deactivation_requested_at = CURRENT_TIMESTAMP, "
                                         "deactivation_effective_at = %1, "
                                         "deactivated_at = NULL, "
                                         "deactivation_reason = ?, "
                                         "updated_at = CURRENT_TIMESTAMP "
                                         "WHERE account = ? AND COALESCE(account_status, 'active') = 'active'")
                              .arg(currentTimestampPlusDaysSql(7)));
            query.addBindValue(reason.trimmed());
            query.addBindValue(normalizedAccount);
            ok = query.exec() && query.numRowsAffected() > 0;
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    if (!ok && rejectReason) *rejectReason = QStringLiteral("账号不存在或已经处于注销流程");
    return ok;
}

bool Server::cancelAccountDeactivation(const QString& account, QString* rejectReason) const {
    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty()) {
        if (rejectReason) *rejectReason = QStringLiteral("账号不能为空");
        return false;
    }
    if (!ensureAccountDatabase()) {
        if (rejectReason) *rejectReason = QStringLiteral("账号数据库不可用");
        return false;
    }

    QString connectionName = "accounts_deactivation_cancel_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("UPDATE accounts SET account_status = 'active', "
                          "deactivation_requested_at = NULL, "
                          "deactivation_effective_at = NULL, "
                          "deactivated_at = NULL, "
                          "deactivation_reason = NULL, "
                          "updated_at = CURRENT_TIMESTAMP "
                          "WHERE account = ? AND account_status = 'deactivation_pending'");
            query.addBindValue(normalizedAccount);
            ok = query.exec() && query.numRowsAffected() > 0;
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    if (!ok && rejectReason) *rejectReason = QStringLiteral("账号不在注销冷静期内");
    return ok;
}

bool Server::updateAccountPasswordHashInSqlite(const QString& account, const QString& passwordHash) const {
    if (account.isEmpty() || passwordHash.isEmpty() || !ensureAccountDatabase()) return false;

    QString connectionName = "accounts_password_update_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("UPDATE accounts SET password_hash = ?, updated_at = CURRENT_TIMESTAMP WHERE account = ?");
            query.addBindValue(passwordHash);
            query.addBindValue(account);
            ok = query.exec();
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

bool Server::recordUserSessionToSqlite(const ChatUser& user, const QString& eventName) const {
    if (!ensureAccountDatabase()) return false;

    QString connectionName = "sessions_write_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = openAccountDatabase(connectionName);
        if (openAccountDatabaseConnection(db, connectionName)) {
            QSqlQuery query(db);
            query.prepare("INSERT INTO user_sessions(user_id, user_name, event_name, peer_address, peer_port, created_at) "
                          "VALUES(?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
            query.addBindValue(user.id);
            query.addBindValue(user.name);
            query.addBindValue(eventName);
            query.addBindValue(user.address.toString());
            query.addBindValue(user.port);
            ok = query.exec();
            if (ok && eventName == "login" && !user.id.isEmpty()) {
                QSqlQuery accountQuery(db);
                accountQuery.prepare("UPDATE accounts SET "
                                     "user_name = ?, "
                                     "last_login_at = CURRENT_TIMESTAMP, "
                                     "last_login_address = ?, "
                                     "login_count = COALESCE(login_count, 0) + 1, "
                                     "updated_at = CURRENT_TIMESTAMP "
                                     "WHERE account = ?");
                accountQuery.addBindValue(user.name);
                accountQuery.addBindValue(user.address.toString());
                accountQuery.addBindValue(user.id);
                ok = accountQuery.exec();
            }
            db.close();
        }
    }
    releaseAccountDatabase(connectionName);
    return ok;
}

QJsonObject Server::loadAccounts() const {
    QFile file(accountsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return {};
    return doc.object();
}

void Server::saveAccounts(const QJsonObject& accounts) const {
    QFile file(accountsFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(accounts).toJson(QJsonDocument::Indented));
}

QString Server::accountsFilePath() const {
    QString dir = appDataDir();
    if (dir.isEmpty()) dir = ".";
    QDir().mkpath(dir);
    return dir + "/accounts.json";
}

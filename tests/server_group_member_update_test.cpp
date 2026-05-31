#include "client.h"
#include "message.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

#include <functional>

namespace {
QString testAppDataDir() {
    const QString overrideDir = QString::fromLocal8Bit(qgetenv("QTNETWORKCHAT_APPDATA_DIR")).trimmed();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

void drainEvents(int rounds = 5) {
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

void disconnectClient(Client& client) {
    client.disconnectFromServer();
    waitFor([&] {
        return !client.isConnected();
    }, 2000);
    drainEvents();
}

quint16 freeLocalPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

QJsonObject publicGroup(const QJsonArray& groups) {
    for (const QJsonValue& value : groups) {
        const QJsonObject group = value.toObject();
        if (group["groupId"].toString() == "public") return group;
    }
    return {};
}

bool publicGroupHasMember(const QJsonArray& groups, const QString& userId) {
    const QJsonArray members = publicGroup(groups)["members"].toArray();
    for (const QJsonValue& value : members) {
        if (value.toObject()["userId"].toString() == userId) return true;
    }
    return false;
}

QString publicGroupMemberRole(const QJsonArray& groups, const QString& userId) {
    const QJsonArray members = publicGroup(groups)["members"].toArray();
    for (const QJsonValue& value : members) {
        const QJsonObject member = value.toObject();
        if (member["userId"].toString() == userId) return member["role"].toString();
    }
    return {};
}

QString publicGroupAnnouncement(const QJsonArray& groups) {
    return publicGroup(groups)["announcement"].toString();
}

bool registerClient(Client& client,
                    const QString& account,
                    const QString& userName,
                    quint16 port) {
    client.setUserInfo(account, userName);
    client.setAccountInfo(account, "secret", true);
    if (!client.connectToServer("127.0.0.1", port)) return false;
    return client.waitForLoginResult(5000);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QtNetworkChatTests");
    QCoreApplication::setApplicationName("server_group_member_update_test");
    QStandardPaths::setTestModeEnabled(true);

    const QString appDataDir = testAppDataDir();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local test port should be available") && ok;
    if (!ok) return 1;

    {
        Server server;
        ok = expect(server.start(port), "server should start on the test port") && ok;
        if (!ok) return 1;

        Client owner;
        Client member;
        Client guest;
        QStringList ownerGroupMessages;
        QStringList ownerSystemMessages;
        QStringList memberSystemMessages;
        QStringList guestSystemMessages;
        QObject::connect(&owner, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::Text) {
                ownerGroupMessages << msg.content;
            } else if (msg.type == MessageType::System) {
                ownerSystemMessages << msg.content;
            }
        });
        QObject::connect(&member, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::System) {
                memberSystemMessages << msg.content;
            }
        });
        QObject::connect(&guest, &Client::newMessage, &app, [&](const Message& msg) {
            if (msg.type == MessageType::System) {
                guestSystemMessages << msg.content;
            }
        });

        const QString ownerId = "910001";
        const QString memberId = "910002";
        const QString guestId = "910003";

    ok = expect(registerClient(owner, ownerId, "Owner", port), "owner should register and log in") && ok;
    ok = expect(waitFor([&] {
        return publicGroupHasMember(owner.serverGroups(), ownerId)
            && publicGroupMemberRole(owner.serverGroups(), ownerId) == "owner";
    }), "owner should receive public group owner role") && ok;

    ok = expect(registerClient(member, memberId, "Member", port), "member should register and log in") && ok;
    ok = expect(waitFor([&] {
        return publicGroupHasMember(member.serverGroups(), ownerId)
            && publicGroupHasMember(member.serverGroups(), memberId)
            && publicGroupMemberRole(member.serverGroups(), memberId) == "member";
    }), "member should receive public group snapshot") && ok;

    const QString ownerAnnouncement = "Owner announcement protocol test";
    ok = expect(owner.sendServerGroupAnnouncementUpdate("public", ownerAnnouncement),
                "owner should submit public group announcement update") && ok;
    ok = expect(waitFor([&] {
        return publicGroupAnnouncement(member.serverGroups()) == ownerAnnouncement;
    }), "owner announcement should be synced to public group members") && ok;

    memberSystemMessages.clear();
    const QString rejectedAnnouncement = "Member announcement should be rejected";
    ok = expect(member.sendServerGroupAnnouncementUpdate("public", rejectedAnnouncement),
                "plain member announcement request should still be sent to server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : memberSystemMessages) {
            if (message.contains(QString::fromUtf8("只有群主或管理员"))) return true;
        }
        return false;
    }), "plain member announcement update should be rejected by server-side role check") && ok;
    ok = expect(publicGroupAnnouncement(member.serverGroups()) == ownerAnnouncement,
                "rejected member announcement should not change public group announcement") && ok;

    ownerSystemMessages.clear();
    ok = expect(owner.sendServerGroupMemberUpdate("public", ownerId, "remove"),
                "owner self-removal request should still be sent to server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : ownerSystemMessages) {
            if (message.contains(QString::fromUtf8("不能移出群主"))) return true;
        }
        return false;
    }), "owner should be rejected when trying to remove themselves from the public group") && ok;
    ok = expect(publicGroupHasMember(owner.serverGroups(), ownerId)
                    && publicGroupMemberRole(owner.serverGroups(), ownerId) == "owner",
                "owner should remain the public group owner after rejected self-removal") && ok;

    ok = expect(owner.sendServerGroupMemberUpdate("public", memberId, "remove"),
                "owner should submit member removal") && ok;
    ok = expect(waitFor([&] {
        return member.serverGroups().isEmpty();
    }), "removed member should receive an empty server group snapshot") && ok;

    const QString blockedBroadcast = "removed member broadcast should be blocked";
    memberSystemMessages.clear();
    ownerGroupMessages.clear();
    ok = expect(member.sendMessage(blockedBroadcast),
                "removed member public message request should still be sent to server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : memberSystemMessages) {
            if (message.contains(QString::fromUtf8("已不在该群组"))) return true;
        }
        return false;
    }), "removed member should receive public message rejection") && ok;
    ok = expect(!ownerGroupMessages.contains(blockedBroadcast),
                "removed member message should not be broadcast to public group") && ok;

    member.disconnectFromServer();
    ok = expect(waitFor([&] {
        return !member.isConnected();
    }), "removed member should disconnect before re-login check") && ok;
    member.setAccountInfo(memberId, "secret", false);
    ok = expect(member.connectToServer("127.0.0.1", port),
                "removed member should reconnect with existing account") && ok;
    ok = expect(member.waitForLoginResult(5000),
                "removed member should log in with existing account") && ok;
    ok = expect(waitFor([&] {
        return member.serverGroups().isEmpty();
    }), "removed member should not be auto-added to public group on re-login") && ok;

    ok = expect(owner.sendServerGroupMemberUpdate("public", memberId, "add"),
                "owner should submit member add") && ok;
    ok = expect(waitFor([&] {
        return publicGroupHasMember(member.serverGroups(), ownerId)
            && publicGroupHasMember(member.serverGroups(), memberId);
    }), "added member should receive restored public group snapshot") && ok;

    ownerSystemMessages.clear();
    ok = expect(owner.sendServerGroupMemberUpdate("public", memberId, "add"),
                "duplicate member add request should still be sent to server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : ownerSystemMessages) {
            if (message.contains(QString::fromUtf8("已经是群成员"))) return true;
        }
        return false;
    }), "duplicate member add should be rejected by server-side membership check") && ok;
    ok = expect(publicGroupHasMember(member.serverGroups(), memberId)
                    && publicGroupMemberRole(member.serverGroups(), memberId) == "member",
                "duplicate member add should keep the existing member role") && ok;

    const QString restoredBroadcast = "restored member broadcast should pass";
    ownerGroupMessages.clear();
    ok = expect(member.sendMessage(restoredBroadcast),
                "restored member should send public message") && ok;
    ok = expect(waitFor([&] {
        return ownerGroupMessages.contains(restoredBroadcast);
    }), "restored member message should be broadcast to public group") && ok;

    ok = expect(owner.sendServerGroupMemberUpdate("public", memberId, "promote_admin"),
                "owner should submit admin promotion") && ok;
    ok = expect(waitFor([&] {
        return publicGroupMemberRole(owner.serverGroups(), memberId) == "admin"
            && publicGroupMemberRole(member.serverGroups(), memberId) == "admin";
    }), "owner should be able to promote a public group member to admin") && ok;

    const QString adminAnnouncement = "Admin announcement protocol test";
    ok = expect(member.sendServerGroupAnnouncementUpdate("public", adminAnnouncement),
                "admin should submit public group announcement update") && ok;
    ok = expect(waitFor([&] {
        return publicGroupAnnouncement(owner.serverGroups()) == adminAnnouncement;
    }), "admin announcement should be accepted and synced") && ok;

    ok = expect(registerClient(guest, guestId, "Guest", port), "guest should register and log in") && ok;
    ok = expect(waitFor([&] {
        return publicGroupHasMember(guest.serverGroups(), ownerId)
            && publicGroupHasMember(guest.serverGroups(), memberId)
            && publicGroupHasMember(guest.serverGroups(), guestId);
    }), "guest should join public group before admin boundary checks") && ok;

    memberSystemMessages.clear();
    ok = expect(member.sendServerGroupMemberUpdate("public", guestId, "promote_admin"),
                "admin role change request should still be sent to server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : memberSystemMessages) {
            if (message.contains(QString::fromUtf8("只有群主可以设置管理员"))) return true;
        }
        return false;
    }), "admin should not be allowed to promote another admin") && ok;
    ok = expect(publicGroupMemberRole(guest.serverGroups(), guestId) == "member",
                "rejected admin promotion should keep guest as plain member") && ok;

    ok = expect(member.sendServerGroupMemberUpdate("public", guestId, "remove"),
                "admin should be able to remove a plain member") && ok;
    ok = expect(waitFor([&] {
        return guest.serverGroups().isEmpty();
    }), "guest removed by admin should receive an empty public group snapshot") && ok;

    const QString blockedFilePath = QDir(appDataDir).filePath("removed-member-file.txt");
    QFile blockedFile(blockedFilePath);
    ok = expect(blockedFile.open(QIODevice::WriteOnly), "test file for removed member should be writable") && ok;
    if (blockedFile.isOpen()) {
        blockedFile.write("removed member public file should be blocked");
        blockedFile.close();
    }
    guestSystemMessages.clear();
    ok = expect(!guest.sendFile(blockedFilePath),
                "removed member public file transfer should be rejected by server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : guestSystemMessages) {
            if (message.contains(QString::fromUtf8("已不在该群组"))) return true;
        }
        return false;
    }), "removed member public file rejection should be visible as a system notice") && ok;

    ok = expect(owner.sendServerGroupMemberUpdate("public", memberId, "demote_admin"),
                "owner should submit admin demotion") && ok;
    ok = expect(waitFor([&] {
        return publicGroupMemberRole(owner.serverGroups(), memberId) == "member"
            && publicGroupMemberRole(member.serverGroups(), memberId) == "member";
    }), "owner should be able to demote an admin back to member") && ok;

    memberSystemMessages.clear();
    ok = expect(member.sendServerGroupAnnouncementUpdate("public", "Demoted admin should be rejected"),
                "demoted admin announcement request should still be sent to server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : memberSystemMessages) {
            if (message.contains(QString::fromUtf8("只有群主或管理员"))) return true;
        }
        return false;
    }), "demoted admin should no longer be allowed to edit announcements") && ok;
    ok = expect(publicGroupAnnouncement(owner.serverGroups()) == adminAnnouncement,
                "rejected demoted-admin announcement should keep current announcement") && ok;

    memberSystemMessages.clear();
    ok = expect(member.sendServerGroupMemberUpdate("public", ownerId, "remove"),
                "plain member request should still be sent to server") && ok;
    ok = expect(waitFor([&] {
        for (const QString& message : memberSystemMessages) {
            if (message.contains(QString::fromUtf8("只有群主或管理员"))) return true;
        }
        return false;
    }), "plain member should be rejected by server-side role check") && ok;
    ok = expect(publicGroupHasMember(member.serverGroups(), ownerId),
                "owner should remain in public group after rejected removal") && ok;

        disconnectClient(owner);
        disconnectClient(member);
        disconnectClient(guest);
        server.stop();
        drainEvents();
    }
    drainEvents();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

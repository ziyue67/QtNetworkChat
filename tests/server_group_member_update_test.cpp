#include "client.h"
#include "message.h"
#include "server.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

#include <functional>

namespace {
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

    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
        QDir().mkpath(appDataDir);
    }

    const quint16 port = freeLocalPort();
    bool ok = true;
    ok = expect(port != 0, "a local test port should be available") && ok;
    if (!ok) return 1;

    Server server;
    ok = expect(server.start(port), "server should start on the test port") && ok;
    if (!ok) return 1;

    Client owner;
    Client member;
    QStringList ownerGroupMessages;
    QStringList memberSystemMessages;
    QObject::connect(&owner, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::Text) {
            ownerGroupMessages << msg.content;
        }
    });
    QObject::connect(&member, &Client::newMessage, &app, [&](const Message& msg) {
        if (msg.type == MessageType::System) {
            memberSystemMessages << msg.content;
        }
    });

    const QString ownerId = "910001";
    const QString memberId = "910002";

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

    ok = expect(owner.sendServerGroupMemberUpdate("public", memberId, "add"),
                "owner should submit member add") && ok;
    ok = expect(waitFor([&] {
        return publicGroupHasMember(member.serverGroups(), ownerId)
            && publicGroupHasMember(member.serverGroups(), memberId);
    }), "added member should receive restored public group snapshot") && ok;

    const QString restoredBroadcast = "restored member broadcast should pass";
    ownerGroupMessages.clear();
    ok = expect(member.sendMessage(restoredBroadcast),
                "restored member should send public message") && ok;
    ok = expect(waitFor([&] {
        return ownerGroupMessages.contains(restoredBroadcast);
    }), "restored member message should be broadcast to public group") && ok;

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

    owner.disconnectFromServer();
    member.disconnectFromServer();
    server.stop();
    if (!appDataDir.isEmpty()) {
        QDir(appDataDir).removeRecursively();
    }
    return ok ? 0 : 1;
}

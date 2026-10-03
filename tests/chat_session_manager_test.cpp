#include "chatsessionmanager.h"

#include <QCoreApplication>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;

    ok = expect(ChatSessionManager::e2eSummary(false, false) == QString::fromUtf8("端到端加密未就绪")
                    && ChatSessionManager::e2eSummary(true, true) == QString::fromUtf8("端到端加密需轮换")
                    && ChatSessionManager::e2eSummary(true, false) == QString::fromUtf8("端到端加密就绪"),
                "e2e summary should reflect session and rotation state") && ok;

    PrivateChatUiState readyPrivate = ChatSessionManager::privateChatState(QStringLiteral("10001"),
                                                                           QString::fromUtf8(" Alice "),
                                                                           true,
                                                                           true,
                                                                           false);
    ok = expect(readyPrivate.windowSuffix == QStringLiteral("私聊: Alice")
                    && readyPrivate.titleText == QStringLiteral("与 Alice 私聊中")
                    && readyPrivate.hintText.contains(QStringLiteral("QQ: 10001"))
                    && readyPrivate.hintText.contains(QString::fromUtf8("在线"))
                    && readyPrivate.hintText.contains(QString::fromUtf8("端到端加密就绪")),
                "private chat state should trim display name and include online/e2e summary") && ok;

    PrivateChatUiState fallbackPrivate = ChatSessionManager::privateChatState(QStringLiteral("10002"),
                                                                              QString(),
                                                                              false,
                                                                              true,
                                                                              true);
    ok = expect(fallbackPrivate.windowSuffix == QStringLiteral("私聊: 10002")
                    && fallbackPrivate.titleText == QStringLiteral("与 10002 私聊中")
                    && fallbackPrivate.hintText.contains(QString::fromUtf8("离线"))
                    && fallbackPrivate.hintText.contains(QString::fromUtf8("端到端加密需轮换")),
                "private chat state should fall back to user id and show rotation state") && ok;

    ConnectionUiState localDisconnect = ChatSessionManager::disconnectedState(QString::fromUtf8("本地群"), true);
    ok = expect(localDisconnect.hintText.contains(QString::fromUtf8("本地群聊"))
                    && localDisconnect.statusMessage.contains(QString::fromUtf8("仍可继续记录")),
                "local group disconnect should keep local recording guidance") && ok;

    ConnectionUiState remoteDisconnect = ChatSessionManager::disconnectedState(QString(), false);
    ok = expect(remoteDisconnect.hintText.contains(QString::fromUtf8("公共聊天室"))
                    && remoteDisconnect.statusMessage.contains(QString::fromUtf8("暂不能发送")),
                "remote disconnect should fall back to public chat and block sending") && ok;

    const QString longError(100, QLatin1Char('x'));
    ConnectionUiState errorState = ChatSessionManager::errorState(QString::fromUtf8("好友A"), false, longError);
    ok = expect(errorState.hintText.contains(QString::fromUtf8("好友A"))
                    && errorState.hintText.endsWith(QString(80, QLatin1Char('x')))
                    && errorState.statusMessage.contains(QString::fromUtf8("暂不能发送到 好友A")),
                "connection error should trim diagnostic text in hint") && ok;

    ok = expect(ChatSessionManager::e2eSessionHint(QString::fromUtf8("好友A"), QStringLiteral("ready")).contains(QString::fromUtf8("已就绪"))
                    && ChatSessionManager::e2eSessionHint(QString::fromUtf8("好友A"), QStringLiteral("rotation-required")).contains(QString::fromUtf8("需要轮换"))
                    && ChatSessionManager::e2eSessionHint(QString::fromUtf8("好友A"), QStringLiteral("missing-session")).contains(QString::fromUtf8("未就绪"))
                    && ChatSessionManager::e2eSessionHint(QString::fromUtf8("好友A"), QStringLiteral("unknown")).isEmpty(),
                "e2e session hints should map known states and ignore unknown states") && ok;

    ok = expect(ChatSessionManager::e2eIdentityPendingHint(QString::fromUtf8("好友A"), QStringLiteral("abcdef0123456789extra"))
                    == QString::fromUtf8("端到端加密身份待核对 · 好友A · 指纹:abcdef0123456789"),
                "identity hint should trim fingerprint to 16 characters") && ok;
    ok = expect(ChatSessionManager::e2eRotationRequestHint(QString::fromUtf8("好友A")).contains(QString::fromUtf8("轮换请求"))
                    && ChatSessionManager::e2eRotationResponseHint(QString::fromUtf8("好友A"), true).contains(QString::fromUtf8("已被接受"))
                    && ChatSessionManager::e2eRotationResponseHint(QString::fromUtf8("好友A"), false).contains(QString::fromUtf8("被拒绝")),
                "rotation hints should expose request and response state") && ok;

    ok = expect(ChatSessionManager::localGroupHint(QStringLiteral("local_group_20260610"),
                                                   QStringLiteral("owner"),
                                                   true)
                    == QString::fromUtf8("本地群聊 · 群号 20260610 · 群主 owner · 我的权限:群主"),
                "local group hint should strip local group prefix and show owner role") && ok;

    return ok ? 0 : 1;
}

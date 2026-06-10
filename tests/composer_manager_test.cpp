#include "composermanager.h"

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

    ComposerContext emptyConnected;
    emptyConnected.targetName = QString::fromUtf8("好友A");
    emptyConnected.clientConnected = true;
    ComposerUiState emptyConnectedState = ComposerManager::uiState(emptyConnected);
    ok = expect(emptyConnectedState.canReachTarget
                    && !emptyConnectedState.canSend
                    && emptyConnectedState.sendFileEnabled
                    && emptyConnectedState.sendImageEnabled
                    && emptyConnectedState.sendToolTip.contains(QString::fromUtf8("请输入消息"))
                    && emptyConnectedState.messagePlaceholder.contains(QString::fromUtf8("好友A")),
                "connected empty draft should allow attachments but disable send") && ok;

    ComposerContext encryptedDraft = emptyConnected;
    encryptedDraft.draftText = QString::fromUtf8("  你好  ");
    encryptedDraft.encryptedReady = true;
    ComposerUiState encryptedDraftState = ComposerManager::uiState(encryptedDraft);
    ok = expect(encryptedDraftState.canSend
                    && encryptedDraftState.sendToolTip.contains(QString::fromUtf8("2 字"))
                    && encryptedDraftState.sendToolTip.contains(QString::fromUtf8("端到端加密"))
                    && encryptedDraftState.messagePlaceholder.contains(QString::fromUtf8("端到端加密"))
                    && encryptedDraftState.messageToolTip.contains(QString::fromUtf8("当前草稿")),
                "encrypted draft should enable send and expose encrypted composer copy") && ok;

    ComposerContext disconnected = encryptedDraft;
    disconnected.clientConnected = false;
    disconnected.encryptedReady = false;
    ComposerUiState disconnectedState = ComposerManager::uiState(disconnected);
    ok = expect(!disconnectedState.canReachTarget
                    && !disconnectedState.canSend
                    && !disconnectedState.sendFileEnabled
                    && !disconnectedState.sendImageEnabled
                    && disconnectedState.sendToolTip.contains(QString::fromUtf8("已断开"))
                    && disconnectedState.messagePlaceholder.contains(QString::fromUtf8("重新登录")),
                "disconnected non-local target should disable all send actions") && ok;

    ComposerContext localGroup = disconnected;
    localGroup.localGroup = true;
    localGroup.targetName = QString::fromUtf8("本地群");
    ComposerUiState localGroupState = ComposerManager::uiState(localGroup);
    ok = expect(localGroupState.canReachTarget
                    && localGroupState.canSend
                    && localGroupState.fileToolTip.contains(QString::fromUtf8("本地群")),
                "local group should remain reachable without server connection") && ok;

    ComposerContext removedPublic;
    removedPublic.draftText = QString::fromUtf8("还能发吗");
    removedPublic.removedFromPublicGroup = true;
    removedPublic.clientConnected = true;
    ComposerUiState removedPublicState = ComposerManager::uiState(removedPublic);
    ok = expect(!removedPublicState.canReachTarget
                    && !removedPublicState.canSend
                    && !removedPublicState.sendFileEnabled
                    && removedPublicState.sendToolTip.contains(QString::fromUtf8("重新邀请"))
                    && removedPublicState.fileToolTip.contains(QString::fromUtf8("暂不能发送文件"))
                    && removedPublicState.imageToolTip.contains(QString::fromUtf8("暂不能发送图片")),
                "removed public group member should be fail-closed even when connected") && ok;

    ComposerContext defaultTarget;
    defaultTarget.clientConnected = true;
    ComposerUiState defaultTargetState = ComposerManager::uiState(defaultTarget);
    ok = expect(defaultTargetState.messagePlaceholder.contains(QString::fromUtf8("公共聊天室")),
                "empty target name should fall back to public chat copy") && ok;

    return ok ? 0 : 1;
}

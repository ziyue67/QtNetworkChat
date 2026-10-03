#include "transferchatitemrenderer.h"

#include <QCoreApplication>
#include <QStandardItem>

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

    ok = expect(TransferChatItemRenderer::foregroundForRole(QStringLiteral("success")) == QColor(Qt::darkGreen)
                    && TransferChatItemRenderer::foregroundForRole(QStringLiteral("danger")) == QColor(180, 70, 70)
                    && TransferChatItemRenderer::foregroundForRole(QStringLiteral("muted")) == QColor(86, 116, 130)
                    && !TransferChatItemRenderer::foregroundForRole(QStringLiteral("plain")).isValid(),
                "foreground role mapping should match received-transfer chat item colors") && ok;

    ok = expect(TransferChatItemRenderer::backgroundForRole(QStringLiteral("success-soft")) == QColor(232, 248, 245)
                    && TransferChatItemRenderer::backgroundForRole(QStringLiteral("danger-soft")) == QColor(255, 245, 245)
                    && TransferChatItemRenderer::backgroundForRole(QStringLiteral("muted-soft")) == QColor(246, 251, 253)
                    && !TransferChatItemRenderer::backgroundForRole(QStringLiteral("plain")).isValid(),
                "background role mapping should match received-transfer chat item colors") && ok;

    TransferChatListItemUiState itemState;
    itemState.text = QString::fromUtf8("文件已自动保存");
    itemState.toolTip = QStringLiteral("C:/Downloads/report.zip");
    itemState.foregroundRole = QStringLiteral("success");
    itemState.backgroundRole = QStringLiteral("success-soft");
    itemState.alignRight = true;
    QStandardItem* item = TransferChatItemRenderer::createItem(itemState);
    ok = expect(item->text() == itemState.text
                    && !item->isEditable()
                    && item->data(Qt::ToolTipRole).toString() == itemState.toolTip
                    && item->foreground().color() == QColor(Qt::darkGreen)
                    && item->background().color() == QColor(232, 248, 245)
                    && item->textAlignment() == (Qt::AlignRight | Qt::AlignVCenter),
                "created transfer chat item should apply text, tooltip, colors, and alignment") && ok;
    delete item;

    TransferChatListItemUiState plainState;
    plainState.text = QStringLiteral("plain");
    QStandardItem* plainItem = TransferChatItemRenderer::createItem(plainState);
    ok = expect(plainItem->textAlignment() == (Qt::AlignLeft | Qt::AlignVCenter)
                    && plainItem->data(Qt::ToolTipRole).toString().isEmpty(),
                "plain transfer chat item should keep default roles and left alignment") && ok;
    delete plainItem;

    ok = expect(static_cast<int>(TransferChatItemRenderer::MediaKindRole) > static_cast<int>(Qt::UserRole)
                    && static_cast<int>(TransferChatItemRenderer::OpenPathRole) == static_cast<int>(TransferChatItemRenderer::MediaKindRole) + 1
                    && static_cast<int>(TransferChatItemRenderer::SenderIdRole) == static_cast<int>(TransferChatItemRenderer::MediaKindRole) + 2
                    && static_cast<int>(TransferChatItemRenderer::SenderNameRole) == static_cast<int>(TransferChatItemRenderer::MediaKindRole) + 3
                    && static_cast<int>(TransferChatItemRenderer::AvatarPathRole) == static_cast<int>(TransferChatItemRenderer::MediaKindRole) + 4,
                "chat item custom roles should stay contiguous for attachment and avatar metadata") && ok;

    return ok ? 0 : 1;
}

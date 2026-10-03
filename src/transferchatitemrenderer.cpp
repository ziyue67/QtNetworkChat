#include "transferchatitemrenderer.h"

#include <QStandardItem>

QColor TransferChatItemRenderer::foregroundForRole(const QString& role) {
    if (role == QStringLiteral("success")) {
        return Qt::darkGreen;
    }
    if (role == QStringLiteral("danger")) {
        return QColor(180, 70, 70);
    }
    if (role == QStringLiteral("muted")) {
        return QColor(86, 116, 130);
    }
    return QColor();
}

QColor TransferChatItemRenderer::backgroundForRole(const QString& role) {
    if (role == QStringLiteral("success-soft")) {
        return QColor(232, 248, 245);
    }
    if (role == QStringLiteral("danger-soft")) {
        return QColor(255, 245, 245);
    }
    if (role == QStringLiteral("muted-soft")) {
        return QColor(246, 251, 253);
    }
    return QColor();
}

QStandardItem* TransferChatItemRenderer::createItem(const TransferChatListItemUiState& itemState) {
    QStandardItem* item = new QStandardItem(itemState.text);
    item->setEditable(false);
    if (!itemState.toolTip.isEmpty()) {
        item->setData(itemState.toolTip, Qt::ToolTipRole);
    }

    const QColor foreground = foregroundForRole(itemState.foregroundRole);
    if (foreground.isValid()) {
        item->setForeground(foreground);
    }

    const QColor background = backgroundForRole(itemState.backgroundRole);
    if (background.isValid()) {
        item->setBackground(background);
    }

    item->setTextAlignment((itemState.alignRight ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    return item;
}

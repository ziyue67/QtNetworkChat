#ifndef TRANSFERCHATITEMRENDERER_H
#define TRANSFERCHATITEMRENDERER_H

#include "transfermanager.h"

#include <QColor>
#include <Qt>

class QStandardItem;

class TransferChatItemRenderer {
public:
    enum ChatItemRole {
        MediaKindRole = Qt::UserRole + 410,
        OpenPathRole,
        SenderIdRole,
        SenderNameRole,
        AvatarPathRole,
        DurationLabelRole
    };

    static QColor foregroundForRole(const QString& role);
    static QColor backgroundForRole(const QString& role);
    static QStandardItem* createItem(const TransferChatListItemUiState& itemState);
};

#endif // TRANSFERCHATITEMRENDERER_H

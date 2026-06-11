#ifndef TRANSFERCHATITEMRENDERER_H
#define TRANSFERCHATITEMRENDERER_H

#include "transfermanager.h"

#include <QColor>

class QStandardItem;

class TransferChatItemRenderer {
public:
    static QColor foregroundForRole(const QString& role);
    static QColor backgroundForRole(const QString& role);
    static QStandardItem* createItem(const TransferChatListItemUiState& itemState);
};

#endif // TRANSFERCHATITEMRENDERER_H

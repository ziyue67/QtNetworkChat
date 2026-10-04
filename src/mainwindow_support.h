#pragma once

#include "message.h"
#include "transfermanager.h"
#include <QIcon>
#include <QPixmap>

class QAction;

// Internal presentation helpers shared by the window's compilation units.
namespace MainWindowSupport {
struct StoredChatMessage {
    QString timestamp;
    QString senderName;
    QString body;
    bool system = false;
};
StoredChatMessage parseStoredChatMessage(const QString& line);
QString bubbleTimestamp(const QString& timestamp);
QPixmap loadChatImagePreview(const QString& filePath);
QPixmap squareAvatarPixmap(const QPixmap& source, int side);
QPixmap roundAvatarPixmap(const QPixmap& source, int side);
QIcon generatedPeerAvatarIcon(const QString& displayName, const QString& seedId, int side = 36);
void applyTransferActionState(QAction* action, const TransferActionUiState& state);
QIcon createChatIcon(const QString& seedText = QString());
QString transferIntegritySummary(const Message& msg);
QString appWindowTitle(const QString& suffix = QString());
} // namespace MainWindowSupport

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "sessionitemdelegate.h"
#include "sessionlistbuilder.h"
#include "chatbubbledelegate.h"
#include "qqnt_log.h"
#include "qqnt_backend_service.h"
#include "dialogs/friendmanagerdialog.h"
#include "dialogs/creategroupdialog.h"
#include "dialogs/addfrienddialog.h"
#include "dialogs/globalsearchdialog.h"
#include "dialogs/memberprofilecard.h"
#include "dialogs/groupnicknamedialog.h"
#include "dialogs/essencepanel.h"
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QApplication>
#include <QDateTime>
#include <QDebug>

#include "chatsessionmanager.h"
#include "chatcontextmanager.h"
#include "composermanager.h"
#include "filetransferstatus.h"
#include "localfilemanager.h"
#include "notificationpanelmanager.h"
#include "qtnetworkchat_version.h"
#include "transferchatitemrenderer.h"
#include "windowstatemanager.h"
#include "views/messagesview.h"
#include "views/contactsview.h"
#include "widgets/contactlistwidget.h"
#include "views/favoritesview.h"
#include "views/settingsview.h"
#include "views/profileview.h"
#include "widgets/appnav.h"
#include "widgets/titlebar.h"
#include "widgets/composerwidget.h"
#include "widgets/groupmembersidebar.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"
#include "theme/thememanager.h"
#include "theme/dialogstyle.h"
#include "windows/screenshotcapturewindow.h"
#include "screenshotgeometry.h"
#include "windows/imagepreviewwindow.h"
#include "windows/forwardwindow.h"
#include "dialogs/mutedurationdialog.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QLinearGradient>
#include <QPolygonF>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTabWidget>
#include <QShortcut>
#include <QUrl>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QProgressDialog>
#include <QCheckBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QComboBox>
#include <QFrame>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QBuffer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QScreen>
#include <QStyleHints>
#include <QPalette>
#include <QWindow>
#include <functional>
#include <algorithm>

#include "mainwindow_support.h"

namespace MainWindowSupport {

// History predates the QQNT message model and stores a human-readable line.
// Keep that storage format for compatibility, but never present its envelope as
// the body of a chat bubble.
StoredChatMessage parseStoredChatMessage(const QString& line) {
    const QRegularExpression systemPattern(
        QStringLiteral(R"(^\s*\[([^\]]+)\]\s*\[系统\]\s*(.*)$)"));
    const QRegularExpression normalPattern(
        QStringLiteral(R"(^\s*\[([^\]]+)\]\s*<([^>]+)>\s*(.*)$)"));

    QString normalizedLine = line.trimmed();
    // Export rows include the database timestamp before the original chat
    // envelope. Stored rows and legacy files only contain the envelope.
    normalizedLine.remove(QRegularExpression(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}\s*\|\s*)")));

    QRegularExpressionMatch match = systemPattern.match(normalizedLine);
    if (match.hasMatch()) {
        return {match.captured(1).trimmed(), QStringLiteral("系统"),
                match.captured(2).trimmed(), true};
    }

    match = normalPattern.match(normalizedLine);
    if (match.hasMatch()) {
        QString body = match.captured(3).trimmed();
        body.remove(QRegularExpression(QStringLiteral(R"(^\[(?:私聊|端到端加密)\]\s*)")));
        return {match.captured(1).trimmed(), match.captured(2).trimmed(), body, false};
    }

    return {QString(), QString(), normalizedLine, false};
}

QString bubbleTimestamp(const QString& timestamp) {
    const QTime time = QTime::fromString(timestamp, QStringLiteral("hh:mm:ss"));
    if (time.isValid()) {
        return time.toString(QStringLiteral("HH:mm"));
    }
    return timestamp.left(5);
}

QPixmap loadChatImagePreview(const QString& filePath) {
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            image = QImage::fromData(file.readAll());
        }
    }
    if (!image.isNull()) {
        return QPixmap::fromImage(image);
    }
    QPixmap pixmap;
    pixmap.load(filePath);
    return pixmap;
}

QPixmap roundAvatarPixmap(const QPixmap& source, int side);
QIcon generatedPeerAvatarIcon(const QString& displayName, const QString& seedId, int side);

void applyTransferActionState(QAction* action, const TransferActionUiState& state) {
    if (!action) return;
    action->setVisible(state.visible);
    action->setEnabled(state.enabled);
    action->setToolTip(state.toolTip);
}

QIcon createChatIcon(const QString& seedText) {
    QIcon icon;
    const int sizes[] = {16, 24, 32, 48, 64, 128};
    for (int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient gradient(0, 0, size, size);
        gradient.setColorAt(0.0, QColor("#2BD8C5"));
        gradient.setColorAt(0.55, QColor("#17A8F3"));
        gradient.setColorAt(1.0, QColor("#6C5CE7"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(QRectF(size * 0.08, size * 0.10, size * 0.84, size * 0.72), size * 0.24, size * 0.24);

        QPolygonF tail;
        tail << QPointF(size * 0.36, size * 0.78)
             << QPointF(size * 0.29, size * 0.94)
             << QPointF(size * 0.54, size * 0.80);
        painter.drawPolygon(tail);

        painter.setBrush(QColor(255, 255, 255, 235));
        const qreal dot = qMax(2.0, size * 0.10);
        painter.drawEllipse(QPointF(size * 0.36, size * 0.45), dot, dot);
        painter.drawEllipse(QPointF(size * 0.50, size * 0.45), dot, dot);
        painter.drawEllipse(QPointF(size * 0.64, size * 0.45), dot, dot);

        if (size >= 48 && !seedText.trimmed().isEmpty()) {
            QFont font = painter.font();
            font.setFamily("Microsoft YaHei");
            font.setBold(true);
            font.setPixelSize(static_cast<int>(size * 0.28));
            painter.setFont(font);
            painter.setPen(QColor(255, 255, 255, 245));
            painter.drawText(QRectF(size * 0.08, size * 0.12, size * 0.84, size * 0.54),
                             Qt::AlignCenter,
                             seedText.left(1).toUpper());
        }

        icon.addPixmap(pixmap);
    }
    return icon;
}

QString transferIntegritySummary(const Message& msg) {
    const bool hasExpectedSize = msg.fileSize > 0;
    const bool hasExpectedHash = !msg.fileHash.trimmed().isEmpty();
    if (!hasExpectedSize && !hasExpectedHash) {
        return "未提供完整性校验";
    }

    const bool sizeOk = !hasExpectedSize || msg.fileSize == msg.fileData.size();
    bool hashOk = true;
    if (hasExpectedHash) {
        const QString actualHash = QString::fromLatin1(QCryptographicHash::hash(msg.fileData, QCryptographicHash::Sha256).toHex());
        hashOk = actualHash.compare(msg.fileHash.trimmed(), Qt::CaseInsensitive) == 0;
    }

    if (sizeOk && hashOk) {
        return "完整性已验证";
    }

    QStringList issues;
    if (!sizeOk) issues << "大小不一致";
    if (!hashOk) issues << "哈希不一致";
    return "完整性校验失败：" + issues.join("、");
}

QPixmap squareAvatarPixmap(const QPixmap& source, int side) {
    if (source.isNull() || side <= 0) return QPixmap();
    QPixmap scaled = source.scaled(side, side, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - side) / 2);
    const int y = qMax(0, (scaled.height() - side) / 2);
    return scaled.copy(x, y, side, side);
}

QPixmap roundAvatarPixmap(const QPixmap& source, int side) {
    const QPixmap square = squareAvatarPixmap(source, side);
    if (square.isNull() || side <= 0) {
        return QPixmap();
    }

    QPixmap rounded(side, side);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, side, side);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, square);
    return rounded;
}

QIcon generatedPeerAvatarIcon(const QString& displayName, const QString& seedId, int side) {
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QByteArray hash = QCryptographicHash::hash((seedId + "|" + displayName).toUtf8(), QCryptographicHash::Sha1);
    const int hue = hash.isEmpty() ? 210 : static_cast<unsigned char>(hash.at(0)) % 360;
    QColor start = QColor::fromHsv(hue, 120, 220);
    QColor end = QColor::fromHsv((hue + 28) % 360, 150, 200);
    QLinearGradient gradient(0, 0, side, side);
    gradient.setColorAt(0.0, start);
    gradient.setColorAt(1.0, end);
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, side, side);

    QFont font = painter.font();
    font.setFamily(QStringLiteral("Microsoft YaHei"));
    font.setBold(true);
    font.setPixelSize(qMax(14, side / 2));
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 245));
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter, displayName.trimmed().isEmpty() ? QStringLiteral("?") : displayName.left(1).toUpper());
    return QIcon(pixmap);
}

QString appWindowTitle(const QString& suffix) {
    return WindowStateManager::appWindowTitle(QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING), suffix);
}
}

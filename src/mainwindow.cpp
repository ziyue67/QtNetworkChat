#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "chatsessionmanager.h"
#include "chatcontextmanager.h"
#include "composermanager.h"
#include "filetransferstatus.h"
#include "localfilemanager.h"
#include "notificationpanelmanager.h"
#include "qtnetworkchat_version.h"
#include "transferchatitemrenderer.h"
#include "windowstatemanager.h"
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
#include <QPushButton>
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QLinearGradient>
#include <QPolygonF>
#include <QRegularExpression>
#include <QKeyEvent>
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
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QCryptographicHash>
#include <QStyle>
#include <QProgressBar>
#include <QBuffer>

namespace {
void applyTransferActionState(QAction* action, const TransferActionUiState& state) {
    if (!action) return;
    action->setVisible(state.visible);
    action->setEnabled(state.enabled);
    action->setToolTip(state.toolTip);
}

void applyToneProperty(QWidget* widget, const QString& tone) {
    if (!widget) {
        return;
    }
    if (widget->property("tone").toString() == tone) {
        return;
    }
    widget->setProperty("tone", tone);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

bool transferWorkspaceStateUsesSavedFileActions(const TransferSendUiState& state) {
    const QString title = state.workspaceTitle.trimmed();
    return title.startsWith(QStringLiteral("文件工作区 · 已保存文件"))
        || title.startsWith(QStringLiteral("文件工作区 · 已接收并保存"))
        || title.startsWith(QStringLiteral("文件工作区 · 已保存待复核"))
        || title.startsWith(QStringLiteral("文件工作区 · 接收保存失败"));
}

QIcon createChatIcon(const QString& seedText = QString()) {
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

QString productMainWindowStyleSheet() {
    return QStringLiteral(R"(
        QMainWindow, QWidget#centralwidget {
            background: #EEF3F8;
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
            color: #122033;
        }
        QFrame#sidePanel, QFrame#chatPanel, QFrame#groupInfoPanel {
            background: #FFFFFF;
            border: 1px solid #D8E4EE;
            border-radius: 18px;
        }
        QFrame#profileCard, QFrame#announcementCard {
            background: transparent;
            border: none;
            border-radius: 0;
        }
        QFrame#chatHeader {
            background: transparent;
            border: none;
            border-bottom: 1px solid #E6EDF4;
        }
        QFrame#inputPanel {
            background: transparent;
            border: none;
            border-top: 1px solid #E6EDF4;
        }
        QFrame#chatSessionCard,
        QFrame#composerStatusCard,
        QFrame#groupSummaryCard,
        QFrame#transferStatusCard,
        QFrame#avatarStatusCard {
            background: #F8FBFD;
            border: 1px solid #D8E4EE;
            border-radius: 14px;
        }
        QFrame#chatSessionCard[tone="accent"],
        QFrame#composerStatusCard[tone="accent"],
        QFrame#groupSummaryCard[tone="accent"],
        QFrame#transferStatusCard[tone="accent"],
        QFrame#avatarStatusCard[tone="accent"] {
            background: #EEF5FF;
            border: 1px solid #CFE0F8;
        }
        QFrame#chatSessionCard[tone="warning"],
        QFrame#composerStatusCard[tone="warning"],
        QFrame#groupSummaryCard[tone="warning"],
        QFrame#transferStatusCard[tone="warning"],
        QFrame#avatarStatusCard[tone="warning"] {
            background: #FFF7ED;
            border: 1px solid #F1D4AB;
        }
        QFrame#chatSessionCard[tone="success"],
        QFrame#composerStatusCard[tone="success"],
        QFrame#groupSummaryCard[tone="success"],
        QFrame#transferStatusCard[tone="success"],
        QFrame#avatarStatusCard[tone="success"] {
            background: #F0FDF4;
            border: 1px solid #C5E7D0;
        }
        QFrame#chatSessionCard[tone="danger"],
        QFrame#composerStatusCard[tone="danger"],
        QFrame#groupSummaryCard[tone="danger"],
        QFrame#transferStatusCard[tone="danger"],
        QFrame#avatarStatusCard[tone="danger"] {
            background: #FFF5F5;
            border: 1px solid #F2C8C5;
        }
        QLabel#appTitleLabel {
            color: #0F172A;
            font-size: 19px;
            font-weight: 800;
        }
        QLabel#profileNameLabel {
            color: #0F172A;
            font-size: 15px;
            font-weight: 800;
        }
        QLabel#profileIdLabel {
            color: #617284;
            font-size: 12px;
        }
        QLabel#avatarLabel {
            background: #E8F1FF;
            color: #1D4ED8;
            border: 1px solid #D6E4FF;
            border-radius: 28px;
            font-size: 22px;
            font-weight: 800;
        }
        QLabel#onlineTitleLabel, QLabel#announcementTitleLabel, QLabel#memberTitleLabel {
            color: #0F172A;
            font-size: 13px;
            font-weight: 800;
        }
        QLabel#chatTitleLabel {
            color: #0F172A;
            font-size: 18px;
            font-weight: 800;
        }
        QLabel#chatSessionMetaLabel {
            color: #617284;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#chatHintLabel {
            color: #1E4E8C;
            background: #EEF5FF;
            border: 1px solid #D7E6FB;
            border-radius: 12px;
            padding: 5px 10px;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#chatSessionStatusLabel,
        QLabel#composerStatusDetailLabel,
        QLabel#groupSummaryDetailLabel {
            color: #5F7285;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#composerStatusTitleLabel,
        QLabel#groupSummaryTitleLabel {
            color: #0F172A;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#transferStatusTitleLabel {
            color: #0F172A;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#avatarStatusTitleLabel {
            color: #0F172A;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#transferStatusDetailLabel {
            color: #5F7285;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#avatarStatusDetailLabel {
            color: #5F7285;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#announcementBodyLabel {
            color: #56677B;
            font-size: 12px;
            line-height: 1.45;
        }
        QListView#userListView, QListView#chatListView, QListView#groupMemberListView {
            background: #F8FBFD;
            border: 1px solid #E2EAF2;
            border-radius: 16px;
            padding: 8px;
            outline: none;
        }
        QListView#userListView::item, QListView#groupMemberListView::item {
            min-height: 42px;
            border-radius: 10px;
            padding: 6px 10px;
        }
        QListView#chatListView::item {
            min-height: 34px;
            border-radius: 12px;
            margin: 4px 0;
            padding: 8px 12px;
        }
        QListView#userListView::item:selected,
        QListView#userListView::item:hover,
        QListView#groupMemberListView::item:selected,
        QListView#groupMemberListView::item:hover,
        QListView#chatListView::item:hover {
            background: #EAF2FF;
            color: #174EA6;
        }
        QLineEdit#contactSearchEdit, QLineEdit#memberSearchEdit {
            min-height: 36px;
            background: #FFFFFF;
            color: #162334;
            border: 1px solid #D7E2EC;
            border-radius: 14px;
            padding: 4px 12px;
        }
        QLineEdit#contactSearchEdit:focus,
        QLineEdit#memberSearchEdit:focus,
        QTextEdit#messageEdit:focus,
        QInputDialog QLineEdit:focus,
        QInputDialog QTextEdit:focus,
        QMessageBox QLineEdit:focus {
            background: #FFFFFF;
            border: 1px solid #3B82F6;
        }
        QTextEdit#messageEdit {
            background: #F8FBFD;
            color: #142235;
            border: 1px solid #D7E2EC;
            border-radius: 14px;
            padding: 10px 12px;
            selection-background-color: #3B82F6;
            selection-color: #FFFFFF;
        }
        QPushButton {
            min-height: 34px;
            padding: 6px 12px;
            border-radius: 12px;
            border: 1px solid #D7E2EC;
            background: #FFFFFF;
            color: #324659;
            font-weight: 700;
        }
        QPushButton:hover {
            background: #F7FAFE;
            border-color: #BFD0E2;
        }
        QPushButton:pressed {
            background: #EDF4FF;
            border-color: #9FBBE4;
        }
        QPushButton:disabled {
            background: #F8FAFC;
            color: #9AA7B5;
            border-color: #E2E8F0;
        }
        QPushButton#sendBtn, QMessageBox QPushButton[text="Yes"], QInputDialog QPushButton[text="OK"] {
            background: #2563EB;
            color: #FFFFFF;
            border: 1px solid #2563EB;
            min-width: 108px;
        }
        QPushButton#sendBtn:hover {
            background: #1D4ED8;
            border-color: #1D4ED8;
        }
        QPushButton#sendBtn:pressed {
            background: #1E40AF;
            border-color: #1E40AF;
        }
        QPushButton#sendBtn:disabled {
            background: #BFDBFE;
            color: #EFF6FF;
            border-color: #BFDBFE;
        }
        QPushButton#toolBtn, QPushButton#iconToolBtn,
        QPushButton#friendNoticeBtn, QPushButton#groupNoticeBtn,
        QPushButton#copyAccountBtn, QPushButton#friendManagerBtn,
        QPushButton#groupChatBtn, QPushButton#globalSearchBtn,
        QPushButton#groupMemberWorkspaceBtn,
        QPushButton#createMenuBtn, QPushButton#uploadAvatarBtn {
            background: #F8FBFD;
            border: 1px solid #DFE8F1;
            color: #324659;
        }
        QPushButton#toolBtn:hover, QPushButton#iconToolBtn:hover,
        QPushButton#friendNoticeBtn:hover, QPushButton#groupNoticeBtn:hover,
        QPushButton#copyAccountBtn:hover, QPushButton#friendManagerBtn:hover,
        QPushButton#groupChatBtn:hover, QPushButton#globalSearchBtn:hover,
        QPushButton#groupMemberWorkspaceBtn:hover,
        QPushButton#createMenuBtn:hover, QPushButton#uploadAvatarBtn:hover {
            background: #EDF4FF;
            border-color: #C8D8F0;
            color: #174EA6;
        }
        QPushButton#transferGhostBtn {
            background: #FFFFFF;
            border: 1px solid #D7E2EC;
            color: #324659;
            min-height: 32px;
            padding: 5px 12px;
            border-radius: 12px;
        }
        QPushButton#transferGhostBtn:hover {
            background: #F7FAFE;
            border-color: #BFD0E2;
        }
        QPushButton#transferPrimaryBtn {
            background: #2563EB;
            color: #FFFFFF;
            border: 1px solid #2563EB;
            min-height: 32px;
            padding: 5px 12px;
            border-radius: 12px;
        }
        QPushButton#transferPrimaryBtn:hover {
            background: #1D4ED8;
            border-color: #1D4ED8;
        }
        QPushButton#transferDangerBtn {
            background: #FFF6F5;
            color: #B42318;
            border: 1px solid #F0D0CB;
            min-height: 32px;
            padding: 5px 12px;
            border-radius: 12px;
        }
        QPushButton#transferDangerBtn:hover {
            background: #FEEDEC;
            border-color: #EAB8B0;
        }
        QPushButton#clearBtn {
            background: #FFF6F5;
            color: #B42318;
            border-color: #F0D0CB;
        }
        QPushButton#clearBtn:hover {
            background: #FEEDEC;
            border-color: #EAB8B0;
        }
        QMenuBar {
            background: #FFFFFF;
            color: #516273;
            border-bottom: 1px solid #E2E8F0;
            spacing: 4px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 5px 10px;
            border-radius: 6px;
        }
        QMenuBar::item:selected {
            background: #EEF4FF;
            color: #1D4ED8;
        }
        QMenu {
            background: #FFFFFF;
            color: #253342;
            border: 1px solid #D6E2EB;
            border-radius: 8px;
            padding: 6px;
        }
        QMenu::item {
            padding: 7px 24px 7px 12px;
            border-radius: 6px;
        }
        QMenu::item:selected {
            background: #EAF4FF;
            color: #185ABD;
        }
        QMenu::separator {
            height: 1px;
            background: #E7EEF4;
            margin: 6px 4px;
        }
        QToolTip {
            background: #203144;
            color: #FFFFFF;
            border: none;
            border-radius: 6px;
            padding: 6px 8px;
        }
        QDialog, QMessageBox, QInputDialog {
            background: #F3F6FB;
            color: #0F172A;
        }
        QMessageBox QLabel, QInputDialog QLabel {
            color: #233548;
        }
        QMessageBox QLineEdit, QInputDialog QLineEdit, QInputDialog QTextEdit, QInputDialog QComboBox {
            min-height: 36px;
            background: #FFFFFF;
            color: #162334;
            border: 1px solid #D7E2EC;
            border-radius: 12px;
            padding: 4px 10px;
        }
        QMessageBox QPushButton, QInputDialog QPushButton {
            min-height: 34px;
            min-width: 88px;
            padding: 6px 14px;
            border-radius: 12px;
            border: 1px solid #D7E2EC;
            background: #FFFFFF;
            color: #334155;
            font-weight: 700;
        }
        QMessageBox QPushButton:hover, QInputDialog QPushButton:hover {
            background: #F7FAFE;
            border-color: #BFD0E2;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 4px 2px;
        }
        QScrollBar::handle:vertical {
            background: #C5D7E5;
            border-radius: 5px;
            min-height: 36px;
        }
        QScrollBar::handle:vertical:hover {
            background: #9FBCD2;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
            height: 0px;
        }
        QStatusBar {
            background: #F7FAFD;
            color: #637488;
            border-top: 1px solid #E0E8F0;
            padding-left: 6px;
        }
        QStatusBar::item {
            border: none;
        }
    )");
}

QString toolWorkspaceDialogStyleSheet() {
    return QStringLiteral(R"(
        QDialog#globalSearchDialog, QDialog#quickAddDialog, QDialog#friendManagerDialog {
            background: #F3F6FB;
            color: #0F172A;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QFrame#searchHeader, QFrame#managerHeader, QFrame#managerBody {
            background: #FFFFFF;
            border: 1px solid #D8E4EE;
            border-radius: 18px;
        }
        QFrame#workspaceSectionCard {
            background: #FFFFFF;
            border: 1px solid #D8E4EE;
            border-radius: 16px;
        }
        QLabel#searchDialogTitle, QLabel#managerTitle, QLabel#quickAddTitle {
            color: #0F172A;
            font-size: 20px;
            font-weight: 800;
        }
        QLabel#searchDialogSubTitle, QLabel#managerSubTitle, QLabel#quickAddHint,
        QLabel#globalActionHint {
            color: #5F7285;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#globalStatsLabel, QLabel#managerStats, QLabel#quickAddStats {
            min-height: 24px;
            background: #EDF4FF;
            color: #1D4ED8;
            border: 1px solid #D6E4FB;
            border-radius: 12px;
            padding: 2px 10px;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#globalPreviewLabel, QLabel#managerSelectionPreview,
        QLabel#quickAddPreviewCard {
            background: #FFFFFF;
            color: #223548;
            border: 1px solid #D8E4EE;
            border-radius: 14px;
            padding: 10px 12px;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#managerOperationGuide, QLabel#quickAddActionTip {
            background: #FFF8E9;
            color: #8A5B00;
            border: 1px solid #F8D9A1;
            border-radius: 14px;
            padding: 8px 12px;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#workspaceSectionTitle {
            color: #0F172A;
            font-size: 13px;
            font-weight: 800;
        }
        QLabel#workspaceSectionHint {
            color: #5F7285;
            font-size: 12px;
            font-weight: 600;
        }
        QFrame#operationHeader, QFrame#operationBody {
            background: #FFFFFF;
            border: 1px solid #D8E4EE;
            border-radius: 18px;
        }
        QLabel#operationTitle {
            color: #0F172A;
            font-size: 20px;
            font-weight: 800;
        }
        QLabel#operationSubTitle, QLabel#operationHintLabel {
            color: #5F7285;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#operationStatusChip {
            min-height: 24px;
            padding: 2px 10px;
            border-radius: 12px;
            background: #EDF4FF;
            color: #1D4ED8;
            border: 1px solid #D6E4FB;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#operationStatusChip[tone="warning"] {
            background: #FFF7ED;
            color: #B45309;
            border: 1px solid #F3D1A8;
        }
        QLabel#operationStatusChip[tone="success"] {
            background: #F0FDF4;
            color: #15803D;
            border: 1px solid #B7E0C2;
        }
        QLabel#operationStatusChip[tone="accent"] {
            background: #EDF4FF;
            color: #1D4ED8;
            border: 1px solid #D6E4FB;
        }
        QLabel#operationMetricLabel {
            color: #1D4ED8;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#operationDetailLabel {
            color: #223548;
            font-size: 13px;
            font-weight: 700;
        }
        QLineEdit#globalSearchInput, QLineEdit#managerSearch, QLineEdit#quickAddInput {
            min-height: 40px;
            background: #F8FBFD;
            color: #162334;
            border: 1px solid #D7E2EC;
            border-radius: 16px;
            padding: 4px 14px;
            font-size: 14px;
        }
        QLineEdit#globalSearchInput:focus, QLineEdit#managerSearch:focus, QLineEdit#quickAddInput:focus {
            background: #FFFFFF;
            border: 1px solid #3B82F6;
        }
        QListWidget#globalResultList, QListWidget#managerList, QListWidget#quickAddSuggestionList {
            background: #FFFFFF;
            border: 1px solid #DCE8F2;
            border-radius: 16px;
            padding: 8px;
            outline: none;
        }
        QListWidget#globalResultList::item, QListWidget#managerList::item, QListWidget#quickAddSuggestionList::item {
            background: #F8FBFD;
            color: #223548;
            border-radius: 12px;
            margin: 4px 0;
            padding: 10px 12px;
        }
        QListWidget#globalResultList::item:selected, QListWidget#globalResultList::item:hover,
        QListWidget#managerList::item:selected, QListWidget#managerList::item:hover,
        QListWidget#quickAddSuggestionList::item:selected, QListWidget#quickAddSuggestionList::item:hover {
            background: #EAF2FF;
            color: #174EA6;
        }
        QLabel#activeSearchTab {
            min-width: 60px;
            background: #EAF2FF;
            color: #1D4ED8;
            border: 1px solid #D4E4FB;
            border-radius: 12px;
            padding: 7px 12px;
            font-weight: 800;
        }
        QLabel#searchTab {
            min-width: 60px;
            background: #F8FBFD;
            color: #64748B;
            border: 1px solid #E3EBF3;
            border-radius: 12px;
            padding: 7px 12px;
            font-weight: 700;
        }
        QPushButton {
            min-height: 36px;
            border-radius: 14px;
            padding: 6px 14px;
            font-weight: 700;
        }
        QPushButton#globalSearchPrimaryBtn, QPushButton#quickSearchBtn, QPushButton#managerPrimaryBtn {
            background: #2563EB;
            color: #FFFFFF;
            border: 1px solid #2563EB;
        }
        QPushButton#globalSearchPrimaryBtn:hover, QPushButton#quickSearchBtn:hover, QPushButton#managerPrimaryBtn:hover {
            background: #1D4ED8;
            border-color: #1D4ED8;
        }
        QPushButton#globalSearchGhostBtn, QPushButton#quickCancelBtn, QPushButton#managerSecondaryBtn {
            background: #FFFFFF;
            color: #324659;
            border: 1px solid #D7E2EC;
        }
        QPushButton#globalSearchGhostBtn:hover, QPushButton#quickCancelBtn:hover, QPushButton#managerSecondaryBtn:hover {
            background: #F7FAFE;
            border-color: #BFD0E2;
        }
        QPushButton#managerDangerBtn {
            background: #FFF6F5;
            color: #B42318;
            border: 1px solid #F0D0CB;
        }
        QPushButton#managerDangerBtn:hover {
            background: #FEEDEC;
            border-color: #EAB8B0;
        }
    )");
}

QString productDialogStyleSheet() {
    return productMainWindowStyleSheet() + toolWorkspaceDialogStyleSheet() + QStringLiteral(R"(
        QFileDialog, QProgressDialog {
            background: #F3F6FB;
            color: #0F172A;
            font-family: "Microsoft YaHei", "Segoe UI";
        }
        QFileDialog QLabel, QProgressDialog QLabel {
            color: #233548;
        }
        QFileDialog QLineEdit, QFileDialog QComboBox,
        QFileDialog QListView, QFileDialog QTreeView {
            background: #FFFFFF;
            color: #162334;
            border: 1px solid #D7E2EC;
            border-radius: 12px;
        }
        QFileDialog QLineEdit {
            min-height: 36px;
            padding: 4px 10px;
        }
        QFileDialog QListView, QFileDialog QTreeView {
            padding: 6px;
        }
        QFileDialog QLineEdit:focus,
        QFileDialog QListView:focus,
        QFileDialog QTreeView:focus {
            border-color: #3B82F6;
        }
        QFileDialog QPushButton, QProgressDialog QPushButton {
            min-height: 34px;
            min-width: 88px;
            padding: 6px 14px;
            border-radius: 12px;
            border: 1px solid #D7E2EC;
            background: #FFFFFF;
            color: #334155;
            font-weight: 700;
        }
        QFileDialog QPushButton:hover, QProgressDialog QPushButton:hover {
            background: #F7FAFE;
            border-color: #BFD0E2;
        }
        QProgressBar {
            min-height: 14px;
            border: 1px solid #D7E2EC;
            border-radius: 7px;
            background: #EAF0F6;
            text-align: center;
            color: #486171;
        }
        QProgressBar::chunk {
            border-radius: 7px;
            background: #2563EB;
        }
        QProgressBar#operationProgressBar {
            min-height: 18px;
            border: 1px solid #D7E2EC;
            border-radius: 9px;
            background: #EAF0F6;
            text-align: center;
            color: #486171;
            font-size: 11px;
            font-weight: 700;
        }
        QProgressBar#operationProgressBar::chunk {
            border-radius: 9px;
            background: #2563EB;
        }
    )");
}

void applyProductDialogChrome(QWidget* widget) {
    if (!widget) {
        return;
    }
    widget->setStyleSheet(productDialogStyleSheet());
}

void assignButtonIcon(QPushButton* button, QWidget* widget, QStyle::StandardPixmap iconType) {
    if (!button || !widget) return;
    button->setIcon(widget->style()->standardIcon(iconType));
    button->setIconSize(QSize(16, 16));
}

QAction* addMenuActionWithIcon(QMenu& menu,
                               QWidget* widget,
                               const QString& title,
                               const QString& toolTip,
                               const QString& commandId = QString(),
                               bool enabled = true,
                               QStyle::StandardPixmap iconType = QStyle::SP_FileDialogInfoView) {
    QAction* action = menu.addAction(widget->style()->standardIcon(iconType), title);
    action->setToolTip(toolTip);
    action->setStatusTip(toolTip);
    action->setData(commandId);
    action->setEnabled(enabled);
    return action;
}

struct WorkspaceSectionCard {
    QFrame* frame = nullptr;
    QVBoxLayout* contentLayout = nullptr;
};

struct WorkspaceDialogShell {
    QVBoxLayout* rootLayout = nullptr;
    QFrame* headerFrame = nullptr;
    QVBoxLayout* headerLayout = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* subTitleLabel = nullptr;
    QFrame* bodyFrame = nullptr;
    QVBoxLayout* bodyLayout = nullptr;
    QHBoxLayout* searchRowLayout = nullptr;
    QLineEdit* searchEdit = nullptr;
    QListWidget* listWidget = nullptr;
    QHBoxLayout* summaryRowLayout = nullptr;
    QLabel* hintLabel = nullptr;
    QLabel* statsLabel = nullptr;
    QLabel* previewLabel = nullptr;
};

struct TransferOperationDialog {
    QDialog dialog;
    QLabel* statusChip = nullptr;
    QLabel* detailLabel = nullptr;
    QLabel* metricLabel = nullptr;
    QLabel* hintLabel = nullptr;
    QProgressBar* progressBar = nullptr;
    QPushButton* cancelButton = nullptr;

    explicit TransferOperationDialog(QWidget* parent)
        : dialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint) {}
};

int firstEnabledListRow(const QListWidget* listWidget) {
    if (!listWidget) {
        return -1;
    }
    for (int i = 0; i < listWidget->count(); ++i) {
        const QListWidgetItem* item = listWidget->item(i);
        if (item && item->flags().testFlag(Qt::ItemIsEnabled)) {
            return i;
        }
    }
    return -1;
}

void selectPreferredListRow(QListWidget* listWidget, int preferredRow = -1) {
    if (!listWidget || listWidget->count() <= 0) {
        return;
    }
    if (preferredRow >= 0 && preferredRow < listWidget->count()) {
        QListWidgetItem* preferredItem = listWidget->item(preferredRow);
        if (preferredItem && preferredItem->flags().testFlag(Qt::ItemIsEnabled)) {
            listWidget->setCurrentRow(preferredRow);
            return;
        }
    }
    const int fallbackRow = firstEnabledListRow(listWidget);
    if (fallbackRow >= 0) {
        listWidget->setCurrentRow(fallbackRow);
    } else {
        listWidget->setCurrentRow(0);
    }
}

void addWorkspaceEmptyStateItem(QListWidget* listWidget,
                                const QString& title,
                                const QString& detail,
                                const QString& preview = QString()) {
    if (!listWidget) {
        return;
    }

    QListWidgetItem* emptyItem = new QListWidgetItem(QStringLiteral("%1\n%2").arg(title, detail), listWidget);
    emptyItem->setFlags(Qt::NoItemFlags);
    emptyItem->setToolTip(preview.trimmed().isEmpty() ? QStringLiteral("%1\n%2").arg(title, detail) : preview);
    emptyItem->setSizeHint(QSize(0, 78));
    emptyItem->setForeground(QColor(100, 116, 139));
}

bool hasEnabledListRow(const QListWidget* listWidget) {
    return firstEnabledListRow(listWidget) >= 0;
}

struct GroupWorkspaceResult {
    bool applied = false;
    QString primaryValue;
    QString secondaryValue;
    QString selectedId;
};

WorkspaceSectionCard createWorkspaceSectionCard(QWidget* parent,
                                                const QString& title,
                                                const QString& hint = QString()) {
    WorkspaceSectionCard card;
    card.frame = new QFrame(parent);
    card.frame->setObjectName(QStringLiteral("workspaceSectionCard"));

    QVBoxLayout* frameLayout = new QVBoxLayout(card.frame);
    frameLayout->setContentsMargins(16, 14, 16, 14);
    frameLayout->setSpacing(10);

    QLabel* titleLabel = new QLabel(title, card.frame);
    titleLabel->setObjectName(QStringLiteral("workspaceSectionTitle"));
    frameLayout->addWidget(titleLabel);

    if (!hint.trimmed().isEmpty()) {
        QLabel* hintLabel = new QLabel(hint, card.frame);
        hintLabel->setObjectName(QStringLiteral("workspaceSectionHint"));
        hintLabel->setWordWrap(true);
        frameLayout->addWidget(hintLabel);
    }

    card.contentLayout = new QVBoxLayout;
    card.contentLayout->setContentsMargins(0, 0, 0, 0);
    card.contentLayout->setSpacing(8);
    frameLayout->addLayout(card.contentLayout);
    return card;
}

QHBoxLayout* createWorkspaceButtonRow(const QList<QWidget*>& buttons,
                                      QWidget* trailingWidget = nullptr) {
    QHBoxLayout* rowLayout = new QHBoxLayout;
    rowLayout->setSpacing(8);
    for (QWidget* button : buttons) {
        if (button) {
            rowLayout->addWidget(button);
        }
    }
    rowLayout->addStretch();
    if (trailingWidget) {
        rowLayout->addWidget(trailingWidget);
    }
    return rowLayout;
}

void addWorkspaceSectionCard(QVBoxLayout* parentLayout,
                             QWidget* parent,
                             const QString& title,
                             const QString& hint,
                             const QList<QWidget*>& buttons,
                             QWidget* trailingWidget = nullptr) {
    if (!parentLayout || !parent) {
        return;
    }
    WorkspaceSectionCard card = createWorkspaceSectionCard(parent, title, hint);
    card.contentLayout->addLayout(createWorkspaceButtonRow(buttons, trailingWidget));
    parentLayout->addWidget(card.frame);
}

WorkspaceDialogShell createWorkspaceDialogShell(QDialog& dialog,
                                                const QString& dialogObjectName,
                                                const QString& windowTitle,
                                                const QSize& dialogSize,
                                                const QString& titleObjectName,
                                                const QString& titleText,
                                                const QString& subTitleObjectName,
                                                const QString& subTitleText,
                                                const QString& searchObjectName,
                                                const QString& searchPlaceholder,
                                                const QString& searchToolTip,
                                                const QString& listObjectName,
                                                bool listWordWrap,
                                                const QString& hintObjectName,
                                                const QString& hintText,
                                                const QString& statsObjectName,
                                                const QString& previewObjectName,
                                                const QString& previewText,
                                                const QString& headerObjectName = QStringLiteral("managerHeader"),
                                                const QString& bodyObjectName = QStringLiteral("managerBody")) {
    WorkspaceDialogShell shell;
    dialog.setObjectName(dialogObjectName);
    dialog.setWindowTitle(windowTitle);
    dialog.setFixedSize(dialogSize);

    shell.rootLayout = new QVBoxLayout(&dialog);
    shell.rootLayout->setContentsMargins(0, 0, 0, 0);
    shell.rootLayout->setSpacing(0);

    shell.headerFrame = new QFrame(&dialog);
    shell.headerFrame->setObjectName(headerObjectName);
    shell.headerLayout = new QVBoxLayout(shell.headerFrame);
    shell.headerLayout->setContentsMargins(24, 18, 24, 14);
    shell.headerLayout->setSpacing(8);

    shell.titleLabel = new QLabel(titleText, shell.headerFrame);
    shell.titleLabel->setObjectName(titleObjectName);
    shell.headerLayout->addWidget(shell.titleLabel);

    shell.subTitleLabel = new QLabel(subTitleText, shell.headerFrame);
    shell.subTitleLabel->setObjectName(subTitleObjectName);
    shell.subTitleLabel->setVisible(!subTitleText.trimmed().isEmpty());
    shell.headerLayout->addWidget(shell.subTitleLabel);
    shell.rootLayout->addWidget(shell.headerFrame);

    shell.bodyFrame = new QFrame(&dialog);
    shell.bodyFrame->setObjectName(bodyObjectName);
    shell.bodyLayout = new QVBoxLayout(shell.bodyFrame);
    shell.bodyLayout->setContentsMargins(24, 22, 24, 22);
    shell.bodyLayout->setSpacing(12);

    shell.searchRowLayout = new QHBoxLayout;
    shell.searchRowLayout->setSpacing(8);
    shell.searchEdit = new QLineEdit(shell.bodyFrame);
    shell.searchEdit->setObjectName(searchObjectName);
    shell.searchEdit->setPlaceholderText(searchPlaceholder);
    shell.searchEdit->setClearButtonEnabled(true);
    shell.searchEdit->setToolTip(searchToolTip);
    shell.searchRowLayout->addWidget(shell.searchEdit, 1);
    shell.bodyLayout->addLayout(shell.searchRowLayout);

    shell.listWidget = new QListWidget(shell.bodyFrame);
    shell.listWidget->setObjectName(listObjectName);
    shell.listWidget->setWordWrap(listWordWrap);
    shell.bodyLayout->addWidget(shell.listWidget, 1);

    shell.summaryRowLayout = new QHBoxLayout;
    shell.summaryRowLayout->setSpacing(10);
    shell.hintLabel = new QLabel(hintText, shell.bodyFrame);
    shell.hintLabel->setObjectName(hintObjectName);
    shell.statsLabel = new QLabel(shell.bodyFrame);
    shell.statsLabel->setObjectName(statsObjectName);
    shell.previewLabel = new QLabel(previewText, shell.bodyFrame);
    shell.previewLabel->setObjectName(previewObjectName);
    shell.previewLabel->setWordWrap(true);
    shell.summaryRowLayout->addWidget(shell.hintLabel);
    shell.summaryRowLayout->addWidget(shell.statsLabel);
    shell.summaryRowLayout->addWidget(shell.previewLabel, 1);
    shell.bodyLayout->addLayout(shell.summaryRowLayout);

    shell.rootLayout->addWidget(shell.bodyFrame);
    return shell;
}

void moveWorkspaceShellStatsToHeader(WorkspaceDialogShell& shell) {
    if (!shell.headerLayout || !shell.titleLabel || !shell.statsLabel || !shell.summaryRowLayout) {
        return;
    }
    shell.summaryRowLayout->removeWidget(shell.statsLabel);
    shell.headerLayout->removeWidget(shell.titleLabel);

    QHBoxLayout* titleRowLayout = new QHBoxLayout;
    titleRowLayout->setContentsMargins(0, 0, 0, 0);
    titleRowLayout->setSpacing(10);
    titleRowLayout->addWidget(shell.titleLabel);
    titleRowLayout->addStretch();
    titleRowLayout->addWidget(shell.statsLabel);
    shell.headerLayout->insertLayout(0, titleRowLayout);
    shell.statsLabel->setVisible(true);
}

QPushButton* createWorkspaceButton(QWidget* parent,
                                   QWidget* iconWidget,
                                   const QString& text,
                                   const QString& objectName,
                                   const QString& toolTip,
                                   QStyle::StandardPixmap iconType) {
    QPushButton* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setToolTip(toolTip);
    assignButtonIcon(button, iconWidget, iconType);
    return button;
}

QPushButton* createWorkspaceButton(QWidget* parent,
                                   QWidget* iconWidget,
                                   const NoticeButtonSpec& spec,
                                   QStyle::StandardPixmap iconType) {
    return createWorkspaceButton(parent,
                                 iconWidget,
                                 spec.text,
                                 spec.objectName,
                                 spec.toolTip,
                                 iconType);
}

void setupTransferOperationDialog(TransferOperationDialog& chrome,
                                  QWidget* iconWidget,
                                  const QString& dialogObjectName,
                                  const QString& windowTitle,
                                  const QString& title,
                                  const QString& subTitle,
                                  const QString& hintText,
                                  const QString& cancelText) {
    chrome.dialog.setObjectName(dialogObjectName);
    chrome.dialog.setWindowTitle(windowTitle);
    chrome.dialog.setFixedSize(QSize(560, 340));
    chrome.dialog.setWindowModality(Qt::ApplicationModal);
    chrome.dialog.setModal(true);
    chrome.dialog.setStyleSheet(productDialogStyleSheet());

    QVBoxLayout* rootLayout = new QVBoxLayout(&chrome.dialog);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QFrame* headerFrame = new QFrame(&chrome.dialog);
    headerFrame->setObjectName(QStringLiteral("operationHeader"));
    QVBoxLayout* headerLayout = new QVBoxLayout(headerFrame);
    headerLayout->setContentsMargins(24, 18, 24, 14);
    headerLayout->setSpacing(8);

    QHBoxLayout* titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(10);
    QLabel* titleLabel = new QLabel(title, headerFrame);
    titleLabel->setObjectName(QStringLiteral("operationTitle"));
    chrome.statusChip = new QLabel(QStringLiteral("准备中"), headerFrame);
    chrome.statusChip->setObjectName(QStringLiteral("operationStatusChip"));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    titleRow->addWidget(chrome.statusChip);
    headerLayout->addLayout(titleRow);

    QLabel* subTitleLabel = new QLabel(subTitle, headerFrame);
    subTitleLabel->setObjectName(QStringLiteral("operationSubTitle"));
    subTitleLabel->setWordWrap(true);
    headerLayout->addWidget(subTitleLabel);
    rootLayout->addWidget(headerFrame);

    QFrame* bodyFrame = new QFrame(&chrome.dialog);
    bodyFrame->setObjectName(QStringLiteral("operationBody"));
    QVBoxLayout* bodyLayout = new QVBoxLayout(bodyFrame);
    bodyLayout->setContentsMargins(24, 22, 24, 22);
    bodyLayout->setSpacing(12);

    WorkspaceSectionCard progressCard = createWorkspaceSectionCard(bodyFrame,
                                                                   QStringLiteral("当前阶段"),
                                                                   QStringLiteral("进度、取消状态和最终结果会同时同步到主窗口文件工作区。"));
    QHBoxLayout* metricRow = new QHBoxLayout;
    metricRow->setContentsMargins(0, 0, 0, 0);
    metricRow->setSpacing(10);
    chrome.metricLabel = new QLabel(QStringLiteral("0% · 等待开始"), progressCard.frame);
    chrome.metricLabel->setObjectName(QStringLiteral("operationMetricLabel"));
    metricRow->addWidget(chrome.metricLabel);
    metricRow->addStretch();
    progressCard.contentLayout->addLayout(metricRow);

    chrome.detailLabel = new QLabel(QStringLiteral("正在准备当前操作。"), progressCard.frame);
    chrome.detailLabel->setObjectName(QStringLiteral("operationDetailLabel"));
    chrome.detailLabel->setWordWrap(true);
    progressCard.contentLayout->addWidget(chrome.detailLabel);

    chrome.progressBar = new QProgressBar(progressCard.frame);
    chrome.progressBar->setObjectName(QStringLiteral("operationProgressBar"));
    chrome.progressBar->setRange(0, 100);
    chrome.progressBar->setValue(0);
    chrome.progressBar->setTextVisible(true);
    progressCard.contentLayout->addWidget(chrome.progressBar);
    bodyLayout->addWidget(progressCard.frame);

    WorkspaceSectionCard actionCard = createWorkspaceSectionCard(bodyFrame,
                                                                 QStringLiteral("取消与恢复"),
                                                                 hintText);
    chrome.hintLabel = new QLabel(QStringLiteral("若需要中止当前动作，可从这里直接取消；系统会保留最近状态，方便后续判断是否重试或恢复。"),
                                  actionCard.frame);
    chrome.hintLabel->setObjectName(QStringLiteral("operationHintLabel"));
    chrome.hintLabel->setWordWrap(true);
    actionCard.contentLayout->addWidget(chrome.hintLabel);
    chrome.cancelButton = createWorkspaceButton(actionCard.frame,
                                                iconWidget,
                                                cancelText,
                                                QStringLiteral("managerSecondaryBtn"),
                                                QStringLiteral("取消当前传输操作"),
                                                QStyle::SP_DialogCancelButton);
    actionCard.contentLayout->addLayout(createWorkspaceButtonRow({chrome.cancelButton}));
    bodyLayout->addWidget(actionCard.frame);
    bodyLayout->addStretch();

    rootLayout->addWidget(bodyFrame);
    applyToneProperty(chrome.statusChip, QStringLiteral("accent"));
}

void updateTransferOperationDialog(TransferOperationDialog& chrome,
                                   const QString& phaseText,
                                   const QString& detailText,
                                   int percent,
                                   const QString& metricText,
                                   const QString& tone) {
    if (chrome.statusChip) {
        chrome.statusChip->setText(phaseText);
        applyToneProperty(chrome.statusChip, tone);
    }
    if (chrome.detailLabel) {
        chrome.detailLabel->setText(detailText);
    }
    if (chrome.metricLabel) {
        chrome.metricLabel->setText(metricText);
    }
    if (chrome.progressBar) {
        const int boundedPercent = qMax(0, qMin(percent, 100));
        chrome.progressBar->setValue(boundedPercent);
        chrome.progressBar->setFormat(QStringLiteral("%1%").arg(boundedPercent));
    }
    QApplication::processEvents();
}

QString selectedFriendManagerEntryId(QListWidget* friendList) {
    if (!friendList || !friendList->currentItem()) return QString();
    return friendList->currentItem()->data(Qt::UserRole).toString();
}

bool trySelectedValidFriendId(QListWidget* friendList,
                              QStatusBar* statusBar,
                              const QString& emptyMessage,
                              QString* friendId) {
    const QString selectedId = selectedFriendManagerEntryId(friendList);
    if (selectedId.isEmpty()) {
        if (statusBar) statusBar->showMessage(emptyMessage, 1800);
        return false;
    }
    if (selectedId.startsWith("search_add:")) {
        if (statusBar) statusBar->showMessage("请先选择有效好友，或直接搜索并申请", 2200);
        return false;
    }
    if (friendId) {
        *friendId = selectedId;
    }
    return true;
}

QString selectedFriendManagerTargetId(QListWidget* friendList) {
    QString selectedId = selectedFriendManagerEntryId(friendList);
    if (selectedId.startsWith("search_add:")) {
        selectedId = selectedId.mid(QString("search_add:").size());
    }
    return selectedId;
}

QStringList visibleFriendManagerIds(QListWidget* friendList) {
    QStringList ids;
    if (!friendList) return ids;
    for (int i = 0; i < friendList->count(); ++i) {
        QListWidgetItem* item = friendList->item(i);
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        if (id.isEmpty() || id.startsWith("search_add:") || ids.contains(id)) continue;
        ids << id;
    }
    return ids;
}

QString selectedFriendNoticeEntryId(QListWidget* noticeList) {
    if (!noticeList || !noticeList->currentItem()) return QString();
    return noticeList->currentItem()->data(Qt::UserRole).toString();
}

QString selectedFriendNoticeTargetId(QListWidget* noticeList, QLineEdit* searchEdit = nullptr) {
    return NotificationPanelManager::friendNoticeTargetId(
        selectedFriendNoticeEntryId(noticeList),
        searchEdit ? searchEdit->text() : QString());
}

bool trySelectedRealFriendNoticeId(QListWidget* noticeList,
                                   QStatusBar* statusBar,
                                   const QString& emptyMessage,
                                   const QString& searchAddMessage,
                                   QString* requestId) {
    const QString currentId = selectedFriendNoticeEntryId(noticeList);
    if (currentId.isEmpty()) {
        if (statusBar) statusBar->showMessage(emptyMessage, 1800);
        return false;
    }
    if (!NotificationPanelManager::isRealFriendNoticeRequestId(currentId)) {
        if (statusBar) statusBar->showMessage(searchAddMessage, 2200);
        return false;
    }
    if (requestId) {
        *requestId = currentId;
    }
    return true;
}

QStringList visibleFriendNoticeIds(QListWidget* noticeList) {
    QStringList entryIds;
    if (!noticeList) return entryIds;
    for (int i = 0; i < noticeList->count(); ++i) {
        QListWidgetItem* item = noticeList->item(i);
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        if (id.isEmpty()) continue;
        entryIds << id;
    }
    return NotificationPanelManager::visibleFriendNoticeTargetIds(entryIds);
}

GroupWorkspaceResult runGroupFormWorkspaceDialog(QWidget* parent,
                                                 const QString& dialogObjectName,
                                                 const QString& windowTitle,
                                                 const QString& titleText,
                                                 const QString& subTitleText,
                                                 const QString& searchPlaceholder,
                                                 const QString& searchToolTip,
                                                 const QString& guideText,
                                                 const QString& previewPlaceholder,
                                                 const QString& statsText,
                                                 const QString& primaryLabel,
                                                 const QString& secondaryLabel,
                                                 const QString& listSectionTitle,
                                                 const QString& listSectionHint,
                                                 const QString& formSectionTitle,
                                                 const QString& formSectionHint,
                                                 const QString& primaryFieldLabelText,
                                                 const QString& primaryFieldPlaceholder,
                                                 const QString& secondaryFieldLabelText,
                                                 const QString& secondaryFieldPlaceholder,
                                                 const QString& submitButtonText,
                                                 const QString& submitButtonTip,
                                                 const QString& closeButtonText,
                                                 const QString& closeButtonTip,
                                                 const QList<QPair<QString, QString>>& rows,
                                                 bool primaryMultiline,
                                                 bool secondaryMultiline,
                                                 const QString& initialPrimaryValue = QString(),
                                                 const QString& initialSecondaryValue = QString(),
                                                 const QString& initialSelectedId = QString()) {
    GroupWorkspaceResult result;
    QDialog dialog(parent);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        dialogObjectName,
        windowTitle,
        QSize(920, 760),
        QStringLiteral("managerTitle"),
        titleText,
        QStringLiteral("managerSubTitle"),
        subTitleText,
        QStringLiteral("managerSearch"),
        searchPlaceholder,
        searchToolTip,
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        guideText,
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        previewPlaceholder,
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(26, 18, 26, 16);
    shell.bodyLayout->setContentsMargins(24, 22, 24, 22);
    shell.bodyLayout->setSpacing(12);
    shell.statsLabel->setText(statsText);
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* listWidget = shell.listWidget;
    QLabel* hintLabel = shell.hintLabel;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;

    auto rowPreviewText = [](const QString& line) {
        const QStringList parts = line.split(QLatin1Char('\n'));
        return parts.isEmpty() ? line : parts.first();
    };
    const QString primaryFieldName = !primaryLabel.trimmed().isEmpty()
        ? primaryLabel.trimmed()
        : (!primaryFieldLabelText.trimmed().isEmpty() ? primaryFieldLabelText.trimmed() : QStringLiteral("主要输入"));
    const QString secondaryFieldName = !secondaryLabel.trimmed().isEmpty()
        ? secondaryLabel.trimmed()
        : (!secondaryFieldLabelText.trimmed().isEmpty() ? secondaryFieldLabelText.trimmed() : QStringLiteral("补充说明"));

    auto emptyPreviewText = [=]() {
        const QString filter = searchEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的参考项。可直接在右侧表单继续填写，或稍后切换到更合适的工作区上下文。")
            : QStringLiteral("当前筛选词“%1”没有匹配到参考项。\n可调整关键词，或直接在右侧表单继续填写。").arg(filter);
    };

    auto fillList = [=]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const auto& row : rows) {
            if (!filter.isEmpty()
                && !row.first.contains(filter, Qt::CaseInsensitive)
                && !row.second.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(row.second);
            item->setData(Qt::UserRole, row.first);
            item->setToolTip(row.second);
            item->setSizeHint(QSize(0, row.second.contains(QLatin1Char('\n')) ? 72 : 62));
            listWidget->addItem(item);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                listWidget,
                QStringLiteral("没有匹配的参考项"),
                QStringLiteral("试试调整关键词，或直接在右侧表单继续填写。"),
                emptyPreviewText());
        }
        if (listWidget->count() > 0) {
            int rowIndex = 0;
            if (!initialSelectedId.isEmpty()) {
                for (int i = 0; i < listWidget->count(); ++i) {
                    QListWidgetItem* item = listWidget->item(i);
                    if (item && item->data(Qt::UserRole).toString() == initialSelectedId) {
                        rowIndex = i;
                        break;
                    }
                }
            }
            selectPreferredListRow(listWidget, rowIndex);
        }
        statsLabel->setText(QStringLiteral("%1 · 可见 %2 / %3 项").arg(statsText).arg(visibleCount).arg(rows.size()));
    };

    QLabel* primaryFieldLabel = new QLabel(primaryFieldLabelText, shell.bodyFrame);
    primaryFieldLabel->setObjectName(QStringLiteral("workspaceSectionHint"));
    primaryFieldLabel->setVisible(!primaryFieldLabelText.trimmed().isEmpty());
    QWidget* primaryInput = nullptr;
    QLineEdit* primaryLineEdit = nullptr;
    QTextEdit* primaryTextEdit = nullptr;
    if (primaryMultiline) {
        primaryTextEdit = new QTextEdit(shell.bodyFrame);
        primaryTextEdit->setObjectName(QStringLiteral("messageEdit"));
        primaryTextEdit->setPlaceholderText(primaryFieldPlaceholder);
        primaryTextEdit->setPlainText(initialPrimaryValue);
        primaryTextEdit->setMinimumHeight(120);
        primaryInput = primaryTextEdit;
    } else {
        primaryLineEdit = new QLineEdit(shell.bodyFrame);
        primaryLineEdit->setObjectName(QStringLiteral("managerSearch"));
        primaryLineEdit->setPlaceholderText(primaryFieldPlaceholder);
        primaryLineEdit->setText(initialPrimaryValue);
        primaryInput = primaryLineEdit;
    }

    QLabel* secondaryFieldLabel = new QLabel(secondaryFieldLabelText, shell.bodyFrame);
    secondaryFieldLabel->setObjectName(QStringLiteral("workspaceSectionHint"));
    secondaryFieldLabel->setVisible(!secondaryFieldLabelText.trimmed().isEmpty());
    QWidget* secondaryInput = nullptr;
    QLineEdit* secondaryLineEdit = nullptr;
    QTextEdit* secondaryTextEdit = nullptr;
    if (!secondaryFieldLabelText.trimmed().isEmpty() || !secondaryFieldPlaceholder.trimmed().isEmpty() || !initialSecondaryValue.trimmed().isEmpty()) {
        if (secondaryMultiline) {
            secondaryTextEdit = new QTextEdit(shell.bodyFrame);
            secondaryTextEdit->setObjectName(QStringLiteral("messageEdit"));
            secondaryTextEdit->setPlaceholderText(secondaryFieldPlaceholder);
            secondaryTextEdit->setPlainText(initialSecondaryValue);
            secondaryTextEdit->setMinimumHeight(88);
            secondaryInput = secondaryTextEdit;
        } else {
            secondaryLineEdit = new QLineEdit(shell.bodyFrame);
            secondaryLineEdit->setObjectName(QStringLiteral("managerSearch"));
            secondaryLineEdit->setPlaceholderText(secondaryFieldPlaceholder);
            secondaryLineEdit->setText(initialSecondaryValue);
            secondaryInput = secondaryLineEdit;
        }
    }

    QPushButton* useSelectedBtn = createWorkspaceButton(shell.bodyFrame,
                                                        &dialog,
                                                        QStringLiteral("带入选中项"),
                                                        QStringLiteral("managerSecondaryBtn"),
                                                        QStringLiteral("将当前选中项的主信息带入输入区域，便于继续编辑"),
                                                        QStyle::SP_ArrowDown);
    QPushButton* clearInputBtn = createWorkspaceButton(shell.bodyFrame,
                                                       &dialog,
                                                       QStringLiteral("清空输入"),
                                                       QStringLiteral("managerSecondaryBtn"),
                                                       QStringLiteral("清空当前表单输入"),
                                                       QStyle::SP_DialogResetButton);
    QPushButton* submitBtn = createWorkspaceButton(shell.bodyFrame,
                                                   &dialog,
                                                   submitButtonText,
                                                   QStringLiteral("managerPrimaryBtn"),
                                                   submitButtonTip,
                                                   QStyle::SP_DialogApplyButton);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame,
                                                  &dialog,
                                                  closeButtonText,
                                                  QStringLiteral("managerSecondaryBtn"),
                                                  closeButtonTip,
                                                  QStyle::SP_DialogCloseButton);

    shell.bodyLayout->removeWidget(listWidget);

    WorkspaceSectionCard listCard = createWorkspaceSectionCard(shell.bodyFrame, listSectionTitle, listSectionHint);
    listCard.contentLayout->addWidget(listWidget);
    shell.bodyLayout->insertWidget(1, listCard.frame, 1);

    WorkspaceSectionCard formCard = createWorkspaceSectionCard(shell.bodyFrame, formSectionTitle, formSectionHint);
    if (primaryFieldLabel->isVisible()) {
        formCard.contentLayout->addWidget(primaryFieldLabel);
    }
    if (primaryInput) {
        formCard.contentLayout->addWidget(primaryInput);
    }
    if (secondaryFieldLabel->isVisible()) {
        formCard.contentLayout->addWidget(secondaryFieldLabel);
    }
    if (secondaryInput) {
        formCard.contentLayout->addWidget(secondaryInput);
    }
    formCard.contentLayout->addLayout(createWorkspaceButtonRow({useSelectedBtn, clearInputBtn, submitBtn}, closeBtn));
    shell.bodyLayout->addWidget(formCard.frame);

    auto currentPrimaryValue = [=]() {
        return primaryTextEdit ? primaryTextEdit->toPlainText().trimmed()
                               : (primaryLineEdit ? primaryLineEdit->text().trimmed() : QString());
    };
    auto currentSecondaryValue = [=]() {
        if (!secondaryInput) {
            return QString();
        }
        return secondaryTextEdit ? secondaryTextEdit->toPlainText().trimmed()
                                 : (secondaryLineEdit ? secondaryLineEdit->text().trimmed() : QString());
    };
    auto setPrimaryValue = [=](const QString& value) {
        if (primaryTextEdit) {
            primaryTextEdit->setPlainText(value);
        } else if (primaryLineEdit) {
            primaryLineEdit->setText(value);
        }
    };
    auto setSecondaryValue = [=](const QString& value) {
        if (secondaryTextEdit) {
            secondaryTextEdit->setPlainText(value);
        } else if (secondaryLineEdit) {
            secondaryLineEdit->setText(value);
        }
    };
    auto selectedItem = [listWidget]() {
        return listWidget->currentItem();
    };
    auto currentSelectedId = [selectedItem]() {
        QListWidgetItem* item = selectedItem();
        return item ? item->data(Qt::UserRole).toString().trimmed() : QString();
    };
    auto updatePreview = [=]() {
        QListWidgetItem* item = selectedItem();
        if (!item) {
            previewLabel->setText(hasEnabledListRow(listWidget)
                                      ? (currentPrimaryValue().isEmpty()
                                             ? previewPlaceholder
                                             : QStringLiteral("%1：%2\n可继续带入参考项、补充说明，或直接提交当前表单。")
                                                   .arg(primaryFieldName, currentPrimaryValue()))
                                      : emptyPreviewText());
            return;
        }
        const QString currentInput = currentPrimaryValue().isEmpty() ? QStringLiteral("未填写") : currentPrimaryValue();
        const QString secondaryInputText = currentSecondaryValue().isEmpty() ? QStringLiteral("未填写") : currentSecondaryValue();
        previewLabel->setText(QStringLiteral("%1\n%2：%3\n%4：%5")
                                  .arg(item->toolTip(),
                                       primaryFieldName,
                                       currentInput,
                                       secondaryFieldName,
                                       secondaryInputText));
    };
    auto updateActionState = [=]() {
        const bool hasSelection = !currentSelectedId().isEmpty();
        const bool hasPrimary = !currentPrimaryValue().isEmpty();
        const bool hasSecondary = !currentSecondaryValue().isEmpty();
        const bool hasInput = hasPrimary || hasSecondary;
        const bool hasVisibleReference = hasEnabledListRow(listWidget);
        const bool canSubmit = hasPrimary || hasSelection;

        useSelectedBtn->setEnabled(hasSelection);
        useSelectedBtn->setToolTip(hasSelection
                                       ? QStringLiteral("将当前选中项带入输入区域，便于继续编辑")
                                       : QStringLiteral("请先从左侧选择一个参考项"));
        clearInputBtn->setEnabled(hasInput);
        clearInputBtn->setToolTip(hasInput
                                      ? QStringLiteral("清空当前表单输入")
                                      : QStringLiteral("当前没有需要清空的表单输入"));
        submitBtn->setEnabled(canSubmit);
        submitBtn->setToolTip(canSubmit
                                  ? submitButtonTip
                                  : QStringLiteral("请先填写主要输入，或从左侧选择一个参考项"));
        hintLabel->setText(!hasVisibleReference
                               ? emptyPreviewText()
                               : (hasPrimary
                                      ? QStringLiteral("当前表单已准备好，可直接提交，或继续从左侧带入参考项补充上下文。")
                                      : (hasSelection
                                             ? QStringLiteral("当前已选中参考项，可带入输入区后提交，或直接继续手动填写。")
                                             : guideText)));
    };

    QObject::connect(searchEdit, &QLineEdit::textChanged, &dialog, [=](const QString&) {
        fillList();
        updatePreview();
        updateActionState();
    });
    QObject::connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    QObject::connect(useSelectedBtn, &QPushButton::clicked, &dialog, [=]() {
        QListWidgetItem* item = selectedItem();
        if (!item) {
            return;
        }
        setPrimaryValue(item->data(Qt::UserRole).toString());
        if (secondaryInput && currentSecondaryValue().isEmpty()) {
            setSecondaryValue(rowPreviewText(item->toolTip()));
        }
        updatePreview();
        updateActionState();
    });
    QObject::connect(clearInputBtn, &QPushButton::clicked, &dialog, [=]() {
        setPrimaryValue(QString());
        setSecondaryValue(QString());
        updatePreview();
        updateActionState();
    });
    QObject::connect(submitBtn, &QPushButton::clicked, &dialog, [&]() {
        result.primaryValue = currentPrimaryValue();
        result.secondaryValue = currentSecondaryValue();
        QListWidgetItem* item = selectedItem();
        result.selectedId = item ? item->data(Qt::UserRole).toString() : initialSelectedId;
        result.applied = true;
        dialog.accept();
    });
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (primaryLineEdit) {
        QObject::connect(primaryLineEdit, &QLineEdit::returnPressed, &dialog, [&]() { submitBtn->click(); });
        QObject::connect(primaryLineEdit, &QLineEdit::textChanged, &dialog, [=](const QString&) {
            updatePreview();
            updateActionState();
        });
    }
    if (primaryTextEdit) {
        QObject::connect(primaryTextEdit, &QTextEdit::textChanged, &dialog, [=]() {
            updatePreview();
            updateActionState();
        });
    }
    if (secondaryLineEdit) {
        QObject::connect(secondaryLineEdit, &QLineEdit::textChanged, &dialog, [=](const QString&) {
            updatePreview();
            updateActionState();
        });
    }
    if (secondaryTextEdit) {
        QObject::connect(secondaryTextEdit, &QTextEdit::textChanged, &dialog, [=]() {
            updatePreview();
            updateActionState();
        });
    }

    fillList();
    updatePreview();
    updateActionState();
    dialog.setStyleSheet(productDialogStyleSheet());
    if (primaryLineEdit) {
        primaryLineEdit->setFocus();
        primaryLineEdit->selectAll();
    } else if (primaryTextEdit) {
        primaryTextEdit->setFocus();
    }
    dialog.exec();
    return result;
}

QString selectedGroupNoticeEntryId(QListWidget* noticeList) {
    if (!noticeList || !noticeList->currentItem()) return QString();
    return noticeList->currentItem()->data(Qt::UserRole).toString();
}

bool isGroupCreateEntryId(const QString& groupId) {
    return NotificationPanelManager::isGroupCreateEntryId(groupId);
}

QString groupCreateEntryName(const QString& groupId) {
    return NotificationPanelManager::groupCreateEntryName(groupId);
}

bool trySelectedInspectableGroupNoticeId(QListWidget* noticeList,
                                         QStatusBar* statusBar,
                                         const QString& emptyMessage,
                                         const QString& createMessage,
                                         QString* groupId) {
    if (!noticeList || !noticeList->currentItem()) {
        if (statusBar) statusBar->showMessage(emptyMessage, 1800);
        return false;
    }
    const QString currentId = selectedGroupNoticeEntryId(noticeList);
    if (!NotificationPanelManager::isInspectableGroupNoticeId(currentId)) {
        if (statusBar) statusBar->showMessage(createMessage, 2200);
        return false;
    }
    if (groupId) {
        *groupId = currentId;
    }
    return true;
}

QStringList visibleGroupNoticeIds(QListWidget* noticeList) {
    QStringList entryIds;
    if (!noticeList) return entryIds;
    for (int i = 0; i < noticeList->count(); ++i) {
        QListWidgetItem* item = noticeList->item(i);
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        entryIds << id;
    }
    return NotificationPanelManager::uniqueGroupNoticeEntryIds(entryIds);
}

QString serverGroupAuditActionText(const QString& action) {
    const QString normalized = action.trimmed().toLower();
    if (normalized == QLatin1String("announcement_update")) return QStringLiteral("更新公告");
    if (normalized == QLatin1String("add")) return QStringLiteral("加入成员");
    if (normalized == QLatin1String("remove")) return QStringLiteral("移出成员");
    if (normalized == QLatin1String("promote_admin")) return QStringLiteral("设为管理员");
    if (normalized == QLatin1String("demote_admin")) return QStringLiteral("取消管理员");
    return normalized.isEmpty() ? QStringLiteral("群操作") : normalized;
}

QString serverGroupAuditSummary(const QJsonObject& event) {
    const QString action = serverGroupAuditActionText(event.value("action").toString());
    const QString actorName = event.value("actorName").toString().trimmed();
    const QString actorId = event.value("actorId").toString().trimmed();
    const QString targetName = event.value("targetUserName").toString().trimmed();
    const QString targetId = event.value("targetUserId").toString().trimmed();
    const QString createdAt = event.value("createdAt").toString().trimmed();
    const QString actor = actorName.isEmpty() ? actorId : QString("%1(%2)").arg(actorName, actorId);
    const QString target = targetId.isEmpty()
        ? QString()
        : (targetName.isEmpty() ? targetId : QString("%1(%2)").arg(targetName, targetId));
    const QString timePart = createdAt.isEmpty() ? QString() : QString(" · %1").arg(createdAt);
    return target.isEmpty()
        ? QString("%1 · %2%3").arg(action, actor, timePart)
        : QString("%1 · %2 -> %3%4").arg(action, actor, target, timePart);
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

QPixmap initialAvatarPixmap(const QString& displayName, int side) {
    if (side <= 0) return QPixmap();
    const QString seed = displayName.trimmed().isEmpty() ? QStringLiteral("?") : displayName.trimmed();
    const uint hue = qHash(seed) % 360;

    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor base = QColor::fromHsv(static_cast<int>(hue), 110, 220);
    QLinearGradient gradient(0, 0, side, side);
    gradient.setColorAt(0.0, base.lighter(126));
    gradient.setColorAt(1.0, base.darker(112));
    painter.setPen(QPen(QColor(255, 255, 255, 190), qMax(1, side / 18)));
    painter.setBrush(gradient);
    painter.drawEllipse(QRectF(1, 1, side - 2, side - 2));

    QFont font = painter.font();
    font.setFamily(QStringLiteral("Microsoft YaHei"));
    font.setBold(true);
    font.setPixelSize(qMax(12, side / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, side, side), Qt::AlignCenter, seed.left(1).toUpper());
    return pixmap;
}

QString appWindowTitle(const QString& suffix = QString()) {
    return WindowStateManager::appWindowTitle(QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING), suffix);
}
}

MainWindow::MainWindow(Client* client, const QString& userId, const QString& userName, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_client(client)
    , m_clientStorage(userName)
    , m_userListModel(new QStandardItemModel(this))
    , m_chatModel(new QStandardItemModel(this))
    , m_groupMemberModel(new QStandardItemModel(this))
    , m_historyService(userId)
    , m_currentUserId(userId)
    , m_currentUserName(userName)
    , m_hasServerGroupSnapshot(false)
    , m_wasInPublicServerGroup(false)
    , m_privateChatTarget(QString())
    , m_trayIcon(new QSystemTrayIcon(this))
    , m_resumeSavedTransferAction(nullptr)
    , m_clearSavedTransferAction(nullptr)
    , m_hasLastTransferRecoveryUiState(false)
    , m_hasLastTransferStatusEvent(false)
    , m_hasTransferWorkspaceSendState(false)
    , m_hasTransferWorkspaceSavedFileState(false)
    , m_unreadCount(0)
    , m_isQuitting(false)
{
    ui->setupUi(this);
    setMinimumSize(980, 680);
    setWindowIcon(createChatIcon(userName));
    setupUi();
    setupTray();

    if (m_client && !m_client->parent()) {
        m_client->setParent(this);
    }
    if (!m_client) {
        QMessageBox box(QMessageBox::Critical,
                        QStringLiteral("启动失败"),
                        QStringLiteral("客户端未初始化，当前窗口无法继续加载。"),
                        QMessageBox::Ok,
                        this);
        applyProductDialogChrome(&box);
        box.setIconPixmap(style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(28, 28));
        if (QAbstractButton* okButton = box.button(QMessageBox::Ok)) {
            okButton->setText(QStringLiteral("知道了"));
            okButton->setObjectName(QStringLiteral("managerPrimaryBtn"));
        }
        box.exec();
        close();
        return;
    }

    setWindowTitle(appWindowTitle(userName));
    ui->avatarLabel->setText(userName.left(1).toUpper());
    ui->profileNameLabel->setText(userName);
    ui->profileIdLabel->setText(QString("QQ %1").arg(userId));
    loadAvatar();
    ui->appTitleLabel->setText("Qt 聊天室");
    ui->chatTitleLabel->setText("公共聊天室");
    ui->chatHintLabel->setText(QString("公共会话工作区 · 当前账号 QQ %1 · 双击左侧成员即可切换私聊").arg(m_currentUserId));

    connect(m_client, &Client::connected, this, [this]() {
        appendSystemMessage("已连接服务器 · " + m_client->transportSecurityDescription());
        ui->statusbar->showMessage(m_client->transportSecurityDescription(), 2200);
        refreshComposerState();
        updateSavedOutgoingTransferRecoveryUi(true);
    });
    connect(m_client, &Client::disconnected, this, &MainWindow::onClientDisconnected);
    connect(m_client, &Client::newMessage, this, &MainWindow::onNewMessage);
    connect(m_client, &Client::userJoined, this, &MainWindow::onUserJoined);
    connect(m_client, &Client::userLeft, this, &MainWindow::onUserLeft);
    connect(m_client, &Client::userListUpdated, this, &MainWindow::onUserListUpdated);
    connect(m_client, &Client::connectionError, this, &MainWindow::onClientError);
    connect(m_client, &Client::friendRequestReceived, this, &MainWindow::onFriendRequestReceived);
    connect(m_client, &Client::friendSearchResult, this, &MainWindow::onFriendSearchResult);
    connect(m_client, &Client::friendRequestSent, this, &MainWindow::onFriendRequestSent);
    connect(m_client, &Client::friendResponseReceived, this, &MainWindow::onFriendResponseReceived);
    connect(m_client, &Client::serverGroupSnapshotReceived, this, &MainWindow::onServerGroupSnapshotReceived);
    connect(m_client, &Client::e2eSessionStateChanged, this, &MainWindow::onE2ESessionStateChanged);
    connect(m_client, &Client::e2eIdentityStateChanged, this, &MainWindow::onE2EIdentityStateChanged);
    connect(m_client, &Client::e2eSessionRotationRequested, this, &MainWindow::onE2ESessionRotationRequested);
    connect(m_client, &Client::e2eSessionRotationResponded, this, &MainWindow::onE2ESessionRotationResponded);
    connect(m_client, &Client::fileTransferStatusChanged, this, &MainWindow::onFileTransferStatusChanged);
    connect(m_client, &Client::fileReceiveProgress, this, [this](const QString& fileName, qint64 bytesReceived, qint64 totalBytes) {
        const int percent = totalBytes > 0
            ? qBound(0, static_cast<int>((bytesReceived * 100) / totalBytes), 100)
            : 0;
        const QString detail = QString("文件接收中 · %1 · %2 / %3 · %4%")
            .arg(fileName, LocalFileManager::humanFileSize(bytesReceived), LocalFileManager::humanFileSize(totalBytes))
            .arg(percent);
        ui->chatHintLabel->setText(detail);
        ui->statusbar->showMessage(detail, 1600);
    });

    m_currentUserId = m_client->currentUserId();
    m_currentUserName = m_client->currentUserName();
    m_historyService.setUserId(m_currentUserId);
    m_clientStorage.setUserName(m_currentUserName);
    setWindowIcon(createChatIcon(m_currentUserName));
    ui->profileNameLabel->setText(m_currentUserName);
    ui->profileIdLabel->setText(QString("QQ %1").arg(m_currentUserId));
    ui->profileNameLabel->setToolTip(QString("当前昵称：%1").arg(m_currentUserName));
    ui->profileIdLabel->setToolTip(QString("当前 QQ 号：%1").arg(m_currentUserId));
    ui->profileCard->setToolTip("右键打开账号工作区，统一处理账号、头像、搜索与好友入口");
    ui->copyAccountBtn->setToolTip(QString("复制 QQ 号 %1 到剪贴板").arg(m_currentUserId));
    saveProfileToSqlite();
    ui->addFriendBtn->hide();
    ui->uploadAvatarBtn->setText("头像工作区");
    if (m_client->hasServerGroupSnapshot()) {
        onServerGroupSnapshotReceived(m_client->serverGroups());
    }

    if (m_currentUserId.isEmpty()) {
        ui->statusbar->showMessage("已连接");
    } else {
        ui->statusbar->showMessage("已连接 - 用户ID: " + m_currentUserId);
    }
    loadHistory("group");
    updateSavedOutgoingTransferRecoveryUi(true);
}

MainWindow::~MainWindow() {
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

bool MainWindow::sendTransferWithProgress(const QString& filePath,
                                          const QString& receiverId,
                                          const QString& targetName,
                                          const QString& kind,
                                          bool asImage,
                                          QString* transferSummary,
                                          bool* canceled) {
    if (!m_client) return false;

    const QFileInfo info(filePath);
    constexpr int maxAttempts = 3;
    if (transferSummary) transferSummary->clear();
    if (canceled) *canceled = false;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        QString preparedSummary;
        bool cancelRequested = false;
        TransferOperationDialog progress(this);
        setupTransferOperationDialog(progress,
                                     this,
                                     QStringLiteral("transferSendingDialog"),
                                     QStringLiteral("发送中"),
                                     QStringLiteral("发送%1").arg(kind),
                                     QStringLiteral("当前操作会持续同步到文件工作区，便于随时判断进度、取消或后续恢复。"),
                                     QStringLiteral("取消发送会保留最近状态摘要，方便你稍后判断是否重试。"),
                                     QStringLiteral("取消发送"));
        TransferSendUiState sendingWorkspaceState;
        sendingWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 正在发送");
        sendingWorkspaceState.workspaceDetail = QStringLiteral("正在发送%1“%2”到 %3。进度、重试和最终结果会持续显示在这里。")
            .arg(kind, info.fileName(), targetName);
        sendingWorkspaceState.hintText = QStringLiteral("正在发送%1 · %2").arg(kind, info.fileName());
        sendingWorkspaceState.statusMessage = QStringLiteral("正在发送%1：%2").arg(kind, info.fileName());
        sendingWorkspaceState.statusTone = QStringLiteral("accent");
        setTransferWorkspaceSendState(sendingWorkspaceState);
        const TransferProgressUiState initialState =
            m_transferManager.sendingInitialState(kind, info.fileName(), targetName);
        updateTransferOperationDialog(progress,
                                      QStringLiteral("准备发送"),
                                      QStringLiteral("正在准备把%1“%2”发送到 %3。").arg(kind, info.fileName(), targetName),
                                      initialState.percent,
                                      QStringLiteral("%1% · 等待准备").arg(initialState.percent),
                                      QStringLiteral("accent"));
        progress.dialog.show();
        QApplication::processEvents();

        QMetaObject::Connection cancelConnection = connect(
            progress.cancelButton,
            &QPushButton::clicked,
            this,
            [this, &progress, &info, &kind, &cancelRequested, canceled]() {
                cancelRequested = true;
                if (canceled) *canceled = true;
                const TransferProgressUiState cancelState =
                    m_transferManager.sendingCancelState(kind, info.fileName());
                updateTransferOperationDialog(progress,
                                              QStringLiteral("正在取消"),
                                              QStringLiteral("%1“%2”正在取消发送，系统会保留最近状态并在结束后更新文件工作区。")
                                                  .arg(kind, info.fileName()),
                                              cancelState.percent,
                                              QStringLiteral("%1% · 正在取消").arg(cancelState.percent),
                                              QStringLiteral("warning"));
                TransferSendUiState canceledWorkspaceState;
                canceledWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 正在取消");
                canceledWorkspaceState.workspaceDetail = QStringLiteral("%1“%2”正在取消发送，系统会保留最新状态并在结束后更新这里。")
                    .arg(kind, info.fileName());
                canceledWorkspaceState.hintText = QStringLiteral("正在取消发送%1 · %2").arg(kind, info.fileName());
                canceledWorkspaceState.statusMessage = QStringLiteral("正在取消发送%1：%2").arg(kind, info.fileName());
                canceledWorkspaceState.statusTone = QStringLiteral("warning");
                setTransferWorkspaceSendState(canceledWorkspaceState);
                if (m_client) m_client->cancelCurrentOutgoingTransfer();
                ui->statusbar->showMessage(QString("正在取消发送%1：%2").arg(kind, info.fileName()), 1600);
                QApplication::processEvents();
            });
        QMetaObject::Connection progressConnection = connect(
            m_client,
            &Client::fileTransferProgress,
            this,
            [this, &progress, &info, &targetName, &kind](const QString& fileName, qint64 bytesPrepared, qint64 totalBytes) {
                if (fileName != info.fileName()) return;
                const TransferProgressUiState state = m_transferManager.sendingProgressState(kind, fileName, targetName, bytesPrepared, totalBytes);
                updateTransferOperationDialog(progress,
                                              QStringLiteral("正在发送"),
                                              QStringLiteral("正在发送%1“%2”到 %3，已准备 %4 / %5。")
                                                  .arg(kind,
                                                       fileName,
                                                       targetName,
                                                       LocalFileManager::humanFileSize(bytesPrepared),
                                                       LocalFileManager::humanFileSize(totalBytes)),
                                              state.percent,
                                              QStringLiteral("%1% · %2").arg(state.percent).arg(state.labelText),
                                              QStringLiteral("accent"));
                TransferSendUiState progressWorkspaceState;
                progressWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 正在发送");
                progressWorkspaceState.workspaceDetail = QStringLiteral("正在发送%1“%2”到 %3，已准备 %4 / %5。")
                    .arg(kind,
                         fileName,
                         targetName,
                         LocalFileManager::humanFileSize(bytesPrepared),
                         LocalFileManager::humanFileSize(totalBytes));
                progressWorkspaceState.hintText = QStringLiteral("正在发送%1 · %2").arg(kind, fileName);
                progressWorkspaceState.statusMessage = QStringLiteral("正在发送%1：%2").arg(kind, fileName);
                progressWorkspaceState.statusTone = QStringLiteral("accent");
                setTransferWorkspaceSendState(progressWorkspaceState);
                QApplication::processEvents();
            });
        QMetaObject::Connection preparedConnection = connect(
            m_client,
            &Client::fileTransferPrepared,
            this,
            [this, &progress, &info, &targetName, &kind, &preparedSummary](const QString& fileName,
                                                                      qint64 totalBytes,
                                                                      qint64 chunkSize,
                                                                      qint64 chunkCount,
                                                                      const QString& fileHash) {
                if (fileName != info.fileName()) return;
                const TransferProgressUiState state = m_transferManager.sendingPreparedState(kind, fileName, targetName, totalBytes, chunkSize, chunkCount, fileHash);
                preparedSummary = state.manifestSummary;
                updateTransferOperationDialog(progress,
                                              QStringLiteral("清单已就绪"),
                                              QStringLiteral("%1“%2”已生成校验清单并继续发送到 %3。%4")
                                                  .arg(kind, fileName, targetName, state.manifestSummary),
                                              qMax(state.percent, 10),
                                              QStringLiteral("%1% · 清单已生成").arg(qMax(state.percent, 10)),
                                              QStringLiteral("success"));
                TransferSendUiState preparedWorkspaceState;
                preparedWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 清单已就绪");
                preparedWorkspaceState.workspaceDetail = QStringLiteral("%1“%2”已生成校验清单并继续发送到 %3。%4")
                    .arg(kind, fileName, targetName, state.manifestSummary);
                preparedWorkspaceState.hintText = QStringLiteral("已生成%1发送清单 · %2").arg(kind, fileName);
                preparedWorkspaceState.statusMessage = QStringLiteral("%1发送清单已生成：%2").arg(kind, fileName);
                preparedWorkspaceState.statusTone = QStringLiteral("success");
                setTransferWorkspaceSendState(preparedWorkspaceState);
                QApplication::processEvents();
            });

        const bool ok = asImage
            ? m_client->sendImage(filePath, receiverId)
            : m_client->sendFile(filePath, receiverId);

        QObject::disconnect(progressConnection);
        QObject::disconnect(preparedConnection);
        QObject::disconnect(cancelConnection);
        updateTransferOperationDialog(progress,
                                      ok ? QStringLiteral("发送完成") : (cancelRequested ? QStringLiteral("已取消") : QStringLiteral("发送失败")),
                                      ok
                                          ? QStringLiteral("%1“%2”已发送完成，可回到文件工作区继续查看摘要、回执和保存动作。").arg(kind, info.fileName())
                                          : (cancelRequested
                                              ? QStringLiteral("%1“%2”已取消发送，最近状态会保留在文件工作区。").arg(kind, info.fileName())
                                              : QStringLiteral("%1“%2”发送未完成，可在文件工作区查看失败原因并决定是否重试。").arg(kind, info.fileName())),
                                      ok ? 100 : progress.progressBar->value(),
                                      ok ? QStringLiteral("100% · 已完成")
                                         : (cancelRequested ? QStringLiteral("%1% · 已取消").arg(progress.progressBar->value())
                                                            : QStringLiteral("%1% · 未完成").arg(progress.progressBar->value())),
                                      ok ? QStringLiteral("success")
                                         : QStringLiteral("warning"));
        progress.dialog.close();

        if (cancelRequested && !ok) {
            if (transferSummary) *transferSummary = "已取消";
            if (canceled) *canceled = true;
            return false;
        }

        if (ok) {
            if (transferSummary) *transferSummary = preparedSummary;
            return true;
        }

        if (attempt < maxAttempts) {
            if (confirmDestructiveAction(QString("%1发送失败").arg(kind),
                                         QString("%1“%2”发送失败，是否立即重试？\n当前为第 %3 次，共最多 %4 次。")
                                             .arg(kind, info.fileName())
                                             .arg(attempt)
                                             .arg(maxAttempts),
                                         QStringLiteral("立即重试"),
                                         QStringLiteral("先取消"),
                                         this)) {
                ui->statusbar->showMessage(QString("正在重试发送%1：%2").arg(kind, info.fileName()), 1800);
                continue;
            }
        }

        return false;
    }

    return false;
}

void MainWindow::onFileTransferStatusChanged(const QString& fileName,
                                             const QString& transferId,
                                             const QString& reason,
                                             qint64 receivedBytes,
                                             qint64 totalBytes) {
    showFileTransferStatusEvent(fileName, transferId, reason, receivedBytes, totalBytes);
}

void MainWindow::showFileTransferStatusEvent(const QString& fileName,
                                             const QString& transferId,
                                             const QString& reason,
                                             qint64 receivedBytes,
                                             qint64 totalBytes) {
    const TransferStatusEvent event = m_transferManager.statusEvent(fileName, transferId, reason, receivedBytes, totalBytes);
    m_lastTransferStatusDiagnostic = event.diagnostic;
    applyTransferActionState(m_copyLastTransferStatusAction, event.copyDiagnostic.action);
    appendSystemMessage(event.message);
    ui->chatHintLabel->setText(event.chatHintText);
    ui->statusbar->showMessage(event.statusBarMessage, event.statusBarTimeoutMs);
    clearTransferWorkspaceSendState();
    refreshTransferWorkspaceCard(nullptr, &event, nullptr);
}

LocalSavedFileState MainWindow::savedFileActionState(const QModelIndex& index) const {
    LocalSavedFileState state;
    if (!index.isValid()) return state;
    const QString openPath = index.data(TransferChatItemRenderer::OpenPathRole).toString().trimmed();
    if (!openPath.isEmpty()) {
        return LocalFileManager::savedFileStateFromChatText(QStringLiteral("保存路径：") + openPath,
                                                            index.data(Qt::ToolTipRole).toString());
    }
    return LocalFileManager::savedFileStateFromChatText(index.data().toString(),
                                                        index.data(Qt::ToolTipRole).toString());
}

QString MainWindow::avatarPathForUser(const QString& userId) const {
    const QString trimmedUserId = userId.trimmed();
    if (!trimmedUserId.isEmpty() && trimmedUserId == m_currentUserId) {
        const QString selfAvatar = getAvatarFilePath();
        if (QFileInfo::exists(selfAvatar)) {
            return selfAvatar;
        }
    }

    const QString cachedPeerAvatar = m_clientStorage.peerAvatarFilePath(trimmedUserId);
    if (!trimmedUserId.isEmpty() && QFileInfo::exists(cachedPeerAvatar)) {
        return cachedPeerAvatar;
    }

    const ChatUser knownUser = m_knownUsers.value(trimmedUserId);
    const QString knownAvatar = knownUser.avatar.trimmed();
    if (!knownAvatar.isEmpty() && QFileInfo::exists(knownAvatar)) {
        return knownAvatar;
    }
    return QString();
}

void MainWindow::cacheKnownUserAvatars() const {
    for (auto it = m_knownUsers.constBegin(); it != m_knownUsers.constEnd(); ++it) {
        const QString userId = it.key().trimmed();
        const QString avatar = it.value().avatar.trimmed();
        if (userId.isEmpty() || avatar.isEmpty()) {
            continue;
        }
        m_clientStorage.savePeerAvatar(userId, QByteArray::fromBase64(avatar.toLatin1()));
    }
}

QPixmap MainWindow::chatAvatarPixmap(const QString& userId, const QString& displayName, int side) const {
    const QString avatarPath = avatarPathForUser(userId);
    if (!avatarPath.isEmpty()) {
        const QPixmap avatar(avatarPath);
        const QPixmap square = squareAvatarPixmap(avatar, side);
        if (!square.isNull()) {
            return square;
        }
    }
    const QString remoteAvatar = m_knownUsers.value(userId.trimmed()).avatar.trimmed();
    if (!remoteAvatar.isEmpty()) {
        QPixmap avatar;
        if (avatar.loadFromData(QByteArray::fromBase64(remoteAvatar.toLatin1()))) {
            const QPixmap square = squareAvatarPixmap(avatar, side);
            if (!square.isNull()) {
                return square;
            }
        }
    }
    return initialAvatarPixmap(displayName.isEmpty() ? userId : displayName, side);
}

QPixmap MainWindow::chatAttachmentDecoration(const QString& senderId,
                                             const QString& senderName,
                                             const QPixmap& mediaPreview,
                                             bool isVideo,
                                             int previewWidth,
                                             int previewHeight) const {
    const int avatarSide = 36;
    const int gap = 10;
    const int width = avatarSide + gap + previewWidth;
    const int height = qMax(avatarSide, previewHeight);
    QPixmap canvas(width, height);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawPixmap(0, 0, chatAvatarPixmap(senderId, senderName, avatarSide));

    const QRect previewRect(avatarSide + gap, 0, previewWidth, previewHeight);
    painter.setPen(QPen(isVideo ? QColor(126, 87, 194) : QColor(207, 224, 248), 1));
    painter.setBrush(isVideo ? QColor(245, 240, 255) : QColor(255, 255, 255));
    painter.drawRoundedRect(previewRect.adjusted(0, 0, -1, -1), 10, 10);
    if (!mediaPreview.isNull()) {
        const QPixmap scaled = mediaPreview.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint topLeft(previewRect.x() + (previewRect.width() - scaled.width()) / 2,
                             previewRect.y() + (previewRect.height() - scaled.height()) / 2);
        painter.drawPixmap(topLeft, scaled);
    } else {
        QFont font = painter.font();
        font.setFamily(QStringLiteral("Microsoft YaHei"));
        font.setBold(true);
        font.setPixelSize(16);
        painter.setFont(font);
        painter.setPen(isVideo ? QColor(126, 87, 194) : QColor(49, 94, 140));
        painter.drawText(previewRect, Qt::AlignCenter, isVideo ? QStringLiteral("视频") : QStringLiteral("文件"));
    }
    return canvas;
}

void MainWindow::applyChatItemVisualMetadata(QStandardItem* item,
                                             const QString& senderId,
                                             const QString& senderName,
                                             const QString& mediaKind,
                                             const QString& openPath,
                                             const QPixmap& mediaPreview) const {
    if (!item) {
        return;
    }

    const QString displayName = senderName.trimmed().isEmpty() ? contactDisplayName(senderId) : senderName.trimmed();
    if (!senderId.trimmed().isEmpty()) {
        item->setData(senderId.trimmed(), TransferChatItemRenderer::SenderIdRole);
    }
    if (!displayName.isEmpty()) {
        item->setData(displayName, TransferChatItemRenderer::SenderNameRole);
    }
    const QString avatarPath = avatarPathForUser(senderId);
    if (!avatarPath.isEmpty()) {
        item->setData(avatarPath, TransferChatItemRenderer::AvatarPathRole);
    } else if (!m_knownUsers.value(senderId.trimmed()).avatar.trimmed().isEmpty()) {
        item->setData(QStringLiteral("remote-inline-avatar"), TransferChatItemRenderer::AvatarPathRole);
    }
    if (!mediaKind.trimmed().isEmpty()) {
        item->setData(mediaKind.trimmed(), TransferChatItemRenderer::MediaKindRole);
    }
    if (!openPath.trimmed().isEmpty()) {
        item->setData(openPath.trimmed(), TransferChatItemRenderer::OpenPathRole);
    }

    const bool isVideo = mediaKind == QLatin1String("video");
    if (mediaKind == QLatin1String("image") || mediaKind == QLatin1String("video") || mediaKind == QLatin1String("file")) {
        item->setData(chatAttachmentDecoration(senderId, displayName, mediaPreview, isVideo), Qt::DecorationRole);
    } else {
        item->setData(chatAvatarPixmap(senderId, displayName, 36), Qt::DecorationRole);
    }

    QStringList tooltipRows;
    if (!displayName.isEmpty()) {
        tooltipRows << QStringLiteral("发送者：%1").arg(displayName);
    }
    if (!senderId.trimmed().isEmpty()) {
        tooltipRows << QStringLiteral("QQ：%1").arg(senderId.trimmed());
    }
    if (!mediaKind.trimmed().isEmpty()) {
        tooltipRows << QStringLiteral("类型：%1").arg(mediaKind);
    }
    if (!openPath.trimmed().isEmpty()) {
        tooltipRows << QStringLiteral("双击打开：%1").arg(openPath.trimmed());
        tooltipRows << QStringLiteral("保存路径：%1").arg(openPath.trimmed());
    }
    const QString existingTip = item->data(Qt::ToolTipRole).toString().trimmed();
    if (!existingTip.isEmpty()) {
        tooltipRows << existingTip;
    }
    if (!tooltipRows.isEmpty()) {
        item->setData(tooltipRows.join('\n'), Qt::ToolTipRole);
    }
}

QStandardItem* MainWindow::createChatMessageItem(const QString& text,
                                                 const QString& senderId,
                                                 const QString& senderName,
                                                 bool alignRight,
                                                 const QColor& foreground,
                                                 const QColor& background,
                                                 const QString& mediaKind,
                                                 const QString& openPath,
                                                 const QPixmap& mediaPreview) const {
    QStandardItem* item = new QStandardItem(text);
    item->setEditable(false);
    if (foreground.isValid()) {
        item->setForeground(foreground);
    }
    if (background.isValid()) {
        item->setBackground(background);
    }
    item->setTextAlignment((alignRight ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    applyChatItemVisualMetadata(item, senderId, senderName, mediaKind, openPath, mediaPreview);
    return item;
}

bool MainWindow::openChatAttachmentFromIndex(const QModelIndex& index) {
    const LocalSavedFileState savedFileState = savedFileActionState(index);
    if (!savedFileState.hasSavePath) {
        return false;
    }

    showSavedFileWorkspace(savedFileState,
                           index.data().toString(),
                           QStringLiteral("已切换到保存文件工作区"));
    ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                                               chatContextSavedFileState(savedFileState),
                                                                               savedFileState.savePath);
    command.failureStatusMessage = QStringLiteral("文件不存在或无法打开");
    openSavedFileFromState(savedFileState, command);
    return true;
}

ChatContextSavedFileState MainWindow::chatContextSavedFileState(const LocalSavedFileState& savedFileState) const {
    ChatContextSavedFileState state;
    state.hasSavePath = savedFileState.hasSavePath;
    state.canOpenFile = savedFileState.canOpenFile;
    state.canOpenFolder = savedFileState.canOpenFolder;
    state.fileExists = savedFileState.fileInfo.exists();
    return state;
}

bool MainWindow::copySavedFilePathToClipboard(const ChatContextSavedFileCommand& command) {
    if (!command.canExecute) {
        ui->statusbar->showMessage(command.missingStatusMessage, command.timeoutMs);
        return false;
    }

    QApplication::clipboard()->setText(command.clipboardText);
    ui->statusbar->showMessage(command.successStatusMessage, command.timeoutMs);
    return true;
}

void MainWindow::showSavedFileWorkspace(const LocalSavedFileState& savedFileState,
                                        const QString& chatText,
                                        const QString& fallbackStatusMessage) {
    if (!savedFileState.hasSavePath) {
        if (!fallbackStatusMessage.trimmed().isEmpty()) {
            ui->statusbar->showMessage(fallbackStatusMessage, 1800);
        }
        return;
    }

    setTransferWorkspaceSavedFileState(savedFileState, chatText);
    const QString fileName = savedFileState.fileInfo.fileName().trimmed().isEmpty()
        ? QStringLiteral("已保存文件")
        : savedFileState.fileInfo.fileName();
    const QString fileSize = savedFileState.fileInfo.exists()
        ? LocalFileManager::humanFileSize(savedFileState.fileInfo.size())
        : QString();
    const QString flowText = ChatContextManager::mediaFlowText(chatText,
                                                               m_privateChatTarget,
                                                               m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget),
                                                               m_currentUserId,
                                                               m_currentUserName);
    const TransferWorkspaceSummaryState summary =
        m_transferManager.savedFileWorkspaceSummary(fileName,
                                                    fileSize,
                                                    savedFileState.savePath,
                                                    savedFileState.canOpenFile,
                                                    savedFileState.canOpenFolder,
                                                    flowText);

    TransferSendUiState workspaceState;
    workspaceState.workspaceTitle = summary.title;
    workspaceState.workspaceDetail = summary.detail;
    workspaceState.hintText = savedFileState.canOpenFile
        ? QStringLiteral("已保存文件工作区 · %1").arg(fileName)
        : QStringLiteral("已保存文件需检查路径 · %1").arg(fileName);
    workspaceState.statusMessage = savedFileState.canOpenFile
        ? QStringLiteral("已切换到保存文件工作区：") + fileName
        : QStringLiteral("已切换到保存文件排查工作区：") + fileName;
    workspaceState.statusTone = savedFileState.canOpenFile ? QStringLiteral("success") : QStringLiteral("warning");
    setTransferWorkspaceSendState(workspaceState);
}

void MainWindow::clearSavedFileWorkspace() {
    m_hasTransferWorkspaceSavedFileState = false;
    m_transferWorkspaceSavedFileState = LocalSavedFileState();
    m_transferWorkspaceSavedChatText.clear();
    if (m_hasTransferWorkspaceSendState
        && transferWorkspaceStateUsesSavedFileActions(m_transferWorkspaceSendState)) {
        clearTransferWorkspaceSendState();
        refreshTransferWorkspaceCard();
    }
}

void MainWindow::setTransferWorkspaceSavedFileState(const LocalSavedFileState& savedFileState,
                                                    const QString& chatText) {
    m_hasTransferWorkspaceSavedFileState = savedFileState.hasSavePath;
    m_transferWorkspaceSavedFileState = savedFileState;
    m_transferWorkspaceSavedChatText = chatText;
}

QString MainWindow::transferWorkspaceStatusSnapshotText() const {
    QStringList rows;
    rows << QStringLiteral("文件工作区状态");

    const TransferWorkspaceSummaryState summary = currentTransferWorkspaceSummary();
    if (!summary.title.trimmed().isEmpty()) {
        rows << QStringLiteral("摘要标题:%1").arg(summary.title);
    }
    if (!summary.detail.trimmed().isEmpty()) {
        rows << QStringLiteral("摘要说明:%1").arg(summary.detail);
    }
    if (!summary.nextStep.trimmed().isEmpty()) {
        rows << QStringLiteral("下一步:%1").arg(summary.nextStep);
    }
    if (!summary.preservedState.trimmed().isEmpty()) {
        rows << QStringLiteral("保留状态:%1").arg(summary.preservedState);
    }
    if (!summary.diagnosticHint.trimmed().isEmpty()) {
        rows << QStringLiteral("诊断提示:%1").arg(summary.diagnosticHint);
    }

    if (m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.hasSavedTransfer) {
        rows << QStringLiteral("恢复记录:%1").arg(m_lastTransferRecoveryUiState.fileName);
        rows << QStringLiteral("恢复模式:%1").arg(m_lastTransferRecoveryUiState.recoveryMode.isEmpty()
                                                      ? QStringLiteral("未标记")
                                                      : m_lastTransferRecoveryUiState.recoveryMode);
        rows << QStringLiteral("恢复状态:%1").arg(m_lastTransferRecoveryUiState.canAutoResume
                                                      ? QStringLiteral("可恢复")
                                                      : QStringLiteral("需手动重发"));
        if (!m_lastTransferRecoveryUiState.detail.isEmpty()) {
            rows << QStringLiteral("恢复说明:%1").arg(m_lastTransferRecoveryUiState.detail);
        }
    }

    if (m_hasLastTransferStatusEvent && !m_lastTransferStatusEvent.message.isEmpty()) {
        rows << QStringLiteral("最近事件:%1").arg(m_lastTransferStatusEvent.message);
        if (!m_lastTransferStatusEvent.actionHint.isEmpty()) {
            rows << QStringLiteral("事件建议:%1").arg(m_lastTransferStatusEvent.actionHint);
        }
    }

    if (m_hasTransferWorkspaceSendState && !m_transferWorkspaceSendState.workspaceTitle.isEmpty()) {
        rows << QStringLiteral("当前工作区:%1").arg(m_transferWorkspaceSendState.workspaceTitle);
        if (!m_transferWorkspaceSendState.workspaceDetail.isEmpty()) {
            rows << QStringLiteral("工作区详情:%1").arg(m_transferWorkspaceSendState.workspaceDetail);
        }
    }

    if (m_hasTransferWorkspaceSavedFileState && m_transferWorkspaceSavedFileState.hasSavePath) {
        rows << QStringLiteral("保存文件:%1").arg(m_transferWorkspaceSavedFileState.fileInfo.fileName());
        rows << QStringLiteral("保存路径:%1").arg(m_transferWorkspaceSavedFileState.savePath);
        rows << QStringLiteral("文件可打开:%1").arg(m_transferWorkspaceSavedFileState.canOpenFile ? QStringLiteral("是") : QStringLiteral("否"));
        rows << QStringLiteral("目录可打开:%1").arg(m_transferWorkspaceSavedFileState.canOpenFolder ? QStringLiteral("是") : QStringLiteral("否"));
    }

    if (!m_lastTransferStatusDiagnostic.trimmed().isEmpty()) {
        rows << QStringLiteral("最近诊断:%1").arg(m_lastTransferStatusDiagnostic);
    }
    return rows.join(QLatin1Char('\n'));
}

TransferWorkspaceSummaryState MainWindow::currentTransferWorkspaceSummary(const TransferRecoveryUiState* recoveryState,
                                                                         const TransferStatusEvent* latestEvent,
                                                                         const TransferSendUiState* sendState) const {
    const bool hasDiagnostic = !m_lastTransferStatusDiagnostic.trimmed().isEmpty();

    const TransferRecoveryUiState* resolvedRecovery = nullptr;
    TransferRecoveryUiState localRecovery;
    if (recoveryState && recoveryState->hasSavedTransfer) {
        resolvedRecovery = recoveryState;
    } else if (m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.hasSavedTransfer) {
        localRecovery = m_lastTransferRecoveryUiState;
        resolvedRecovery = &localRecovery;
    }

    const TransferStatusEvent* resolvedEvent = nullptr;
    TransferStatusEvent localEvent;
    if (latestEvent && !latestEvent->message.trimmed().isEmpty()) {
        resolvedEvent = latestEvent;
    } else if (m_hasLastTransferStatusEvent && !m_lastTransferStatusEvent.message.trimmed().isEmpty()) {
        localEvent = m_lastTransferStatusEvent;
        resolvedEvent = &localEvent;
    }

    const TransferSendUiState* resolvedSend = nullptr;
    TransferSendUiState localSend;
    if (sendState && (!sendState->workspaceTitle.trimmed().isEmpty() || !sendState->workspaceDetail.trimmed().isEmpty())) {
        resolvedSend = sendState;
    } else if (m_hasTransferWorkspaceSendState
               && (!m_transferWorkspaceSendState.workspaceTitle.trimmed().isEmpty()
                   || !m_transferWorkspaceSendState.workspaceDetail.trimmed().isEmpty())) {
        localSend = m_transferWorkspaceSendState;
        resolvedSend = &localSend;
    }

    if (resolvedRecovery) {
        return m_transferManager.recoveryWorkspaceSummary(*resolvedRecovery, resolvedEvent, hasDiagnostic);
    }
    if (resolvedEvent) {
        return m_transferManager.statusWorkspaceSummary(*resolvedEvent,
                                                        m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.hasSavedTransfer,
                                                        hasDiagnostic);
    }
    if (resolvedSend) {
        return m_transferManager.sendWorkspaceSummary(*resolvedSend, hasDiagnostic);
    }
    if (m_hasTransferWorkspaceSavedFileState && m_transferWorkspaceSavedFileState.hasSavePath) {
        const QFileInfo fileInfo = m_transferWorkspaceSavedFileState.fileInfo;
        const QString fileName = fileInfo.fileName().trimmed().isEmpty()
            ? QStringLiteral("已保存文件")
            : fileInfo.fileName();
        const QString fileSize = fileInfo.exists()
            ? LocalFileManager::humanFileSize(fileInfo.size())
            : QString();
        return m_transferManager.savedFileWorkspaceSummary(fileName,
                                                           fileSize,
                                                           m_transferWorkspaceSavedFileState.savePath,
                                                           m_transferWorkspaceSavedFileState.canOpenFile,
                                                           m_transferWorkspaceSavedFileState.canOpenFolder,
                                                           ChatContextManager::plainContentText(m_transferWorkspaceSavedChatText));
    }
    return m_transferManager.emptyWorkspaceSummary(hasDiagnostic);
}

bool MainWindow::openSavedFileFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command) {
    if (!command.canExecute) {
        ui->statusbar->showMessage(command.missingStatusMessage, command.timeoutMs);
        return false;
    }

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(savedFileState.fileInfo.absoluteFilePath()))) {
        ui->statusbar->showMessage(command.successStatusMessage, command.timeoutMs);
        return true;
    }

    showFileTransferStatusEvent(savedFileState.fileInfo.fileName(),
                                QString(),
                                command.failureTransferReason,
                                0,
                                0);
    ui->statusbar->showMessage(command.failureStatusMessage, command.timeoutMs);
    return false;
}

bool MainWindow::openSavedFolderFromState(const LocalSavedFileState& savedFileState, const ChatContextSavedFileCommand& command) {
    if (!command.canExecute) {
        ui->statusbar->showMessage(command.missingStatusMessage, command.timeoutMs);
        return false;
    }

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(savedFileState.folderInfo.absoluteFilePath()))) {
        ui->statusbar->showMessage(command.successStatusMessage, command.timeoutMs);
        return true;
    }

    showFileTransferStatusEvent(savedFileState.fileInfo.fileName(),
                                QString(),
                                QStringLiteral("receive-open-failed"),
                                0,
                                0);
    ui->statusbar->showMessage(command.failureStatusMessage, command.timeoutMs);
    return false;
}

void MainWindow::refreshAvatarWorkspaceCard() {
    if (!ui->avatarStatusCard || !ui->avatarStatusTitleLabel || !ui->avatarStatusDetailLabel) {
        return;
    }

    const QString avatarPath = getAvatarFilePath();
    const QFileInfo avatarInfo(avatarPath);
    if (avatarInfo.exists()) {
        ui->avatarStatusTitleLabel->setText(QStringLiteral("头像工作区 · 已保存"));
        ui->avatarStatusDetailLabel->setText(
            QStringLiteral("%1 已保存到本机。可右键头像按钮复制路径、打开目录，或继续更换头像。")
                .arg(QStringLiteral("%1 · %2").arg(avatarInfo.fileName(), LocalFileManager::humanFileSize(avatarInfo.size()))));
        applyToneProperty(ui->avatarStatusCard, QStringLiteral("success"));
        return;
    }

    ui->avatarStatusTitleLabel->setText(QStringLiteral("头像工作区"));
    ui->avatarStatusDetailLabel->setText(QStringLiteral("当前使用默认头像，可上传新头像，之后可继续复制路径或打开头像目录。"));
    applyToneProperty(ui->avatarStatusCard, QStringLiteral("muted"));
}

void MainWindow::showAvatarWorkspace() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("avatarWorkspaceDialog"),
        QStringLiteral("头像工作区"),
        QSize(860, 700),
        QStringLiteral("managerTitle"),
        QStringLiteral("头像工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把更换头像、复制路径、打开目录和状态留档统一放进一个稳定工作面里。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索头像动作"),
        QStringLiteral("按头像、更换、路径、目录或状态动作筛选"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一个头像动作，再决定更换头像、复制路径、打开目录，或复制当前状态。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释当前头像动作会如何影响本机头像文件、目录和资料卡。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* listWidget = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    struct AvatarWorkspaceRow {
        QString id;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool accent = false;
        bool muted = false;
    };

    auto avatarCardText = [this]() {
        const QFileInfo avatarInfo(getAvatarFilePath());
        return QStringLiteral("我的头像卡片\nQQ:%1\n昵称:%2\n头像:%3")
            .arg(m_currentUserId,
                 m_currentUserName,
                 avatarInfo.exists()
                     ? QStringLiteral("%1 · %2").arg(avatarInfo.fileName(), LocalFileManager::humanFileSize(avatarInfo.size()))
                     : QStringLiteral("当前使用默认头像"));
    };
    auto avatarStatusText = [this]() {
        const QFileInfo avatarInfo(getAvatarFilePath());
        return avatarInfo.exists()
            ? QStringLiteral("头像工作区 · 当前头像 %1 · %2 · 路径 %3")
                  .arg(avatarInfo.fileName(),
                       LocalFileManager::humanFileSize(avatarInfo.size()),
                       avatarInfo.absoluteFilePath())
            : QStringLiteral("头像工作区 · 当前使用默认头像，可立即选择并保存新头像");
    };
    auto buildRows = [this]() {
        QList<AvatarWorkspaceRow> rows;
        const QFileInfo avatarInfo(getAvatarFilePath());
        const bool hasAvatarFile = avatarInfo.exists();

        rows << AvatarWorkspaceRow{
            QStringLiteral("avatar-overview"),
            QStringLiteral("当前头像"),
            hasAvatarFile
                ? QStringLiteral("%1 · %2").arg(avatarInfo.fileName(), LocalFileManager::humanFileSize(avatarInfo.size()))
                : QStringLiteral("当前使用默认头像"),
            hasAvatarFile
                ? QStringLiteral("头像总览\n头像：%1\n路径：%2\n可继续：更换头像、复制路径、打开目录")
                      .arg(avatarInfo.fileName(), avatarInfo.absoluteFilePath())
                : QStringLiteral("头像总览\n当前使用默认头像\n可继续：选择并保存新头像，然后再复制路径或打开目录。"),
            QStringLiteral("头像 总览 路径 目录"),
            true,
            false};
        rows << AvatarWorkspaceRow{
            QStringLiteral("avatar-replace"),
            QStringLiteral("更换头像"),
            QStringLiteral("打开头像选择器，更新当前账号头像"),
            QStringLiteral("更换头像\n会把新头像裁成统一方形并保存到本机资料目录。"),
            QStringLiteral("更换 头像 上传 保存"),
            false,
            false};
        rows << AvatarWorkspaceRow{
            QStringLiteral("avatar-copy-card"),
            QStringLiteral("复制头像卡片"),
            QStringLiteral("复制当前头像、昵称和账号摘要"),
            QStringLiteral("复制头像卡片\n便于留档、反馈或发给其他人核对当前头像状态。"),
            QStringLiteral("复制 头像 卡片"),
            false,
            false};
        rows << AvatarWorkspaceRow{
            QStringLiteral("avatar-copy-path"),
            QStringLiteral("复制头像路径"),
            hasAvatarFile ? QStringLiteral("复制当前头像在本机的路径") : QStringLiteral("当前还没有保存过自定义头像"),
            hasAvatarFile
                ? QStringLiteral("复制头像路径\n当前头像文件位于：%1").arg(avatarInfo.absoluteFilePath())
                : QStringLiteral("复制头像路径\n当前没有可复制的头像文件路径。"),
            QStringLiteral("复制 路径 头像"),
            false,
            !hasAvatarFile};
        rows << AvatarWorkspaceRow{
            QStringLiteral("avatar-open-folder"),
            QStringLiteral("打开头像目录"),
            hasAvatarFile ? QStringLiteral("直接打开当前头像所在目录") : QStringLiteral("当前还没有保存过自定义头像"),
            hasAvatarFile
                ? QStringLiteral("打开头像目录\n会定位到当前头像所在目录，便于继续替换、备份或清理。")
                : QStringLiteral("打开头像目录\n当前没有自定义头像文件，建议先保存一个头像。"),
            QStringLiteral("打开 目录 头像"),
            false,
            !hasAvatarFile};
        rows << AvatarWorkspaceRow{
            QStringLiteral("avatar-copy-status"),
            QStringLiteral("复制头像状态"),
            QStringLiteral("复制当前头像工作区状态"),
            QStringLiteral("复制头像状态\n整理当前头像文件、路径和是否已保存，便于反馈和记录。"),
            QStringLiteral("复制 状态 头像"),
            false,
            false};

        return rows;
    };

    auto rows = buildRows();
    auto emptyPreviewText = [searchEdit]() {
        const QString filter = searchEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的头像动作。可先准备头像文件，再从这里继续更换、复制路径或打开目录。")
            : QStringLiteral("当前筛选词“%1”没有匹配到头像动作。\n试试“头像”、“路径”、“目录”或“状态”这些关键词。").arg(filter);
    };

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const AvatarWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !row.keywords.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.id);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (row.accent) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.muted) {
                item->setForeground(QColor(100, 116, 139));
            }
            listWidget->addItem(item);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                listWidget,
                QStringLiteral("没有匹配的头像动作"),
                QStringLiteral("试试“头像”、“路径”、“目录”或“状态”这些关键词。"),
                emptyPreviewText());
        }
        statsLabel->setText(QStringLiteral("可见 %1 / %2 项").arg(visibleCount).arg(rows.size()));
        selectPreferredListRow(listWidget, 0);
    };

    auto selectedRow = [=, &rows]() -> AvatarWorkspaceRow* {
        QListWidgetItem* currentItem = listWidget->currentItem();
        if (!currentItem) {
            return nullptr;
        }
        const QString id = currentItem->data(Qt::UserRole).toString();
        for (AvatarWorkspaceRow& row : rows) {
            if (row.id == id) {
                return &row;
            }
        }
        return nullptr;
    };

    auto updatePreview = [=, &rows]() {
        AvatarWorkspaceRow* row = selectedRow();
        previewLabel->setText(row ? row->preview
                                  : (firstEnabledListRow(listWidget) >= 0
                                         ? QStringLiteral("这里会解释当前头像动作会如何影响本机头像文件、目录和资料卡。")
                                         : emptyPreviewText()));
    };

    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("执行当前动作"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("执行当前头像动作"), QStyle::SP_ArrowForward);
    QPushButton* copyCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制头像卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前头像摘要"), QStyle::SP_DialogSaveButton);
    QPushButton* copyStatusBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区状态"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制头像工作区当前状态"), QStyle::SP_MessageBoxInformation);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭头像工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("头像动作"),
        QStringLiteral("把更换头像、路径复制、目录打开和状态留档统一收进一个固定入口。"),
        {openBtn, copyCardBtn, copyStatusBtn},
        closeBtn);

    auto avatarClipboardText = [=, &rows]() {
        AvatarWorkspaceRow* row = selectedRow();
        if (!row) {
            return avatarStatusText();
        }
        if (row->id == QLatin1String("avatar-copy-card")) {
            return avatarCardText();
        }
        if (row->id == QLatin1String("avatar-copy-path")) {
            return QFileInfo(getAvatarFilePath()).absoluteFilePath();
        }
        return avatarStatusText();
    };
    auto updateActionState = [=, &rows]() {
        AvatarWorkspaceRow* row = selectedRow();
        const QFileInfo avatarInfo(getAvatarFilePath());
        const bool hasActionableRow = hasEnabledListRow(listWidget);
        if (!row) {
            openBtn->setEnabled(false);
            openBtn->setText(QStringLiteral("执行当前动作"));
            openBtn->setToolTip(hasActionableRow
                                    ? QStringLiteral("先选择一个头像动作后继续更换、复制路径或打开目录")
                                    : QStringLiteral("当前没有可执行的头像动作"));
            copyCardBtn->setEnabled(hasActionableRow);
            copyCardBtn->setToolTip(hasActionableRow
                                        ? QStringLiteral("复制当前头像工作区总览")
                                        : QStringLiteral("当前没有可复制的头像摘要"));
            copyStatusBtn->setEnabled(true);
            copyStatusBtn->setToolTip(QStringLiteral("复制头像工作区当前状态"));
            return;
        }

        copyCardBtn->setEnabled(true);
        copyStatusBtn->setEnabled(true);
        copyStatusBtn->setToolTip(QStringLiteral("复制头像工作区当前状态"));
        openBtn->setEnabled(true);
        if (row->id == QLatin1String("avatar-replace")) {
            openBtn->setText(QStringLiteral("更换头像"));
            openBtn->setToolTip(QStringLiteral("打开头像选择器并保存新头像"));
            copyCardBtn->setToolTip(QStringLiteral("复制当前头像卡片与状态摘要"));
        } else if (row->id == QLatin1String("avatar-copy-card")) {
            openBtn->setText(QStringLiteral("复制头像卡"));
            openBtn->setToolTip(QStringLiteral("复制当前头像卡片"));
            copyCardBtn->setToolTip(QStringLiteral("复制当前头像卡片"));
        } else if (row->id == QLatin1String("avatar-copy-path")) {
            openBtn->setText(QStringLiteral("复制头像路径"));
            openBtn->setEnabled(avatarInfo.exists());
            openBtn->setToolTip(avatarInfo.exists()
                                    ? QStringLiteral("复制当前头像在本机的路径")
                                    : QStringLiteral("当前还没有保存过自定义头像"));
            copyCardBtn->setToolTip(avatarInfo.exists()
                                        ? QStringLiteral("复制当前头像路径")
                                        : QStringLiteral("当前还没有保存过自定义头像"));
        } else if (row->id == QLatin1String("avatar-open-folder")) {
            openBtn->setText(QStringLiteral("打开头像目录"));
            openBtn->setEnabled(avatarInfo.exists());
            openBtn->setToolTip(avatarInfo.exists()
                                    ? QStringLiteral("定位到当前头像所在目录")
                                    : QStringLiteral("当前还没有保存过自定义头像"));
            copyCardBtn->setToolTip(QStringLiteral("复制当前头像卡片与目录状态摘要"));
        } else if (row->id == QLatin1String("avatar-overview")) {
            openBtn->setText(QStringLiteral("复制头像状态"));
            openBtn->setToolTip(QStringLiteral("复制当前头像工作区状态"));
            copyCardBtn->setToolTip(QStringLiteral("复制当前头像工作区状态"));
        } else {
            openBtn->setText(QStringLiteral("复制头像状态"));
            openBtn->setToolTip(QStringLiteral("复制当前头像工作区状态"));
            copyCardBtn->setToolTip(QStringLiteral("复制当前头像工作区状态"));
        }
    };

    auto runSelectedCommand = [=, &rows, this, &dialog]() {
        AvatarWorkspaceRow* row = selectedRow();
        if (!row) {
            return;
        }
        const QFileInfo avatarInfo(getAvatarFilePath());
        if (row->id == QLatin1String("avatar-replace")) {
            dialog.accept();
            onUploadAvatar();
            return;
        }
        if (row->id == QLatin1String("avatar-overview")) {
            copyTextWithStatus(avatarStatusText(), QStringLiteral("头像状态已复制"), 2200);
            return;
        }
        if (row->id == QLatin1String("avatar-copy-card")) {
            copyTextWithStatus(avatarCardText(), QStringLiteral("头像卡片已复制"), 2200);
            return;
        }
        if (row->id == QLatin1String("avatar-copy-path")) {
            if (!avatarInfo.exists()) {
                ui->statusbar->showMessage(QStringLiteral("当前还没有保存过自定义头像"), 2200);
                return;
            }
            copyTextWithStatus(avatarInfo.absoluteFilePath(), QStringLiteral("头像路径已复制"), 2200);
            return;
        }
        if (row->id == QLatin1String("avatar-open-folder")) {
            if (!avatarInfo.exists()) {
                ui->statusbar->showMessage(QStringLiteral("当前还没有保存过自定义头像"), 2200);
                return;
            }
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(avatarInfo.absolutePath()))) {
                ui->statusbar->showMessage(QStringLiteral("头像目录无法打开"), 2200);
            } else {
                ui->statusbar->showMessage(QStringLiteral("已打开头像目录"), 2200);
            }
            return;
        }
        if (row->id == QLatin1String("avatar-copy-status")) {
            copyTextWithStatus(avatarStatusText(), QStringLiteral("头像状态已复制"), 2200);
        }
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, [runSelectedCommand](QListWidgetItem*) {
        runSelectedCommand();
    });
    connect(openBtn, &QPushButton::clicked, &dialog, runSelectedCommand);
    connect(copyCardBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        AvatarWorkspaceRow* row = selectedRow();
        QString statusText = QStringLiteral("头像工作区总览已复制");
        if (row) {
            if (row->id == QLatin1String("avatar-copy-path")) {
                statusText = QStringLiteral("头像路径已复制");
            } else if (row->id == QLatin1String("avatar-overview")
                       || row->id == QLatin1String("avatar-copy-status")) {
                statusText = QStringLiteral("头像状态已复制");
            } else {
                statusText = QStringLiteral("头像卡已复制");
            }
        }
        copyTextWithStatus(avatarClipboardText(), statusText, 2200);
    });
    connect(copyStatusBtn, &QPushButton::clicked, &dialog, [=, this]() {
        copyTextWithStatus(avatarStatusText(), QStringLiteral("头像工作区状态已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(QFileInfo::exists(getAvatarFilePath())
        ? QStringLiteral("当前账号：%1 · 已保存头像").arg(m_currentUserId)
        : QStringLiteral("当前账号：%1 · 使用默认头像").arg(m_currentUserId));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::showAvatarWorkspaceMenu(const QPoint& globalPos) {
    Q_UNUSED(globalPos)
    showAvatarWorkspace();
}

void MainWindow::copyTextWithStatus(const QString& text, const QString& statusMessage, int timeoutMs) {
    QApplication::clipboard()->setText(text);
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

bool MainWindow::showChoiceDialog(const QString& title,
                                  const QString& message,
                                  const QString& confirmText,
                                  const QString& cancelText,
                                  bool destructiveConfirm,
                                  const QString& canceledStatusMessage,
                                  int canceledStatusTimeoutMs,
                                  QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    QDialog dialog(dialogParent);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        destructiveConfirm ? QStringLiteral("dangerChoiceWorkspaceDialog") : QStringLiteral("choiceWorkspaceDialog"),
        title,
        QSize(720, 420),
        QStringLiteral("managerTitle"),
        title,
        QStringLiteral("managerSubTitle"),
        destructiveConfirm
            ? QStringLiteral("这是一个会改变当前状态的确认动作，请在继续前再核对一次。")
            : QStringLiteral("请确认这一步是否继续执行。"),
        QStringLiteral("managerSearch"),
        QString(),
        QString(),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        destructiveConfirm
            ? QStringLiteral("阅读当前说明后，选择继续执行或取消返回。")
            : QStringLiteral("确认当前说明后，选择继续或取消。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        message,
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    shell.searchEdit->hide();
    shell.listWidget->hide();
    shell.hintLabel->hide();
    shell.statsLabel->setText(destructiveConfirm ? QStringLiteral("需要确认") : QStringLiteral("等待确认"));
    shell.previewLabel->setText(message);

    WorkspaceSectionCard contextCard = createWorkspaceSectionCard(shell.bodyFrame,
                                                                  destructiveConfirm ? QStringLiteral("风险说明") : QStringLiteral("操作说明"),
                                                                  destructiveConfirm
                                                                      ? QStringLiteral("请确认你了解这一步会改变当前状态。")
                                                                      : QStringLiteral("请确认你已阅读当前说明。"));
    QLabel* messageLabel = new QLabel(message, contextCard.frame);
    messageLabel->setObjectName(QStringLiteral("managerSelectionPreview"));
    messageLabel->setWordWrap(true);
    contextCard.contentLayout->addWidget(messageLabel);
    shell.bodyLayout->insertWidget(0, contextCard.frame);

    QPushButton* confirmButton = createWorkspaceButton(shell.bodyFrame,
                                                       &dialog,
                                                       confirmText.trimmed().isEmpty() ? QStringLiteral("继续") : confirmText,
                                                       destructiveConfirm ? QStringLiteral("managerDangerBtn") : QStringLiteral("managerPrimaryBtn"),
                                                       destructiveConfirm ? QStringLiteral("继续执行当前风险动作") : QStringLiteral("继续执行当前动作"),
                                                       destructiveConfirm ? QStyle::SP_TrashIcon : QStyle::SP_DialogApplyButton);
    QPushButton* cancelButton = nullptr;
    if (!cancelText.trimmed().isEmpty()) {
        cancelButton = createWorkspaceButton(shell.bodyFrame,
                                             &dialog,
                                             cancelText,
                                             QStringLiteral("managerSecondaryBtn"),
                                             QStringLiteral("取消并返回"),
                                             QStyle::SP_DialogCloseButton);
    }
    addWorkspaceSectionCard(shell.bodyLayout,
                            shell.bodyFrame,
                            QStringLiteral("确认动作"),
                            destructiveConfirm
                                ? QStringLiteral("只有确认当前修改不可避免时，才执行危险动作。")
                                : QStringLiteral("确认无误后再继续，避免误操作。"),
                            {confirmButton},
                            cancelButton);

    bool confirmed = false;
    QObject::connect(confirmButton, &QPushButton::clicked, &dialog, [&]() {
        confirmed = true;
        dialog.accept();
    });
    if (cancelButton) {
        QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    }

    dialog.setStyleSheet(productDialogStyleSheet());
    confirmButton->setFocus();
    dialog.exec();
    if (!confirmed && !canceledStatusMessage.isEmpty() && canceledStatusTimeoutMs > 0 && ui && ui->statusbar) {
        ui->statusbar->showMessage(canceledStatusMessage, canceledStatusTimeoutMs);
    }
    return confirmed;
}

QString MainWindow::showSingleFieldDialog(const QString& dialogObjectName,
                                          const QString& title,
                                          const QString& subTitle,
                                          const QString& fieldLabel,
                                          const QString& placeholder,
                                          const QString& initialValue,
                                          bool multiline,
                                          bool* accepted,
                                          QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    QDialog dialog(dialogParent);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        dialogObjectName,
        title,
        QSize(760, multiline ? 620 : 520),
        QStringLiteral("managerTitle"),
        title,
        QStringLiteral("managerSubTitle"),
        subTitle,
        QStringLiteral("managerSearch"),
        QString(),
        QString(),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("检查输入内容后确认；取消不会改动当前状态。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        initialValue.trimmed().isEmpty() ? QStringLiteral("这里会显示当前输入内容。") : initialValue,
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    shell.searchEdit->hide();
    shell.listWidget->hide();
    shell.hintLabel->hide();
    shell.statsLabel->setText(multiline ? QStringLiteral("多行输入") : QStringLiteral("单项输入"));

    WorkspaceSectionCard formCard = createWorkspaceSectionCard(shell.bodyFrame,
                                                               fieldLabel.trimmed().isEmpty() ? QStringLiteral("输入内容") : fieldLabel,
                                                               QStringLiteral("整理当前输入后确认，系统会使用这里的内容继续执行。"));
    QLabel* fieldTitle = new QLabel(fieldLabel, formCard.frame);
    fieldTitle->setObjectName(QStringLiteral("workspaceSectionHint"));
    fieldTitle->setVisible(!fieldLabel.trimmed().isEmpty());
    if (fieldTitle->isVisible()) {
        formCard.contentLayout->addWidget(fieldTitle);
    }

    QWidget* inputWidget = nullptr;
    QLineEdit* lineEdit = nullptr;
    QTextEdit* textEdit = nullptr;
    if (multiline) {
        textEdit = new QTextEdit(formCard.frame);
        textEdit->setObjectName(QStringLiteral("messageEdit"));
        textEdit->setPlaceholderText(placeholder);
        textEdit->setPlainText(initialValue);
        textEdit->setMinimumHeight(220);
        inputWidget = textEdit;
    } else {
        lineEdit = new QLineEdit(formCard.frame);
        lineEdit->setObjectName(QStringLiteral("managerSearch"));
        lineEdit->setPlaceholderText(placeholder);
        lineEdit->setText(initialValue);
        lineEdit->setClearButtonEnabled(true);
        inputWidget = lineEdit;
    }
    formCard.contentLayout->addWidget(inputWidget);
    shell.bodyLayout->insertWidget(0, formCard.frame);

    QPushButton* confirmButton = createWorkspaceButton(shell.bodyFrame,
                                                       &dialog,
                                                       QStringLiteral("确认"),
                                                       QStringLiteral("managerPrimaryBtn"),
                                                       QStringLiteral("确认当前输入并继续"),
                                                       QStyle::SP_DialogApplyButton);
    QPushButton* clearButton = createWorkspaceButton(shell.bodyFrame,
                                                     &dialog,
                                                     QStringLiteral("清空"),
                                                     QStringLiteral("managerSecondaryBtn"),
                                                     QStringLiteral("清空当前输入"),
                                                     QStyle::SP_DialogResetButton);
    QPushButton* cancelButton = createWorkspaceButton(shell.bodyFrame,
                                                      &dialog,
                                                      QStringLiteral("取消"),
                                                      QStringLiteral("managerSecondaryBtn"),
                                                      QStringLiteral("取消并返回"),
                                                      QStyle::SP_DialogCloseButton);
    addWorkspaceSectionCard(shell.bodyLayout,
                            shell.bodyFrame,
                            QStringLiteral("输入动作"),
                            QStringLiteral("确认前可以继续修改、清空或直接取消。"),
                            {confirmButton, clearButton},
                            cancelButton);

    auto currentValue = [=]() {
        return textEdit ? textEdit->toPlainText().trimmed()
                        : (lineEdit ? lineEdit->text().trimmed() : QString());
    };
    auto updatePreview = [=]() {
        const QString value = currentValue();
        shell.previewLabel->setText(value.isEmpty() ? QStringLiteral("这里会显示当前输入内容。") : value);
    };

    bool applied = false;
    QString result;
    QObject::connect(confirmButton, &QPushButton::clicked, &dialog, [&]() {
        applied = true;
        result = currentValue();
        dialog.accept();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [=]() {
        if (textEdit) {
            textEdit->clear();
            textEdit->setFocus();
        } else if (lineEdit) {
            lineEdit->clear();
            lineEdit->setFocus();
        }
        updatePreview();
    });
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (lineEdit) {
        QObject::connect(lineEdit, &QLineEdit::textChanged, &dialog, [=](const QString&) { updatePreview(); });
        QObject::connect(lineEdit, &QLineEdit::returnPressed, &dialog, [&]() { confirmButton->click(); });
    }
    if (textEdit) {
        QObject::connect(textEdit, &QTextEdit::textChanged, &dialog, [=]() { updatePreview(); });
    }

    updatePreview();
    dialog.setStyleSheet(productDialogStyleSheet());
    if (lineEdit) {
        lineEdit->setFocus();
        lineEdit->selectAll();
    } else if (textEdit) {
        textEdit->setFocus();
    }
    dialog.exec();
    if (accepted) {
        *accepted = applied;
    }
    return applied ? result : QString();
}

QString MainWindow::showItemPickerDialog(const QString& dialogObjectName,
                                         const QString& title,
                                         const QString& subTitle,
                                         const QString& label,
                                         const QStringList& items,
                                         bool* accepted,
                                         QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    QDialog dialog(dialogParent);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        dialogObjectName,
        title,
        QSize(780, 640),
        QStringLiteral("managerTitle"),
        title,
        QStringLiteral("managerSubTitle"),
        subTitle,
        QStringLiteral("managerSearch"),
        QStringLiteral("筛选候选项"),
        QStringLiteral("输入关键词筛选当前候选项"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一个候选项，再确认继续；取消不会改动当前状态。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会显示当前选中的候选项。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QListWidget* listWidget = shell.listWidget;
    QLineEdit* searchEdit = shell.searchEdit;
    QLabel* previewLabel = shell.previewLabel;
    shell.hintLabel->setText(label);

    auto fillList = [=]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const QString& itemText : items) {
            if (!filter.isEmpty() && !itemText.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(itemText);
            item->setData(Qt::UserRole, itemText);
            item->setToolTip(itemText);
            item->setSizeHint(QSize(0, 58));
            listWidget->addItem(item);
            ++visibleCount;
        }
        shell.statsLabel->setText(QStringLiteral("可见 %1 / %2 项").arg(visibleCount).arg(items.size()));
        selectPreferredListRow(listWidget, 0);
    };
    auto selectedValue = [=]() -> QString {
        QListWidgetItem* currentItem = listWidget->currentItem();
        return currentItem ? currentItem->data(Qt::UserRole).toString().trimmed() : QString();
    };
    auto updatePreview = [=]() {
        const QString value = selectedValue();
        previewLabel->setText(value.isEmpty() ? QStringLiteral("这里会显示当前选中的候选项。") : value);
    };

    QPushButton* confirmButton = createWorkspaceButton(shell.bodyFrame,
                                                       &dialog,
                                                       QStringLiteral("确认"),
                                                       QStringLiteral("managerPrimaryBtn"),
                                                       QStringLiteral("确认当前选中项并继续"),
                                                       QStyle::SP_DialogApplyButton);
    QPushButton* clearFilterButton = createWorkspaceButton(shell.bodyFrame,
                                                           &dialog,
                                                           QStringLiteral("清空筛选"),
                                                           QStringLiteral("managerSecondaryBtn"),
                                                           QStringLiteral("清空当前筛选条件"),
                                                           QStyle::SP_DialogResetButton);
    QPushButton* cancelButton = createWorkspaceButton(shell.bodyFrame,
                                                      &dialog,
                                                      QStringLiteral("取消"),
                                                      QStringLiteral("managerSecondaryBtn"),
                                                      QStringLiteral("取消并返回"),
                                                      QStyle::SP_DialogCloseButton);
    addWorkspaceSectionCard(shell.bodyLayout,
                            shell.bodyFrame,
                            QStringLiteral("选择动作"),
                            QStringLiteral("筛选、选中并确认当前候选项。"),
                            {confirmButton, clearFilterButton},
                            cancelButton);

    bool applied = false;
    QString result;
    QObject::connect(searchEdit, &QLineEdit::textChanged, &dialog, [=](const QString&) {
        fillList();
        updatePreview();
    });
    QObject::connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
    });
    QObject::connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem*) {
        confirmButton->click();
    });
    QObject::connect(confirmButton, &QPushButton::clicked, &dialog, [&]() {
        result = selectedValue();
        if (result.isEmpty()) {
            return;
        }
        applied = true;
        dialog.accept();
    });
    QObject::connect(clearFilterButton, &QPushButton::clicked, &dialog, [=]() {
        searchEdit->clear();
        searchEdit->setFocus();
    });
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    fillList();
    updatePreview();
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
    if (accepted) {
        *accepted = applied;
    }
    return applied ? result : QString();
}

void MainWindow::refreshTransferWorkspaceCard(const TransferRecoveryUiState* recoveryState,
                                              const TransferStatusEvent* latestEvent,
                                              const TransferSendUiState* sendState) {
    if (!ui->transferStatusCard
        || !ui->transferStatusTitleLabel
        || !ui->transferStatusDetailLabel
        || !ui->transferResumeBtn
        || !ui->transferClearBtn
        || !ui->transferCopyStatusBtn) {
        return;
    }

    if (recoveryState && recoveryState->hasSavedTransfer) {
        m_hasLastTransferRecoveryUiState = true;
        m_lastTransferRecoveryUiState = *recoveryState;
    }

    if (latestEvent && !latestEvent->message.trimmed().isEmpty()) {
        m_hasLastTransferStatusEvent = true;
        m_lastTransferStatusEvent = *latestEvent;
    }

    const TransferWorkspaceSummaryState summary = currentTransferWorkspaceSummary(recoveryState,
                                                                                 latestEvent,
                                                                                 sendState);
    ui->transferStatusTitleLabel->setText(summary.title.trimmed().isEmpty()
        ? QStringLiteral("文件工作区")
        : summary.title);

    QStringList detailLines;
    if (!summary.detail.trimmed().isEmpty()) {
        detailLines << summary.detail.trimmed();
    }
    if (!summary.nextStep.trimmed().isEmpty()) {
        detailLines << summary.nextStep.trimmed();
    }
    if (!summary.preservedState.trimmed().isEmpty()) {
        detailLines << summary.preservedState.trimmed();
    }
    if (!summary.diagnosticHint.trimmed().isEmpty()) {
        detailLines << summary.diagnosticHint.trimmed();
    }
    ui->transferStatusDetailLabel->setText(detailLines.isEmpty()
        ? QStringLiteral("当前没有未完成发送，也没有新的文件诊断。发送、接收、恢复和保存动作会在这里持续更新。")
        : detailLines.join(QLatin1Char('\n')));

    const bool hasRecovery = m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.hasSavedTransfer;
    ui->transferResumeBtn->setVisible(hasRecovery && m_lastTransferRecoveryUiState.resumeAction.visible);
    ui->transferResumeBtn->setEnabled(hasRecovery && m_lastTransferRecoveryUiState.resumeAction.enabled);
    ui->transferResumeBtn->setToolTip(hasRecovery
        ? m_lastTransferRecoveryUiState.resumeAction.toolTip
        : QStringLiteral("当前没有可恢复的未完成发送"));
    ui->transferClearBtn->setVisible(hasRecovery && m_lastTransferRecoveryUiState.clearAction.visible);
    ui->transferClearBtn->setEnabled(hasRecovery && m_lastTransferRecoveryUiState.clearAction.enabled);
    ui->transferClearBtn->setToolTip(hasRecovery
        ? m_lastTransferRecoveryUiState.clearAction.toolTip
        : QStringLiteral("当前没有可清理的恢复记录"));

    const bool hasDiagnostic = !m_lastTransferStatusDiagnostic.trimmed().isEmpty();
    ui->transferCopyStatusBtn->setVisible(true);
    ui->transferCopyStatusBtn->setEnabled(true);
    ui->transferCopyStatusBtn->setToolTip(hasDiagnostic
        ? QStringLiteral("打开文件工作区，查看最近诊断、恢复入口、保存路径和下一步建议")
        : QStringLiteral("打开文件工作区，查看当前文件链路、恢复入口和下一步建议"));
    applyToneProperty(ui->transferStatusCard, summary.statusTone.trimmed().isEmpty()
        ? QStringLiteral("muted")
        : summary.statusTone.trimmed());
    refreshMainWorkbenchChrome();
}

void MainWindow::setTransferWorkspaceSendState(const TransferSendUiState& state) {
    m_hasTransferWorkspaceSendState = true;
    m_transferWorkspaceSendState = state;
    refreshTransferWorkspaceCard(nullptr, nullptr, &m_transferWorkspaceSendState);
}

void MainWindow::clearTransferWorkspaceSendState() {
    m_hasTransferWorkspaceSendState = false;
    m_transferWorkspaceSendState = TransferSendUiState();
}

FriendManagerVisibleTargetSummary MainWindow::friendNoticeVisibleTarget(const QString& userId) const {
    FriendManagerVisibleTargetSummary target;
    target.userId = userId.trimmed();
    if (!target.userId.isEmpty()) {
        target.displayName = m_friendNames.value(target.userId, contactDisplayName(target.userId));
        target.online = isContactOnline(target.userId);
    }
    return target;
}

QList<FriendManagerVisibleTargetSummary> MainWindow::visibleFriendNoticeTargets(QListWidget* noticeList) const {
    QList<FriendManagerVisibleTargetSummary> targets;
    const QStringList visibleIds = ::visibleFriendNoticeIds(noticeList);
    for (const QString& id : visibleIds) {
        if (id.isEmpty()) {
            continue;
        }
        targets << friendNoticeVisibleTarget(id);
    }
    return targets;
}

FriendNoticeSelectionSnapshot MainWindow::currentFriendNoticeSelectionSnapshot(QListWidget* noticeList,
                                                                               QLineEdit* searchEdit) const {
    const QString currentId = selectedFriendNoticeEntryId(noticeList);
    const FriendNoticeSelectionSnapshot snapshot =
        NotificationPanelManager::friendNoticeSelectionSnapshot(
            currentId,
            searchEdit ? searchEdit->text() : QString(),
            friendNoticeVisibleTarget(NotificationPanelManager::friendNoticeTargetId(
                currentId,
                searchEdit ? searchEdit->text() : QString())).displayName);
    return snapshot;
}

QList<GroupNoticeMemberInput> MainWindow::groupNoticeMemberCopyInputs(const QStringList& memberIds) const {
    QList<GroupNoticeMemberInput> inputs;
    for (const QString& id : memberIds) {
        GroupNoticeMemberInput input;
        input.userId = id;
        input.displayName = contactDisplayName(id);
        input.self = id == m_currentUserId;
        input.online = isContactOnline(id);
        inputs << input;
    }
    return inputs;
}

QStringList MainWindow::publicGroupNoticeMemberIds() const {
    QStringList onlineUserIds;
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        onlineUserIds << it.key();
    }
    return NotificationPanelManager::publicGroupMemberIds(m_currentUserId, onlineUserIds);
}

QStringList MainWindow::groupNoticeMemberIds(const QString& groupId) const {
    QStringList members = groupId.isEmpty()
        ? publicGroupNoticeMemberIds()
        : m_localGroupMembers.value(groupId);
    if (members.isEmpty()) {
        members << m_currentUserId;
    }
    return members;
}

QList<GroupNoticeListGroupInput> MainWindow::groupNoticeLocalGroups() const {
    QList<GroupNoticeListGroupInput> localGroups;
    for (const QString& groupId : m_localGroupIds) {
        GroupNoticeListGroupInput group;
        group.groupId = groupId;
        group.groupName = m_localGroupNames.value(groupId, "群聊");
        group.groupNumber = groupId.mid(QString("local_group_").size());
        group.memberCount = groupNoticeMemberIds(groupId).size();
        group.announcement = m_localGroupAnnouncements.value(
            groupId,
            QString("%1 已创建，可继续邀请好友并发送消息。").arg(group.groupName));
        localGroups << group;
    }
    return localGroups;
}

void MainWindow::fillGroupNoticeList(QListWidget* noticeList,
                                     QLabel* countLabel,
                                     QLineEdit* searchEdit) const {
    if (!noticeList || !countLabel || !searchEdit) {
        return;
    }
    noticeList->clear();
    const GroupNoticeListRenderUiState renderState =
        NotificationPanelManager::groupNoticeListRenderUiState(m_knownUsers.size(),
                                                               groupNoticeLocalGroups(),
                                                               searchEdit->text());
    countLabel->setText(renderState.countText);
    for (const GroupNoticeListEntryUiState& entry : renderState.entries) {
        QListWidgetItem* item = new QListWidgetItem(entry.text);
        item->setData(Qt::UserRole, entry.entryId);
        item->setSizeHint(QSize(0, entry.rowHeight));
        item->setToolTip(entry.toolTip);
        if (entry.accent) {
            item->setForeground(QColor(18, 150, 247));
        }
        noticeList->addItem(item);
    }
    selectPreferredListRow(noticeList, 0);
}

GroupNoticeSelectionSnapshot MainWindow::currentGroupNoticeSelectionSnapshot(QListWidget* noticeList,
                                                                            QLineEdit* searchEdit) const {
    const QString groupId = selectedGroupNoticeEntryId(noticeList);
    return NotificationPanelManager::groupNoticeSelectionSnapshot(
        groupId,
        searchEdit ? searchEdit->text().trimmed() : QString(),
        m_knownUsers.size(),
        m_localGroupNames.value(groupId, "群聊"),
        m_localGroupMembers.value(groupId).size(),
        m_localGroupAnnouncements.value(groupId),
        m_currentUserId);
}

QList<GroupNoticeBatchTargetInput> MainWindow::visibleGroupNoticeBatchTargets(QListWidget* noticeList) const {
    QList<GroupNoticeBatchTargetInput> targets;
    const QStringList visibleIds = ::visibleGroupNoticeIds(noticeList);
    for (const QString& id : visibleIds) {
        GroupNoticeBatchTargetInput target;
        target.entryId = id;
        if (isGroupCreateEntryId(id)) {
            target.memberCount = 1;
            target.onlineCount = 1;
        } else if (id.isEmpty()) {
            const QStringList publicMembers = groupNoticeMemberIds(id);
            target.memberCount = publicMembers.size();
            target.onlineCount = publicMembers.size();
        } else if (id.startsWith("local_group_")) {
            const QStringList members = groupNoticeMemberIds(id);
            int groupOnline = 0;
            for (const QString& memberId : members) {
                if (memberId == m_currentUserId || isContactOnline(memberId)) {
                    ++groupOnline;
                }
            }
            target.groupName = m_localGroupNames.value(id, "群聊");
            target.groupNumber = id.mid(QString("local_group_").size());
            target.memberCount = qMax(1, members.size());
            target.onlineCount = groupOnline;
        }
        targets << target;
    }
    return targets;
}

bool MainWindow::openSelectedGroupNoticeEntry(QListWidget* noticeList, QDialog* dialog) {
    if (!noticeList || !noticeList->currentItem()) {
        ui->statusbar->showMessage("请先选择要进入的群聊", 1800);
        return false;
    }
    const QString groupId = selectedGroupNoticeEntryId(noticeList);
    if (isGroupCreateEntryId(groupId)) {
        QString groupName = groupCreateEntryName(groupId);
        if (groupName.isEmpty()) {
            groupName = "搜索群聊";
        }
        const QString newGroupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        m_localGroupIds << newGroupId;
        m_localGroupNames[newGroupId] = groupName;
        m_localGroupAnnouncements[newGroupId] = QString("%1 已从群通知搜索创建，可继续邀请好友并发送消息。").arg(groupName);
        m_localGroupMembers[newGroupId] = QStringList{m_currentUserId};
        saveLocalGroups();
        refreshFriendList();
        if (dialog) {
            dialog->accept();
        }
        switchToLocalGroup(newGroupId, groupName);
        appendSystemMessage("已从群通知搜索创建群聊: " + groupName);
        ui->statusbar->showMessage("已创建并进入群聊: " + groupName, 2200);
        return true;
    }
    if (dialog) {
        dialog->accept();
    }
    if (groupId.isEmpty()) {
        onBackToGroupChat();
        ui->statusbar->showMessage("已进入公共聊天室", 1800);
        return true;
    }
    const QString groupName = m_localGroupNames.value(groupId, "群聊");
    switchToLocalGroup(groupId, groupName);
    ui->statusbar->showMessage("已进入群聊: " + groupName, 1800);
    return true;
}

bool MainWindow::handleSavedFileContextCommand(const QString& commandId,
                                               const QString& chatText,
                                               const LocalSavedFileState& savedFileState) {
    const ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(commandId,
                                                                                     chatContextSavedFileState(savedFileState),
                                                                                     savedFileState.savePath);
    if (!command.handled) {
        return false;
    }

    if (savedFileState.hasSavePath) {
        showSavedFileWorkspace(savedFileState,
                               chatText,
                               QStringLiteral("已同步到保存文件工作区"));
    }

    if (command.action == ChatContextSavedFileCommand::Action::CopySavePath) {
        return copySavedFilePathToClipboard(command);
    }
    if (command.action == ChatContextSavedFileCommand::Action::OpenSavedFile) {
        return openSavedFileFromState(savedFileState, command);
    }
    if (command.action == ChatContextSavedFileCommand::Action::OpenSaveFolder) {
        return openSavedFolderFromState(savedFileState, command);
    }
    return false;
}

bool MainWindow::confirmAction(const QString& title,
                               const QString& message,
                               const QString& canceledStatusMessage,
                               int canceledStatusTimeoutMs,
                               QWidget* parent) {
    return showChoiceDialog(title,
                            message,
                            QStringLiteral("继续"),
                            QStringLiteral("取消"),
                            false,
                            canceledStatusMessage,
                            canceledStatusTimeoutMs,
                            parent);
}

void MainWindow::showWarningDialog(const QString& title,
                                   const QString& message,
                                   QWidget* parent) const {
    MainWindow* self = const_cast<MainWindow*>(this);
    self->showChoiceDialog(title,
                           message,
                           QStringLiteral("知道了"),
                           QString(),
                           false,
                           QString(),
                           0,
                           parent);
}

bool MainWindow::confirmDestructiveAction(const QString& title,
                                          const QString& message,
                                          const QString& confirmText,
                                          const QString& cancelText,
                                          QWidget* parent) const {
    MainWindow* self = const_cast<MainWindow*>(this);
    return self->showChoiceDialog(title,
                                  message,
                                  confirmText.trimmed().isEmpty() ? QStringLiteral("继续") : confirmText,
                                  cancelText.trimmed().isEmpty() ? QStringLiteral("取消") : cancelText,
                                  true,
                                  QString(),
                                  0,
                                  parent);
}

QString MainWindow::promptTextValue(const QString& title,
                                    const QString& label,
                                    const QString& initialValue,
                                    bool* accepted,
                                    QWidget* parent) const {
    return showSingleFieldDialog(QStringLiteral("promptTextWorkspaceDialog"),
                                 title,
                                 QStringLiteral("填写后即可继续当前动作；取消不会改动现有状态。"),
                                 label,
                                 label,
                                 initialValue,
                                 false,
                                 accepted,
                                 parent);
}

QString MainWindow::promptMultilineValue(const QString& title,
                                         const QString& label,
                                         const QString& initialValue,
                                         bool* accepted,
                                         QWidget* parent) const {
    return showSingleFieldDialog(QStringLiteral("promptMultilineWorkspaceDialog"),
                                 title,
                                 QStringLiteral("整理好多行内容后再继续，适合公告、说明和核对文本。"),
                                 label,
                                 label,
                                 initialValue,
                                 true,
                                 accepted,
                                 parent);
}

QString MainWindow::promptItemValue(const QString& title,
                                    const QString& label,
                                    const QStringList& items,
                                    bool* accepted,
                                    QWidget* parent) const {
    return showItemPickerDialog(QStringLiteral("promptItemWorkspaceDialog"),
                                title,
                                QStringLiteral("从候选项里选定一个目标，再继续当前动作。"),
                                label,
                                items,
                                accepted,
                                parent);
}

QString MainWindow::selectOpenFilePath(const QString& title,
                                       const QString& initialPath,
                                       const QString& filters,
                                       QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    QFileDialog dialog(dialogParent, title, initialPath, filters);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setLabelText(QFileDialog::Accept, QStringLiteral("选择"));
    dialog.setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    applyProductDialogChrome(&dialog);
    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QString MainWindow::selectSaveFilePath(const QString& title,
                                       const QString& initialPath,
                                       const QString& filters,
                                       QWidget* parent) const {
    QWidget* dialogParent = parent ? parent : const_cast<MainWindow*>(this);
    QFileDialog dialog(dialogParent, title, initialPath, filters);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setLabelText(QFileDialog::Accept, QStringLiteral("保存"));
    dialog.setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    applyProductDialogChrome(&dialog);
    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }
    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QStringList MainWindow::currentSessionMemberIds() const {
    if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
        return m_localGroupMembers.value(m_privateChatTarget);
    }

    QStringList ids;
    for (auto it = m_knownUsers.constBegin(); it != m_knownUsers.constEnd(); ++it) {
        ids << it.key();
    }
    return ids;
}

bool MainWindow::applyAvatarSelection(const LocalFileSelectionResult& selection) {
    if (selection.canceled) {
        ui->statusbar->showMessage(selection.canceledStatusMessage, selection.canceledStatusTimeoutMs);
        refreshAvatarWorkspaceCard();
        return false;
    }
    if (!selection.accepted) {
        showWarningDialog(selection.failureTitle, selection.failureMessage, this);
        ui->statusbar->showMessage(selection.rejectedStatusMessage.isEmpty() ? selection.statusMessage
                                                                             : selection.rejectedStatusMessage,
                                   selection.rejectedStatusTimeoutMs);
        refreshAvatarWorkspaceCard();
        return false;
    }

    const QPixmap pixmap(selection.filePath);
    if (pixmap.isNull()) {
        const LocalFileSelectionResult invalidAvatar = LocalFileManager::invalidAvatarDataResult();
        showWarningDialog(invalidAvatar.invalidDataTitle, invalidAvatar.invalidDataMessage, this);
        ui->statusbar->showMessage(invalidAvatar.invalidDataStatusMessage, invalidAvatar.invalidDataStatusTimeoutMs);
        refreshAvatarWorkspaceCard();
        return false;
    }

    return persistAvatarPixmap(pixmap, selection.fileInfo);
}

bool MainWindow::persistAvatarPixmap(const QPixmap& pixmap, const QFileInfo& info) {
    QPixmap savedAvatar = squareAvatarPixmap(pixmap, 256);
    if (savedAvatar.isNull() || !savedAvatar.save(getAvatarFilePath(), "PNG")) {
        const LocalFileSelectionResult saveFailed = LocalFileManager::avatarSaveFailedResult();
        showWarningDialog(saveFailed.saveFailedTitle, saveFailed.saveFailedMessage, this);
        ui->statusbar->showMessage(saveFailed.saveFailedStatusMessage, saveFailed.saveFailedStatusTimeoutMs);
        refreshAvatarWorkspaceCard();
        return false;
    }

    ui->avatarLabel->setPixmap(squareAvatarPixmap(savedAvatar, ui->avatarLabel->width()));
    saveProfileToSqlite();
    if (m_client) {
        QByteArray avatarBytes;
        QBuffer buffer(&avatarBytes);
        buffer.open(QIODevice::WriteOnly);
        savedAvatar.save(&buffer, "PNG");
        m_client->sendAvatarUpdate(avatarBytes);
    }
    const LocalAvatarAppliedState appliedState = LocalFileManager::avatarAppliedState(info);
    ui->avatarLabel->setToolTip(appliedState.toolTip);
    ui->uploadAvatarBtn->setToolTip(appliedState.toolTip);
    appendSystemMessage(appliedState.detail);
    ui->chatHintLabel->setText(appliedState.detail);
    ui->statusbar->showMessage(appliedState.detail, appliedState.statusTimeoutMs);
    refreshAvatarWorkspaceCard();
    return true;
}

void MainWindow::openPrivateSession(const QString& userId) {
    if (userId.isEmpty()) {
        return;
    }
    const QString userName = contactDisplayName(userId);
    m_privateChatTarget = userId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({QStringLiteral("聊天记录")});
    loadHistory(userId);
    const PrivateChatUiState privateState = ChatSessionManager::privateChatState(
        userId,
        userName,
        isContactOnline(userId),
        m_client && m_client->hasE2ESession(userId),
        m_client && m_client->e2eSessionNeedsRotation(userId));
    setWindowTitle(appWindowTitle(privateState.windowSuffix));
    ui->chatTitleLabel->setText(privateState.titleText);
    ui->chatHintLabel->setText(privateState.hintText);
    refreshGroupMemberPanel();
    refreshComposerState();
}

bool MainWindow::ensureFriendRequestQueued(const QString& userId,
                                           const QString& successTemplate,
                                           bool refreshGroups) {
    if (userId.isEmpty() || userId == m_currentUserId) {
        return false;
    }
    const QString displayName = contactDisplayName(userId);
    if (m_friendIds.contains(userId)) {
        return true;
    }
    if (m_pendingOutgoingFriendRequests.contains(userId)) {
        ui->statusbar->showMessage(QStringLiteral("已向 %1 发送过好友申请，等待对方处理").arg(displayName), 2500);
        return false;
    }
    if (!m_client || !m_client->sendFriendRequest(userId)) {
        ui->statusbar->showMessage(QStringLiteral("好友申请发起失败：%1").arg(displayName), 3000);
        return false;
    }
    m_friendNames[userId] = displayName;
    m_pendingOutgoingFriendRequests << userId;
    refreshFriendList();
    if (refreshGroups) {
        refreshGroupMemberPanel();
    }
    if (!successTemplate.isEmpty()) {
        appendSystemMessage(successTemplate.arg(userId));
    }
    return true;
}

QString MainWindow::createLocalGroupSession(const QString& groupName,
                                            const QStringList& members,
                                            const QString& announcement) {
    const QString normalizedName = groupName.trimmed().isEmpty() ? QStringLiteral("我的群聊") : groupName.trimmed();
    const QString groupId = QStringLiteral("local_group_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz"));
    QStringList normalizedMembers = members;
    if (!normalizedMembers.contains(m_currentUserId)) {
        normalizedMembers.prepend(m_currentUserId);
    }
    normalizedMembers.removeAll(QString());
    normalizedMembers.removeDuplicates();
    m_localGroupIds << groupId;
    m_localGroupNames[groupId] = normalizedName;
    m_localGroupAnnouncements[groupId] = announcement.isEmpty()
        ? QStringLiteral("%1 已创建，可继续邀请好友并发送消息。").arg(normalizedName)
        : announcement;
    m_localGroupMembers[groupId] = normalizedMembers;
    saveLocalGroups();
    refreshFriendList();
    return groupId;
}

int MainWindow::appendMembersToLocalGroup(const QString& groupId, const QStringList& memberIds) {
    if (groupId.isEmpty() || !m_localGroupIds.contains(groupId)) {
        return 0;
    }
    int appended = 0;
    QStringList& members = m_localGroupMembers[groupId];
    if (!members.contains(m_currentUserId)) {
        members.prepend(m_currentUserId);
    }
    for (const QString& memberId : memberIds) {
        if (memberId.isEmpty() || memberId == m_currentUserId || members.contains(memberId)) {
            continue;
        }
        members << memberId;
        ++appended;
    }
    if (appended > 0) {
        saveLocalGroups();
        refreshFriendList();
        refreshGroupMemberPanel();
    }
    return appended;
}

bool MainWindow::showCreateGroupWorkspace(QWidget* parent) {
    QList<QPair<QString, QString>> rows;
    rows.append(qMakePair(QStringLiteral("我的群聊"),
                          QStringLiteral("默认群聊模板\n适合先建一个基础会话，后续再慢慢邀请好友和补公告。")));
    rows.append(qMakePair(QStringLiteral("好友群聊"),
                          QStringLiteral("好友群聊模板\n更适合从已有好友关系起步，之后继续做群内编排。")));
    rows.append(qMakePair(QStringLiteral("项目协作群"),
                          QStringLiteral("项目协作模板\n适合围绕固定主题沟通，建议同时补一条群公告说明目标。")));

    const GroupWorkspaceResult formResult = runGroupFormWorkspaceDialog(
        parent ? parent : this,
        QStringLiteral("createGroupWorkspaceDialog"),
        QStringLiteral("创建群聊工作区"),
        QStringLiteral("创建群聊"),
        QStringLiteral("先决定群聊名字和定位，再开始邀请好友、补充公告和进入会话。"),
        QStringLiteral("搜索常用群名模板"),
        QStringLiteral("按群名关键词筛选常用模板，选中后可直接带入输入框"),
        QStringLiteral("先给群聊一个明确名字，后续邀请、公告和群成员动作都会沿用这套上下文。"),
        QStringLiteral("选中模板后可直接带入群名，也可以输入自己的群聊名称。"),
        QStringLiteral("模板 %1 项").arg(rows.size()),
        QStringLiteral("群聊名称"),
        QStringLiteral("群聊定位"),
        QStringLiteral("常用模板"),
        QStringLiteral("从常用场景里选一个起点，减少空白状态下的决策成本。"),
        QStringLiteral("创建表单"),
        QStringLiteral("填写群聊名称，也可以顺手写一句群公告或用途说明。"),
        QStringLiteral("群聊名称"),
        QStringLiteral("例如：我的群聊 / 项目协作群"),
        QStringLiteral("群聊说明（可选）"),
        QStringLiteral("例如：用于讨论版本联调、文件流转和验收安排"),
        QStringLiteral("创建并进入"),
        QStringLiteral("创建群聊并立即切换到该会话"),
        QStringLiteral("取消"),
        QStringLiteral("关闭创建群聊工作区"),
        rows,
        false,
        false,
        QStringLiteral("我的群聊"));

    if (!formResult.applied) {
        return false;
    }

    QString groupName = formResult.primaryValue.trimmed();
    if (groupName.isEmpty()) {
        groupName = formResult.selectedId.trimmed();
    }
    if (groupName.isEmpty()) {
        groupName = QStringLiteral("我的群聊");
    }
    QString announcement = formResult.secondaryValue.trimmed();
    if (!announcement.isEmpty()) {
        announcement = QStringLiteral("%1").arg(announcement);
    }

    const QString groupId = createLocalGroupSession(groupName, QStringList(), announcement);
    switchToLocalGroup(groupId, groupName);
    appendSystemMessage(QStringLiteral("已创建群聊: %1").arg(groupName));
    if (!announcement.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("群聊已创建，并已带入群说明"), 2200);
    } else {
        ui->statusbar->showMessage(QStringLiteral("群聊已创建: %1").arg(groupName), 2200);
    }
    return true;
}

bool MainWindow::showInviteFriendToGroupWorkspace(const QString& groupId, QWidget* parent) {
    if (groupId.isEmpty() || !m_localGroupIds.contains(groupId)) {
        ui->statusbar->showMessage(QStringLiteral("当前群聊不存在，无法邀请好友"), 2200);
        return false;
    }

    QList<QPair<QString, QString>> rows;
    for (const QString& friendId : m_friendIds) {
        const bool alreadyInGroup = m_localGroupMembers.value(groupId).contains(friendId);
        const QString text = QStringLiteral("%1 (QQ:%2)\n%3 · %4")
                                 .arg(contactDisplayName(friendId),
                                      friendId,
                                      isContactOnline(friendId) ? QStringLiteral("在线") : QStringLiteral("离线"),
                                      alreadyInGroup ? QStringLiteral("已在群内") : QStringLiteral("可邀请"));
        rows.append(qMakePair(friendId, text));
    }

    const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
    const GroupWorkspaceResult formResult = runGroupFormWorkspaceDialog(
        parent ? parent : this,
        QStringLiteral("inviteFriendToGroupWorkspaceDialog"),
        QStringLiteral("邀请好友加入群聊"),
        QStringLiteral("邀请好友"),
        QStringLiteral("当前群：%1。先从好友列表里选一个目标，再决定是否补一条邀请备注。").arg(groupName),
        QStringLiteral("搜索好友 QQ / 昵称"),
        QStringLiteral("按 QQ 号或昵称筛选好友，选中后可直接带入邀请目标"),
        QStringLiteral("这里只展示好友列表，已在群内的好友会保留显示，避免重复邀请。"),
        QStringLiteral("选中好友后，这里会显示他的 QQ、状态和是否已在群里。"),
        QStringLiteral("好友 %1 人").arg(rows.size()),
        QStringLiteral("邀请目标"),
        QStringLiteral("邀请备注"),
        QStringLiteral("好友列表"),
        QStringLiteral("优先从现有好友里邀请，减少按 QQ 手输时的出错概率。"),
        QStringLiteral("邀请表单"),
        QStringLiteral("可直接使用选中好友，也可以手动输入 QQ；备注只用于本次提示和上下文整理。"),
        QStringLiteral("好友 QQ"),
        QStringLiteral("例如：10001"),
        QStringLiteral("邀请备注（可选）"),
        QStringLiteral("例如：一起进群讨论版本联调"),
        QStringLiteral("邀请入群"),
        QStringLiteral("把当前目标邀请进本地群聊"),
        QStringLiteral("关闭"),
        QStringLiteral("关闭好友邀请工作区"),
        rows,
        false,
        false);

    if (!formResult.applied) {
        return false;
    }

    QString friendId = formResult.primaryValue.trimmed();
    if (friendId.isEmpty()) {
        friendId = formResult.selectedId.trimmed();
    }
    if (friendId.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("请先选择或输入要邀请的好友 QQ"), 1800);
        return false;
    }
    if (!m_friendIds.contains(friendId)) {
        ui->statusbar->showMessage(QStringLiteral("该 QQ 不是当前好友，请改用按 QQ 号邀请"), 2200);
        return false;
    }
    if (m_localGroupMembers[groupId].contains(friendId)) {
        ui->statusbar->showMessage(QStringLiteral("%1 已在目标群聊中").arg(contactDisplayName(friendId)), 1800);
        return false;
    }

    appendMembersToLocalGroup(groupId, QStringList{friendId});
    switchToLocalGroup(groupId, groupName);
    refreshGroupMemberPanel();
    appendSystemMessage(QStringLiteral("已邀请 %1 加入群聊").arg(contactDisplayName(friendId)));
    saveHistory(groupId,
                QStringLiteral("[%1] [系统] 已邀请 %2 加入群聊")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")),
                         contactDisplayName(friendId)));
    const QString note = formResult.secondaryValue.trimmed();
    if (!note.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("已邀请 %1，并记录备注：%2").arg(contactDisplayName(friendId), note), 2600);
    } else {
        ui->statusbar->showMessage(QStringLiteral("已邀请 %1 加入群聊").arg(contactDisplayName(friendId)), 2200);
    }
    return true;
}

bool MainWindow::showInviteAccountToGroupWorkspace(const QString& groupId, QWidget* parent) {
    if (groupId.isEmpty() || !m_localGroupIds.contains(groupId)) {
        ui->statusbar->showMessage(QStringLiteral("当前群聊不存在，无法按 QQ 邀请"), 2200);
        return false;
    }
    if (!isCurrentUserGroupOwner(groupId)) {
        ui->statusbar->showMessage(QStringLiteral("只有群主可以按 QQ 号邀请新成员入群"), 2400);
        return false;
    }

    QList<QPair<QString, QString>> rows;
    for (const QString& memberId : m_localGroupMembers.value(groupId)) {
        rows.append(qMakePair(memberId,
                              QStringLiteral("%1 (QQ:%2)\n%3")
                                  .arg(contactDisplayName(memberId),
                                       memberId,
                                       memberId == m_currentUserId ? QStringLiteral("你自己")
                                                                   : QStringLiteral("已在当前群聊中"))));
    }
    const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
    const GroupWorkspaceResult formResult = runGroupFormWorkspaceDialog(
        parent ? parent : this,
        QStringLiteral("inviteAccountToGroupWorkspaceDialog"),
        QStringLiteral("按 QQ 号邀请入群"),
        QStringLiteral("按 QQ 号邀请"),
        QStringLiteral("当前群：%1。适合邀请还不是好友的人，系统会在需要时自动补发好友申请。").arg(groupName),
        QStringLiteral("搜索当前群成员"),
        QStringLiteral("先看看群里已有谁，避免重复邀请同一个 QQ"),
        QStringLiteral("系统会拦截自己、已在群内的成员，并在必要时尝试补一条好友申请。"),
        QStringLiteral("可先查看当前群成员，再在下方输入新的 QQ 号继续邀请。"),
        QStringLiteral("当前成员 %1 人").arg(rows.size()),
        QStringLiteral("邀请 QQ"),
        QStringLiteral("邀请备注"),
        QStringLiteral("当前群成员"),
        QStringLiteral("这里只展示当前群已有成员，帮助你避免重复邀请。"),
        QStringLiteral("按 QQ 邀请"),
        QStringLiteral("输入 QQ 号后系统会做重复校验，并在成功后刷新当前群成员面板。"),
        QStringLiteral("QQ 账号"),
        QStringLiteral("例如：10086"),
        QStringLiteral("邀请备注（可选）"),
        QStringLiteral("例如：后续补发好友申请，方便继续私聊"),
        QStringLiteral("邀请入群"),
        QStringLiteral("按 QQ 号执行邀请"),
        QStringLiteral("关闭"),
        QStringLiteral("关闭按 QQ 邀请工作区"),
        rows,
        false,
        false);

    if (!formResult.applied) {
        return false;
    }

    QString account = formResult.primaryValue.trimmed();
    if (account.isEmpty()) {
        account = formResult.selectedId.trimmed();
    }
    if (account.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("请输入 QQ 号后再邀请入群"), 1800);
        return false;
    }

    const QString previousTarget = m_privateChatTarget;
    if (previousTarget != groupId) {
        switchToLocalGroup(groupId, groupName);
    }
    const bool added = addAccountToCurrentLocalGroup(account, parent ? parent : this);
    if (!added && previousTarget != groupId && !previousTarget.isEmpty() && previousTarget != groupId) {
        if (previousTarget.startsWith(QStringLiteral("local_group_"))) {
            switchToLocalGroup(previousTarget, m_localGroupNames.value(previousTarget, QStringLiteral("群聊")));
        }
    }
    return added;
}

bool MainWindow::showRenameGroupWorkspace(const QString& groupId, QWidget* parent) {
    if (groupId.isEmpty() || !m_localGroupIds.contains(groupId)) {
        ui->statusbar->showMessage(QStringLiteral("当前群聊不存在，无法重命名"), 2200);
        return false;
    }

    QList<QPair<QString, QString>> rows;
    const QString oldName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
    rows.append(qMakePair(oldName,
                          QStringLiteral("%1\n当前名称 · 成员 %2 人")
                              .arg(oldName)
                              .arg(m_localGroupMembers.value(groupId).size())));
    rows.append(qMakePair(QStringLiteral("项目协作群"),
                          QStringLiteral("项目协作群\n适合围绕固定项目和版本节奏沟通。")));
    rows.append(qMakePair(QStringLiteral("好友群聊"),
                          QStringLiteral("好友群聊\n适合日常闲聊和邀请可见好友一起进入。")));

    const GroupWorkspaceResult formResult = runGroupFormWorkspaceDialog(
        parent ? parent : this,
        QStringLiteral("renameGroupWorkspaceDialog"),
        QStringLiteral("重命名群聊"),
        QStringLiteral("重命名群聊"),
        QStringLiteral("当前群：%1。建议用更明确的名字表达用途，后续搜索和通知都会更清晰。").arg(oldName),
        QStringLiteral("搜索常用名称"),
        QStringLiteral("从常用群名模板里找灵感，也可以直接输入自己的名称"),
        QStringLiteral("群名会同时影响左侧列表、群公告上下文和后续邀请话术。"),
        QStringLiteral("选中模板后可以直接带入，也可以自己输入更具体的名称。"),
        QStringLiteral("模板 %1 项").arg(rows.size()),
        QStringLiteral("新群名"),
        QStringLiteral("命名说明"),
        QStringLiteral("名称参考"),
        QStringLiteral("保留当前名称和常用模板，方便快速对比后再改。"),
        QStringLiteral("重命名表单"),
        QStringLiteral("请输入新的群聊名称；命名说明只作为这次编辑时的参考。"),
        QStringLiteral("新群聊名称"),
        QStringLiteral("例如：Qt 联调群 / 周会讨论群"),
        QStringLiteral("命名说明（可选）"),
        QStringLiteral("例如：按版本节奏组织群聊，避免和普通闲聊混淆"),
        QStringLiteral("保存名称"),
        QStringLiteral("应用新的群聊名称"),
        QStringLiteral("关闭"),
        QStringLiteral("关闭群聊重命名工作区"),
        rows,
        false,
        false,
        oldName);

    if (!formResult.applied) {
        return false;
    }

    QString newName = formResult.primaryValue.trimmed();
    if (newName.isEmpty()) {
        newName = formResult.selectedId.trimmed();
    }
    if (newName.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("群聊名称不能为空"), 1800);
        return false;
    }
    if (newName == oldName) {
        ui->statusbar->showMessage(QStringLiteral("群聊名称未改变"), 1600);
        return false;
    }
    m_localGroupNames[groupId] = newName;
    saveLocalGroups();
    refreshFriendList();
    if (m_privateChatTarget == groupId) {
        switchToLocalGroup(groupId, newName);
    }
    appendSystemMessage(QStringLiteral("群聊已重命名为：%1").arg(newName));
    ui->statusbar->showMessage(QStringLiteral("群聊已重命名为：%1").arg(newName), 2200);
    return true;
}

bool MainWindow::showEditGroupAnnouncementWorkspace(QWidget* parent) {
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    const bool isServerPublicGroup = m_privateChatTarget.isEmpty();
    if (isLocalGroup && !isCurrentUserGroupOwner(m_privateChatTarget)) {
        ui->statusbar->showMessage(QStringLiteral("只有群主可以编辑群公告"), 2400);
        appendSystemMessage(QStringLiteral("群公告编辑被权限保护拦截：当前账号不是群主"));
        return false;
    }
    if (isServerPublicGroup && !canCurrentUserManageServerGroup(QStringLiteral("public"))) {
        ui->statusbar->showMessage(QStringLiteral("只有群主或管理员可以编辑公共群公告"), 2400);
        appendSystemMessage(QStringLiteral("公共群公告编辑被服务端角色保护拦截"));
        return false;
    }

    QList<QPair<QString, QString>> rows;
    const QString oldText = ui->announcementBodyLabel->text().trimmed();
    rows.append(qMakePair(oldText,
                          QStringLiteral("当前群公告\n%1").arg(oldText.isEmpty() ? QStringLiteral("暂无公告") : oldText)));
    rows.append(qMakePair(QStringLiteral("欢迎加入本群，重要通知会统一在这里更新。"),
                          QStringLiteral("欢迎模板\n适合做基础说明和统一入口。")));
    rows.append(qMakePair(QStringLiteral("本群用于版本联调与问题收敛，请优先同步上下文后再提问。"),
                          QStringLiteral("协作模板\n适合项目群、联调群和验收群。")));

    const GroupWorkspaceResult formResult = runGroupFormWorkspaceDialog(
        parent ? parent : this,
        QStringLiteral("groupAnnouncementWorkspaceDialog"),
        QStringLiteral("编辑群公告"),
        QStringLiteral("编辑群公告"),
        QStringLiteral("建议把公告写成“用途 + 当前节奏 + 注意事项”，这样成员进入群时更容易读懂。"),
        QStringLiteral("搜索公告模板"),
        QStringLiteral("按关键词筛选公告模板，选中后可带入下方继续编辑"),
        QStringLiteral("群公告会显示在右侧信息区，也是新成员理解群上下文的第一入口。"),
        QStringLiteral("选中模板后可直接带入，也可以继续补充更具体的上下文。"),
        QStringLiteral("模板 %1 项").arg(rows.size()),
        QStringLiteral("公告内容"),
        QStringLiteral("补充说明"),
        QStringLiteral("公告参考"),
        QStringLiteral("保留当前公告和常用模板，便于在已有上下文上继续修改。"),
        QStringLiteral("公告编辑"),
        QStringLiteral("建议直接在这里完成最终文案，避免后续再来回修改。"),
        QStringLiteral("群公告内容"),
        QStringLiteral("请输入新的群公告内容"),
        QStringLiteral("补充说明（可选）"),
        QStringLiteral("例如：这次主要补充版本节奏和群内响应约定"),
        QStringLiteral("保存公告"),
        QStringLiteral("提交并应用新的群公告"),
        QStringLiteral("关闭"),
        QStringLiteral("关闭群公告工作区"),
        rows,
        true,
        false,
        oldText);

    if (!formResult.applied) {
        ui->statusbar->showMessage(QStringLiteral("已取消编辑群公告"), 1600);
        return false;
    }

    const GroupAnnouncementEditDecision announcementDecision =
        GroupManager::announcementEditDecision(oldText,
                                               formResult.primaryValue,
                                               isLocalGroup,
                                               ui->chatTitleLabel->text());
    if (!announcementDecision.changed) {
        ui->statusbar->showMessage(announcementDecision.unchangedStatusMessage, 1600);
        return false;
    }
    if (isServerPublicGroup) {
        if (!m_client || !m_client->sendServerGroupAnnouncementUpdate(QStringLiteral("public"), announcementDecision.text)) {
            ui->statusbar->showMessage(QStringLiteral("群公告提交失败，请检查连接状态"), 2400);
            appendSystemMessage(QStringLiteral("群公告提交失败：客户端未连接或发送失败"));
            return false;
        }
        appendSystemMessage(QStringLiteral("群公告更新已提交，等待服务端同步"));
        ui->statusbar->showMessage(announcementDecision.submittedStatusMessage, 2200);
        return true;
    }

    ui->announcementBodyLabel->setText(announcementDecision.text);
    if (isLocalGroup) {
        m_localGroupAnnouncements[m_privateChatTarget] = announcementDecision.text;
        saveLocalGroups();
        saveHistory(m_privateChatTarget,
                    QStringLiteral("[%1] [系统] 群公告已更新: %2")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")),
                             announcementDecision.text));
    }
    appendSystemMessage(QStringLiteral("群公告已更新"));
    ui->statusbar->showMessage(announcementDecision.appliedStatusMessage, 2200);
    return true;
}

bool MainWindow::addAccountToCurrentLocalGroup(const QString& account, QWidget* parent) {
    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("请输入 QQ 号后再邀请入群"), 1800);
        return false;
    }
    if (!m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
        ui->statusbar->showMessage(QStringLiteral("当前不是本地群聊，不能直接邀请入群"), 2200);
        return false;
    }
    if (!isCurrentUserGroupOwner(m_privateChatTarget)) {
        ui->statusbar->showMessage(QStringLiteral("只有群主可以邀请新成员入群"), 2400);
        return false;
    }
    if (normalizedAccount == m_currentUserId) {
        ui->statusbar->showMessage(QStringLiteral("你已在当前群聊中，无需重复邀请"), 1800);
        return false;
    }
    if (m_localGroupMembers[m_privateChatTarget].contains(normalizedAccount)) {
        ui->statusbar->showMessage(QStringLiteral("该 QQ 已在当前群聊中"), 1800);
        return false;
    }

    QString requestNote;
    m_localGroupMembers[m_privateChatTarget] << normalizedAccount;
    if (!m_friendIds.contains(normalizedAccount) && !m_pendingOutgoingFriendRequests.contains(normalizedAccount)) {
        const QString displayName = contactDisplayName(normalizedAccount);
        if (m_client && m_client->sendFriendRequest(normalizedAccount)) {
            m_friendNames[normalizedAccount] = displayName;
            m_pendingOutgoingFriendRequests << normalizedAccount;
            requestNote = QStringLiteral("，好友申请等待确认");
        } else {
            requestNote = QStringLiteral("，好友申请发起失败");
            ui->statusbar->showMessage(QStringLiteral("已邀请入群，但好友申请发起失败：%1").arg(displayName), 3000);
        }
    } else if (m_pendingOutgoingFriendRequests.contains(normalizedAccount)) {
        requestNote = QStringLiteral("，好友申请已在等待确认");
    }
    saveLocalGroups();
    refreshFriendList();
    switchToLocalGroup(m_privateChatTarget, m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊")));
    refreshGroupMemberPanel();
    appendSystemMessage(QStringLiteral("已按 QQ 号邀请 %1 加入群聊%2").arg(normalizedAccount, requestNote));
    saveHistory(m_privateChatTarget,
                QStringLiteral("[%1] [系统] 已按 QQ 号邀请 %2 加入群聊")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), normalizedAccount));
    Q_UNUSED(parent)
    return true;
}

bool MainWindow::handleGroupMemberSearchSubmit(const QString& text, QWidget* parent) {
    const QString normalizedText = text.trimmed();
    if (normalizedText.isEmpty()) {
        if (ui->memberSearchEdit) {
            ui->memberSearchEdit->setFocus();
        }
        ui->statusbar->showMessage(m_privateChatTarget.startsWith(QStringLiteral("local_group_"))
                                       ? QStringLiteral("请输入 QQ 号后邀请入群")
                                       : QStringLiteral("请输入 QQ 号或关键词后再搜索"),
                                   1800);
        return false;
    }
    if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
        return addAccountToCurrentLocalGroup(normalizedText, parent);
    }
    if (canCurrentUserManageServerGroup(QStringLiteral("public"))) {
        return requestServerGroupMemberUpdate(normalizedText, QStringLiteral("add"));
    }
    searchAndAddAccount(normalizedText, parent ? parent : this);
    return true;
}

bool MainWindow::handleGroupMemberEntryActivated(const QString& targetId, QWidget* parent) {
    const QString normalizedTargetId = targetId.trimmed();
    if (normalizedTargetId.isEmpty() || normalizedTargetId == m_currentUserId) {
        return false;
    }
    if (normalizedTargetId.startsWith(QStringLiteral("group_search_add:"))) {
        const QString account = normalizedTargetId.mid(QStringLiteral("group_search_add:").size()).trimmed();
        if (m_privateChatTarget.isEmpty() && canCurrentUserManageServerGroup(QStringLiteral("public"))) {
            return requestServerGroupMemberUpdate(account, QStringLiteral("add"));
        }
        searchAndAddAccount(account, parent ? parent : this);
        return true;
    }
    if (normalizedTargetId.startsWith(QStringLiteral("group_invite:"))) {
        const QString account = normalizedTargetId.mid(QStringLiteral("group_invite:").size()).trimmed();
        return addAccountToCurrentLocalGroup(account, parent);
    }
    ensureFriendRequestQueued(normalizedTargetId,
                              QStringLiteral("已向群成员发起好友申请 QQ:%1，等待对方同意"),
                              true);
    openPrivateSession(normalizedTargetId);
    return true;
}

void MainWindow::copyVisibleGroupMembers(bool onlineOnly) {
    QStringList cards;
    if (!m_groupMemberModel) {
        ui->statusbar->showMessage(onlineOnly ? QStringLiteral("当前没有可复制的在线成员") : QStringLiteral("当前没有可复制成员"), 2200);
        return;
    }
    for (int i = 0; i < m_groupMemberModel->rowCount(); ++i) {
        QStandardItem* item = m_groupMemberModel->item(i);
        if (!item) continue;
        const QString id = item->data(Qt::UserRole + 1).toString();
        if (id.isEmpty() || id.startsWith(QStringLiteral("group_search_add:")) || id.startsWith(QStringLiteral("group_invite:"))) continue;
        if (onlineOnly && id != m_currentUserId && !isContactOnline(id)) continue;
        cards << QStringLiteral("QQ:%1 昵称:%2 状态:%3")
                     .arg(id,
                          contactDisplayName(id),
                          (id == m_currentUserId || isContactOnline(id)) ? QStringLiteral("在线") : QStringLiteral("离线"));
    }
    if (cards.isEmpty()) {
        ui->statusbar->showMessage(onlineOnly ? QStringLiteral("当前筛选没有在线成员") : QStringLiteral("当前筛选没有可复制成员"), 2200);
        return;
    }
    QApplication::clipboard()->setText(cards.join(QLatin1Char('\n')));
    ui->statusbar->showMessage(onlineOnly
                                   ? QStringLiteral("已复制 %1 个在线成员").arg(cards.size())
                                   : QStringLiteral("已复制 %1 个可见成员").arg(cards.size()),
                               2200);
}

bool MainWindow::promptAndSetGroupMemberRemark(const QString& memberId, QWidget* parent) {
    if (memberId.trimmed().isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("当前条目不支持设置备注"), 1800);
        return false;
    }
    const QString oldRemark = contactDisplayName(memberId);
    bool ok = false;
    const QString remark = promptTextValue(QStringLiteral("设置备注"),
                                           QStringLiteral("备注名称:"),
                                           oldRemark,
                                           &ok,
                                           parent ? parent : this);
    if (!ok) {
        return false;
    }
    if (remark.isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("备注名称不能为空"), 1800);
        return false;
    }
    if (remark == oldRemark) {
        ui->statusbar->showMessage(QStringLiteral("备注未改变"), 1600);
        return false;
    }
    m_friendNames[memberId] = remark;
    if (m_friendIds.contains(memberId)) {
        saveFriends();
        ui->statusbar->showMessage(QStringLiteral("已设置备注：%1").arg(remark), 2200);
    } else {
        ui->statusbar->showMessage(QStringLiteral("已为群成员 %1 设置临时备注，未改变好友关系").arg(memberId), 2600);
    }
    refreshFriendList();
    refreshGroupMemberPanel();
    appendSystemMessage(QStringLiteral("已设置 %1 的备注为 %2").arg(memberId, remark));
    return true;
}

bool MainWindow::removeGroupMemberWithConfirmation(const QString& memberId, QWidget* parent) {
    if (memberId.trimmed().isEmpty()) {
        ui->statusbar->showMessage(QStringLiteral("当前成员不可从群聊移除"), 2200);
        return false;
    }
    const bool isLocalGroup = m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    const bool isServerPublicGroup = m_privateChatTarget.isEmpty() && !m_serverGroupMembers.value(QStringLiteral("public")).isEmpty();
    if (!isLocalGroup && !isServerPublicGroup) {
        ui->statusbar->showMessage(QStringLiteral("当前会话不支持移除群成员"), 2200);
        return false;
    }
    const GroupMemberContextMenuPlan plan = m_groupManager.memberContextMenuPlan(
        memberId,
        m_currentUserId,
        isLocalGroup,
        isServerPublicGroup,
        isLocalGroup ? groupOwnerId(m_privateChatTarget) : QString(),
        isLocalGroup && isCurrentUserGroupOwner(m_privateChatTarget),
        m_serverGroupOwners,
        m_serverGroupMemberRoles);
    if (!plan.canManageGroup) {
        ui->statusbar->showMessage(plan.removeDeniedMessage, 2400);
        return false;
    }
    if (memberId == plan.ownerId) {
        ui->statusbar->showMessage(plan.ownerRemoveDeniedMessage, 2200);
        return false;
    }
    const QString memberName = contactDisplayName(memberId);
    const QString groupName = isLocalGroup ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
                                           : m_serverGroupNames.value(QStringLiteral("public"), QStringLiteral("公共聊天室"));
    if (!confirmDestructiveAction(QStringLiteral("移出群成员"),
                                  QStringLiteral("确定将“%1”移出群聊“%2”吗？").arg(memberName, groupName),
                                  QStringLiteral("移出成员"),
                                  QStringLiteral("保留成员"),
                                  parent ? parent : this)) {
        ui->statusbar->showMessage(QStringLiteral("已取消移出群成员"), 1600);
        return false;
    }
    if (isLocalGroup) {
        m_localGroupMembers[m_privateChatTarget].removeAll(memberId);
        saveLocalGroups();
        refreshGroupMemberPanel();
        appendSystemMessage(QStringLiteral("已将 %1 移出群聊").arg(memberName));
        return true;
    }
    return requestServerGroupMemberUpdate(memberId, QStringLiteral("remove"));
}

bool MainWindow::handleCreateMenuCommand(const QString& commandId) {
    if (commandId == QLatin1String("create-group")) {
        showCreateGroupWorkspace(this);
        return true;
    }

    if (commandId == QLatin1String("create-group-with-friends")) {
        QString groupName = ui->contactSearchEdit->text().trimmed();
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("好友群聊");
        }

        const QStringList members = m_friendIds;
        const QString groupId = createLocalGroupSession(
            groupName,
            members,
            QStringLiteral("%1 已创建，已自动邀请全部好友。").arg(groupName));
        switchToLocalGroup(groupId, groupName);
        const int invitedCount = members.size();
        appendSystemMessage(QStringLiteral("已创建群聊并邀请 %1 位好友").arg(invitedCount));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已创建群聊并邀请 %2 位好友")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")))
                        .arg(invitedCount));
        return true;
    }

    if (commandId == QLatin1String("open-global-search")) {
        onShowGlobalSearch();
        return true;
    }
    if (commandId == QLatin1String("focus-contact-search")) {
        ui->contactSearchEdit->setFocus();
        ui->contactSearchEdit->selectAll();
        ui->statusbar->showMessage(QStringLiteral("已定位到 QQ 搜索框，输入账号后回车自动查找"), 2500);
        return true;
    }
    if (commandId == QLatin1String("refresh-contacts")) {
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("联系人和群成员已刷新"), 2000);
        return true;
    }
    if (commandId == QLatin1String("clear-search")) {
        ui->contactSearchEdit->clear();
        ui->memberSearchEdit->clear();
        m_contactFilter.clear();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("搜索条件已清空"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-id")) {
        QString chatId = m_privateChatTarget;
        if (chatId.startsWith(QStringLiteral("local_group_"))) {
            chatId = chatId.mid(QStringLiteral("local_group_").size());
        }
        if (chatId.isEmpty()) {
            chatId = m_currentUserId;
        }
        copyTextWithStatus(chatId, QStringLiteral("当前会话号已复制: ") + chatId, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-card")) {
        QString card;
        if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            card = QStringLiteral("群聊 QQ:%1\n%2\n公告:%3")
                .arg(m_privateChatTarget.mid(QStringLiteral("local_group_").size()),
                     m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊")),
                     m_localGroupAnnouncements.value(m_privateChatTarget, ui->announcementBodyLabel->text()));
        } else if (!m_privateChatTarget.isEmpty()) {
            card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3")
                .arg(m_privateChatTarget,
                     contactDisplayName(m_privateChatTarget),
                     isContactOnline(m_privateChatTarget) ? QStringLiteral("在线") : QStringLiteral("离线"));
        } else {
            card = QStringLiteral("公共聊天室\n当前账号:%1\n在线成员:%2")
                .arg(m_currentUserId)
                .arg(m_knownUsers.size());
        }
        copyTextWithStatus(card, QStringLiteral("当前会话名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-current-invite")) {
        QString text;
        if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            text = QStringLiteral("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。")
                .arg(m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊")),
                     m_privateChatTarget.mid(QStringLiteral("local_group_").size()),
                     m_currentUserName,
                     m_currentUserId);
        } else if (!m_privateChatTarget.isEmpty()) {
            text = QStringLiteral("你好 %1，我是 %2（QQ:%3）。方便的话加个好友，我们可以继续私聊。")
                .arg(contactDisplayName(m_privateChatTarget), m_currentUserName, m_currentUserId);
        } else {
            text = QStringLiteral("你好，我是 %1（QQ:%2），欢迎加入公共聊天室，也可以通过 QQ 搜索加我好友。")
                .arg(m_currentUserName, m_currentUserId);
        }
        copyTextWithStatus(text, QStringLiteral("当前会话邀请语已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-members")) {
        QStringList cards;
        const QStringList ids = currentSessionMemberIds();
        for (const QString& id : ids) {
            cards << QStringLiteral("QQ:%1 昵称:%2 状态:%3")
                .arg(id,
                     contactDisplayName(id),
                     (id == m_currentUserId || isContactOnline(id)) ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前会话没有成员可复制"), 2200);
            return true;
        }
        copyTextWithStatus(cards.join(QLatin1Char('\n')),
                           QStringLiteral("已复制 %1 个当前成员").arg(cards.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-online")) {
        QStringList cards;
        const QStringList ids = currentSessionMemberIds();
        for (const QString& id : ids) {
            if (id != m_currentUserId && !isContactOnline(id)) {
                continue;
            }
            cards << QStringLiteral("在线 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
        }
        if (cards.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前会话没有在线成员可复制"), 2200);
            return true;
        }
        copyTextWithStatus(cards.join(QLatin1Char('\n')),
                           QStringLiteral("已复制 %1 个在线成员").arg(cards.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-all-contacts")) {
        QStringList rows;
        rows << QStringLiteral("我的QQ:%1 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("好友:%1 群聊:%2 在线:%3")
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size());
        for (const QString& id : m_friendIds) {
            rows << QStringLiteral("好友 QQ:%1 昵称:%2 状态:%3")
                .arg(id,
                     contactDisplayName(id),
                     isContactOnline(id) ? QStringLiteral("在线") : QStringLiteral("离线"));
        }
        for (const QString& groupId : m_localGroupIds) {
            rows << QStringLiteral("群聊 QQ:%1 名称:%2 成员:%3")
                .arg(groupId.mid(QStringLiteral("local_group_").size()),
                     m_localGroupNames.value(groupId, QStringLiteral("群聊")),
                     QString::number(m_localGroupMembers.value(groupId).size()));
        }
        copyTextWithStatus(rows.join(QLatin1Char('\n')),
                           QStringLiteral("已复制联系人摘要 %1 行").arg(rows.size()),
                           2200);
        return true;
    }
    if (commandId == QLatin1String("copy-search-summary")) {
        QString filter = ui->contactSearchEdit->text().trimmed();
        if (filter.isEmpty()) {
            filter = QStringLiteral("全部");
        }
        const QString summary = QStringLiteral("QQ搜索:%1\n好友:%2\n本地群:%3\n在线成员:%4\n当前会话:%5")
            .arg(filter)
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size())
            .arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(summary, QStringLiteral("QQ 搜索摘要已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-quick-guide")) {
        QStringList rows;
        rows << QStringLiteral("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("1. 点击综合搜索可按 QQ 号/昵称查找用户、好友和群聊");
        rows << QStringLiteral("2. 搜索结果可直接打开、发起好友申请、复制名片或复制邀请卡");
        rows << QStringLiteral("3. 好友申请支持推荐在线用户、复制申请话术和自动发送申请");
        rows << QStringLiteral("4. 好友管理器可搜索、备注、邀入群、复制在线好友和统计");
        rows << QStringLiteral("当前好友:%1 · 群聊:%2 · 在线:%3")
            .arg(m_friendIds.size())
            .arg(m_localGroupIds.size())
            .arg(m_knownUsers.size());
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("QQ 功能指南已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-media-guide")) {
        QStringList rows;
        rows << QStringLiteral("上传指南 · 我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        rows << QStringLiteral("1. 点击 图片/视频 可发送 png、jpg、gif、mp4、mov、avi、mkv、wmv、flv、webm");
        rows << QStringLiteral("2. 图片会显示预览卡片，视频会以文件卡片发送");
        rows << QStringLiteral("3. 点击 闪传文件 可发送文档、压缩包和媒体文件");
        rows << QStringLiteral("4. 聊天记录右键可复制媒体卡片或查收话术");
        rows << QStringLiteral("当前会话:%1")
            .arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("上传指南已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-current-media-pack")) {
        const QString sessionName = m_privateChatTarget.isEmpty()
            ? QStringLiteral("公共聊天室")
            : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith(QStringLiteral("local_group_"))) {
            sessionId = sessionId.mid(QStringLiteral("local_group_").size());
        }
        if (sessionId.isEmpty()) {
            sessionId = QStringLiteral("public");
        }
        QStringList rows;
        rows << QStringLiteral("媒体发送包 · 会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QStringLiteral("发送者:%1 · QQ:%2").arg(m_currentUserName, m_currentUserId);
        rows << QStringLiteral("图片/视频入口：点击工具栏 图片/视频，或菜单栏 发送图片/视频");
        rows << QStringLiteral("文件入口：点击工具栏 闪传文件，或菜单栏 闪传文件");
        rows << QStringLiteral("支持格式：png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm + 文档/压缩包");
        rows << QStringLiteral("查收话术：我已准备发送图片/视频/文件到 %1，请注意查收。").arg(sessionName);
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("当前媒体发送包已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-full-media-plan")) {
        const QString sessionName = m_privateChatTarget.isEmpty()
            ? QStringLiteral("公共聊天室")
            : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith(QStringLiteral("local_group_"))) {
            sessionId = sessionId.mid(QStringLiteral("local_group_").size());
        }
        if (sessionId.isEmpty()) {
            sessionId = QStringLiteral("public");
        }
        QStringList rows;
        rows << QStringLiteral("完整媒体计划 · 当前会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QStringLiteral("我的QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4 · 在线:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 QString::number(m_knownUsers.size()));
        rows << QStringLiteral("1. 先用综合搜索/好友申请确认目标 QQ 或群聊");
        rows << QStringLiteral("2. 通过好友管理/群通知复制媒体包、邀请语和成员列表");
        rows << QStringLiteral("3. 点击 图片/视频 发送图片、GIF 或视频；点击 闪传文件 发送文档和压缩包");
        rows << QStringLiteral("4. 发送后聊天记录会生成媒体卡片、查收话术；接收后生成回执话术和保存路径");
        rows << QStringLiteral("当前查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(sessionName);
        copyTextWithStatus(rows.join(QLatin1Char('\n')), QStringLiteral("完整媒体计划已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("edit-announcement")) {
        onEditGroupAnnouncement();
        return true;
    }
    if (commandId == QLatin1String("copy-announcement")) {
        copyTextWithStatus(ui->announcementBodyLabel->text(), QStringLiteral("群公告已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("show-friend-notice")) {
        onShowFriendNotifications();
        return true;
    }
    if (commandId == QLatin1String("show-group-notice")) {
        onShowGroupNotifications();
        return true;
    }
    if (commandId == QLatin1String("send-image")) {
        onSendImage();
        return true;
    }
    if (commandId == QLatin1String("send-file")) {
        onSendFile();
        return true;
    }
    return false;
}

void MainWindow::setChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs) {
    ui->messageEdit->setPlainText(text);
    ui->messageEdit->setFocus();
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

void MainWindow::insertChatDraftText(const QString& text, const QString& statusMessage, int timeoutMs) {
    ui->messageEdit->insertPlainText(text);
    ui->messageEdit->setFocus();
    ui->statusbar->showMessage(statusMessage, timeoutMs);
}

ChatContextComposerState MainWindow::currentChatContextComposerState() const {
    ChatContextComposerState composerState;
    composerState.privateChatTarget = m_privateChatTarget;
    composerState.targetDisplayName = m_privateChatTarget.isEmpty()
        ? QStringLiteral("公共聊天室")
        : contactDisplayName(m_privateChatTarget);
    composerState.currentUserId = m_currentUserId;
    composerState.currentUserName = m_currentUserName;
    composerState.currentGroupName = m_privateChatTarget.startsWith("local_group_")
        ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
        : QStringLiteral("群聊");
    composerState.currentGroupMemberCount = m_localGroupMembers.value(m_privateChatTarget).size();
    composerState.currentTargetOnline = isContactOnline(m_privateChatTarget);
    composerState.friendCount = m_friendIds.size();
    composerState.localGroupCount = m_localGroupIds.size();
    composerState.knownUserCount = m_knownUsers.size();
    return composerState;
}

bool MainWindow::applyChatContextComposerCommand(const QString& commandId) {
    const ChatContextComposerCommand command =
        ChatContextManager::composerCommand(commandId, currentChatContextComposerState());
    if (!command.handled) {
        return false;
    }

    if (command.action == ChatContextComposerCommand::Action::SetDraft) {
        setChatDraftText(command.text, command.statusMessage, command.timeoutMs);
        return true;
    }
    if (command.action == ChatContextComposerCommand::Action::InsertText) {
        insertChatDraftText(command.text, command.statusMessage, command.timeoutMs);
        return true;
    }
    return false;
}

QAction* MainWindow::addChatContextAction(QMenu& menu,
                                          const QString& title,
                                          const QString& tip,
                                          const QString& commandId,
                                          bool enabled) {
    QAction* action = menu.addAction(title);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    action->setData(commandId);
    action->setEnabled(enabled);
    return action;
}

bool MainWindow::handleChatContextCommand(const QString& commandId,
                                          const QString& chatText,
                                          const LocalSavedFileState& savedFileState) {
    const ChatContextCommandRoute route = ChatContextManager::commandRoute(commandId);
    if (!route.handled) {
        return false;
    }

    const QString targetDisplayName = m_privateChatTarget.isEmpty()
        ? QStringLiteral("公共聊天室")
        : contactDisplayName(m_privateChatTarget);

    if (route.kind == ChatContextCommandRoute::Kind::Copy) {
        const ChatContextCopyResult copyResult = ChatContextManager::copyCommandResult(
            commandId,
            chatText,
            m_privateChatTarget,
            targetDisplayName,
            m_currentUserId,
            m_currentUserName);
        if (!copyResult.handled) {
            return false;
        }
        copyTextWithStatus(copyResult.clipboardText, copyResult.statusMessage, copyResult.timeoutMs);
        return true;
    }

    if (route.kind == ChatContextCommandRoute::Kind::SavedFile) {
        return handleSavedFileContextCommand(commandId, chatText, savedFileState);
    }

    if (route.kind == ChatContextCommandRoute::Kind::Draft) {
        const ChatContextDraftResult draftResult = ChatContextManager::draftCommandResult(
            commandId,
            chatText,
            m_privateChatTarget,
            targetDisplayName);
        if (!draftResult.handled) {
            return false;
        }
        if (draftResult.action == ChatContextDraftResult::Action::SetDraft) {
            setChatDraftText(draftResult.draftText, draftResult.statusMessage, draftResult.timeoutMs);
        } else if (draftResult.action == ChatContextDraftResult::Action::Resend) {
            ui->messageEdit->setPlainText(draftResult.resendText);
            ui->messageEdit->setFocus();
            onSendMessage();
        }
        return true;
    }

    return false;
}

void MainWindow::updateSavedOutgoingTransferRecoveryUi(bool announce) {
    if (!m_resumeSavedTransferAction || !m_clearSavedTransferAction) return;

    QJsonObject state;
    const bool hasSavedTransfer = m_client && m_client->loadOutgoingTransferState(&state);
    const QJsonObject recoveryStatus = hasSavedTransfer ? m_client->savedOutgoingTransferRecoveryStatus() : QJsonObject();
    const TransferRecoveryUiState uiState = m_transferManager.recoveryUiState(
        hasSavedTransfer,
        m_client && m_client->isConnected(),
        state,
        recoveryStatus,
        announce);
    applyTransferActionState(m_resumeSavedTransferAction, uiState.resumeAction);
    applyTransferActionState(m_clearSavedTransferAction, uiState.clearAction);
    refreshTransferWorkspaceCard(&uiState, nullptr, nullptr);

    if (announce && !uiState.announceMessage.isEmpty()) {
        appendSystemMessage(uiState.announceMessage);
        ui->statusbar->showMessage(uiState.statusMessage, uiState.canAutoResume ? 3200 : 3600);
    }
}

void MainWindow::onClearSavedOutgoingTransfer() {
    if (!m_client) return;

    QJsonObject state;
    if (!m_client->loadOutgoingTransferState(&state)) {
        updateSavedOutgoingTransferRecoveryUi(false);
        ui->statusbar->showMessage(m_transferManager.clearRecoveryPrompt(QJsonObject()).noSavedStatusMessage, 2200);
        return;
    }

    const TransferClearRecoveryPrompt prompt = m_transferManager.clearRecoveryPrompt(state);
    if (!confirmDestructiveAction(prompt.title,
                                  prompt.message,
                                  QStringLiteral("清除记录"),
                                  QStringLiteral("保留记录"),
                                  this)) {
        ui->statusbar->showMessage(prompt.keptStatusMessage, 1800);
        return;
    }

    if (m_client->clearOutgoingTransferState()) {
        appendSystemMessage(prompt.clearedSystemMessage);
        ui->statusbar->showMessage(prompt.clearedStatusMessage, 2200);
    } else {
        appendSystemMessage(prompt.clearFailedSystemMessage);
        ui->statusbar->showMessage(prompt.clearFailedStatusMessage, 2600);
    }
    updateSavedOutgoingTransferRecoveryUi(false);
}

void MainWindow::onResumeSavedOutgoingTransfer() {
    if (!m_client) return;

    QJsonObject state;
    if (!m_client->loadOutgoingTransferState(&state)) {
        updateSavedOutgoingTransferRecoveryUi(false);
        ui->statusbar->showMessage("暂无可恢复的未完成发送", 2200);
        return;
    }

    const QFileInfo info(state["filePath"].toString());
    const QString fileName = info.fileName().isEmpty() ? "未命名文件" : info.fileName();
    const QString receiverId = state["receiverId"].toString().trimmed();
    const QString targetName = receiverId.isEmpty() ? "公共聊天室" : QString("QQ:%1").arg(receiverId);
    const QJsonObject recoveryStatus = m_client->savedOutgoingTransferRecoveryStatus();
    if (!recoveryStatus.value("canAutoResume").toBool(false)) {
        const TransferResumeBlockedPrompt prompt = m_transferManager.resumeBlockedPrompt(state, recoveryStatus);
        appendSystemMessage(prompt.systemMessage);
        ui->chatHintLabel->setText(prompt.hintText);
        ui->statusbar->showMessage(prompt.statusMessage, 3600);
        if (confirmDestructiveAction(prompt.title,
                                     prompt.message,
                                     QStringLiteral("丢弃记录"),
                                     QStringLiteral("稍后处理"),
                                     this) && m_client->clearOutgoingTransferState()) {
            appendSystemMessage(prompt.clearedSystemMessage);
            ui->chatHintLabel->setText(prompt.clearedHintText);
            ui->statusbar->showMessage(prompt.clearedStatusMessage, 2200);
        }
        updateSavedOutgoingTransferRecoveryUi(false);
        return;
    }

        TransferOperationDialog progress(this);
        setupTransferOperationDialog(progress,
                                     this,
                                     QStringLiteral("transferResumeDialog"),
                                     QStringLiteral("恢复发送"),
                                     QStringLiteral("恢复未完成发送"),
                                     QStringLiteral("恢复过程会和文件工作区保持同步，方便你判断是否继续等待或改为手动重发。"),
                                     QStringLiteral("取消恢复后，系统会保留最近进度和阻塞原因。"),
                                     QStringLiteral("取消恢复"));
    TransferSendUiState resumingWorkspaceState;
    resumingWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 正在恢复");
    resumingWorkspaceState.workspaceDetail = QStringLiteral("正在恢复未完成发送“%1”到 %2。进度和结果会继续显示在这里。")
        .arg(fileName, targetName);
    resumingWorkspaceState.hintText = QStringLiteral("正在恢复未完成发送 · %1").arg(fileName);
    resumingWorkspaceState.statusMessage = QStringLiteral("正在恢复未完成发送：") + fileName;
    resumingWorkspaceState.statusTone = QStringLiteral("accent");
    setTransferWorkspaceSendState(resumingWorkspaceState);

    const TransferProgressUiState initialResumeState = m_transferManager.resumeInitialState(fileName, targetName);
    updateTransferOperationDialog(progress,
                                  QStringLiteral("准备恢复"),
                                  QStringLiteral("正在恢复未完成发送“%1”到 %2。").arg(fileName, targetName),
                                  initialResumeState.percent,
                                  QStringLiteral("%1% · 等待恢复").arg(initialResumeState.percent),
                                  QStringLiteral("accent"));
    progress.dialog.show();
    QApplication::processEvents();

    bool cancelRequested = false;
    QMetaObject::Connection cancelConnection = connect(
        progress.cancelButton,
        &QPushButton::clicked,
        this,
        [this, &progress, &cancelRequested, &fileName]() {
            cancelRequested = true;
            const TransferProgressUiState cancelState = m_transferManager.resumeCancelState(fileName);
            updateTransferOperationDialog(progress,
                                          QStringLiteral("正在取消恢复"),
                                          QStringLiteral("正在取消未完成发送“%1”的恢复流程，结束后会在文件工作区显示最终状态。")
                                              .arg(fileName),
                                          cancelState.percent,
                                          QStringLiteral("%1% · 正在取消").arg(cancelState.percent),
                                          QStringLiteral("warning"));
            TransferSendUiState cancelWorkspaceState;
            cancelWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 正在取消恢复");
            cancelWorkspaceState.workspaceDetail = QStringLiteral("正在取消未完成发送“%1”的恢复流程，结束后会在这里显示最终状态。")
                .arg(fileName);
            cancelWorkspaceState.hintText = QStringLiteral("正在取消恢复发送 · %1").arg(fileName);
            cancelWorkspaceState.statusMessage = QStringLiteral("正在取消恢复发送：") + fileName;
            cancelWorkspaceState.statusTone = QStringLiteral("warning");
            setTransferWorkspaceSendState(cancelWorkspaceState);
            if (m_client) m_client->cancelCurrentOutgoingTransfer();
            ui->statusbar->showMessage("正在取消恢复发送：" + fileName, 1600);
            QApplication::processEvents();
        });
    QMetaObject::Connection progressConnection = connect(
        m_client,
        &Client::fileTransferProgress,
            this,
            [this, &progress, &fileName, &targetName](const QString& currentFileName, qint64 bytesPrepared, qint64 totalBytes) {
                if (currentFileName != fileName) return;
                const TransferProgressUiState state = m_transferManager.resumeProgressState(fileName, targetName, bytesPrepared, totalBytes);
                updateTransferOperationDialog(progress,
                                              QStringLiteral("正在恢复"),
                                              QStringLiteral("正在恢复“%1”到 %2，已准备 %3 / %4。")
                                                  .arg(fileName,
                                                       targetName,
                                                       LocalFileManager::humanFileSize(bytesPrepared),
                                                       LocalFileManager::humanFileSize(totalBytes)),
                                              state.percent,
                                              QStringLiteral("%1% · %2").arg(state.percent).arg(state.labelText),
                                              QStringLiteral("accent"));
                TransferSendUiState progressWorkspaceState;
                progressWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 正在恢复");
                progressWorkspaceState.workspaceDetail = QStringLiteral("正在恢复“%1”到 %2，已准备 %3 / %4。")
                    .arg(fileName,
                         targetName,
                         LocalFileManager::humanFileSize(bytesPrepared),
                         LocalFileManager::humanFileSize(totalBytes));
                progressWorkspaceState.hintText = QStringLiteral("正在恢复未完成发送 · %1").arg(fileName);
                progressWorkspaceState.statusMessage = QStringLiteral("正在恢复未完成发送：") + fileName;
                progressWorkspaceState.statusTone = QStringLiteral("accent");
                setTransferWorkspaceSendState(progressWorkspaceState);
                QApplication::processEvents();
            });
    QMetaObject::Connection preparedConnection = connect(
        m_client,
        &Client::fileTransferPrepared,
        this,
        [this, &progress, &fileName, &targetName](const QString& currentFileName,
                                             qint64 totalBytes,
                                             qint64 chunkSize,
                                             qint64 chunkCount,
                                             const QString& fileHash) {
            if (currentFileName != fileName) return;
            const TransferProgressUiState state = m_transferManager.resumePreparedState(fileName, targetName, totalBytes, chunkSize, chunkCount, fileHash);
            updateTransferOperationDialog(progress,
                                          QStringLiteral("恢复清单已就绪"),
                                          QStringLiteral("未完成发送“%1”已恢复校验清单并继续发送到 %2。%3")
                                              .arg(fileName, targetName, state.manifestSummary),
                                          qMax(state.percent, 10),
                                          QStringLiteral("%1% · 清单已恢复").arg(qMax(state.percent, 10)),
                                          QStringLiteral("success"));
            TransferSendUiState preparedWorkspaceState;
            preparedWorkspaceState.workspaceTitle = QStringLiteral("文件工作区 · 恢复清单已就绪");
            preparedWorkspaceState.workspaceDetail = QStringLiteral("未完成发送“%1”已恢复校验清单并继续发送到 %2。%3")
                .arg(fileName, targetName, state.manifestSummary);
            preparedWorkspaceState.hintText = QStringLiteral("已恢复发送清单 · %1").arg(fileName);
            preparedWorkspaceState.statusMessage = QStringLiteral("恢复发送清单已生成：") + fileName;
            preparedWorkspaceState.statusTone = QStringLiteral("success");
            setTransferWorkspaceSendState(preparedWorkspaceState);
            QApplication::processEvents();
        });

    ui->statusbar->showMessage("正在恢复未完成发送：" + fileName, 1800);
    QString rejectReason;
    const bool resumed = m_client->resumeSavedOutgoingTransfer(&rejectReason, 5000);

    QObject::disconnect(progressConnection);
    QObject::disconnect(preparedConnection);
    QObject::disconnect(cancelConnection);
    updateTransferOperationDialog(progress,
                                  resumed ? QStringLiteral("恢复完成") : (cancelRequested ? QStringLiteral("恢复已取消") : QStringLiteral("恢复失败")),
                                  resumed
                                      ? QStringLiteral("未完成发送“%1”已恢复完成，可回到文件工作区继续查看结果。").arg(fileName)
                                      : (cancelRequested
                                          ? QStringLiteral("未完成发送“%1”已取消恢复，最近状态会保留在文件工作区。").arg(fileName)
                                          : QStringLiteral("未完成发送“%1”恢复失败，可查看阻塞原因并决定是否清理记录或手动重发。").arg(fileName)),
                                  resumed ? 100 : progress.progressBar->value(),
                                  resumed ? QStringLiteral("100% · 已完成")
                                          : (cancelRequested ? QStringLiteral("%1% · 已取消").arg(progress.progressBar->value())
                                                             : QStringLiteral("%1% · 未完成").arg(progress.progressBar->value())),
                                  resumed ? QStringLiteral("success")
                                          : QStringLiteral("warning"));
    progress.dialog.close();

    const TransferResumeResultState resultState = m_transferManager.resumeResultState(
        fileName,
        targetName,
        resumed,
        cancelRequested,
        rejectReason);
    if (resultState.succeeded) {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        ui->statusbar->showMessage(resultState.statusMessage, 2600);
    } else if (resultState.canceled) {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        ui->statusbar->showMessage(resultState.statusMessage, 2200);
    } else {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        ui->statusbar->showMessage(resultState.statusMessage, 3200);
        showWarningDialog(resultState.failureTitle, resultState.failureMessage, this);
        if (confirmDestructiveAction(QStringLiteral("清理恢复记录"),
                                     QStringLiteral("恢复未完成发送仍然失败，是否清理当前恢复记录并回到手动重发？"),
                                     QStringLiteral("清理记录"),
                                     QStringLiteral("保留记录"),
                                     this) && m_client->clearOutgoingTransferState()) {
            appendSystemMessage(resultState.clearedSystemMessage);
            ui->chatHintLabel->setText(resultState.clearedHintText);
            ui->statusbar->showMessage(resultState.clearedStatusMessage, 2200);
        }
    }

    updateSavedOutgoingTransferRecoveryUi(false);
}

void MainWindow::onShowTransferWorkspace() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("transferWorkspaceDialog"),
        QStringLiteral("文件工作区"),
        QSize(960, 780),
        QStringLiteral("managerTitle"),
        QStringLiteral("文件工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把发送状态、恢复入口、最近诊断、已保存文件和下一步建议集中处理，减少分散在状态栏和右键菜单里的判断。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索文件名 / 路径 / 状态关键词"),
        QStringLiteral("按文件名、保存路径或状态关键词筛选当前文件工作区条目"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一条文件状态，再决定恢复、重发、复制诊断、打开文件或继续排查。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释当前文件链路、系统保留了什么，以及下一步该恢复、等待、排查还是重新发送。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(26, 18, 26, 16);
    shell.bodyLayout->setContentsMargins(24, 22, 24, 22);
    shell.bodyLayout->setSpacing(12);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* stateList = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    struct TransferWorkspaceRow {
        QString id;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool enabled = true;
        QString statusTone;
    };

    auto buildRows = [this]() {
        QList<TransferWorkspaceRow> rows;
        const bool activeSendUsesSavedFileActions =
            m_hasTransferWorkspaceSendState
            && transferWorkspaceStateUsesSavedFileActions(m_transferWorkspaceSendState);
        {
            TransferWorkspaceRow row;
            row.id = QStringLiteral("send-file");
            row.title = QStringLiteral("发送文件");
            row.detail = QStringLiteral("选择文档、压缩包或其他文件，进入当前会话发送流程。");
            row.preview = QStringLiteral("发送文件入口\n会先检查当前会话是否可发送，再生成分片和校验清单，随后在这里持续显示发送、失败或恢复状态。");
            row.keywords = row.title + row.detail + row.preview + QStringLiteral("发送 文件 选择 上传");
            row.statusTone = QStringLiteral("accent");
            rows << row;
        }
        {
            TransferWorkspaceRow row;
            row.id = QStringLiteral("send-media");
            row.title = QStringLiteral("发送图片/视频");
            row.detail = QStringLiteral("选择图片或视频，统一走媒体发送和回执链路。");
            row.preview = QStringLiteral("发送图片/视频入口\n会自动识别图片或视频，并把后续进度、失败、恢复和已保存文件反馈都收进同一个文件工作区。");
            row.keywords = row.title + row.detail + row.preview + QStringLiteral("发送 图片 视频 媒体");
            row.statusTone = QStringLiteral("accent");
            rows << row;
        }
        if (m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.hasSavedTransfer) {
            const TransferWorkspaceSummaryState summary =
                m_transferManager.recoveryWorkspaceSummary(m_lastTransferRecoveryUiState,
                                                           m_hasLastTransferStatusEvent ? &m_lastTransferStatusEvent : nullptr,
                                                           !m_lastTransferStatusDiagnostic.trimmed().isEmpty());
            TransferWorkspaceRow row;
            row.id = QStringLiteral("recovery");
            row.title = summary.title;
            row.detail = QStringLiteral("%1\n%2")
                             .arg(m_lastTransferRecoveryUiState.fileName.isEmpty()
                                      ? QStringLiteral("未命名文件")
                                      : m_lastTransferRecoveryUiState.fileName,
                                  summary.detail);
            row.preview = QStringLiteral("%1\n诊断：%2")
                              .arg(summary.previewText,
                                   summary.diagnosticHint);
            row.keywords = row.title + row.detail + row.preview;
            row.statusTone = summary.statusTone;
            rows << row;
        }
        if (m_hasLastTransferStatusEvent && !m_lastTransferStatusEvent.message.isEmpty()) {
            const TransferWorkspaceSummaryState summary =
                m_transferManager.statusWorkspaceSummary(m_lastTransferStatusEvent,
                                                         m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.hasSavedTransfer,
                                                         !m_lastTransferStatusDiagnostic.trimmed().isEmpty());
            TransferWorkspaceRow row;
            row.id = QStringLiteral("diagnostic");
            row.title = summary.title;
            row.detail = QStringLiteral("%1\n%2")
                             .arg(m_lastTransferStatusEvent.title.isEmpty()
                                      ? QStringLiteral("文件事件")
                                      : m_lastTransferStatusEvent.title,
                                  summary.detail);
            row.preview = QStringLiteral("%1\n诊断：%2")
                              .arg(summary.previewText,
                                   m_lastTransferStatusDiagnostic.trimmed().isEmpty()
                                       ? summary.diagnosticHint
                                       : m_lastTransferStatusDiagnostic);
            row.keywords = row.title + row.detail + row.preview;
            row.statusTone = summary.statusTone;
            rows << row;
        }
        if (m_hasTransferWorkspaceSendState
            && (!m_transferWorkspaceSendState.workspaceTitle.trimmed().isEmpty()
                || !m_transferWorkspaceSendState.workspaceDetail.trimmed().isEmpty())) {
            const TransferWorkspaceSummaryState summary =
                m_transferManager.sendWorkspaceSummary(m_transferWorkspaceSendState,
                                                       !m_lastTransferStatusDiagnostic.trimmed().isEmpty());
            TransferWorkspaceRow row;
            row.id = QStringLiteral("active-send");
            row.title = summary.title;
            row.detail = summary.detail;
            row.preview = QStringLiteral("%1\n诊断：%2")
                              .arg(summary.previewText,
                                   summary.diagnosticHint);
            row.keywords = row.title + row.detail + row.preview;
            row.statusTone = summary.statusTone;
            rows << row;
        }
        if (m_hasTransferWorkspaceSavedFileState && m_transferWorkspaceSavedFileState.hasSavePath) {
            const QString fileName = m_transferWorkspaceSavedFileState.fileInfo.fileName().trimmed().isEmpty()
                ? QStringLiteral("已保存文件")
                : m_transferWorkspaceSavedFileState.fileInfo.fileName();
            const QString fileSize = m_transferWorkspaceSavedFileState.fileInfo.exists()
                ? LocalFileManager::humanFileSize(m_transferWorkspaceSavedFileState.fileInfo.size())
                : QString();
            const TransferWorkspaceSummaryState summary =
                m_transferManager.savedFileWorkspaceSummary(fileName,
                                                            fileSize,
                                                            m_transferWorkspaceSavedFileState.savePath,
                                                            m_transferWorkspaceSavedFileState.canOpenFile,
                                                            m_transferWorkspaceSavedFileState.canOpenFolder,
                                                            ChatContextManager::plainContentText(m_transferWorkspaceSavedChatText));
            TransferWorkspaceRow row;
            row.id = QStringLiteral("saved-file");
            row.title = summary.title;
            row.detail = QStringLiteral("%1\n%2").arg(fileName, summary.detail);
            row.preview = summary.previewText;
            row.keywords = row.title + row.detail + row.preview;
            row.statusTone = summary.statusTone;
            if (!activeSendUsesSavedFileActions) {
                rows << row;
            }
        }
        if (rows.isEmpty()) {
            const TransferWorkspaceSummaryState summary =
                m_transferManager.emptyWorkspaceSummary(!m_lastTransferStatusDiagnostic.trimmed().isEmpty());
            TransferWorkspaceRow row;
            row.id = QStringLiteral("empty");
            row.title = QStringLiteral("文件工作区当前为空");
            row.detail = summary.detail;
            row.preview = summary.previewText;
            row.keywords = row.title + row.detail + row.preview;
            row.enabled = false;
            row.statusTone = summary.statusTone;
            rows << row;
        }
        return rows;
    };

    auto rows = buildRows();

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        stateList->clear();
        int visibleCount = 0;
        for (const TransferWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !row.keywords.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.id);
            item->setData(Qt::UserRole + 1, row.preview);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (!row.enabled) {
                item->setFlags(Qt::NoItemFlags);
                item->setForeground(QColor(135, 150, 165));
            } else if (row.statusTone == QLatin1String("accent")) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.statusTone == QLatin1String("success")) {
                item->setForeground(QColor(0, 121, 107));
            } else if (row.statusTone == QLatin1String("warning")) {
                item->setForeground(QColor(180, 83, 9));
            } else if (row.statusTone == QLatin1String("danger")) {
                item->setForeground(QColor(185, 28, 28));
            }
            stateList->addItem(item);
            ++visibleCount;
        }
        statsLabel->setText(QStringLiteral("可见 %1 项").arg(visibleCount));
        selectPreferredListRow(stateList, 0);
    };

    auto selectedRowId = [stateList]() {
        QListWidgetItem* item = stateList->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };

    auto updatePreview = [=]() {
        QListWidgetItem* item = stateList->currentItem();
        if (!item) {
            previewLabel->setText(QStringLiteral("这里会解释当前文件链路、系统保留了什么，以及下一步该恢复、等待、排查还是重新发送。"));
            return;
        }
        previewLabel->setText(item->data(Qt::UserRole + 1).toString());
    };

    QPushButton* sendFileBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("发送文件"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("选择文件并发送到当前会话"), QStyle::SP_FileIcon);
    QPushButton* sendMediaBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("发送图片/视频"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("选择图片或视频并发送到当前会话"), QStyle::SP_DriveHDIcon);
    QPushButton* resumeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("恢复发送"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("继续恢复未完成发送"), QStyle::SP_ArrowForward);
    QPushButton* clearBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("清理恢复记录"), QStringLiteral("managerDangerBtn"), QStringLiteral("清理当前保存的恢复记录"), QStyle::SP_TrashIcon);
    QPushButton* copyDiagBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制诊断"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制最近一次文件传输诊断"), QStyle::SP_DialogSaveButton);
    QPushButton* openFileBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("打开文件"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("打开当前已保存文件"), QStyle::SP_DialogOpenButton);
    QPushButton* openFolderBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("打开目录"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("打开当前已保存文件所在目录"), QStyle::SP_DirOpenIcon);
    QPushButton* copyPathBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制路径"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前已保存文件路径"), QStyle::SP_FileDialogDetailedView);
    QPushButton* copySnapshotBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区摘要"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前文件工作区状态摘要"), QStyle::SP_FileDialogListView);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭文件工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("发送入口"),
        QStringLiteral("不管是文件、图片还是视频，都从这里统一进入发送流程；后续进度、失败和恢复会继续回到这个工作区。"),
        {sendFileBtn, sendMediaBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("恢复与排查"),
        QStringLiteral("未完成发送、最近失败和离线/保存异常会优先收在这里，适合先恢复、重发、清理或复制诊断。"),
        {resumeBtn, clearBtn, copyDiagBtn, copySnapshotBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("已保存文件"),
        QStringLiteral("从聊天记录或接收结果切进来后，可在这里打开文件、打开目录或复制路径。"),
        {openFileBtn, openFolderBtn, copyPathBtn});

    auto updateActionState = [=]() {
        const QString rowId = selectedRowId();
        const bool sendFileSelected = rowId == QLatin1String("send-file");
        const bool sendMediaSelected = rowId == QLatin1String("send-media");
        const bool recoverySelected = rowId == QLatin1String("recovery");
        const bool activeSendSelected = rowId == QLatin1String("active-send");
        const bool activeSendUsesSavedFileActions =
            activeSendSelected
            && m_hasTransferWorkspaceSendState
            && transferWorkspaceStateUsesSavedFileActions(m_transferWorkspaceSendState);
        const bool diagnosticSelected = rowId == QLatin1String("diagnostic")
            || (activeSendSelected && !activeSendUsesSavedFileActions);
        const bool savedFileSelected = rowId == QLatin1String("saved-file");
        const bool savedFileCapableSelected = savedFileSelected || activeSendUsesSavedFileActions;
        sendFileBtn->setEnabled(sendFileSelected || rowId.isEmpty() || rowId == QLatin1String("empty"));
        sendMediaBtn->setEnabled(sendMediaSelected || rowId.isEmpty() || rowId == QLatin1String("empty"));
        resumeBtn->setEnabled(recoverySelected && m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.resumeAction.enabled);
        clearBtn->setEnabled(recoverySelected && m_hasLastTransferRecoveryUiState && m_lastTransferRecoveryUiState.clearAction.enabled);
        copyDiagBtn->setEnabled((diagnosticSelected || recoverySelected) && !m_lastTransferStatusDiagnostic.trimmed().isEmpty());
        openFileBtn->setEnabled(savedFileCapableSelected && m_hasTransferWorkspaceSavedFileState && m_transferWorkspaceSavedFileState.canOpenFile);
        openFolderBtn->setEnabled(savedFileCapableSelected && m_hasTransferWorkspaceSavedFileState && m_transferWorkspaceSavedFileState.canOpenFolder);
        copyPathBtn->setEnabled(savedFileCapableSelected && m_hasTransferWorkspaceSavedFileState && m_transferWorkspaceSavedFileState.hasSavePath);
        copySnapshotBtn->setEnabled(true);
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(stateList, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(stateList, &QListWidget::itemDoubleClicked, &dialog, [this, &dialog](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        const QString rowId = item->data(Qt::UserRole).toString();
        if (rowId == QLatin1String("send-file")) {
            dialog.accept();
            onSendFile();
            return;
        }
        if (rowId == QLatin1String("send-media")) {
            dialog.accept();
            onSendImage();
            return;
        }
        if (rowId == QLatin1String("recovery")) {
            dialog.accept();
            onResumeSavedOutgoingTransfer();
            return;
        }
        const bool activeSendUsesSavedFileActions =
            rowId == QLatin1String("active-send")
            && m_hasTransferWorkspaceSendState
            && transferWorkspaceStateUsesSavedFileActions(m_transferWorkspaceSendState);
        if ((rowId == QLatin1String("saved-file") || activeSendUsesSavedFileActions)
            && m_hasTransferWorkspaceSavedFileState
            && m_transferWorkspaceSavedFileState.canOpenFile) {
            ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                                                       chatContextSavedFileState(m_transferWorkspaceSavedFileState),
                                                                                       m_transferWorkspaceSavedFileState.savePath);
            command.failureStatusMessage = QStringLiteral("保存文件不存在或无法打开");
            openSavedFileFromState(m_transferWorkspaceSavedFileState, command);
        }
    });
    connect(sendFileBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onSendFile();
    });
    connect(sendMediaBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onSendImage();
    });
    connect(resumeBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onResumeSavedOutgoingTransfer();
    });
    connect(clearBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onClearSavedOutgoingTransfer();
    });
    connect(copyDiagBtn, &QPushButton::clicked, &dialog, [this]() {
        const TransferDiagnosticCopyUiState copyState = m_transferManager.diagnosticCopyUiState(m_lastTransferStatusDiagnostic);
        if (!copyState.action.enabled) {
            ui->statusbar->showMessage(copyState.emptyStatusMessage, 1800);
            return;
        }
        QApplication::clipboard()->setText(copyState.clipboardText);
        ui->statusbar->showMessage(copyState.copiedStatusMessage, 2200);
    });
    connect(openFileBtn, &QPushButton::clicked, &dialog, [this]() {
        if (!m_hasTransferWorkspaceSavedFileState) {
            ui->statusbar->showMessage(QStringLiteral("当前没有可打开的已保存文件"), 1800);
            return;
        }
        ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                                                   chatContextSavedFileState(m_transferWorkspaceSavedFileState),
                                                                                   m_transferWorkspaceSavedFileState.savePath);
        command.failureStatusMessage = QStringLiteral("保存文件不存在或无法打开");
        openSavedFileFromState(m_transferWorkspaceSavedFileState, command);
    });
    connect(openFolderBtn, &QPushButton::clicked, &dialog, [this]() {
        if (!m_hasTransferWorkspaceSavedFileState) {
            ui->statusbar->showMessage(QStringLiteral("当前没有可打开的保存目录"), 1800);
            return;
        }
        const ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("open-save-folder"),
                                                                                         chatContextSavedFileState(m_transferWorkspaceSavedFileState),
                                                                                         m_transferWorkspaceSavedFileState.savePath);
        openSavedFolderFromState(m_transferWorkspaceSavedFileState, command);
    });
    connect(copyPathBtn, &QPushButton::clicked, &dialog, [this]() {
        if (!m_hasTransferWorkspaceSavedFileState) {
            ui->statusbar->showMessage(QStringLiteral("当前没有可复制的保存路径"), 1800);
            return;
        }
        const ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("copy-save-path"),
                                                                                         chatContextSavedFileState(m_transferWorkspaceSavedFileState),
                                                                                         m_transferWorkspaceSavedFileState.savePath);
        copySavedFilePathToClipboard(command);
    });
    connect(copySnapshotBtn, &QPushButton::clicked, &dialog, [this]() {
        copyTextWithStatus(transferWorkspaceStatusSnapshotText(), QStringLiteral("文件工作区摘要已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(QStringLiteral("当前会话：%1").arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget)));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowComposerWorkspace() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("composerWorkspaceDialog"),
        QStringLiteral("消息工作区"),
        QSize(980, 780),
        QStringLiteral("managerTitle"),
        QStringLiteral("消息工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把输入草稿、快捷短语、会话摘要、@提及和发送入口集中处理，减少依赖零散按钮和右键菜单。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索草稿 / 快捷短语 / 动作"),
        QStringLiteral("按关键词筛选当前会话可用的消息动作、短语和发送辅助"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选一个动作或短语，再决定直接带入输入框、追加草稿、发送消息或继续打开图片/文件入口。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会说明当前会话、草稿状态，以及这条动作对输入区会产生什么影响。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(26, 18, 26, 16);
    shell.bodyLayout->setContentsMargins(24, 22, 24, 22);
    shell.bodyLayout->setSpacing(12);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* actionList = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    const ChatContextComposerState composerState = currentChatContextComposerState();
    const QString draftText = ui->messageEdit->toPlainText().trimmed();
    const QString targetName = composerState.targetDisplayName;
    const bool hasDraft = !draftText.isEmpty();
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    const bool canReachTarget = isLocalGroup || (m_client && m_client->isConnected());
    const QString clipboardText = QApplication::clipboard()->text().trimmed();
    const bool hasClipboardText = !clipboardText.isEmpty();

    struct ComposerWorkspaceRow {
        QString id;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool enabled = true;
        bool phrase = false;
        QString phraseText;
        bool insertMode = false;
    };

    auto buildRows = [=, this]() {
        QList<ComposerWorkspaceRow> rows;

        ComposerWorkspaceRow draftSummary;
        draftSummary.id = QStringLiteral("draft-summary");
        draftSummary.title = hasDraft ? QStringLiteral("当前草稿 · 已就绪") : QStringLiteral("当前草稿 · 仍为空");
        draftSummary.detail = hasDraft
            ? QStringLiteral("当前草稿 %1 字\n可直接发送、清空、补充快捷短语，或复制成工作区摘要。").arg(draftText.size())
            : QStringLiteral("当前还没有输入消息\n可以先选快捷短语、@提及、会话提示，再继续补正文。");
        draftSummary.preview = hasDraft ? draftText : QStringLiteral("当前会话还没有输入中的消息内容。");
        draftSummary.keywords = draftSummary.title + draftSummary.detail + draftSummary.preview;
        rows << draftSummary;

        const QList<ChatContextComposerMenuAction> composerActions = ChatContextManager::composerMenuActions();
        for (const ChatContextComposerMenuAction& action : composerActions) {
            ComposerWorkspaceRow row;
            row.id = action.commandId;
            row.title = action.title;
            row.detail = action.toolTip;
            row.preview = QStringLiteral("当前会话：%1\n草稿长度：%2 字").arg(targetName).arg(draftText.size());
            row.keywords = row.title + row.detail + row.preview;
            rows << row;
        }

        const QList<ChatContextPhraseMenuPlan> phrasePlans = ChatContextManager::composerPhraseMenuPlans();
        for (const ChatContextPhraseMenuPlan& plan : phrasePlans) {
            for (const QString& phrase : plan.phrases) {
                ComposerWorkspaceRow row;
                row.id = QStringLiteral("phrase:") + phrase;
                row.title = QStringLiteral("%1 · %2").arg(plan.title, phrase.left(18));
                row.detail = plan.insertedStatusMessage;
                row.preview = phrase;
                row.keywords = row.title + row.detail + row.preview;
                row.phrase = true;
                row.phraseText = phrase;
                row.insertMode = plan.title.contains(QStringLiteral("追加"));
                rows << row;
            }
        }

        ComposerMentionMenuPlan mentionPlan;
        QStringList mentionIds;
        QMap<QString, QString> mentionNames;
        if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            mentionIds = m_localGroupMembers.value(m_privateChatTarget);
        } else {
            for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
                mentionIds << it.key();
                mentionNames[it.key()] = it.value().name;
            }
        }
        for (const QString& memberId : mentionIds) {
            if (!mentionNames.contains(memberId)) {
                mentionNames[memberId] = memberId == m_currentUserId ? m_currentUserName : contactDisplayName(memberId);
            }
        }
        mentionPlan = ComposerManager::mentionMenuPlan(mentionIds, m_currentUserId, mentionNames);
        for (const ComposerMentionAction& mentionAction : mentionPlan.actions) {
            ComposerWorkspaceRow row;
            row.id = QStringLiteral("mention:") + mentionAction.insertText;
            row.title = QStringLiteral("@ 提及 · %1").arg(mentionAction.title);
            row.detail = mentionAction.statusMessage;
            row.preview = mentionAction.insertText;
            row.keywords = row.title + row.detail + row.preview;
            row.phrase = true;
            row.phraseText = mentionAction.insertText;
            row.insertMode = true;
            rows << row;
        }

        ComposerWorkspaceRow imageRow;
        imageRow.id = QStringLiteral("open-image");
        imageRow.title = QStringLiteral("图片/视频入口");
        imageRow.detail = ui->imageBtn->toolTip();
        imageRow.preview = QStringLiteral("打开图片/视频选择器，沿用当前会话上下文。");
        imageRow.keywords = imageRow.title + imageRow.detail + imageRow.preview;
        imageRow.enabled = ui->imageBtn->isEnabled();
        rows << imageRow;

        ComposerWorkspaceRow fileRow;
        fileRow.id = QStringLiteral("open-file");
        fileRow.title = QStringLiteral("闪传文件入口");
        fileRow.detail = ui->fileBtn->toolTip();
        fileRow.preview = QStringLiteral("打开文件选择器，沿用当前会话上下文。");
        fileRow.keywords = fileRow.title + fileRow.detail + fileRow.preview;
        fileRow.enabled = ui->fileBtn->isEnabled();
        rows << fileRow;

        ComposerWorkspaceRow sendRow;
        sendRow.id = QStringLiteral("send-now");
        sendRow.title = QStringLiteral("立即发送");
        sendRow.detail = ui->sendBtn->toolTip();
        sendRow.preview = hasDraft ? draftText : QStringLiteral("当前没有可发送草稿。");
        sendRow.keywords = sendRow.title + sendRow.detail + sendRow.preview;
        sendRow.enabled = ui->sendBtn->isEnabled();
        rows << sendRow;

        ComposerWorkspaceRow copySummaryRow;
        copySummaryRow.id = QStringLiteral("copy-composer-summary");
        copySummaryRow.title = QStringLiteral("复制消息工作区摘要");
        copySummaryRow.detail = QStringLiteral("复制当前会话、草稿和输入入口摘要，便于调试或向外同步上下文。");
        copySummaryRow.preview = QStringLiteral("会话：%1\n草稿：%2 字\n好友：%3 · 群聊：%4")
                                     .arg(targetName)
                                     .arg(draftText.size())
                                     .arg(m_friendIds.size())
                                     .arg(m_localGroupIds.size());
        copySummaryRow.keywords = copySummaryRow.title + copySummaryRow.detail + copySummaryRow.preview;
        rows << copySummaryRow;

        ComposerWorkspaceRow clipboardRow;
        clipboardRow.id = QStringLiteral("paste-clipboard");
        clipboardRow.title = QStringLiteral("贴入剪贴板");
        clipboardRow.detail = hasClipboardText
            ? QStringLiteral("把剪贴板内容带入当前输入区，继续编辑或发送。")
            : QStringLiteral("当前剪贴板为空，暂时没有可带入的内容。");
        clipboardRow.preview = hasClipboardText ? clipboardText.left(120) : QStringLiteral("剪贴板为空");
        clipboardRow.keywords = clipboardRow.title + clipboardRow.detail + clipboardRow.preview;
        clipboardRow.enabled = hasClipboardText;
        rows << clipboardRow;

        return rows;
    };

    auto rows = buildRows();

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        actionList->clear();
        int visibleCount = 0;
        for (const ComposerWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !row.keywords.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.id);
            item->setData(Qt::UserRole + 1, row.preview);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (!row.enabled) {
                item->setFlags(Qt::NoItemFlags);
                item->setForeground(QColor(135, 150, 165));
            } else if (row.phrase) {
                item->setForeground(QColor(29, 78, 216));
            }
            actionList->addItem(item);
            ++visibleCount;
        }
        statsLabel->setText(QStringLiteral("可见 %1 项 · 草稿 %2 字").arg(visibleCount).arg(draftText.size()));
        selectPreferredListRow(actionList, 0);
    };

    auto selectedActionId = [actionList]() {
        QListWidgetItem* item = actionList->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };

    auto updatePreview = [=]() {
        QListWidgetItem* item = actionList->currentItem();
        if (!item) {
            previewLabel->setText(QStringLiteral("这里会说明当前会话、草稿状态，以及这条动作对输入区会产生什么影响。"));
            return;
        }
        previewLabel->setText(item->data(Qt::UserRole + 1).toString());
    };

    QPushButton* replaceDraftBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("设为草稿"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("用当前动作或短语替换输入框内容"), QStyle::SP_DialogApplyButton);
    QPushButton* appendDraftBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("追加到草稿"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("把当前动作或短语追加到输入框"), QStyle::SP_ArrowRight);
    QPushButton* sendBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("立即发送"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("按当前会话上下文直接发送草稿"), QStyle::SP_ArrowForward);
    QPushButton* openImageBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("图片/视频"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("打开图片/视频发送入口"), QStyle::SP_FileIcon);
    QPushButton* openFileBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("闪传文件"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("打开文件发送入口"), QStyle::SP_DriveFDIcon);
    QPushButton* clearDraftBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("清空草稿"), QStringLiteral("managerDangerBtn"), QStringLiteral("清空当前输入区草稿"), QStyle::SP_TrashIcon);
    QPushButton* copySummaryBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区摘要"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前消息工作区摘要"), QStyle::SP_DialogSaveButton);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭消息工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("草稿与短语"),
        QStringLiteral("先把动作、短语或 @ 提及带入草稿，再决定继续补正文还是直接发送。"),
        {replaceDraftBtn, appendDraftBtn, clearDraftBtn, copySummaryBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("发送入口"),
        QStringLiteral("当前会话允许时，可以直接发送文本，或者继续进入图片/视频、文件发送入口。"),
        {sendBtn, openImageBtn, openFileBtn});

    auto updateActionState = [=]() {
        const QString id = selectedActionId();
        const bool hasSelected = !id.isEmpty();
        const bool actionIsPhrase = id.startsWith(QStringLiteral("phrase:")) || id.startsWith(QStringLiteral("mention:"));
        const bool canReplace = hasSelected && id != QLatin1String("send-now") && id != QLatin1String("open-image") && id != QLatin1String("open-file") && id != QLatin1String("copy-composer-summary");
        replaceDraftBtn->setEnabled(canReplace);
        appendDraftBtn->setEnabled(actionIsPhrase || id == QLatin1String("paste-clipboard"));
        clearDraftBtn->setEnabled(hasDraft);
        sendBtn->setEnabled(ui->sendBtn->isEnabled());
        openImageBtn->setEnabled(ui->imageBtn->isEnabled());
        openFileBtn->setEnabled(ui->fileBtn->isEnabled());
        copySummaryBtn->setEnabled(true);
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(actionList, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(replaceDraftBtn, &QPushButton::clicked, &dialog, [=, this]() {
        const QString id = selectedActionId();
        if (id.isEmpty()) {
            return;
        }
        if (id.startsWith(QStringLiteral("phrase:"))) {
            setChatDraftText(id.mid(QStringLiteral("phrase:").size()), QStringLiteral("已设为当前草稿"), 1400);
            return;
        }
        if (id.startsWith(QStringLiteral("mention:"))) {
            setChatDraftText(id.mid(QStringLiteral("mention:").size()), QStringLiteral("已设为 @ 提及草稿"), 1400);
            return;
        }
        if (id == QLatin1String("paste-clipboard")) {
            setChatDraftText(clipboardText, QStringLiteral("已把剪贴板设为当前草稿"), 1400);
            return;
        }
        applyChatContextComposerCommand(id);
    });
    connect(appendDraftBtn, &QPushButton::clicked, &dialog, [=, this]() {
        const QString id = selectedActionId();
        if (id.startsWith(QStringLiteral("phrase:"))) {
            insertChatDraftText(id.mid(QStringLiteral("phrase:").size()), QStringLiteral("已追加快捷短语"), 1400);
            return;
        }
        if (id.startsWith(QStringLiteral("mention:"))) {
            insertChatDraftText(id.mid(QStringLiteral("mention:").size()), QStringLiteral("已追加 @ 提及"), 1400);
            return;
        }
        if (id == QLatin1String("paste-clipboard") && hasClipboardText) {
            insertChatDraftText(clipboardText, QStringLiteral("已追加剪贴板内容"), 1400);
        }
    });
    connect(sendBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onSendMessage();
    });
    connect(openImageBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onSendImage();
    });
    connect(openFileBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onSendFile();
    });
    connect(clearDraftBtn, &QPushButton::clicked, &dialog, [this]() {
        ui->messageEdit->clear();
        ui->messageEdit->setFocus();
        ui->statusbar->showMessage(QStringLiteral("输入草稿已清空"), 1400);
        refreshComposerState();
    });
    connect(copySummaryBtn, &QPushButton::clicked, &dialog, [=, this]() {
        QStringList rowsText;
        rowsText << QStringLiteral("消息工作区摘要");
        rowsText << QStringLiteral("当前会话:%1").arg(targetName);
        rowsText << QStringLiteral("草稿长度:%1").arg(ui->messageEdit->toPlainText().trimmed().size());
        rowsText << QStringLiteral("可发送:%1").arg(ui->sendBtn->isEnabled() ? QStringLiteral("是") : QStringLiteral("否"));
        rowsText << QStringLiteral("图片入口:%1").arg(ui->imageBtn->isEnabled() ? QStringLiteral("可用") : QStringLiteral("不可用"));
        rowsText << QStringLiteral("文件入口:%1").arg(ui->fileBtn->isEnabled() ? QStringLiteral("可用") : QStringLiteral("不可用"));
        rowsText << QStringLiteral("好友:%1 · 群聊:%2 · 在线成员:%3")
                        .arg(m_friendIds.size())
                        .arg(m_localGroupIds.size())
                        .arg(m_knownUsers.size());
        if (!ui->messageEdit->toPlainText().trimmed().isEmpty()) {
            rowsText << QStringLiteral("当前草稿:%1").arg(ui->messageEdit->toPlainText().trimmed());
        }
        copyTextWithStatus(rowsText.join(QLatin1Char('\n')), QStringLiteral("消息工作区摘要已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(QStringLiteral("当前会话：%1 · 好友 %2 · 群聊 %3").arg(targetName).arg(m_friendIds.size()).arg(m_localGroupIds.size()));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::showChatHistoryWorkspaceForRow(int preferredRow) {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("chatHistoryWorkspaceDialog"),
        QStringLiteral("消息记录工作区"),
        QSize(980, 800),
        QStringLiteral("managerTitle"),
        QStringLiteral("消息记录工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把聊天记录里的复制、引用、转发、重发、保存文件动作统一收进一个工作面，减少来回试右键菜单。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索消息内容 / 动作"),
        QStringLiteral("按消息内容、发送人、时间或动作关键词筛选当前聊天记录"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一条消息，再决定复制摘要、带入草稿、转发重发，或切到保存文件排查动作。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释选中消息的上下文、可用动作，以及它会如何影响当前输入区或文件工作区。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(26, 18, 26, 16);
    shell.bodyLayout->setContentsMargins(24, 22, 24, 22);
    shell.bodyLayout->setSpacing(12);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* messageList = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    struct ChatHistoryWorkspaceRow {
        QModelIndex index;
        QString chatText;
        LocalSavedFileState savedFileState;
        bool isMediaMessage = false;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
    };

    auto buildRows = [this]() {
        QList<ChatHistoryWorkspaceRow> rows;
        if (!m_chatModel) {
            return rows;
        }
        for (int row = 0; row < m_chatModel->rowCount(); ++row) {
            QModelIndex index = m_chatModel->index(row, 0);
            if (!index.isValid()) {
                continue;
            }
            const QString chatText = index.data().toString();
            if (chatText.trimmed().isEmpty()) {
                continue;
            }
            ChatHistoryWorkspaceRow item;
            item.index = index;
            item.chatText = chatText;
            item.savedFileState = savedFileActionState(index);
            const ChatContextSavedFileState savedContextState = chatContextSavedFileState(item.savedFileState);
            item.isMediaMessage = ChatContextManager::isMediaMessage(chatText, savedContextState);
            const QString sender = ChatContextManager::senderText(chatText).trimmed().isEmpty()
                ? QStringLiteral("系统/会话")
                : ChatContextManager::senderText(chatText);
            const QString timeText = ChatContextManager::timeText(chatText).trimmed().isEmpty()
                ? QStringLiteral("未知时间")
                : ChatContextManager::timeText(chatText);
            const QString plainText = ChatContextManager::plainContentText(chatText).trimmed();
            item.title = QStringLiteral("%1 · %2").arg(sender, timeText);
            item.detail = plainText.isEmpty()
                ? QStringLiteral("消息内容较短或为结构化卡片，请查看下方预览。")
                : plainText.left(96);
            if (item.savedFileState.hasSavePath) {
                item.detail += QStringLiteral("\n已保存文件：%1").arg(item.savedFileState.fileInfo.fileName());
            } else if (item.isMediaMessage) {
                item.detail += QStringLiteral("\n媒体消息：%1").arg(ChatContextManager::mediaTypeFromChatText(chatText));
            }
            item.preview = QStringLiteral("发送人：%1\n时间：%2\n正文：%3")
                               .arg(sender,
                                    timeText,
                                    plainText.isEmpty() ? chatText.left(180) : plainText);
            if (item.savedFileState.hasSavePath) {
                item.preview += QStringLiteral("\n保存路径：%1").arg(item.savedFileState.savePath);
            }
            item.keywords = item.title + item.detail + item.preview + chatText;
            rows << item;
        }
        return rows;
    };

    auto rows = buildRows();

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        messageList->clear();
        int visibleCount = 0;
        for (const ChatHistoryWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !row.keywords.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.index.row());
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, row.savedFileState.hasSavePath || row.isMediaMessage ? 84 : 74));
            if (row.savedFileState.hasSavePath) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.isMediaMessage) {
                item->setForeground(QColor(13, 148, 136));
            }
            messageList->addItem(item);
            ++visibleCount;
        }
        statsLabel->setText(QStringLiteral("可见 %1 / %2 条").arg(visibleCount).arg(rows.size()));
        if (messageList->count() <= 0) {
            return;
        }

        int preferredVisibleRow = -1;
        if (preferredRow >= 0) {
            for (int i = 0; i < messageList->count(); ++i) {
                QListWidgetItem* item = messageList->item(i);
                if (item && item->data(Qt::UserRole).toInt() == preferredRow) {
                    preferredVisibleRow = i;
                    break;
                }
            }
        }
        if (preferredVisibleRow >= 0) {
            messageList->setCurrentRow(preferredVisibleRow);
        } else {
            messageList->setCurrentRow(messageList->count() - 1);
        }
    };

    auto selectedRowIndex = [messageList]() {
        QListWidgetItem* item = messageList->currentItem();
        return item ? item->data(Qt::UserRole).toInt() : -1;
    };

    auto findSelectedRow = [=, &rows]() -> ChatHistoryWorkspaceRow* {
        const int rowIndex = selectedRowIndex();
        for (ChatHistoryWorkspaceRow& row : rows) {
            if (row.index.row() == rowIndex) {
                return &row;
            }
        }
        return nullptr;
    };

    auto updatePreview = [=, &rows]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row) {
            previewLabel->setText(QStringLiteral("这里会解释选中消息的上下文、可用动作，以及它会如何影响当前输入区或文件工作区。"));
            return;
        }
        previewLabel->setText(row->preview);
    };

    QPushButton* copySummaryBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制消息摘要"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前选中消息的文本摘要"), QStyle::SP_DialogSaveButton);
    QPushButton* quoteBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("引用到草稿"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("把当前消息引用到输入区"), QStyle::SP_ArrowForward);
    QPushButton* forwardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("转发到草稿"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("把当前消息正文带入输入区继续处理"), QStyle::SP_ArrowRight);
    QPushButton* resendBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("重发/再发"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("把当前消息按可重发方式重新带入或直接再发"), QStyle::SP_BrowserReload);
    QPushButton* copyMediaBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制媒体卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制媒体/文件消息摘要"), QStyle::SP_FileIcon);
    QPushButton* openFileWorkspaceBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("打开文件工作区"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("把当前保存文件切到文件工作区继续处理"), QStyle::SP_FileDialogDetailedView);
    QPushButton* openSavedFileBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("打开保存文件"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("直接打开当前保存文件"), QStyle::SP_DialogOpenButton);
    QPushButton* openFolderBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("打开保存目录"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("直接打开当前保存目录"), QStyle::SP_DirOpenIcon);
    QPushButton* copyPathBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制保存路径"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前保存文件路径"), QStyle::SP_FileDialogContentsView);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭消息记录工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("消息动作"),
        QStringLiteral("复制、引用、转发、重发这些动作现在都从这里集中发起，不必先记右键菜单。"),
        {copySummaryBtn, quoteBtn, forwardBtn, resendBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("媒体与保存文件"),
        QStringLiteral("带保存路径或媒体结构的消息，会在这里补充文件工作区和本地文件动作。"),
        {copyMediaBtn, openFileWorkspaceBtn, openSavedFileBtn, openFolderBtn, copyPathBtn});

    auto updateActionState = [=, &rows]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        const bool hasSelection = row != nullptr;
        const bool hasSavedFile = hasSelection && row->savedFileState.hasSavePath;
        const bool hasMedia = hasSelection && row->isMediaMessage;
        copySummaryBtn->setEnabled(hasSelection);
        quoteBtn->setEnabled(hasSelection);
        forwardBtn->setEnabled(hasSelection);
        resendBtn->setEnabled(hasSelection);
        copyMediaBtn->setEnabled(hasSelection && (hasMedia || hasSavedFile));
        openFileWorkspaceBtn->setEnabled(hasSavedFile);
        openSavedFileBtn->setEnabled(hasSavedFile && row->savedFileState.canOpenFile);
        openFolderBtn->setEnabled(hasSavedFile && row->savedFileState.canOpenFolder);
        copyPathBtn->setEnabled(hasSavedFile);
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(messageList, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(copySummaryBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row) {
            return;
        }
        const QString summary = QStringLiteral("消息摘要\n%1\n%2")
                                    .arg(row->title,
                                         ChatContextManager::plainContentText(row->chatText).trimmed().isEmpty()
                                             ? row->chatText
                                             : ChatContextManager::plainContentText(row->chatText));
        copyTextWithStatus(summary, QStringLiteral("消息摘要已复制"), 2200);
    });
    connect(quoteBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row) {
            return;
        }
        handleChatContextCommand(QStringLiteral("quote"), row->chatText, row->savedFileState);
    });
    connect(forwardBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row) {
            return;
        }
        handleChatContextCommand(QStringLiteral("forward"), row->chatText, row->savedFileState);
    });
    connect(resendBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row) {
            return;
        }
        handleChatContextCommand(QStringLiteral("resend"), row->chatText, row->savedFileState);
    });
    connect(copyMediaBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row) {
            return;
        }
        const QString commandId = row->savedFileState.hasSavePath
            ? QStringLiteral("copy-file-notice")
            : QStringLiteral("copy-media-card");
        handleChatContextCommand(commandId, row->chatText, row->savedFileState);
    });
    connect(openFileWorkspaceBtn, &QPushButton::clicked, &dialog, [this, &rows, &dialog, findSelectedRow]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row || !row->savedFileState.hasSavePath) {
            return;
        }
        showSavedFileWorkspace(row->savedFileState, row->chatText, QStringLiteral("已切换到保存文件工作区"));
        dialog.accept();
        onShowTransferWorkspace();
    });
    connect(openSavedFileBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row || !row->savedFileState.hasSavePath) {
            return;
        }
        ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("open-saved-file"),
                                                                                   chatContextSavedFileState(row->savedFileState),
                                                                                   row->savedFileState.savePath);
        command.failureStatusMessage = QStringLiteral("保存文件不存在或无法打开");
        openSavedFileFromState(row->savedFileState, command);
    });
    connect(openFolderBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row || !row->savedFileState.hasSavePath) {
            return;
        }
        const ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("open-save-folder"),
                                                                                         chatContextSavedFileState(row->savedFileState),
                                                                                         row->savedFileState.savePath);
        openSavedFolderFromState(row->savedFileState, command);
    });
    connect(copyPathBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ChatHistoryWorkspaceRow* row = findSelectedRow();
        if (!row || !row->savedFileState.hasSavePath) {
            return;
        }
        const ChatContextSavedFileCommand command = ChatContextManager::savedFileCommand(QStringLiteral("copy-save-path"),
                                                                                         chatContextSavedFileState(row->savedFileState),
                                                                                         row->savedFileState.savePath);
        copySavedFilePathToClipboard(command);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(QStringLiteral("当前会话：%1 · 消息 %2 条").arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget)).arg(rows.size()));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowChatHistoryWorkspace() {
    const QModelIndex currentIndex = ui->chatListView ? ui->chatListView->currentIndex() : QModelIndex();
    showChatHistoryWorkspaceForRow(currentIndex.isValid() ? currentIndex.row() : -1);
}

void MainWindow::setupUi() {
    m_userListModel->setHorizontalHeaderLabels({"在线用户"});
    ui->userListView->setModel(m_userListModel);
    ui->userListView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    ui->chatListView->setModel(m_chatModel);
    ui->chatListView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->chatListView->setToolTip("双击消息可进入消息记录工作区；带保存路径的文件消息会继续打开本地文件");

    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});
    ui->groupMemberListView->setModel(m_groupMemberModel);
    ui->groupMemberListView->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->messageEdit->setPlaceholderText("输入消息... (Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿)");
    ui->messageEdit->setFocus();
    ui->messageEdit->installEventFilter(this);
    ui->messageEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->contactSearchEdit->installEventFilter(this);
    ui->memberSearchEdit->installEventFilter(this);
    ui->contactSearchEdit->setToolTip("搜索联系人、QQ 号或群聊；按 Enter 搜索账号，Esc 清空");
    ui->memberSearchEdit->setToolTip("搜索当前群成员；群聊中可输入 QQ 号后按 Enter 邀请");

    ui->clearBtn->setObjectName("clearBtn");
    ui->clearBtn->setToolTip("清空当前会话的本地聊天记录");
    ui->fileBtn->setObjectName("toolBtn");
    ui->fileBtn->setToolTip("闪传文件，支持文档、压缩包和媒体文件");
    ui->imageBtn->setObjectName("toolBtn");
    ui->imageBtn->setText("图片/视频");
    ui->imageBtn->setToolTip("发送图片或视频文件，图片会显示预览");
    ui->emojiBtn->setObjectName("toolBtn");
    ui->emojiBtn->setText("消息工作区");
    ui->emojiBtn->setToolTip("打开消息工作区，集中处理草稿、快捷短语、@提及和发送辅助");
    ui->mentionBtn->setObjectName("toolBtn");
    ui->mentionBtn->setText("快捷提及");
    ui->mentionBtn->setToolTip("快速 @ 群成员，或插入会话提醒短语");
    ui->transferResumeBtn->setObjectName("transferPrimaryBtn");
    ui->transferResumeBtn->setToolTip("检测到未完成发送时，可从这里直接恢复");
    ui->transferClearBtn->setObjectName("transferDangerBtn");
    ui->transferClearBtn->setToolTip("清理本机保存的恢复记录，回到手动重发");
    ui->transferCopyStatusBtn->setObjectName("transferGhostBtn");
    ui->transferCopyStatusBtn->setToolTip("打开文件工作区，查看最近诊断、恢复入口和已保存文件动作");
    ui->sendBtn->setToolTip("请输入消息后发送");
    ui->sendBtn->setEnabled(false);
    ui->globalSearchBtn->setToolTip("打开综合搜索；搜索框有内容时直接搜索该 QQ 号");
    ui->createMenuBtn->setToolTip("打开创建和快捷操作菜单");
    ui->friendNoticeBtn->setToolTip("打开通知控制台，统一查看好友申请、群通知和处理计划");
    ui->groupNoticeBtn->setToolTip("打开通知控制台，统一查看群通知、群公告、成员和批量计划");
    ui->groupMemberWorkspaceBtn->setToolTip("打开群成员工作区，集中处理成员查看、邀请、备注和复制动作");
    ui->copyAccountBtn->setToolTip("复制当前 QQ 账号");
    ui->friendManagerBtn->setToolTip("打开好友管理器");
    ui->groupChatBtn->setToolTip("返回公共聊天室");
    ui->uploadAvatarBtn->setToolTip("打开头像工作区，集中处理更换、路径复制和目录动作");
    setStyleSheet(productMainWindowStyleSheet());
    assignButtonIcon(ui->copyAccountBtn, this, QStyle::SP_DialogSaveButton);
    assignButtonIcon(ui->uploadAvatarBtn, this, QStyle::SP_FileDialogContentsView);
    assignButtonIcon(ui->friendManagerBtn, this, QStyle::SP_FileDialogDetailedView);
    assignButtonIcon(ui->groupChatBtn, this, QStyle::SP_ComputerIcon);
    assignButtonIcon(ui->addFriendBtn, this, QStyle::SP_FileDialogNewFolder);
    assignButtonIcon(ui->friendNoticeBtn, this, QStyle::SP_MessageBoxInformation);
    assignButtonIcon(ui->groupNoticeBtn, this, QStyle::SP_MessageBoxWarning);
    assignButtonIcon(ui->groupMemberWorkspaceBtn, this, QStyle::SP_FileDialogListView);
    assignButtonIcon(ui->globalSearchBtn, this, QStyle::SP_FileDialogStart);
    assignButtonIcon(ui->createMenuBtn, this, QStyle::SP_FileDialogNewFolder);
    assignButtonIcon(ui->emojiBtn, this, QStyle::SP_DialogApplyButton);
    assignButtonIcon(ui->mentionBtn, this, QStyle::SP_ArrowRight);
    assignButtonIcon(ui->imageBtn, this, QStyle::SP_FileIcon);
    assignButtonIcon(ui->fileBtn, this, QStyle::SP_DriveFDIcon);
    assignButtonIcon(ui->clearBtn, this, QStyle::SP_TrashIcon);
    assignButtonIcon(ui->transferResumeBtn, this, QStyle::SP_ArrowForward);
    assignButtonIcon(ui->transferClearBtn, this, QStyle::SP_TrashIcon);
    assignButtonIcon(ui->transferCopyStatusBtn, this, QStyle::SP_FileDialogDetailedView);
    assignButtonIcon(ui->sendBtn, this, QStyle::SP_ArrowForward);

    QAction* contactWorkspaceAction = new QAction("联系人工作区", this);
    QAction* friendManagerAction = new QAction("好友管理器", this);
    QAction* backGroupAction = new QAction("返回群聊", this);
    QAction* avatarAction = new QAction("头像工作区", this);
    QAction* sendImageAction = new QAction("发送图片/视频", this);
    QAction* sendFileAction = new QAction("闪传文件", this);
    QAction* composerWorkspaceAction = new QAction("消息工作区", this);
    QAction* chatHistoryWorkspaceAction = new QAction("消息记录工作区", this);
    QAction* notificationWorkspaceAction = new QAction("通知控制台", this);
    QAction* groupInfoWorkspaceAction = new QAction("群信息工作区", this);
    m_resumeSavedTransferAction = new QAction("恢复未完成发送", this);
    m_resumeSavedTransferAction->setVisible(false);
    m_resumeSavedTransferAction->setEnabled(false);
    m_clearSavedTransferAction = new QAction("清除恢复记录", this);
    m_clearSavedTransferAction->setVisible(false);
    m_clearSavedTransferAction->setEnabled(false);
    QAction* transferWorkspaceAction = new QAction("文件工作区", this);
    m_copyLastTransferStatusAction = new QAction("复制最近文件状态", this);
    m_copyLastTransferStatusAction->setVisible(false);
    m_copyLastTransferStatusAction->setEnabled(false);
    QAction* filterHistoryAction = new QAction("按日期查记录", this);
    QAction* exportHistoryAction = new QAction("导出聊天记录", this);
    QAction* copyAccountAction = new QAction("复制账号", this);
    QAction* copySummaryAction = new QAction("复制账号摘要", this);
    QAction* logoutAction = new QAction("退出登录", this);
    ui->menubar->addAction(contactWorkspaceAction);
    ui->menubar->addAction(friendManagerAction);
    ui->menubar->addAction(backGroupAction);
    ui->menubar->addAction(avatarAction);
    ui->menubar->addAction(sendImageAction);
    ui->menubar->addAction(sendFileAction);
    ui->menubar->addAction(composerWorkspaceAction);
    ui->menubar->addAction(chatHistoryWorkspaceAction);
    ui->menubar->addAction(notificationWorkspaceAction);
    ui->menubar->addAction(groupInfoWorkspaceAction);
    ui->menubar->addAction(m_resumeSavedTransferAction);
    ui->menubar->addAction(m_clearSavedTransferAction);
    ui->menubar->addAction(transferWorkspaceAction);
    ui->menubar->addAction(m_copyLastTransferStatusAction);
    ui->menubar->addAction(filterHistoryAction);
    ui->menubar->addAction(exportHistoryAction);
    ui->menubar->addAction(copyAccountAction);
    ui->menubar->addAction(copySummaryAction);
    ui->menubar->addAction(logoutAction);

    connect(contactWorkspaceAction, &QAction::triggered, this, [this]() {
        onShowContactWorkspace();
    });
    connect(friendManagerAction, &QAction::triggered, this, &MainWindow::onShowFriendManager);
    connect(backGroupAction, &QAction::triggered, this, &MainWindow::onBackToGroupChat);
    connect(avatarAction, &QAction::triggered, this, &MainWindow::showAvatarWorkspace);
    connect(sendImageAction, &QAction::triggered, this, &MainWindow::onSendImage);
    connect(sendFileAction, &QAction::triggered, this, &MainWindow::onSendFile);
    connect(composerWorkspaceAction, &QAction::triggered, this, &MainWindow::onShowComposerWorkspace);
    connect(chatHistoryWorkspaceAction, &QAction::triggered, this, &MainWindow::onShowChatHistoryWorkspace);
    connect(notificationWorkspaceAction, &QAction::triggered, this, &MainWindow::onShowNotificationWorkspace);
    connect(groupInfoWorkspaceAction, &QAction::triggered, this, &MainWindow::showGroupInfoWorkspace);
    connect(m_resumeSavedTransferAction, &QAction::triggered, this, &MainWindow::onResumeSavedOutgoingTransfer);
    connect(m_clearSavedTransferAction, &QAction::triggered, this, &MainWindow::onClearSavedOutgoingTransfer);
    connect(transferWorkspaceAction, &QAction::triggered, this, &MainWindow::onShowTransferWorkspace);
    connect(m_copyLastTransferStatusAction, &QAction::triggered, this, [this]() {
        const TransferDiagnosticCopyUiState copyState = m_transferManager.diagnosticCopyUiState(m_lastTransferStatusDiagnostic);
        if (!copyState.action.enabled) {
            ui->statusbar->showMessage(copyState.emptyStatusMessage, 1800);
            return;
        }
        QApplication::clipboard()->setText(copyState.clipboardText);
        ui->statusbar->showMessage(copyState.copiedStatusMessage, 2200);
    });
    connect(filterHistoryAction, &QAction::triggered, this, &MainWindow::onFilterHistoryByDate);
    connect(exportHistoryAction, &QAction::triggered, this, &MainWindow::onExportHistory);
    connect(copyAccountAction, &QAction::triggered, this, &MainWindow::onCopyAccount);
    connect(copySummaryAction, &QAction::triggered, this, [this]() {
        QString summary = QString("账号摘要\nQQ:%1\n昵称:%2\n好友:%3\n群聊:%4\n当前会话:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(summary);
        ui->statusbar->showMessage("账号摘要已复制", 2200);
    });
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);
    refreshFriendList();

    connect(ui->sendBtn, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(ui->messageEdit, &QTextEdit::textChanged, this, &MainWindow::refreshComposerState);
    refreshComposerState();
    connect(ui->fileBtn, &QPushButton::clicked, this, &MainWindow::onSendFile);
    connect(ui->imageBtn, &QPushButton::clicked, this, &MainWindow::onSendImage);
    connect(ui->emojiBtn, &QPushButton::clicked, this, &MainWindow::onShowComposerWorkspace);
    connect(ui->mentionBtn, &QPushButton::clicked, this, &MainWindow::onInsertMention);
    connect(ui->transferResumeBtn, &QPushButton::clicked, this, &MainWindow::onResumeSavedOutgoingTransfer);
    connect(ui->transferClearBtn, &QPushButton::clicked, this, &MainWindow::onClearSavedOutgoingTransfer);
    connect(ui->transferCopyStatusBtn, &QPushButton::clicked, this, &MainWindow::onShowTransferWorkspace);
    QShortcut* contactSearchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(contactSearchShortcut, &QShortcut::activated, this, [this]() {
        ui->contactSearchEdit->setFocus();
        ui->contactSearchEdit->selectAll();
        ui->statusbar->showMessage("已定位到联系人搜索", 1600);
    });
    QShortcut* memberSearchShortcut = new QShortcut(QKeySequence("Ctrl+Shift+F"), this);
    connect(memberSearchShortcut, &QShortcut::activated, this, [this]() {
        ui->memberSearchEdit->setFocus();
        ui->memberSearchEdit->selectAll();
        ui->statusbar->showMessage("已定位到成员搜索", 1600);
    });
    QShortcut* globalSearchShortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(globalSearchShortcut, &QShortcut::activated, this, [this]() {
        onShowGlobalSearch();
    });
    QShortcut* contactWorkspaceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+J"), this);
    connect(contactWorkspaceShortcut, &QShortcut::activated, this, [this]() {
        onShowContactWorkspace();
    });
    QShortcut* composerWorkspaceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+K"), this);
    connect(composerWorkspaceShortcut, &QShortcut::activated, this, &MainWindow::onShowComposerWorkspace);
    QShortcut* chatHistoryWorkspaceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+H"), this);
    connect(chatHistoryWorkspaceShortcut, &QShortcut::activated, this, &MainWindow::onShowChatHistoryWorkspace);
    QShortcut* notificationWorkspaceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+N"), this);
    connect(notificationWorkspaceShortcut, &QShortcut::activated, this, &MainWindow::onShowNotificationWorkspace);
    QShortcut* groupInfoWorkspaceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+G"), this);
    connect(groupInfoWorkspaceShortcut, &QShortcut::activated, this, &MainWindow::showGroupInfoWorkspace);
    connect(ui->messageEdit, &QTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        const QString draftText = ui->messageEdit->toPlainText().trimmed();
        const QString clipboardText = QApplication::clipboard()->text().trimmed();
        const bool hasDraft = !draftText.isEmpty();
        const bool hasClipboardText = !clipboardText.isEmpty();
        const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
        const bool canReachTarget = isLocalGroup || (m_client && m_client->isConnected());
        auto describeInputAction = [](QAction* action, const QString& tip) {
            action->setToolTip(tip);
            action->setStatusTip(tip);
        };
        ChatContextComposerRuntimeState runtimeState;
        runtimeState.hasDraft = hasDraft;
        runtimeState.hasClipboardText = hasClipboardText;
        runtimeState.canReachTarget = canReachTarget;
        runtimeState.draftTextLength = draftText.size();
        runtimeState.targetDisplayName = targetName;
        const QList<ChatContextComposerMenuAction> runtimeActions = ChatContextManager::composerRuntimeActions(runtimeState);
        QAction* pasteAction = nullptr;
        QAction* pasteSendAction = nullptr;
        QAction* sendAction = nullptr;
        QAction* clearAction = nullptr;
        QAction* mentionAction = nullptr;
        for (const ChatContextComposerMenuAction& spec : runtimeActions) {
            QAction* action = menu.addAction(spec.title);
            describeInputAction(action, spec.toolTip);
            action->setData(spec.commandId);
            if (spec.commandId == QLatin1String("composer-paste")) {
                action->setEnabled(hasClipboardText);
                pasteAction = action;
            } else if (spec.commandId == QLatin1String("composer-paste-send")) {
                action->setEnabled(hasClipboardText && canReachTarget);
                pasteSendAction = action;
            } else if (spec.commandId == QLatin1String("composer-send")) {
                action->setEnabled(hasDraft && canReachTarget);
                sendAction = action;
            } else if (spec.commandId == QLatin1String("composer-clear")) {
                action->setEnabled(hasDraft);
                clearAction = action;
            } else if (spec.commandId == QLatin1String("composer-mention")) {
                mentionAction = action;
            }
        }
        menu.addSeparator();
        const QList<ChatContextComposerMenuAction> composerActions = ChatContextManager::composerMenuActions();
        QList<QAction*> composerMenuQtActions;
        composerMenuQtActions.reserve(composerActions.size());
        for (int i = 0; i < 8 && i < composerActions.size(); ++i) {
            const ChatContextComposerMenuAction& spec = composerActions.at(i);
            QAction* action = menu.addAction(spec.title);
            describeInputAction(action, spec.toolTip);
            action->setData(spec.commandId);
            composerMenuQtActions.append(action);
        }
        menu.addSeparator();
        const QList<ChatContextPhraseMenuPlan> phraseMenuPlans = ChatContextManager::composerPhraseMenuPlans();
        for (const ChatContextPhraseMenuPlan& plan : phraseMenuPlans) {
            QMenu* phraseMenu = menu.addMenu(plan.title);
            for (const QString& phrase : plan.phrases) {
                QAction* phraseAction = phraseMenu->addAction(phrase);
                connect(phraseAction, &QAction::triggered, ui->messageEdit, [this, phrase, plan]() {
                    insertChatDraftText(phrase, plan.insertedStatusMessage, 1400);
                });
            }
        }
        for (int i = 8; i < composerActions.size(); ++i) {
            const ChatContextComposerMenuAction& spec = composerActions.at(i);
            QAction* action = menu.addAction(spec.title);
            describeInputAction(action, spec.toolTip);
            action->setData(spec.commandId);
            composerMenuQtActions.append(action);
        }
        QAction* selected = menu.exec(ui->messageEdit->viewport()->mapToGlobal(pos));
        if (!selected) return;
        if (selected == pasteAction) {
            ui->messageEdit->paste();
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage(QString("已粘贴到输入框 · 当前 %1 字").arg(ui->messageEdit->toPlainText().trimmed().size()), 1600);
        } else if (selected == pasteSendAction) {
            ui->messageEdit->paste();
            onSendMessage();
        } else if (selected == sendAction) {
            onSendMessage();
        } else if (selected == clearAction) {
            ui->messageEdit->clear();
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage("输入草稿已清空", 1400);
        } else if (selected == mentionAction) {
            onInsertMention();
        } else {
            const QString commandId = selected->data().toString();
            if (!commandId.isEmpty()) {
                applyChatContextComposerCommand(commandId);
            }
        }
    });
    connect(ui->userListView, &QListView::doubleClicked, this, &MainWindow::onPrivateChat);
    connect(ui->userListView, &QListView::customContextMenuRequested, this, &MainWindow::onUserContextMenu);
    connect(ui->chatListView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;

        if (openChatAttachmentFromIndex(index)) {
            return;
        }
        showChatHistoryWorkspaceForRow(index.row());
    });
    connect(ui->chatListView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current, const QModelIndex&) {
        if (!current.isValid()) {
            clearSavedFileWorkspace();
            return;
        }
        const LocalSavedFileState savedFileState = savedFileActionState(current);
        if (!savedFileState.hasSavePath) {
            clearSavedFileWorkspace();
            return;
        }
        showSavedFileWorkspace(savedFileState, current.data().toString());
    });
    connect(ui->chatListView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = ui->chatListView->indexAt(pos);
        if (!index.isValid()) return;
        QString text = index.data().toString();
        if (text.isEmpty()) return;
        QMenu menu(this);
        const LocalSavedFileState savedFileState = savedFileActionState(index);
        const ChatContextSavedFileState savedContextState = chatContextSavedFileState(savedFileState);
        const bool isMediaMessage = ChatContextManager::isMediaMessage(text, savedContextState);
        const QList<ChatContextMenuActionSpec> actionSpecs = ChatContextManager::menuActionSpecs(
            isMediaMessage,
            savedContextState);
        auto iconForChatContextCommand = [](const QString& commandId) {
            if (commandId == QLatin1String("copy-message")
                    || commandId == QLatin1String("copy-plain")
                    || commandId == QLatin1String("copy-sender")
                    || commandId == QLatin1String("copy-time")
                    || commandId == QLatin1String("copy-media-card")
                    || commandId == QLatin1String("copy-file-notice")
                    || commandId == QLatin1String("copy-receipt")
                    || commandId == QLatin1String("copy-media-flow")
                    || commandId == QLatin1String("copy-save-path")) {
                return QStyle::SP_DialogSaveButton;
            }
            if (commandId == QLatin1String("open-saved-file")) {
                return QStyle::SP_DialogOpenButton;
            }
            if (commandId == QLatin1String("open-save-folder")) {
                return QStyle::SP_DirOpenIcon;
            }
            if (commandId == QLatin1String("quote")
                    || commandId == QLatin1String("forward")
                    || commandId == QLatin1String("resend")
                    || commandId == QLatin1String("mention-reply")) {
                return QStyle::SP_ArrowForward;
            }
            return QStyle::SP_FileDialogInfoView;
        };
        QAction* historyWorkspaceAction = addMenuActionWithIcon(menu,
                                                                this,
                                                                QStringLiteral("打开消息记录工作区"),
                                                                QStringLiteral("把这条消息带入消息记录工作区，集中处理复制、引用、转发和文件动作"),
                                                                QStringLiteral("open-chat-history-workspace"),
                                                                true,
                                                                QStyle::SP_FileDialogDetailedView);
        QAction* savedFileWorkspaceAction = nullptr;
        if (savedFileState.hasSavePath) {
            savedFileWorkspaceAction = addMenuActionWithIcon(menu,
                                                             this,
                                                             QStringLiteral("打开保存文件工作区"),
                                                             QStringLiteral("把这条已保存文件消息切到文件工作区，统一处理打开文件、打开目录、复制路径和诊断"),
                                                             QStringLiteral("open-saved-file-workspace"),
                                                             true,
                                                             QStyle::SP_DriveHDIcon);
        }
        QAction* senderWorkspaceAction = nullptr;
        const QString senderId = index.data(TransferChatItemRenderer::SenderIdRole).toString();
        if (!senderId.trimmed().isEmpty() && senderId != m_currentUserId) {
            senderWorkspaceAction = addMenuActionWithIcon(menu,
                                                          this,
                                                          QStringLiteral("查看发送者头像/资料"),
                                                          QStringLiteral("打开发送者资料工作区；远端未同步头像时显示稳定首字母头像"),
                                                          QStringLiteral("open-sender-workspace"),
                                                          true,
                                                          QStyle::SP_FileDialogInfoView);
        }
        menu.addSeparator();
        for (const ChatContextMenuActionSpec& spec : actionSpecs) {
            if (spec.separatorBefore) {
                menu.addSeparator();
            }
            addMenuActionWithIcon(menu,
                                  this,
                                  spec.title,
                                  spec.toolTip,
                                  spec.commandId,
                                  spec.enabled,
                                  iconForChatContextCommand(spec.commandId));
        }
        QAction* selected = menu.exec(ui->chatListView->viewport()->mapToGlobal(pos));
        if (!selected) return;
        if (selected == historyWorkspaceAction) {
            showChatHistoryWorkspaceForRow(index.row());
            return;
        }
        if (selected == savedFileWorkspaceAction) {
            showSavedFileWorkspace(savedFileState, text, QStringLiteral("已切换到保存文件工作区"));
            onShowTransferWorkspace();
            return;
        }
        if (selected == senderWorkspaceAction) {
            showUserEntryWorkspace(senderId, index.data(TransferChatItemRenderer::SenderNameRole).toString());
            return;
        }
        handleChatContextCommand(selected->data().toString(), text, savedFileState);
    });
    connect(ui->contactSearchEdit, &QLineEdit::textChanged, this, &MainWindow::onContactSearchChanged);
    ui->contactSearchEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->contactSearchEdit, &QLineEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        const QString clipboardText = QApplication::clipboard()->text().trimmed();
        const bool hasClipboardText = !clipboardText.isEmpty();
        const bool hasSearchText = !ui->contactSearchEdit->text().trimmed().isEmpty();
        QAction* contactWorkspaceAction = menu.addAction(QStringLiteral("打开联系人工作区"));
        QAction* globalSearchWorkspaceAction = menu.addAction(QStringLiteral("打开综合搜索工作区"));
        menu.addSeparator();
        QAction* pasteAction = menu.addAction("粘贴");
        QAction* pasteSearchAction = menu.addAction("粘贴并搜索");
        QAction* createGroupAction = menu.addAction("用关键词建群");
        QAction* copySearchCardAction = menu.addAction("复制搜索名片");
        QAction* clearAction = menu.addAction("清空搜索");
        pasteAction->setEnabled(hasClipboardText);
        pasteSearchAction->setEnabled(hasClipboardText);
        clearAction->setEnabled(hasSearchText);
        pasteAction->setToolTip(hasClipboardText ? "把剪贴板文字粘贴到 QQ 搜索框" : "剪贴板里没有可粘贴的文字");
        pasteSearchAction->setToolTip(hasClipboardText ? "粘贴剪贴板文字并搜索 QQ 账号" : "剪贴板里没有可搜索的文字");
        clearAction->setToolTip(hasSearchText ? "清空当前 QQ 搜索条件" : "搜索框已经是空的");
        QAction* selected = menu.exec(ui->contactSearchEdit->mapToGlobal(pos));
        if (selected == contactWorkspaceAction) {
            onShowContactWorkspace(ui->contactSearchEdit->text().trimmed());
        } else if (selected == globalSearchWorkspaceAction) {
            onShowGlobalSearch(ui->contactSearchEdit->text().trimmed());
        } else if (selected == pasteAction) {
            ui->contactSearchEdit->paste();
            ui->contactSearchEdit->setFocus();
            ui->statusbar->showMessage("已粘贴到 QQ 搜索框", 1600);
        } else if (selected == pasteSearchAction) {
            ui->contactSearchEdit->clear();
            ui->contactSearchEdit->paste();
            searchAndAddAccount(ui->contactSearchEdit->text().trimmed(), this);
        } else if (selected == createGroupAction) {
            QString groupName = ui->contactSearchEdit->text().trimmed();
            if (groupName.isEmpty()) groupName = "搜索群聊";
            QString groupId = "local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
            m_localGroupIds << groupId;
            m_localGroupNames[groupId] = groupName;
            m_localGroupAnnouncements[groupId] = QString("%1 已从 QQ 搜索框创建，可继续邀请好友并发送消息。").arg(groupName);
            m_localGroupMembers[groupId] = QStringList{m_currentUserId};
            saveLocalGroups();
            refreshFriendList();
            switchToLocalGroup(groupId, groupName);
            ui->statusbar->showMessage("已从 QQ 搜索框创建群聊: " + groupName, 2500);
        } else if (selected == copySearchCardAction) {
            QString keyword = ui->contactSearchEdit->text().trimmed();
            if (keyword.isEmpty()) keyword = "全部";
            QString card = QString("QQ搜索名片\n关键词:%1\n我的QQ:%2\n昵称:%3\n好友:%4\n群聊:%5")
                .arg(keyword, m_currentUserId, m_currentUserName)
                .arg(m_friendIds.size())
                .arg(m_localGroupIds.size());
            QApplication::clipboard()->setText(card);
            ui->statusbar->showMessage("QQ 搜索名片已复制", 2200);
        } else if (selected == clearAction) {
            ui->contactSearchEdit->clear();
            ui->contactSearchEdit->setFocus();
            ui->statusbar->showMessage("QQ 搜索已清空", 1400);
        }
    });
    connect(ui->contactSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString text = ui->contactSearchEdit->text().trimmed();
        if (text.isEmpty()) {
            onShowContactWorkspace();
            return;
        }
        onShowGlobalSearch(text);
    });
    connect(ui->globalSearchBtn, &QPushButton::clicked, this, [this]() {
        QString text = ui->contactSearchEdit->text().trimmed();
        if (text.isEmpty()) {
            onShowGlobalSearch();
        } else {
            onShowGlobalSearch(text);
        }
    });
    connect(ui->createMenuBtn, &QPushButton::clicked, this, &MainWindow::onShowCreateMenu);
    connect(ui->friendNoticeBtn, &QPushButton::clicked, this, &MainWindow::onShowNotificationWorkspace);
    connect(ui->groupNoticeBtn, &QPushButton::clicked, this, &MainWindow::onShowNotificationWorkspace);
    connect(ui->groupMemberWorkspaceBtn, &QPushButton::clicked, this, [this]() {
        onShowGroupMemberWorkspace();
    });
    connect(ui->announcementTitleLabel, &QLabel::linkActivated, this, [this](const QString&) {
        showGroupInfoWorkspace();
    });
    ui->groupInfoPanel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->groupInfoPanel, &QFrame::customContextMenuRequested, this, [this](const QPoint&) {
        showGroupInfoWorkspace();
    });
    connect(ui->copyAccountBtn, &QPushButton::clicked, this, &MainWindow::onCopyAccount);
    connect(ui->uploadAvatarBtn, &QPushButton::clicked, this, &MainWindow::showAvatarWorkspace);
    ui->uploadAvatarBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->uploadAvatarBtn, &QPushButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        Q_UNUSED(pos)
        showAvatarWorkspace();
    });
    ui->profileCard->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->profileCard, &QFrame::customContextMenuRequested, this, [this](const QPoint& pos) {
        Q_UNUSED(pos)
        showProfileWorkspace();
    });
    connect(ui->addFriendBtn, &QPushButton::clicked, this, &MainWindow::onShowQuickAddFriend);
    connect(ui->friendManagerBtn, &QPushButton::clicked, this, &MainWindow::onShowFriendManager);
    connect(ui->groupChatBtn, &QPushButton::clicked, this, &MainWindow::onBackToGroupChat);
    connect(ui->memberSearchEdit, &QLineEdit::textChanged, this, [this]() { refreshGroupMemberPanel(); });
    connect(ui->memberSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString text = ui->memberSearchEdit->text().trimmed();
        if (text.isEmpty()) {
            onShowGroupMemberWorkspace();
            return;
        }

        static const QRegularExpression numericAccountPattern(QStringLiteral("^\\d+$"));
        if (numericAccountPattern.match(text).hasMatch()) {
            handleGroupMemberSearchSubmit(text, this);
            ui->memberSearchEdit->selectAll();
            return;
        }

        onShowGroupMemberWorkspace(text);
    });
    ui->memberSearchEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->memberSearchEdit, &QLineEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        const QString clipboardText = QApplication::clipboard()->text().trimmed();
        const bool hasClipboardText = !clipboardText.isEmpty();
        const bool hasSearchText = !ui->memberSearchEdit->text().trimmed().isEmpty();
        QAction* memberWorkspaceAction = menu.addAction(QStringLiteral("打开群成员工作区"));
        menu.addSeparator();
        QAction* pasteAction = menu.addAction("粘贴");
        QAction* pasteSearchAction = menu.addAction("粘贴并搜索");
        QAction* addVisibleAction = menu.addAction("发送可见成员好友申请");
        QAction* copyVisibleAction = menu.addAction("复制可见成员");
        QAction* copyOnlineVisibleAction = menu.addAction("复制在线成员");
        QAction* clearAction = menu.addAction("清空搜索");
        pasteAction->setEnabled(hasClipboardText);
        pasteSearchAction->setEnabled(hasClipboardText);
        clearAction->setEnabled(hasSearchText);
        pasteAction->setToolTip(hasClipboardText ? "把剪贴板文字粘贴到成员搜索框" : "剪贴板里没有可粘贴的文字");
        pasteSearchAction->setToolTip(hasClipboardText ? "粘贴剪贴板文字并刷新成员筛选" : "剪贴板里没有可搜索的文字");
        clearAction->setToolTip(hasSearchText ? "清空当前成员搜索条件" : "成员搜索框已经是空的");
        QAction* selected = menu.exec(ui->memberSearchEdit->mapToGlobal(pos));
        if (selected == memberWorkspaceAction) {
            onShowGroupMemberWorkspace(ui->memberSearchEdit->text().trimmed());
        } else if (selected == pasteAction) {
            ui->memberSearchEdit->paste();
            ui->memberSearchEdit->setFocus();
            ui->statusbar->showMessage("已粘贴到成员搜索框", 1600);
        } else if (selected == pasteSearchAction) {
            ui->memberSearchEdit->clear();
            ui->memberSearchEdit->paste();
            refreshGroupMemberPanel();
            ui->memberSearchEdit->setFocus();
            ui->statusbar->showMessage("成员筛选已更新", 1600);
        } else if (selected == addVisibleAction) {
            int requestCount = 0;
            int pendingSkipped = 0;
            int failedCount = 0;
            for (int i = 0; i < m_groupMemberModel->rowCount(); ++i) {
                QStandardItem* item = m_groupMemberModel->item(i);
                if (!item) continue;
                QString id = item->data(Qt::UserRole + 1).toString();
                if (id.isEmpty() || id == m_currentUserId || id.startsWith("group_search_add:") || id.startsWith("group_invite:") || m_friendIds.contains(id)) continue;
                if (m_pendingOutgoingFriendRequests.contains(id)) {
                    ++pendingSkipped;
                    continue;
                }
                const QString displayName = contactDisplayName(id);
                if (!m_client->sendFriendRequest(id)) {
                    ++failedCount;
                    continue;
                }
                m_friendNames[id] = displayName;
                m_pendingOutgoingFriendRequests << id;
                ++requestCount;
            }
            if (requestCount > 0) {
                refreshFriendList();
                refreshGroupMemberPanel();
                QString detail = QString("已向 %1 个可见群成员发起好友申请").arg(requestCount);
                if (pendingSkipped > 0) detail += QString(" · 已跳过申请中 %1 个").arg(pendingSkipped);
                if (failedCount > 0) detail += QString(" · 失败 %1 个").arg(failedCount);
                appendSystemMessage(detail);
                ui->statusbar->showMessage(detail, 2800);
            } else {
                QString message = pendingSkipped > 0
                    ? QString("可见群成员均已是好友或申请中")
                    : QString("暂无可发送申请的可见群成员");
                if (failedCount > 0) message += QString(" · 失败 %1 个").arg(failedCount);
                ui->statusbar->showMessage(message, 2400);
            }
        } else if (selected == copyVisibleAction) {
            copyVisibleGroupMembers(false);
        } else if (selected == copyOnlineVisibleAction) {
            copyVisibleGroupMembers(true);
        } else if (selected == clearAction) {
            ui->memberSearchEdit->clear();
            ui->memberSearchEdit->setFocus();
            ui->statusbar->showMessage("成员搜索已清空", 1400);
        }
    });
    connect(ui->groupMemberListView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        QString targetId = index.data(Qt::UserRole + 1).toString();
        handleGroupMemberEntryActivated(targetId, this);
    });
    connect(ui->groupMemberListView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = ui->groupMemberListView->indexAt(pos);
        const bool isLocalGroup = m_privateChatTarget.startsWith("local_group_");
        const bool isServerPublicGroup = m_privateChatTarget.isEmpty() && !m_serverGroupMembers.value("public").isEmpty();
        if (!index.isValid() || (!isLocalGroup && !isServerPublicGroup)) return;
        QString memberId = index.data(Qt::UserRole + 1).toString();
        if (memberId.startsWith("group_search_add:") || memberId.startsWith("group_invite:")) return;
        if (memberId.isEmpty() || memberId == m_currentUserId) return;
        QMenu menu(this);
        QAction* memberWorkspaceAction = addMenuActionWithIcon(menu,
                                                               this,
                                                               QStringLiteral("打开群成员工作区"),
                                                               QStringLiteral("把当前成员和筛选上下文带入群成员工作区，集中处理邀请、备注、复制和移出动作"),
                                                               QString(),
                                                               true,
                                                               QStyle::SP_FileDialogDetailedView);
        QAction* objectWorkspaceAction = addMenuActionWithIcon(menu,
                                                               this,
                                                               QStringLiteral("打开对象工作区"),
                                                               QStringLiteral("把当前成员带入对象工作区，统一处理私聊、名片、加密和好友动作"),
                                                               QString(),
                                                               true,
                                                               QStyle::SP_FileDialogInfoView);
        menu.addSeparator();
        const GroupMemberContextMenuPlan plan = GroupManager::memberContextMenuPlan(
            memberId,
            m_currentUserId,
            isLocalGroup,
            isServerPublicGroup,
            isLocalGroup ? groupOwnerId(m_privateChatTarget) : QString(),
            isLocalGroup && isCurrentUserGroupOwner(m_privateChatTarget),
            m_serverGroupOwners,
            m_serverGroupMemberRoles);
        QAction* chatAction = addMenuActionWithIcon(menu,
                                                    this,
                                                    QStringLiteral("私聊"),
                                                    plan.chatToolTip,
                                                    QString(),
                                                    true,
                                                    QStyle::SP_ArrowForward);
        QAction* copyAction = addMenuActionWithIcon(menu,
                                                    this,
                                                    QStringLiteral("复制QQ号"),
                                                    plan.copyToolTip,
                                                    QString(),
                                                    true,
                                                    QStyle::SP_DialogSaveButton);
        QAction* profileAction = addMenuActionWithIcon(menu,
                                                       this,
                                                       QStringLiteral("复制名片"),
                                                       plan.profileToolTip,
                                                       QString(),
                                                       true,
                                                       QStyle::SP_FileDialogDetailedView);
        menu.addSeparator();
        QAction* copyAllAction = addMenuActionWithIcon(menu,
                                                       this,
                                                       QStringLiteral("复制群成员列表"),
                                                       plan.copyAllToolTip,
                                                       QString(),
                                                       true,
                                                       QStyle::SP_FileDialogListView);
        QAction* copyOnlineAction = addMenuActionWithIcon(menu,
                                                          this,
                                                          QStringLiteral("复制在线群成员"),
                                                          plan.copyOnlineToolTip,
                                                          QString(),
                                                          true,
                                                          QStyle::SP_DialogYesButton);
        QAction* renameAction = addMenuActionWithIcon(menu,
                                                      this,
                                                      QStringLiteral("设置备注"),
                                                      plan.renameToolTip,
                                                      QString(),
                                                      true,
                                                      QStyle::SP_FileDialogInfoView);
        QAction* promoteAdminAction = nullptr;
        QAction* demoteAdminAction = nullptr;
        if (isServerPublicGroup) {
            menu.addSeparator();
            promoteAdminAction = addMenuActionWithIcon(menu,
                                                       this,
                                                       QStringLiteral("设为管理员"),
                                                       plan.promoteAdminToolTip,
                                                       QString(),
                                                       plan.promoteAdminEnabled,
                                                       QStyle::SP_DialogApplyButton);
            demoteAdminAction = addMenuActionWithIcon(menu,
                                                      this,
                                                      QStringLiteral("取消管理员"),
                                                      plan.demoteAdminToolTip,
                                                      QString(),
                                                      plan.demoteAdminEnabled,
                                                      QStyle::SP_DialogCancelButton);
        }
        menu.addSeparator();
        QAction* removeAction = addMenuActionWithIcon(menu,
                                                      this,
                                                      QStringLiteral("移出群聊"),
                                                      plan.removeToolTip,
                                                      QString(),
                                                      plan.removeEnabled,
                                                      QStyle::SP_TrashIcon);
        QAction* selected = menu.exec(ui->groupMemberListView->viewport()->mapToGlobal(pos));
        if (selected == memberWorkspaceAction) {
            onShowGroupMemberWorkspace(memberId);
        } else if (selected == objectWorkspaceAction) {
            showUserEntryWorkspace(memberId, contactDisplayName(memberId));
        } else if (selected == chatAction) {
            ensureFriendRequestQueued(memberId,
                                      QStringLiteral("已向群成员发起好友申请 QQ:%1，等待对方同意"),
                                      true);
            openPrivateSession(memberId);
        } else if (selected == copyAction) {
            QApplication::clipboard()->setText(memberId);
            ui->statusbar->showMessage("QQ 号已复制: " + memberId, 2500);
        } else if (selected == profileAction) {
            const QString groupName = isLocalGroup ? m_localGroupNames.value(m_privateChatTarget, "群聊") : m_serverGroupNames.value("public", "公共聊天室");
            QString card = QString("QQ:%1\n昵称:%2\n群聊:%3").arg(memberId, contactDisplayName(memberId), groupName);
            QApplication::clipboard()->setText(card);
            ui->statusbar->showMessage("群成员名片已复制", 1800);
        } else if (selected == copyAllAction) {
            QStringList cards;
            const QStringList memberIds = isLocalGroup ? m_localGroupMembers.value(m_privateChatTarget) : m_serverGroupMembers.value("public");
            for (const QString& id : memberIds) {
                cards << QString("QQ:%1 昵称:%2 状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) || id == m_currentUserId ? "在线" : "离线");
            }
            QApplication::clipboard()->setText(cards.join('\n'));
            ui->statusbar->showMessage(QString("已复制 %1 个群成员").arg(cards.size()), 2200);
        } else if (selected == copyOnlineAction) {
            QStringList cards;
            const QStringList memberIds = isLocalGroup ? m_localGroupMembers.value(m_privateChatTarget) : m_serverGroupMembers.value("public");
            for (const QString& id : memberIds) {
                if (id != m_currentUserId && !isContactOnline(id)) continue;
                cards << QString("在线群成员 QQ:%1 昵称:%2").arg(id, contactDisplayName(id));
            }
            if (cards.isEmpty()) {
                ui->statusbar->showMessage("当前群聊没有在线成员", 2200);
                return;
            }
            QApplication::clipboard()->setText(cards.join('\n'));
            ui->statusbar->showMessage(QString("已复制 %1 个在线群成员").arg(cards.size()), 2200);
        } else if (selected == renameAction) {
            promptAndSetGroupMemberRemark(memberId, this);
        } else if (promoteAdminAction && selected == promoteAdminAction) {
            if (!plan.canSetPublicAdmin) {
                ui->statusbar->showMessage(plan.promoteDeniedMessage, 2400);
                return;
            }
            requestServerGroupMemberUpdate(memberId, "promote_admin");
        } else if (demoteAdminAction && selected == demoteAdminAction) {
            if (!plan.canSetPublicAdmin) {
                ui->statusbar->showMessage(plan.demoteDeniedMessage, 2400);
                return;
            }
            requestServerGroupMemberUpdate(memberId, "demote_admin");
        } else if (selected == removeAction) {
            removeGroupMemberWithConfirmation(memberId, this);
        }
    });
    connect(ui->clearBtn, &QPushButton::clicked, this, &MainWindow::onClearHistory);
    ui->announcementTitleLabel->setText("群公告");
    ui->announcementTitleLabel->setTextFormat(Qt::RichText);
    ui->announcementTitleLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    refreshTransferWorkspaceCard(nullptr,
                                 nullptr,
                                 m_hasTransferWorkspaceSendState ? &m_transferWorkspaceSendState : nullptr);
    refreshGroupMemberPanel();
    refreshMainWorkbenchChrome();
}

void MainWindow::refreshComposerState() {
    const QString draftText = ui->messageEdit->toPlainText().trimmed();
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    ComposerContext context;
    context.draftText = draftText;
    context.targetName = targetName;
    context.localGroup = isLocalGroup;
    context.removedFromPublicGroup = m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup();
    context.clientConnected = m_client && m_client->isConnected();
    context.encryptedReady = !m_privateChatTarget.isEmpty()
        && !isLocalGroup
        && m_client
        && m_client->hasE2ESession(m_privateChatTarget)
        && m_client->e2ePeerIdentityTrusted(m_privateChatTarget)
        && !m_client->e2eSessionNeedsRotation(m_privateChatTarget);
    const ComposerUiState state = ComposerManager::uiState(context);

    ui->sendBtn->setEnabled(state.canSend);
    ui->sendBtn->setToolTip(state.sendToolTip);
    ui->messageEdit->setPlaceholderText(state.messagePlaceholder);
    ui->messageEdit->setToolTip(state.messageToolTip);
    ui->fileBtn->setEnabled(state.sendFileEnabled);
    ui->fileBtn->setToolTip(state.fileToolTip);
    ui->imageBtn->setEnabled(state.sendImageEnabled);
    ui->imageBtn->setToolTip(state.imageToolTip);
    refreshMainWorkbenchChrome();
}

void MainWindow::refreshMainWorkbenchChrome() {
    if (!ui->chatSessionMetaLabel
        || !ui->chatSessionStatusLabel
        || !ui->composerStatusTitleLabel
        || !ui->composerStatusDetailLabel
        || !ui->groupSummaryTitleLabel
        || !ui->groupSummaryDetailLabel) {
        return;
    }

    const QString targetId = m_privateChatTarget.trimmed();
    const bool inPublicRoom = targetId.isEmpty();
    const bool isLocalGroup = targetId.startsWith(QStringLiteral("local_group_"));
    const bool isPrivateChat = !inPublicRoom && !isLocalGroup;
    const bool clientConnected = m_client && m_client->isConnected();
    const bool removedFromPublicGroup = inPublicRoom && isCurrentUserRemovedFromPublicGroup();

    QString chatMeta;
    QString chatStatus;
    QString sessionTone = QStringLiteral("muted");

    if (inPublicRoom) {
        const int memberCount = m_serverGroupMembers.value(QStringLiteral("public")).size();
        chatMeta = QStringLiteral("公共会话 · 当前账号 QQ %1").arg(m_currentUserId.isEmpty() ? QStringLiteral("未登录") : m_currentUserId);
        if (removedFromPublicGroup) {
            chatStatus = QStringLiteral("当前账号已被移出公共群，仅保留历史只读与等待重新邀请入口。");
            sessionTone = QStringLiteral("warning");
        } else {
            chatStatus = memberCount > 0
                ? QStringLiteral("公共聊天室已就绪，可继续消息、文件、群公告和成员协作。当前服务端成员 %1 人。").arg(memberCount)
                : QStringLiteral("公共聊天室可继续发送消息、文件和图片/视频；服务端成员快照会在同步后出现。");
            sessionTone = clientConnected ? QStringLiteral("accent") : QStringLiteral("warning");
        }
    } else if (isLocalGroup) {
        const QString groupName = m_localGroupNames.value(targetId, contactDisplayName(targetId));
        const QStringList members = m_localGroupMembers.value(targetId);
        const QString ownerId = groupOwnerId(targetId);
        const QString ownerName = ownerId == m_currentUserId ? m_currentUserName : contactDisplayName(ownerId);
        chatMeta = QStringLiteral("本地群 · %1 · 成员 %2").arg(groupName).arg(members.isEmpty() ? 1 : members.size());
        chatStatus = isCurrentUserGroupOwner(targetId)
            ? QStringLiteral("你是群主，可继续发送消息、邀请好友、调整公告并管理成员边界。")
            : QStringLiteral("当前位于本地群会话，可继续发送消息、查看公告并从成员工作区处理协作。");
        if (!ownerName.trimmed().isEmpty()) {
            chatStatus += QStringLiteral(" 群主：%1。").arg(ownerName);
        }
        sessionTone = QStringLiteral("accent");
    } else {
        const QString peerName = contactDisplayName(targetId);
        const bool online = isContactOnline(targetId);
        const bool hasE2E = m_client && m_client->hasE2ESession(targetId);
        const bool trustedE2E = hasE2E && m_client->e2ePeerIdentityTrusted(targetId) && !m_client->e2eSessionNeedsRotation(targetId);
        chatMeta = QStringLiteral("私聊 · %1 · QQ %2").arg(peerName, targetId);
        if (!clientConnected) {
            chatStatus = QStringLiteral("当前连接未恢复，草稿会保留；恢复连接后可继续向 %1 发送消息。").arg(peerName);
            sessionTone = QStringLiteral("warning");
        } else if (trustedE2E) {
            chatStatus = QStringLiteral("端到端会话已就绪，可继续发送消息、图片/视频和文件，并保留加密状态证据。");
            sessionTone = QStringLiteral("success");
        } else if (hasE2E) {
            chatStatus = QStringLiteral("已建立端到端会话，但仍需继续关注信任或轮换状态。");
            sessionTone = QStringLiteral("warning");
        } else {
            chatStatus = QStringLiteral("%1当前%2，可继续私聊与文件发送；端到端状态尚未就绪。")
                .arg(peerName, online ? QStringLiteral("在线") : QStringLiteral("离线"));
            sessionTone = QStringLiteral("accent");
        }
    }

    ui->chatSessionMetaLabel->setText(chatMeta);
    ui->chatSessionStatusLabel->setText(chatStatus);
    applyToneProperty(ui->chatSessionCard, sessionTone);

    ComposerContext composerContext;
    composerContext.draftText = ui->messageEdit ? ui->messageEdit->toPlainText().trimmed() : QString();
    composerContext.targetName = inPublicRoom
        ? QStringLiteral("公共聊天室")
        : (isLocalGroup ? m_localGroupNames.value(targetId, QStringLiteral("群聊")) : contactDisplayName(targetId));
    composerContext.localGroup = isLocalGroup;
    composerContext.removedFromPublicGroup = removedFromPublicGroup;
    composerContext.clientConnected = clientConnected;
    composerContext.encryptedReady = isPrivateChat
        && m_client
        && m_client->hasE2ESession(targetId)
        && m_client->e2ePeerIdentityTrusted(targetId)
        && !m_client->e2eSessionNeedsRotation(targetId);
    const ComposerUiState composerState = ComposerManager::uiState(composerContext);
    ui->composerStatusTitleLabel->setText(composerState.workspaceTitle.trimmed().isEmpty()
        ? QStringLiteral("消息工作区")
        : composerState.workspaceTitle);
    QString composerDetail = composerState.workspaceDetail.trimmed();
    if (!composerState.draftSummary.trimmed().isEmpty()) {
        composerDetail = composerDetail.isEmpty()
            ? composerState.draftSummary
            : QStringLiteral("%1 %2").arg(composerDetail, composerState.draftSummary);
    }
    ui->composerStatusDetailLabel->setText(composerDetail.isEmpty()
        ? QStringLiteral("输入框、草稿与发送入口会在这里给出稳定反馈。")
        : composerDetail);
    applyToneProperty(ui->composerStatusCard, composerState.stateTone.trimmed().isEmpty()
        ? QStringLiteral("muted")
        : composerState.stateTone);

    QString summaryTitle;
    QString summaryDetail;
    QString summaryTone = QStringLiteral("muted");
    if (inPublicRoom) {
        summaryTitle = QStringLiteral("公共群总览");
        if (removedFromPublicGroup) {
            summaryDetail = QStringLiteral("当前只保留历史记录查看；公告与成员区域会提示重新邀请前的只读边界。");
            summaryTone = QStringLiteral("warning");
        } else {
            const QString ownerId = m_serverGroupOwners.value(QStringLiteral("public"));
            const QString ownerName = ownerId.isEmpty()
                ? QStringLiteral("未指定")
                : (ownerId == m_currentUserId ? m_currentUserName : m_serverGroupMemberNames.value(QStringLiteral("public|") + ownerId, contactDisplayName(ownerId)));
            summaryDetail = QStringLiteral("群公告、成员与审计信息统一展示在右侧。群主：%1。%2")
                .arg(ownerName,
                     canCurrentUserManageServerGroup(QStringLiteral("public"))
                        ? QStringLiteral("当前账号可编辑公告并执行成员管理。")
                        : QStringLiteral("当前账号可查看公告、成员与通知控制台。"));
            summaryTone = clientConnected ? QStringLiteral("accent") : QStringLiteral("warning");
        }
    } else if (isLocalGroup) {
        const QString groupName = m_localGroupNames.value(targetId, QStringLiteral("群聊"));
        summaryTitle = QStringLiteral("群聊总览 · %1").arg(groupName);
        const int memberCount = m_localGroupMembers.value(targetId).size();
        summaryDetail = QStringLiteral("右侧持续显示公告、成员筛选和群成员工作区入口。当前群成员 %1 人。%2")
            .arg(memberCount > 0 ? memberCount : 1)
            .arg(isCurrentUserGroupOwner(targetId)
                ? QStringLiteral("你可以继续邀请成员、设置备注和移出成员。")
                : QStringLiteral("可查看成员状态、复制成员信息并发起私聊。"));
        summaryTone = QStringLiteral("accent");
    } else {
        summaryTitle = QStringLiteral("当前非群会话");
        summaryDetail = QStringLiteral("右侧区域收拢为会话提示与私聊边界说明；群成员与公告动作请从工作区入口进入。");
        if (m_client && m_client->hasE2ESession(targetId) && m_client->e2ePeerIdentityTrusted(targetId)) {
            summaryDetail += QStringLiteral(" ") + e2eSessionStatusText(targetId).section('\n', 0, 0);
            summaryTone = QStringLiteral("success");
        } else {
            summaryTone = QStringLiteral("muted");
        }
    }
    ui->groupSummaryTitleLabel->setText(summaryTitle);
    ui->groupSummaryDetailLabel->setText(summaryDetail);
    applyToneProperty(ui->groupSummaryCard, summaryTone);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress
        && (watched == ui->contactSearchEdit || watched == ui->memberSearchEdit)) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
            if (edit && !edit->text().isEmpty()) {
                edit->clear();
                ui->statusbar->showMessage(watched == ui->contactSearchEdit ? "联系人搜索已清空" : "成员搜索已清空", 1400);
                return true;
            }
        }
    }

    if (watched == ui->messageEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            if (!ui->messageEdit->toPlainText().trimmed().isEmpty()) {
                ui->messageEdit->clear();
                ui->statusbar->showMessage("输入草稿已清空", 1400);
                return true;
            }
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            const bool wantsNewLine = keyEvent->modifiers().testFlag(Qt::ControlModifier)
                || keyEvent->modifiers().testFlag(Qt::ShiftModifier);
            if (wantsNewLine) {
                ui->messageEdit->insertPlainText("\n");
            } else {
                onSendMessage();
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupTray() {
    m_trayMenu = new QMenu(this);
    QAction* showAction = new QAction("显示窗口", this);
    QAction* copyAccountAction = new QAction("复制账号", this);
    QAction* copySummaryAction = new QAction("复制账号摘要", this);
    QAction* copyMediaPackAction = new QAction("复制媒体发送包", this);
    QAction* copyFullMediaPlanAction = new QAction("复制完整媒体计划", this);
    QAction* sendImageAction = new QAction("发送图片/视频", this);
    QAction* sendFileAction = new QAction("闪传文件", this);
    QAction* logoutAction = new QAction("退出登录", this);
    QAction* quitAction = new QAction("退出", this);
    m_trayMenu->addAction(showAction);
    m_trayMenu->addAction(copyAccountAction);
    m_trayMenu->addAction(copySummaryAction);
    m_trayMenu->addAction(copyMediaPackAction);
    m_trayMenu->addAction(copyFullMediaPlanAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(sendImageAction);
    m_trayMenu->addAction(sendFileAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(logoutAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(quitAction);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("QtNetworkChat");
    m_trayIcon->setIcon(windowIcon().isNull() ? createChatIcon(m_currentUserName) : windowIcon());

    connect(showAction, &QAction::triggered, this, [this]() {
        this->show();
        this->raise();
        this->activateWindow();
    });
    connect(copyAccountAction, &QAction::triggered, this, &MainWindow::onCopyAccount);
    connect(copySummaryAction, &QAction::triggered, this, [this]() {
        QString summary = QString("账号摘要\nQQ:%1\n昵称:%2\n在线状态:在线\n好友:%3\n群聊:%4\n当前会话:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
        QApplication::clipboard()->setText(summary);
        ui->statusbar->showMessage("托盘账号摘要已复制", 2200);
    });
    connect(copyMediaPackAction, &QAction::triggered, this, [this]() {
        QString sessionName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith("local_group_")) sessionId = sessionId.mid(QString("local_group_").size());
        if (sessionId.isEmpty()) sessionId = "public";
        QStringList rows;
        rows << QString("托盘媒体发送包 · 会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QString("QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4").arg(m_currentUserId, m_currentUserName, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()));
        rows << "发送图片/视频：托盘菜单直接点击发送图片/视频";
        rows << "闪传文件：托盘菜单直接点击闪传文件";
        rows << "支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("查收话术：我已通过 QtNetworkChat 发送媒体到 %1，请注意查收。").arg(sessionName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("托盘媒体发送包已复制", 2200);
    });
    connect(copyFullMediaPlanAction, &QAction::triggered, this, [this]() {
        QString sessionName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
        QString sessionId = m_privateChatTarget;
        if (sessionId.startsWith("local_group_")) sessionId = sessionId.mid(QString("local_group_").size());
        if (sessionId.isEmpty()) sessionId = "public";
        QStringList rows;
        rows << QString("托盘完整媒体计划 · 当前会话:%1 · 会话号:%2").arg(sessionName, sessionId);
        rows << QString("我的QQ:%1 · 昵称:%2 · 好友:%3 · 群聊:%4 · 在线:%5")
            .arg(m_currentUserId, m_currentUserName, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()), QString::number(m_knownUsers.size()));
        rows << "1. 用综合搜索或好友申请确认目标 QQ、好友或群聊";
        rows << "2. 从托盘直接复制媒体包，或打开窗口后发送图片/视频、闪传文件";
        rows << "3. 发送后聊天记录可右键复制媒体卡片、查收话术、回执话术和保存路径";
        rows << "4. 支持 png/jpg/gif/mp4/mov/avi/mkv/wmv/flv/webm 和常用文档压缩包";
        rows << QString("当前查收话术：我已准备发送媒体文件到 %1，请注意查收。").arg(sessionName);
        QApplication::clipboard()->setText(rows.join('\n'));
        ui->statusbar->showMessage("托盘完整媒体计划已复制", 2200);
    });
    connect(sendImageAction, &QAction::triggered, this, &MainWindow::onSendImage);
    connect(sendFileAction, &QAction::triggered, this, &MainWindow::onSendFile);
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_isQuitting = true;
        close();
    });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

void MainWindow::onSendMessage() {
    QString text = ui->messageEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        ui->messageEdit->setFocus();
        ui->statusbar->showMessage("请输入消息内容后再发送", 1800);
        return;
    }

    const QString originalText = text;
    if (text == "/card" || text == "名片") {
        text = QString("我的名片：%1（QQ:%2） · 好友%3 · 群聊%4")
            .arg(m_currentUserName, m_currentUserId, QString::number(m_friendIds.size()), QString::number(m_localGroupIds.size()));
    } else if (text == "/invite" || text == "邀请") {
        text = m_privateChatTarget.startsWith("local_group_")
            ? QString("邀请加入群聊“%1”，我是 %2（QQ:%3），进群后一起沟通。")
                .arg(m_localGroupNames.value(m_privateChatTarget, "群聊"), m_currentUserName, m_currentUserId)
            : QString("你好，我是 %1（QQ:%2），方便的话加个好友继续聊。")
                .arg(m_currentUserName, m_currentUserId);
    } else if (text == "/qq" || text == "QQ") {
        text = QString("我的 QQ 号：%1，昵称：%2").arg(m_currentUserId, m_currentUserName);
    } else if (text == "/summary" || text == "摘要") {
        if (m_privateChatTarget.startsWith("local_group_")) {
            text = QString("当前群聊：%1（群号:%2）· 成员%3人 · 我的QQ:%4")
                .arg(m_localGroupNames.value(m_privateChatTarget, "群聊"),
                     m_privateChatTarget.mid(QString("local_group_").size()),
                     QString::number(m_localGroupMembers.value(m_privateChatTarget).size()),
                     m_currentUserId);
        } else if (!m_privateChatTarget.isEmpty()) {
            text = QString("当前私聊：%1 · QQ:%2 · %3 · 我的QQ:%4")
                .arg(contactDisplayName(m_privateChatTarget), m_privateChatTarget, isContactOnline(m_privateChatTarget) ? "在线" : "离线", m_currentUserId);
        } else {
            text = QString("公共聊天室 · 我的QQ:%1 · 好友%2人 · 群聊%3个 · 在线成员%4人")
                .arg(m_currentUserId).arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(m_knownUsers.size());
        }
    } else if (text == "/file" || text == "文件") {
        text = QString("我准备发送文件，请注意查收。我的QQ:%1，当前会话:%2")
            .arg(m_currentUserId, m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget));
    } else if (text == "/image" || text == "图片") {
        text = QString("我准备发送图片，发送后可查看预览卡片。我的QQ:%1")
            .arg(m_currentUserId);
    } else if (text == "/video" || text == "视频") {
        text = QString("我准备发送视频，视频会以文件卡片形式发送，请注意查收。我的QQ:%1")
            .arg(m_currentUserId);
    }

    QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QString groupName = m_localGroupNames.value(m_privateChatTarget, "群聊");
        QString line = QString("[%1] <%2> %3").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), m_currentUserName, text);
        saveHistory(m_privateChatTarget, line);

        QStandardItem* item = createChatMessageItem(line,
                                                    m_currentUserId,
                                                    m_currentUserName,
                                                    true,
                                                    QColor(20, 92, 160),
                                                    QColor(218, 241, 255));
        m_chatModel->appendRow(item);
        ui->messageEdit->clear();
        ui->chatHintLabel->setText(QString("本地群会话工作区 · %1 · 已发送 %2 字%3").arg(groupName).arg(text.size()).arg(originalText == text ? QString() : " · 快捷指令已展开"));
        ui->statusbar->showMessage(QString("已发送到 %1 · %2 字").arg(groupName).arg(text.size()), 1800);
        ui->chatListView->scrollToBottom();
        return;
    }

    if (m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup()) {
        ui->messageEdit->setFocus();
        ui->chatHintLabel->setText("发送已暂停 · 当前账号已不在公共群工作区，等待重新邀请");
        ui->statusbar->showMessage("当前账号已不在公共群，暂不能发送公共群消息", 3000);
        refreshComposerState();
        return;
    }

    if (!m_client || !m_client->isConnected()) {
        ui->messageEdit->setFocus();
        ui->chatHintLabel->setText(QString("发送已暂停 · %1 当前不可达，消息仍保留在输入框").arg(targetName));
        ui->statusbar->showMessage(QString("已断开连接，暂不能发送到 %1").arg(targetName), 3000);
        refreshComposerState();
        return;
    }

    bool ok = false;
    bool sentEncrypted = false;
    QString encryptedRejectReason;
    if (!m_privateChatTarget.isEmpty()) {
        if (m_client->hasE2ESession(m_privateChatTarget) && !m_client->e2eSessionNeedsRotation(m_privateChatTarget)) {
            ok = m_client->sendEncryptedPrivateMessage(m_privateChatTarget, text, &encryptedRejectReason);
            sentEncrypted = ok;
            if (!ok && encryptedRejectReason == QLatin1String("rotation-required")) {
                ui->chatHintLabel->setText(QString("发送已暂停 · %1 的端到端会话需要先完成轮换").arg(targetName));
                ui->statusbar->showMessage("端到端加密会话需要轮换，消息已保留在输入框", 3600);
                appendSystemMessage(QString("%1 的端到端加密会话需要轮换，未发送明文").arg(targetName));
                return;
            }
        } else {
            ok = m_client->sendPrivateMessage(m_privateChatTarget, text);
        }
    } else {
        ok = m_client->sendMessage(text);
    }

    if (ok) {
        QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
        QString line = QString("[%1] <%2> %3%4").arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                                                     m_currentUserName,
                                                     sentEncrypted ? QStringLiteral("[端到端加密] ") : QString(),
                                                     text);
        const QJsonObject e2eStatus = sentEncrypted ? m_client->e2eSessionStatus(peerId) : QJsonObject();
        saveHistory(peerId,
                    line,
                    sentEncrypted ? QStringLiteral("encrypted") : QStringLiteral("plaintext"),
                    e2eStatus.value("keyId").toString(),
                    e2eStatus.value("keyFingerprintSha256").toString());

        QStandardItem* item = createChatMessageItem(line,
                                                    m_currentUserId,
                                                    m_currentUserName,
                                                    true,
                                                    QColor(20, 92, 160),
                                                    QColor(218, 241, 255));
        m_chatModel->appendRow(item);
        int rowCount = m_chatModel->rowCount();
        if (rowCount > MAX_HISTORY_LINES) {
            m_chatModel->removeRows(0, rowCount - MAX_HISTORY_LINES);
        }
        ui->chatListView->scrollToBottom();
        ui->chatHintLabel->setText(QString("发送完成 · %1 · %2 字 · %3%4%5")
            .arg(targetName)
            .arg(text.size())
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"),
                 originalText == text ? QString() : " · 快捷指令已展开",
                 sentEncrypted ? QStringLiteral(" · 端到端加密") : QString()));
        ui->statusbar->showMessage(QString("已发送到 %1 · %2 字%3").arg(targetName).arg(text.size()).arg(sentEncrypted ? QStringLiteral(" · 端到端加密") : QString()), 1800);

        ui->messageEdit->clear();
    } else {
        ui->chatHintLabel->setText(QString("发送未完成 · %1 · 消息已保留在输入框").arg(targetName));
        ui->statusbar->showMessage(QString("发送失败，请检查连接 · %1").arg(targetName), 3000);
        appendSystemMessage(QString("发送失败，消息未送达 %1").arg(targetName));
    }
}

void MainWindow::onSendFile() {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    if (!ensureTransferTargetReady(QStringLiteral("文件"), targetName, isLocalGroup)) {
        return;
    }

    const TransferSelectionPlan selectionPlan = m_transferManager.fileSelectionPlan();
    SelectedTransferFile selectedFile;
    if (!selectTransferFileContext(selectionPlan, &selectedFile)) {
        return;
    }

    const TransferSendUiState preparingState =
        m_transferManager.preparingSendState(selectionPlan.preparingKind,
                                             selectedFile.info.fileName(),
                                             selectedFile.fileSize,
                                             targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        appendLocalGroupFileTransferCompletion(selectionPlan,
                                               selectedFile.info,
                                               selectedFile.fileSize,
                                               targetName,
                                               completedAt);
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(selectedFile.filePath,
                                       m_privateChatTarget,
                                       targetName,
                                       "文件",
                                       false,
                                       &transferSummary,
                                       &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    handleRemoteTransferResult(ok,
                               transferCanceled,
                               selectedFile.filePath,
                               selectedFile.info,
                               selectedFile.fileSize,
                               targetName,
                               selectionPlan.preparingKind,
                               false,
                               false,
                               transferSummary);
}

void MainWindow::onSendImage() {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    if (!ensureTransferTargetReady(QStringLiteral("图片/视频"), targetName, isLocalGroup)) {
        return;
    }

    const TransferSelectionPlan selectionPlan = m_transferManager.mediaSelectionPlan();
    SelectedTransferFile selectedFile;
    if (!selectTransferFileContext(selectionPlan, &selectedFile)) {
        return;
    }

    const TransferMediaSelection mediaSelection = m_transferManager.mediaSelection(selectedFile.info);
    const bool isVideo = mediaSelection.isVideo;
    const QString mediaType = mediaSelection.mediaType;
    const TransferSendUiState preparingState =
        m_transferManager.preparingSendState(mediaType,
                                             selectedFile.info.fileName(),
                                             selectedFile.fileSize,
                                             targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        appendLocalGroupMediaTransferCompletion(selectedFile.filePath,
                                                selectedFile.info,
                                                selectedFile.fileSize,
                                                mediaType,
                                                isVideo,
                                                targetName,
                                                completedAt);
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(selectedFile.filePath,
                                       m_privateChatTarget,
                                       targetName,
                                       mediaType,
                                       !isVideo,
                                       &transferSummary,
                                       &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    handleRemoteTransferResult(ok,
                               transferCanceled,
                               selectedFile.filePath,
                               selectedFile.info,
                               selectedFile.fileSize,
                               targetName,
                               mediaType,
                               true,
                               isVideo,
                               transferSummary);
}

void MainWindow::onNewMessage(const Message& msg) {
    QString displayName = msg.senderName;
    if (msg.type == MessageType::System) {
        appendSystemMessage(msg.content);
        return;
    }

    QString timeStr = msg.timestamp.toString("hh:mm:ss");
    QString line;

    if (msg.type == MessageType::Image) {
        line = QString("[%1] <%2> [图片] %3").arg(timeStr, displayName, msg.fileName);
    } else if (msg.type == MessageType::File) {
        line = QString("[%1] <%2> %3").arg(timeStr, displayName, msg.content);
    } else if (msg.isPrivate()) {
        line = QString("[%1] <%2> [私聊] %3").arg(timeStr, displayName, msg.content);
    } else {
        line = QString("[%1] <%2> %3").arg(timeStr, displayName, msg.content);
    }

    if (msg.isPrivate()) {
        QString peerId = msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId;
        if (!m_privateChatTarget.isEmpty() && peerId != m_privateChatTarget) {
            const QString encryptionState = msg.e2eEnvelope.isValid()
                ? (msg.content == QStringLiteral("加密消息无法解密")
                    ? QStringLiteral("decrypt-failed")
                    : QStringLiteral("encrypted"))
                : QStringLiteral("plaintext");
            const QJsonObject e2eStatus = msg.e2eEnvelope.isValid() && m_client
                ? m_client->e2eSessionStatus(peerId)
                : QJsonObject();
            const bool envelopeMatchesLocalSession = e2eStatus.value("keyId").toString() == msg.e2eEnvelope.keyId;
            saveHistory(peerId,
                        line,
                        encryptionState,
                        msg.e2eEnvelope.keyId,
                        envelopeMatchesLocalSession ? e2eStatus.value("keyFingerprintSha256").toString() : QString());
            ++m_unreadCount;
            updateUnreadState();
            ui->statusbar->showMessage(QString("新的私聊消息 · %1：%2").arg(displayName, msg.content.left(24)), 5000);
            if (m_trayIcon->isVisible()) {
                m_trayIcon->showMessage("新的私聊消息", QString("%1: %2").arg(displayName, msg.content), QSystemTrayIcon::Information, 3000);
            }
            return;
        }
    }

    if (msg.senderId == m_currentUserId) {
        return;
    }

    if (msg.isPrivate()) {
        QStandardItem* item = createChatMessageItem(line,
                                                    msg.senderId,
                                                    displayName,
                                                    false,
                                                    Qt::darkMagenta,
                                                    QColor(252, 240, 255));
        m_chatModel->appendRow(item);
    } else if (msg.senderName == m_currentUserName) {
        QStandardItem* item = createChatMessageItem(line,
                                                    msg.senderId,
                                                    displayName,
                                                    true,
                                                    QColor(20, 92, 160),
                                                    QColor(218, 241, 255));
        m_chatModel->appendRow(item);
    } else {
        QStandardItem* item = createChatMessageItem(line,
                                                    msg.senderId,
                                                    displayName,
                                                    false,
                                                    QColor(38, 50, 56),
                                                    QColor(246, 250, 253));
        m_chatModel->appendRow(item);
    }
    const QString historyPeerId = msg.isPrivate() ? (msg.senderId == m_currentUserId ? msg.receiverId : msg.senderId) : "group";
    const QString encryptionState = msg.e2eEnvelope.isValid()
        ? (msg.content == QStringLiteral("加密消息无法解密")
            ? QStringLiteral("decrypt-failed")
            : QStringLiteral("encrypted"))
        : QStringLiteral("plaintext");
    const QJsonObject e2eStatus = msg.e2eEnvelope.isValid() && m_client
        ? m_client->e2eSessionStatus(historyPeerId)
        : QJsonObject();
    const bool envelopeMatchesLocalSession = e2eStatus.value("keyId").toString() == msg.e2eEnvelope.keyId;
    saveHistory(historyPeerId,
                line,
                encryptionState,
                msg.e2eEnvelope.keyId,
                envelopeMatchesLocalSession ? e2eStatus.value("keyFingerprintSha256").toString() : QString());

    if (msg.type == MessageType::Image || msg.type == MessageType::File) {
        handleReceivedTransferMessage(msg, displayName);
    }

    int rowCount = m_chatModel->rowCount();
    if (rowCount > MAX_HISTORY_LINES) {
        m_chatModel->removeRows(0, rowCount - MAX_HISTORY_LINES);
    }

    if (!isActiveWindow()) {
        ++m_unreadCount;
        updateUnreadState();
        if (m_trayIcon->isVisible()) {
            QString preview = msg.type == MessageType::File ? msg.content : msg.content.left(60);
            if (msg.type == MessageType::Image) preview = "[图片] " + msg.fileName;
            m_trayIcon->showMessage("QtNetworkChat", QString("%1: %2").arg(displayName, preview), QSystemTrayIcon::Information, 3000);
        }
    }

    ui->chatListView->scrollToBottom();
}

void MainWindow::onUserJoined(const QString& userId, const QString& userName) {
    Q_UNUSED(userId)
    appendSystemMessage(userName + " 加入了聊天室");
}

void MainWindow::onUserLeft(const QString& userId, const QString& userName) {
    Q_UNUSED(userId)
    appendSystemMessage(userName + " 离开了聊天室");
}

void MainWindow::onUserListUpdated(const QVector<ChatUser>& users) {
    m_knownUsers.clear();
    for (const ChatUser& user : users) {
        m_knownUsers[user.id] = user;
    }
    cacheKnownUserAvatars();
    refreshFriendList();
    if (!m_privateChatTarget.isEmpty()) {
        ui->chatHintLabel->setText(QString("私聊会话工作区 · QQ %1 · %2 · 可从菜单返回公共会话")
            .arg(m_privateChatTarget, isContactOnline(m_privateChatTarget) ? "在线" : "离线"));
    }
    ui->statusbar->showMessage(QString("在线: %1 人 | 好友: %2 人 | 当前账号: %3")
        .arg(users.size())
        .arg(m_friendIds.size())
        .arg(m_currentUserId));
    refreshGroupMemberPanel();
}

void MainWindow::onServerGroupSnapshotReceived(const QJsonArray& groups) {
    const bool hadServerGroupSnapshot = m_hasServerGroupSnapshot;
    const bool wasInPublicGroup = m_wasInPublicServerGroup;
    m_hasServerGroupSnapshot = true;

    m_serverGroupNames.clear();
    m_serverGroupAnnouncements.clear();
    m_serverGroupOwners.clear();
    m_serverGroupMembers.clear();
    m_serverGroupMemberNames.clear();
    m_serverGroupMemberRoles.clear();
    m_serverGroupAuditEvents.clear();
    m_removedServerGroups.clear();

    for (const QJsonValue& value : groups) {
        const QJsonObject groupObj = value.toObject();
        const QString groupId = groupObj["groupId"].toString();
        if (groupId.isEmpty()) continue;

        m_serverGroupNames[groupId] = groupObj["groupName"].toString(groupId);
        m_serverGroupAnnouncements[groupId] = groupObj["announcement"].toString();
        m_serverGroupOwners[groupId] = groupObj["ownerId"].toString();

        QStringList memberIds;
        const QJsonArray members = groupObj["members"].toArray();
        for (const QJsonValue& memberValue : members) {
            const QJsonObject memberObj = memberValue.toObject();
            const QString memberId = memberObj["userId"].toString();
            if (memberId.isEmpty() || memberIds.contains(memberId)) continue;

            memberIds << memberId;
            m_serverGroupMemberNames[groupId + "|" + memberId] = memberObj["userName"].toString(memberId);
            m_serverGroupMemberRoles[groupId + "|" + memberId] = memberObj["role"].toString("member");
        }
        m_serverGroupMembers[groupId] = memberIds;
        m_serverGroupAuditEvents[groupId] = groupObj["auditEvents"].toArray();
    }

    const QJsonArray removedGroups = m_client ? m_client->removedServerGroups() : QJsonArray();
    for (const QJsonValue& value : removedGroups) {
        const QJsonObject groupObj = value.toObject();
        const QString groupId = groupObj["groupId"].toString();
        if (groupId.isEmpty()) continue;

        m_removedServerGroups[groupId] = groupObj;
        m_serverGroupNames[groupId] = groupObj["groupName"].toString(groupId);
        m_serverGroupAnnouncements[groupId] = groupObj["announcement"].toString();
        m_serverGroupOwners[groupId] = groupObj["ownerId"].toString();
    }

    const bool isInPublicGroup = m_serverGroupMembers.value("public").contains(m_currentUserId);
    m_wasInPublicServerGroup = isInPublicGroup;

    if (m_privateChatTarget.isEmpty()) {
        if (isCurrentUserRemovedFromPublicGroup()) {
            const QJsonObject removedInfo = m_removedServerGroups.value("public");
            const QString removedBy = removedInfo["removedByName"].toString(removedInfo["removedBy"].toString());
            const QString removedAt = removedInfo["removedAt"].toString();
            const QString removedDetail = removedBy.isEmpty()
                ? QStringLiteral("可查看本机历史，等待群主或管理员重新邀请")
                : QStringLiteral("由 %1 移出%2 · 可查看本机历史，等待重新邀请")
                    .arg(removedBy, removedAt.isEmpty() ? QString() : QStringLiteral("于 %1").arg(removedAt));
            ui->chatTitleLabel->setText("公共聊天室");
            ui->chatHintLabel->setText(QString("公共会话工作区受限 · 当前账号 %1 已不在公共群 · %2").arg(m_currentUserId, removedDetail));
            ui->announcementTitleLabel->setText("群公告");
            ui->announcementBodyLabel->setText(QString("当前账号已不在公共群。%1；重新邀请后会自动恢复群公告和成员列表。").arg(removedDetail));
            if (!hadServerGroupSnapshot || wasInPublicGroup) {
                appendSystemMessage(QString("你已不在公共群，暂不能发送公共群消息、文件或图片；%1。").arg(removedDetail));
            }
            ui->statusbar->showMessage("当前账号已不在公共群，等待重新邀请", 3200);
        } else {
            const QString publicAnnouncement = m_serverGroupAnnouncements.value("public");
            const QString publicName = m_serverGroupNames.value("public", "公共聊天室");
            if (!publicName.isEmpty()) {
                ui->chatTitleLabel->setText(publicName);
            }
            ui->announcementTitleLabel->setText(canCurrentUserManageServerGroup("public")
                ? "群公告 <a href=\"edit\">编辑</a>"
                : "群公告");
            if (!publicAnnouncement.isEmpty()) {
                ui->announcementBodyLabel->setText(publicAnnouncement);
            }
            if (hadServerGroupSnapshot && !wasInPublicGroup) {
                appendSystemMessage("你已重新加入公共群，群公告、成员列表和发送入口已恢复。");
            }
            ui->statusbar->showMessage(QString("已同步服务端群组 · %1 个").arg(groups.size()), 1800);
        }
        refreshGroupMemberPanel();
        refreshComposerState();
    }
}

void MainWindow::onE2ESessionStateChanged(const QString& peerId, const QJsonObject& status) {
    if (peerId != m_privateChatTarget) {
        return;
    }
    const QString hint = ChatSessionManager::e2eSessionHint(contactDisplayName(peerId), status.value("state").toString());
    if (!hint.isEmpty()) {
        ui->chatHintLabel->setText(hint);
    }
}

void MainWindow::onE2EIdentityStateChanged(const QString& peerId, const QJsonObject& status) {
    const QString trustState = status.value("trustState").toString();
    const QString fingerprint = status.value("publicKeyFingerprintSha256").toString().left(16);
    if (trustState == QLatin1String("mismatch")) {
        appendSystemMessage(QString("%1 的端到端加密身份指纹发生变化 · 指纹:%2")
            .arg(contactDisplayName(peerId), fingerprint));
        ui->statusbar->showMessage("端到端加密身份指纹变化，请核对", 4200);
    } else if (trustState == QLatin1String("trusted")) {
        appendSystemMessage(QString("已信任 %1 的端到端加密身份 · 指纹:%2")
            .arg(contactDisplayName(peerId), fingerprint));
    } else if (peerId == m_privateChatTarget) {
        ui->chatHintLabel->setText(ChatSessionManager::e2eIdentityPendingHint(contactDisplayName(peerId), fingerprint));
    }
}

void MainWindow::onE2ESessionRotationRequested(const QString& peerId, const QJsonObject& agreement) {
    const QString keyId = agreement.value("keyId").toString();
    const QString fingerprint = agreement.value("publicKeyFingerprintSha256").toString().left(16);
    appendSystemMessage(QString("%1 请求轮换端到端加密会话 · keyId:%2 · 指纹:%3")
        .arg(contactDisplayName(peerId), keyId, fingerprint));
    if (peerId == m_privateChatTarget) {
        ui->chatHintLabel->setText(ChatSessionManager::e2eRotationRequestHint(contactDisplayName(peerId)));
    }
}

void MainWindow::onE2ESessionRotationResponded(const QString& peerId, const QJsonObject& agreement, bool accepted, const QString& reason) {
    const QString keyId = agreement.value("keyId").toString();
    appendSystemMessage(QString("%1 %2端到端加密轮换 · keyId:%3%4")
        .arg(contactDisplayName(peerId),
             accepted ? QStringLiteral("已接受") : QStringLiteral("已拒绝"),
             keyId,
             reason.trimmed().isEmpty() ? QString() : QStringLiteral(" · %1").arg(reason)));
    if (peerId == m_privateChatTarget) {
        ui->chatHintLabel->setText(ChatSessionManager::e2eRotationResponseHint(contactDisplayName(peerId), accepted));
    }
}

void MainWindow::onPrivateChat(const QModelIndex& index) {
    if (!index.isValid()) return;
    QString targetId = index.data(Qt::UserRole + 1).toString();
    if (targetId.isEmpty()) return;
    openUserTargetById(targetId);
}

void MainWindow::onClientDisconnected() {
    appendSystemMessage("已断开服务器连接");
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const ConnectionUiState state = ChatSessionManager::disconnectedState(targetName, isLocalGroup);
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, 3500);
    refreshComposerState();
}

void MainWindow::onClientError(const QString& error) {
    appendSystemMessage("连接错误: " + error);
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const ConnectionUiState state = ChatSessionManager::errorState(targetName, isLocalGroup, error);
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, 3500);
    refreshComposerState();
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        show();
        raise();
        activateWindow();
        clearUnreadState();
    }
}

void MainWindow::onCopyAccount() {
    QApplication::clipboard()->setText(m_currentUserId);
    ui->statusbar->showMessage(QString("QQ 号已复制: %1").arg(m_currentUserId), 2600);
}

void MainWindow::onLogout() {
    if (!confirmAction(QStringLiteral("退出登录"),
                       QStringLiteral("确定退出当前账号并返回登录界面吗？"),
                       QStringLiteral("已取消退出登录"))) {
        return;
    }
    m_isQuitting = true;
    if (m_client) {
        m_client->disconnectFromServer();
    }
    emit logoutRequested();
}

void MainWindow::onClearHistory() {
    const QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
    const QString historyPath = m_historyService.legacyFilePath(peerId);
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? "公共聊天室"
        : (m_privateChatTarget.startsWith("local_group_")
            ? m_localGroupNames.value(m_privateChatTarget, "群聊")
            : contactDisplayName(m_privateChatTarget));

    if (m_chatModel->rowCount() == 0 && !QFile::exists(historyPath) && !m_historyService.hasRecords(peerId)) {
        ui->statusbar->showMessage(QString("%1 暂无可清空的聊天记录").arg(sessionName), 1800);
        return;
    }

    if (!confirmDestructiveAction(QStringLiteral("清空聊天记录"),
                                  QStringLiteral("确定清空“%1”的本地聊天记录吗？此操作不会删除对方设备上的记录。").arg(sessionName),
                                  QStringLiteral("清空记录"),
                                  QStringLiteral("保留记录"),
                                  this)) {
        ui->statusbar->showMessage(QStringLiteral("已取消清空聊天记录"), 1600);
        return;
    }

    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    if (QFile::exists(historyPath)) {
        QFile::remove(historyPath);
    }
    m_historyService.clear(peerId);
    appendSystemMessage(QString("%1 的聊天记录已清空").arg(sessionName));
    ui->statusbar->showMessage(QString("已清空 %1 的本地聊天记录").arg(sessionName), 2200);
}

void MainWindow::onFilterHistoryByDate() {
    const QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? "公共聊天室"
        : (m_privateChatTarget.startsWith("local_group_")
            ? m_localGroupNames.value(m_privateChatTarget, "群聊")
            : contactDisplayName(m_privateChatTarget));

    QDialog dialog(this);
    dialog.setWindowTitle("按日期查记录");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* label = new QLabel(QString("选择要查看的日期：%1").arg(sessionName), &dialog);
    QDateEdit* dateEdit = new QDateEdit(QDate::currentDate(), &dialog);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    dateEdit->setMaximumDate(QDate::currentDate());
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    dialog.setStyleSheet(productDialogStyleSheet());
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("查看"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("managerPrimaryBtn"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("managerSecondaryBtn"));
    layout->addWidget(label);
    layout->addWidget(dateEdit);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        ui->statusbar->showMessage("已取消按日期查记录", 1600);
        return;
    }

    const QDate selectedDate = dateEdit->date();
    const QStringList rows = m_historyService.rowsForDate(peerId, selectedDate);
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});

    if (rows.isEmpty()) {
        appendSystemMessage(QString("%1 在 %2 没有可显示的聊天记录").arg(sessionName, selectedDate.toString("yyyy-MM-dd")));
        ui->statusbar->showMessage(QString("%1 无当天记录").arg(selectedDate.toString("yyyy-MM-dd")), 2200);
        return;
    }

    for (const QString& row : rows) {
        QStandardItem* item = new QStandardItem(row);
        item->setEditable(false);
        item->setBackground(QColor(250, 252, 254));
        item->setForeground(Qt::gray);
        m_chatModel->appendRow(item);
    }
    ui->chatHintLabel->setText(QString("历史记录工作区 · %1 · %2 · 已筛选 %3 条记录")
        .arg(sessionName, selectedDate.toString("yyyy-MM-dd"), QString::number(rows.size())));
    ui->statusbar->showMessage(QString("已筛选 %1 条聊天记录").arg(rows.size()), 2400);
    ui->chatListView->scrollToBottom();
}

void MainWindow::onExportHistory() {
    const QString peerId = m_privateChatTarget.isEmpty() ? "group" : m_privateChatTarget;
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? "公共聊天室"
        : (m_privateChatTarget.startsWith("local_group_")
            ? m_localGroupNames.value(m_privateChatTarget, "群聊")
            : contactDisplayName(m_privateChatTarget));
    const QStringList rows = m_historyService.rowsForExport(peerId);
    if (rows.isEmpty()) {
        ui->statusbar->showMessage(QString("%1 暂无可导出的聊天记录").arg(sessionName), 2200);
        return;
    }

    const QDateTime exportedAt = QDateTime::currentDateTime();
    const HistoryExportSelectionPlan exportPlan =
        m_historyService.exportSelectionPlan(sessionName, exportedAt);
    const QString savePath = selectSaveFilePath(exportPlan.dialogTitle,
                                                exportPlan.defaultPath,
                                                exportPlan.filters,
                                                this);
    if (savePath.isEmpty()) {
        ui->statusbar->showMessage(exportPlan.canceledStatusMessage, exportPlan.canceledStatusTimeoutMs);
        return;
    }

    const HistoryExportWriteResult exportResult =
        m_historyService.writeExportFile(savePath,
                                         sessionName,
                                         m_currentUserId,
                                         m_currentUserName,
                                         rows,
                                         exportedAt);
    if (!exportResult.written) {
        showWarningDialog(exportResult.failureTitle, exportResult.failureMessage, this);
        ui->statusbar->showMessage(exportResult.failureStatusMessage, exportResult.failureStatusTimeoutMs);
        return;
    }

    ui->statusbar->showMessage(exportResult.successStatusMessage, exportResult.successStatusTimeoutMs);
    appendSystemMessage(exportResult.systemMessage);
}

void MainWindow::onAddFriend() {
    bool ok = false;
    const QString account = promptTextValue(QStringLiteral("发起好友申请"),
                                            QStringLiteral("请输入对方 QQ 账号:"),
                                            QString(),
                                            &ok,
                                            this);
    if (!ok) return;
    searchAndAddAccount(account, this);
}

void MainWindow::searchAndAddAccount(const QString& account, QWidget* warningParent) {
    Q_UNUSED(warningParent)
    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty()) {
        ui->statusbar->showMessage("请输入 QQ 号后再搜索", 1800);
        return;
    }
    if (normalizedAccount == m_currentUserId) {
        ui->statusbar->showMessage("不能添加自己为好友", 2500);
        return;
    }
    if (m_friendIds.contains(normalizedAccount)) {
        ui->statusbar->showMessage("该账号已经是你的好友: " + normalizedAccount, 2500);
        return;
    }
    if (!m_client->searchFriendByAccount(normalizedAccount)) {
        ui->statusbar->showMessage("当前未连接，无法搜索账号", 2500);
    } else {
        ui->statusbar->showMessage("正在搜索 QQ / 昵称: " + normalizedAccount, 2500);
    }
}

void MainWindow::onShowGlobalSearch(const QString& initialFilter) {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("globalSearchDialog"),
        QStringLiteral("综合搜索"),
        QSize(940, 740),
        QStringLiteral("searchDialogTitle"),
        QStringLiteral("综合搜索"),
        QStringLiteral("searchDialogSubTitle"),
        QStringLiteral("联系人、群聊、申请入口和结果复制都在这里统一处理。"),
        QStringLiteral("globalSearchInput"),
        QStringLiteral("输入 QQ 号 / 昵称搜索"),
        QStringLiteral("输入 QQ 号、昵称或群名；回车可搜索或打开匹配结果"),
        QStringLiteral("globalResultList"),
        false,
        QStringLiteral("globalActionHint"),
        QStringLiteral("从这里继续聊天、建群、申请好友和复制摘要。"),
        QStringLiteral("globalStatsLabel"),
        QStringLiteral("globalPreviewLabel"),
        QStringLiteral("选择结果后可直接打开会话，或整理名片、邀请卡和媒体计划。"),
        QStringLiteral("searchHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(18, 14, 18, 8);
    shell.headerLayout->setSpacing(10);
    shell.bodyLayout->setContentsMargins(18, 14, 18, 18);
    shell.bodyLayout->setSpacing(12);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* resultList = shell.listWidget;
    QLabel* actionHint = shell.hintLabel;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;

    QPushButton* searchBtn = createWorkspaceButton(shell.headerFrame,
                                                   &dialog,
                                                   QStringLiteral("搜索"),
                                                   QStringLiteral("globalSearchPrimaryBtn"),
                                                   QStringLiteral("按当前关键词刷新综合搜索结果"),
                                                   QStyle::SP_FileDialogContentsView);
    QPushButton* clearBtn = createWorkspaceButton(shell.headerFrame,
                                                  &dialog,
                                                  QStringLiteral("清空"),
                                                  QStringLiteral("globalSearchGhostBtn"),
                                                  QStringLiteral("清空搜索关键词并恢复全部结果"),
                                                  QStyle::SP_DialogResetButton);
    QPushButton* quickAddBtn = createWorkspaceButton(shell.headerFrame,
                                                     &dialog,
                                                     QStringLiteral("好友申请"),
                                                     QStringLiteral("globalSearchGhostBtn"),
                                                     QStringLiteral("打开好友申请工作区，按 QQ 号搜索并申请"),
                                                     QStyle::SP_FileDialogNewFolder);
    QPushButton* friendManagerBtn = createWorkspaceButton(shell.headerFrame,
                                                          &dialog,
                                                          QStringLiteral("好友管理"),
                                                          QStringLiteral("globalSearchGhostBtn"),
                                                          QStringLiteral("打开好友管理器，查看、搜索和整理好友"),
                                                          QStyle::SP_FileDialogDetailedView);
    shell.searchRowLayout->addWidget(searchBtn);
    shell.searchRowLayout->addWidget(clearBtn);
    shell.searchRowLayout->addWidget(quickAddBtn);
    shell.searchRowLayout->addWidget(friendManagerBtn);

    QHBoxLayout* tabLayout = new QHBoxLayout;
    const QStringList tabs = {"全部", "用户", "群聊", "小程序", "机器人"};
    for (const QString& tab : tabs) {
        QLabel* label = new QLabel(tab, shell.headerFrame);
        label->setObjectName(tab == "全部" ? "activeSearchTab" : "searchTab");
        label->setAlignment(Qt::AlignCenter);
        tabLayout->addWidget(label);
    }
    tabLayout->addStretch();
    shell.headerLayout->addLayout(tabLayout);

    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame,
                                                 &dialog,
                                                 QStringLiteral("打开/申请"),
                                                 QStringLiteral("globalSearchPrimaryBtn"),
                                                 QStringLiteral("打开当前结果；陌生用户会尝试发起好友申请"),
                                                 QStyle::SP_ArrowForward);
    QPushButton* createGroupBtn = createWorkspaceButton(shell.bodyFrame,
                                                        &dialog,
                                                        QStringLiteral("用搜索创建群"),
                                                        QStringLiteral("globalSearchGhostBtn"),
                                                        QStringLiteral("使用当前搜索关键词创建一个本地群聊"),
                                                        QStyle::SP_FileDialogNewFolder);
    QPushButton* inviteVisibleBtn = createWorkspaceButton(shell.bodyFrame,
                                                          &dialog,
                                                          QStringLiteral("可见用户建群"),
                                                          QStringLiteral("globalSearchGhostBtn"),
                                                          QStringLiteral("用当前可见用户创建群聊，并邀请可申请用户"),
                                                          QStyle::SP_DialogOpenButton);
    QPushButton* addVisibleBtn = createWorkspaceButton(shell.bodyFrame,
                                                       &dialog,
                                                       QStringLiteral("申请可见用户"),
                                                       QStringLiteral("globalSearchGhostBtn"),
                                                       QStringLiteral("向当前列表里可申请的在线用户批量发起好友申请"),
                                                       QStyle::SP_CommandLink);
    QPushButton* copyBtn = createWorkspaceButton(shell.bodyFrame,
                                                 &dialog,
                                                 QStringLiteral("复制QQ"),
                                                 QStringLiteral("globalSearchGhostBtn"),
                                                 QStringLiteral("复制当前选中结果的 QQ 号或群号"),
                                                 QStyle::SP_DialogSaveButton);
    QPushButton* copyListBtn = createWorkspaceButton(shell.bodyFrame,
                                                     &dialog,
                                                     QStringLiteral("复制结果列表"),
                                                     QStringLiteral("globalSearchGhostBtn"),
                                                     QStringLiteral("复制当前可见搜索结果列表"),
                                                     QStyle::SP_FileDialogListView);
    QPushButton* copyAddTextBtn = createWorkspaceButton(shell.bodyFrame,
                                                        &dialog,
                                                        QStringLiteral("复制申请话术"),
                                                        QStringLiteral("globalSearchGhostBtn"),
                                                        QStringLiteral("复制适合当前选中用户的好友申请话术"),
                                                        QStyle::SP_MessageBoxInformation);
    QPushButton* copyInviteCardBtn = createWorkspaceButton(shell.bodyFrame,
                                                           &dialog,
                                                           QStringLiteral("复制邀请卡"),
                                                           QStringLiteral("globalSearchGhostBtn"),
                                                           QStringLiteral("复制当前用户或群聊的邀请卡片"),
                                                           QStyle::SP_DirLinkIcon);
    QPushButton* copySearchCardBtn = createWorkspaceButton(shell.bodyFrame,
                                                           &dialog,
                                                           QStringLiteral("复制搜索卡片"),
                                                           QStringLiteral("globalSearchGhostBtn"),
                                                           QStringLiteral("复制当前搜索条件和结果摘要"),
                                                           QStyle::SP_FileDialogDetailedView);
    QPushButton* copySearchMediaPackBtn = createWorkspaceButton(shell.bodyFrame,
                                                                &dialog,
                                                                QStringLiteral("复制搜索媒体包"),
                                                                QStringLiteral("globalSearchGhostBtn"),
                                                                QStringLiteral("复制搜索场景下发送图片、视频或文件的准备摘要"),
                                                                QStyle::SP_FileIcon);
    QPushButton* copyBatchMediaPlanBtn = createWorkspaceButton(shell.bodyFrame,
                                                               &dialog,
                                                               QStringLiteral("复制批量媒体计划"),
                                                               QStringLiteral("globalSearchGhostBtn"),
                                                               QStringLiteral("复制当前搜索结果的批量媒体发送计划"),
                                                               QStyle::SP_DriveHDIcon);
    QPushButton* copyMediaGuideBtn = createWorkspaceButton(shell.bodyFrame,
                                                           &dialog,
                                                           QStringLiteral("复制上传指南"),
                                                           QStringLiteral("globalSearchGhostBtn"),
                                                           QStringLiteral("复制搜索后发送图片、视频和文件的简短指南"),
                                                           QStyle::SP_DialogHelpButton);
    QPushButton* copyOnlineBtn = createWorkspaceButton(shell.bodyFrame,
                                                       &dialog,
                                                       QStringLiteral("复制在线"),
                                                       QStringLiteral("globalSearchGhostBtn"),
                                                       QStringLiteral("复制当前可见结果中的在线用户"),
                                                       QStyle::SP_DialogYesButton);
    QPushButton* profileBtn = createWorkspaceButton(shell.bodyFrame,
                                                    &dialog,
                                                    QStringLiteral("复制名片"),
                                                    QStringLiteral("globalSearchGhostBtn"),
                                                    QStringLiteral("复制当前选中结果的资料名片"),
                                                    QStyle::SP_FileDialogInfoView);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("主操作"),
        QStringLiteral("先打开结果，再决定是否建群或批量发送申请。"),
        {openBtn, createGroupBtn, inviteVisibleBtn, addVisibleBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("复制与摘要"),
        QStringLiteral("把当前筛选结果、名片和邀请材料整理出去。"),
        {copyBtn, copyListBtn, copyAddTextBtn, copyInviteCardBtn, profileBtn, copyOnlineBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("媒体与批量计划"),
        QStringLiteral("给后续图片、视频、文件发送准备摘要、批量计划和操作指南。"),
        {copySearchCardBtn, copySearchMediaPackBtn, copyBatchMediaPlanBtn, copyMediaGuideBtn});

    auto globalSearchCopyInputs = [this, resultList]() {
        QList<GlobalSearchResultCopyInput> inputs;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                continue;
            }
            GlobalSearchResultCopyInput input;
            input.entryId = id;
            input.localGroup = id.startsWith(QStringLiteral("local_group_"));
            input.friendContact = m_friendIds.contains(id);
            input.online = isContactOnline(id);
            input.memberCount = input.localGroup ? m_localGroupMembers.value(id).size() : 0;
            input.displayName = input.localGroup ? m_localGroupNames.value(id, QStringLiteral("群聊")) : contactDisplayName(id);
            inputs << input;
        }
        return inputs;
    };
    auto currentGlobalSearchCopyInput = [this, resultList, searchEdit]() {
        GlobalSearchResultCopyInput input;
        QListWidgetItem* item = resultList->currentItem();
        const QString rawId = item ? item->data(Qt::UserRole).toString() : searchEdit->text().trimmed();
        input.entryId = rawId;
        input.localGroup = rawId.startsWith(QStringLiteral("local_group_"));
        input.friendContact = m_friendIds.contains(rawId);
        input.online = isContactOnline(rawId);
        input.memberCount = input.localGroup ? m_localGroupMembers.value(rawId).size() : 0;
        if (input.localGroup) {
            input.displayName = m_localGroupNames.value(rawId, QStringLiteral("群聊"));
        } else if (!rawId.startsWith(QStringLiteral("search_add:"))) {
            input.displayName = contactDisplayName(rawId);
        }
        return input;
    };

    auto updatePreview = [this, resultList, previewLabel, searchEdit]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            previewLabel->setText(firstEnabledListRow(resultList) >= 0
                                      ? QStringLiteral("选择结果后可直接打开会话，或整理名片、邀请卡和媒体计划。")
                                      : (searchEdit->text().trimmed().isEmpty()
                                             ? QStringLiteral("当前没有可见的搜索结果。可输入 QQ 号、昵称或群名开始搜索。")
                                             : QStringLiteral("当前筛选词“%1”没有匹配到可见结果。\n可继续搜索、建群或整理搜索摘要。")
                                                   .arg(searchEdit->text().trimmed())));
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.startsWith("search_add:")) {
            QString account = id.mid(QString("search_add:").size());
            previewLabel->setText(QString("准备搜索并申请 QQ:%1").arg(account));
        } else if (id.startsWith("local_group_")) {
            previewLabel->setText(QString("群聊 · %1 · 群号:%2 · 成员%3人").arg(m_localGroupNames.value(id, "群聊"), id.mid(QString("local_group_").size())).arg(m_localGroupMembers.value(id).size()));
        } else if (!id.isEmpty()) {
            QString relation = m_friendIds.contains(id) ? "好友" : (m_pendingOutgoingFriendRequests.contains(id) ? "申请中" : "可申请");
            previewLabel->setText(QString("联系人 · %1 · QQ:%2 · %3 · %4")
                .arg(contactDisplayName(id), id, isContactOnline(id) ? "在线" : "离线", relation));
        } else {
            previewLabel->setText("输入 QQ 号后可继续搜索，也可以把当前条件整理成搜索卡片。");
        }
    };

    auto fillResults = [this, resultList, actionHint, statsLabel, updatePreview](const QString& filter = QString()) {
        resultList->clear();
        int friendCount = 0;
        int userCount = 0;
        int pendingCount = 0;
        int groupCount = 0;
        for (const QString& id : m_friendIds) {
            QString name = m_friendNames.value(id, id);
            if (!filter.isEmpty()
                && !id.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("好友  QQ:%1\n%2 · %3").arg(id, name, isContactOnline(id) ? "在线" : "离线"));
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(0, 66));
            resultList->addItem(item);
            ++friendCount;
        }
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            const ChatUser& user = it.value();
            if (user.id == m_currentUserId || m_friendIds.contains(user.id)) continue;
            if (!filter.isEmpty()
                && !user.id.contains(filter, Qt::CaseInsensitive)
                && !user.name.contains(filter, Qt::CaseInsensitive)) continue;
            const bool isPending = m_pendingOutgoingFriendRequests.contains(user.id);
            QListWidgetItem* item = new QListWidgetItem(QString("用户  QQ:%1\n%2 · 在线 · %3").arg(user.id, user.name, isPending ? "申请中" : "双击发送申请"));
            item->setData(Qt::UserRole, user.id);
            item->setSizeHint(QSize(0, 66));
            if (isPending) {
                item->setForeground(QColor(170, 110, 20));
                ++pendingCount;
            }
            resultList->addItem(item);
            ++userCount;
        }
        for (const QString& groupId : m_localGroupIds) {
            QString groupName = m_localGroupNames.value(groupId, "群聊");
            if (!filter.isEmpty()
                && !groupId.contains(filter, Qt::CaseInsensitive)
                && !groupName.contains(filter, Qt::CaseInsensitive)) continue;
            QListWidgetItem* item = new QListWidgetItem(QString("群聊  QQ:%1\n%2 · 本地群聊 · 双击进入").arg(groupId.mid(QString("local_group_").size()), groupName));
            item->setData(Qt::UserRole, groupId);
            item->setSizeHint(QSize(0, 66));
            resultList->addItem(item);
            ++groupCount;
        }
        if (!filter.isEmpty()) {
            QListWidgetItem* searchItem = new QListWidgetItem(QString("搜索并申请 QQ：%1\n双击或点击搜索可从服务器查找并发起好友申请").arg(filter));
            searchItem->setData(Qt::UserRole, "search_add:" + filter);
            searchItem->setForeground(QColor(92, 110, 128));
            searchItem->setSizeHint(QSize(0, 58));
            resultList->addItem(searchItem);
        }
        if (resultList->count() == 0) {
            addWorkspaceEmptyStateItem(
                resultList,
                QStringLiteral("没有匹配的搜索结果"),
                QStringLiteral("试试 QQ 号、昵称或群名，或直接从这里建群。"),
                filter.isEmpty()
                    ? QStringLiteral("当前没有可见的搜索结果。可输入 QQ 号、昵称或群名开始搜索。")
                    : QStringLiteral("当前筛选词“%1”没有匹配到可见结果。\n可继续搜索、建群或整理搜索摘要。").arg(filter));
        }
        int directResultCount = friendCount + userCount + groupCount;
        actionHint->setText(filter.isEmpty()
            ? QString("可继续聊天、进群、建群或复制摘要 · 共 %1 项").arg(directResultCount)
            : QString("匹配 %1 项 · 当前关键词 QQ:%2").arg(directResultCount).arg(filter));
        statsLabel->setText(QString("好友%1 · 用户%2 · 申请中%3 · 群聊%4").arg(friendCount).arg(userCount).arg(pendingCount).arg(groupCount));
        selectPreferredListRow(resultList, 0);
        updatePreview();
    };

    auto updateActionState = [=, this]() {
        const QString searchText = searchEdit->text().trimmed();
        QListWidgetItem* item = resultList->currentItem();
        const QString entryId = item ? item->data(Qt::UserRole).toString() : QString();
        const bool hasSelection = !entryId.isEmpty();
        const bool searchAddSelection = entryId.startsWith(QStringLiteral("search_add:"));
        const bool localGroupSelection = entryId.startsWith(QStringLiteral("local_group_"));
        const bool hasActionableResult = hasEnabledListRow(resultList);
        const QString targetId = searchAddSelection ? entryId.mid(QStringLiteral("search_add:").size()) : entryId;
        const QString targetName = targetId.isEmpty()
            ? QString()
            : (localGroupSelection ? m_localGroupNames.value(entryId, QStringLiteral("群聊")) : contactDisplayName(targetId));
        const QList<GlobalSearchResultCopyInput> visibleInputs = globalSearchCopyInputs();
        bool hasVisibleOnline = false;
        bool hasVisibleUserTarget = false;
        for (const GlobalSearchResultCopyInput& input : visibleInputs) {
            if (!input.localGroup) {
                hasVisibleUserTarget = true;
            }
            if (input.online) {
                hasVisibleOnline = true;
            }
        }

        searchBtn->setText(QStringLiteral("搜索服务器"));
        searchBtn->setEnabled(!searchText.isEmpty());
        searchBtn->setToolTip(searchText.isEmpty()
                                  ? QStringLiteral("先输入 QQ 号、昵称或群名再搜索")
                                  : QStringLiteral("按当前关键词刷新综合搜索结果"));
        clearBtn->setEnabled(!searchText.isEmpty());
        clearBtn->setToolTip(searchText.isEmpty()
                                 ? QStringLiteral("当前没有需要清空的搜索条件")
                                 : QStringLiteral("清空当前搜索关键词并恢复默认结果"));
        quickAddBtn->setText(QStringLiteral("好友申请工作区"));
        friendManagerBtn->setText(QStringLiteral("打开好友管理"));
        actionHint->setText(!hasActionableResult
                                ? (searchText.isEmpty()
                                       ? QStringLiteral("当前没有可见的搜索结果。可输入 QQ 号、昵称或群名开始搜索。")
                                       : QStringLiteral("当前筛选词“%1”没有匹配到可见结果，可继续搜索、建群或整理搜索摘要。").arg(searchText))
                                : (searchAddSelection
                                       ? QStringLiteral("当前是搜索建议项，可直接搜索并申请，或复制搜索摘要与邀请材料。")
                                       : (localGroupSelection
                                              ? QStringLiteral("当前结果是群聊，可直接进入、复制群资料或整理群媒体计划。")
                                              : QStringLiteral("当前结果可继续打开私聊、复制名片、发送申请或整理媒体准备。"))));

        if (!hasSelection) {
            openBtn->setText(QStringLiteral("打开当前结果"));
            openBtn->setEnabled(false);
            openBtn->setToolTip(QStringLiteral("请先选择一个搜索结果"));
            createGroupBtn->setEnabled(true);
            createGroupBtn->setText(QStringLiteral("用搜索创建群"));
            inviteVisibleBtn->setEnabled(hasVisibleUserTarget);
            addVisibleBtn->setEnabled(hasVisibleUserTarget);
            copyBtn->setEnabled(false);
            profileBtn->setEnabled(false);
            copyAddTextBtn->setEnabled(!searchText.isEmpty());
            copyInviteCardBtn->setEnabled(false);
            copyListBtn->setEnabled(!visibleInputs.isEmpty());
            copySearchCardBtn->setEnabled(!visibleInputs.isEmpty() || !searchText.isEmpty());
            copySearchMediaPackBtn->setEnabled(!searchText.isEmpty());
            copyBatchMediaPlanBtn->setEnabled(!visibleInputs.isEmpty());
            copyMediaGuideBtn->setEnabled(true);
            copyOnlineBtn->setEnabled(hasVisibleOnline);
            return;
        }

        if (searchAddSelection) {
            openBtn->setText(QStringLiteral("搜索并申请"));
            openBtn->setToolTip(QStringLiteral("搜索 QQ:%1 并发起好友申请").arg(targetId));
        } else if (localGroupSelection) {
            openBtn->setText(QStringLiteral("进入群聊"));
            openBtn->setToolTip(QStringLiteral("进入群聊 %1").arg(targetName));
        } else {
            openBtn->setText(QStringLiteral("打开私聊"));
            openBtn->setToolTip(QStringLiteral("打开与 %1 的私聊会话").arg(targetName));
        }
        openBtn->setEnabled(true);
        createGroupBtn->setEnabled(true);
        inviteVisibleBtn->setEnabled(hasVisibleUserTarget);
        addVisibleBtn->setEnabled(hasVisibleUserTarget);
        copyBtn->setEnabled(true);
        profileBtn->setEnabled(true);
        copyAddTextBtn->setEnabled(true);
        copyInviteCardBtn->setEnabled(true);
        copyListBtn->setEnabled(!visibleInputs.isEmpty());
        copySearchCardBtn->setEnabled(!visibleInputs.isEmpty() || !searchText.isEmpty());
        copySearchMediaPackBtn->setEnabled(!targetId.isEmpty() || !searchText.isEmpty());
        copyBatchMediaPlanBtn->setEnabled(!visibleInputs.isEmpty());
        copyMediaGuideBtn->setEnabled(true);
        copyOnlineBtn->setEnabled(hasVisibleOnline);
    };
    const QString initialSearchText = initialFilter.trimmed();
    if (!initialSearchText.isEmpty()) {
        searchEdit->setText(initialSearchText);
        fillResults(initialSearchText);
    } else {
        fillResults();
    }

    dialog.setStyleSheet(productDialogStyleSheet());

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillResults, updateActionState](const QString& text) {
        fillResults(text.trimmed());
        updateActionState();
    });
    auto runServerSearch = [this, searchEdit, &dialog]() {
        QString account = searchEdit->text().trimmed();
        if (account.isEmpty()) {
            ui->statusbar->showMessage("请输入 QQ 号", 2500);
            return;
        }
        searchAndAddAccount(account, &dialog);
        dialog.accept();
    };
    auto openResult = [this, &dialog, resultList]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            ui->statusbar->showMessage("请先选择要打开的搜索结果", 1800);
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("当前没有可打开的搜索结果", 1800);
            return;
        }
        if (id.startsWith("search_add:")) {
            searchAndAddAccount(id.mid(QString("search_add:").size()), &dialog);
            dialog.accept();
            return;
        }
        if (m_localGroupIds.contains(id)) {
            dialog.accept();
            switchToLocalGroup(id, m_localGroupNames.value(id, "群聊"));
            ui->statusbar->showMessage("已进入群聊: " + m_localGroupNames.value(id, "群聊"), 1800);
            return;
        }
        if (!m_friendIds.contains(id) && m_pendingOutgoingFriendRequests.contains(id)) {
            ui->statusbar->showMessage(QString("%1 的好友申请正在等待确认").arg(contactDisplayName(id)), 2200);
        } else {
            ensureFriendRequestQueued(id, QStringLiteral("已从综合搜索向 %1（QQ:%2）发起好友申请"));
        }
        dialog.accept();
        openPrivateSession(id);
        ui->statusbar->showMessage(QString("已打开与 %1 的私聊").arg(contactDisplayName(id)), 1800);
    };

    connect(searchBtn, &QPushButton::clicked, &dialog, runServerSearch);
    connect(resultList, &QListWidget::currentItemChanged, &dialog, [updatePreview, updateActionState](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(clearBtn, &QPushButton::clicked, &dialog, [searchEdit, fillResults]() {
        searchEdit->clear();
        fillResults();
        searchEdit->setFocus();
    });
    connect(quickAddBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowQuickAddFriend();
    });
    connect(friendManagerBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowFriendManager();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, runServerSearch);
    connect(openBtn, &QPushButton::clicked, &dialog, openResult);
    connect(createGroupBtn, &QPushButton::clicked, &dialog, [this, searchEdit, &dialog]() {
        QString groupName = searchEdit->text().trimmed();
        if (groupName.isEmpty()) groupName = "我的群聊";
        const QString groupId = createLocalGroupSession(groupName);
        dialog.accept();
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage("已从搜索创建群聊: " + groupName);
    });
    connect(inviteVisibleBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit, &dialog]() {
        QString groupName = searchEdit->text().trimmed();
        if (groupName.isEmpty()) groupName = "搜索群聊";
        QStringList members;
        QStringList requestIds;
        int pendingSkipped = 0;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty() || id.startsWith("local_group_") || id.startsWith("search_add:") || id == m_currentUserId || members.contains(id)) continue;
            members << id;
            if (!m_friendIds.contains(id)) {
                if (m_pendingOutgoingFriendRequests.contains(id)) {
                    ++pendingSkipped;
                } else {
                    requestIds << id;
                }
            }
        }
        int invitedCount = members.size();
        if (invitedCount == 0) {
            ui->statusbar->showMessage("当前没有可邀请的可见用户", 2200);
            return;
        }
        if (!confirmAction(QStringLiteral("可见用户建群"),
                           QStringLiteral("确定创建群聊“%1”并邀请 %2 位可见用户吗？其中 %3 位会同时发起好友申请。")
                               .arg(groupName)
                               .arg(invitedCount)
                               .arg(requestIds.size()),
                           QStringLiteral("已取消可见用户建群"),
                           1600,
                           &dialog)) {
            return;
        }
        QStringList sentNames;
        QStringList failedNames;
        for (const QString& id : requestIds) {
            const QString displayName = contactDisplayName(id);
            if (ensureFriendRequestQueued(id)) {
                sentNames << QString("%1(%2)").arg(displayName, id);
            } else {
                failedNames << QString("%1(%2)").arg(displayName, id);
            }
        }
        const QString groupId = createLocalGroupSession(
            groupName,
            members,
            QString("%1 已从综合搜索创建，已邀请可见用户。").arg(groupName));
        dialog.accept();
        switchToLocalGroup(groupId, groupName);
        QString detail = QString("已从综合搜索建群并邀请 %1 位可见用户").arg(invitedCount);
        if (!sentNames.isEmpty()) detail += QString("，已发起好友申请 %1 个").arg(sentNames.size());
        if (pendingSkipped > 0) detail += QString("，跳过申请中 %1 个").arg(pendingSkipped);
        if (!failedNames.isEmpty()) detail += QString("，申请失败 %1 个").arg(failedNames.size());
        appendSystemMessage(detail);
    });
    connect(addVisibleBtn, &QPushButton::clicked, &dialog, [this, resultList, searchEdit, fillResults, &dialog]() {
        QStringList addIds;
        int pendingSkipped = 0;
        for (int i = 0; i < resultList->count(); ++i) {
            QListWidgetItem* item = resultList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty() || id.startsWith("local_group_") || id.startsWith("search_add:") || id == m_currentUserId || m_friendIds.contains(id)) continue;
            if (m_pendingOutgoingFriendRequests.contains(id)) {
                ++pendingSkipped;
                continue;
            }
            if (!addIds.contains(id)) addIds << id;
        }
        if (addIds.isEmpty()) {
            ui->statusbar->showMessage(pendingSkipped > 0 ? "可见用户均已是好友或申请中" : "当前没有可发送申请的用户", 2200);
            searchEdit->setFocus();
            return;
        }
        if (!confirmAction(QStringLiteral("发起可见用户申请"),
                           QString("确定向 %1 位可见用户发起好友申请吗？").arg(addIds.size()),
                           QStringLiteral("已取消发起可见用户申请"),
                           1600,
                           &dialog)) {
            searchEdit->setFocus();
            return;
        }
        QStringList sentNames;
        QStringList failedNames;
        for (const QString& id : addIds) {
            const QString displayName = contactDisplayName(id);
            if (ensureFriendRequestQueued(id)) {
                sentNames << QString("%1(%2)").arg(displayName, id);
            } else {
                failedNames << QString("%1(%2)").arg(displayName, id);
            }
        }
        if (sentNames.isEmpty()) {
            ui->statusbar->showMessage("可见用户好友申请发起失败", 2600);
            searchEdit->setFocus();
            return;
        }
        refreshFriendList();
        fillResults(searchEdit->text().trimmed());
        QString detail = QString("已向 %1 个可见用户发起好友申请").arg(sentNames.size());
        if (pendingSkipped > 0) detail += QString(" · 已跳过申请中 %1 个").arg(pendingSkipped);
        if (!failedNames.isEmpty()) detail += QString(" · 失败 %1 个").arg(failedNames.size());
        appendSystemMessage(detail);
        ui->statusbar->showMessage(detail, 2800);
        searchEdit->setFocus();
    });
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, resultList]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            ui->statusbar->showMessage("请先选择要复制 QQ 的搜索结果", 1800);
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("当前没有可复制的 QQ 号", 1800);
            return;
        }
        if (id.startsWith("search_add:")) id = id.mid(QString("search_add:").size());
        if (id.startsWith("local_group_")) id = id.mid(QString("local_group_").size());
        QApplication::clipboard()->setText(id);
        ui->statusbar->showMessage("QQ 号已复制: " + id, 2500);
    });
    connect(copyListBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs]() {
        const GlobalSearchResultCopyState state =
            FriendManager::globalSearchResultCopyState(globalSearchCopyInputs(), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(copyAddTextBtn, &QPushButton::clicked, &dialog, [this, currentGlobalSearchCopyInput, searchEdit]() {
        const QString text = FriendManager::globalSearchInviteText(
            m_currentUserId,
            m_currentUserName,
            currentGlobalSearchCopyInput(),
            searchEdit->text().trimmed());
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("申请/邀请话术已复制", 2200);
    });
    connect(copyInviteCardBtn, &QPushButton::clicked, &dialog, [this, currentGlobalSearchCopyInput, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchInviteCardState(
            m_currentUserId,
            m_currentUserName,
            currentGlobalSearchCopyInput(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的搜索邀请卡", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("搜索邀请卡已复制", 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchSummaryCardState(
            m_currentUserId,
            m_currentUserName,
            globalSearchCopyInputs(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的综合搜索卡片", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("综合搜索卡片已复制", 2200);
    });
    connect(copySearchMediaPackBtn, &QPushButton::clicked, &dialog, [this, currentGlobalSearchCopyInput, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchMediaPackState(
            m_currentUserId,
            m_currentUserName,
            currentGlobalSearchCopyInput(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的综合搜索媒体包", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("综合搜索媒体包已复制", 2200);
    });
    connect(copyBatchMediaPlanBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::globalSearchBatchMediaPlanState(
            m_currentUserId,
            m_currentUserName,
            globalSearchCopyInputs(),
            searchEdit->text().trimmed());
        if (!state.valid) {
            ui->statusbar->showMessage("当前没有可复制的综合搜索批量媒体计划", 1800);
            return;
        }
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("综合搜索批量媒体计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this]() {
        QApplication::clipboard()->setText(FriendManager::globalSearchMediaGuideText(
            m_currentUserId,
            m_currentUserName,
            m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget)));
        ui->statusbar->showMessage("综合搜索上传指南已复制", 2200);
    });
    connect(copyOnlineBtn, &QPushButton::clicked, &dialog, [this, globalSearchCopyInputs]() {
        const GlobalSearchResultCopyState state =
            FriendManager::globalSearchResultCopyState(globalSearchCopyInputs(), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(profileBtn, &QPushButton::clicked, &dialog, [this, resultList]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            ui->statusbar->showMessage("请先选择要复制名片的搜索结果", 1800);
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("当前没有可复制的名片信息", 1800);
            return;
        }
        QString text = item->text();
        if (id.startsWith("search_add:")) {
            id = id.mid(QString("search_add:").size());
            text = QString("QQ:%1\n一键搜索并申请").arg(id);
        } else if (id.startsWith("local_group_")) {
            QString groupNumber = id.mid(QString("local_group_").size());
            text = QString("群聊 QQ:%1\n%2").arg(groupNumber, m_localGroupNames.value(id, "群聊"));
        } else {
            text = QString("QQ:%1\n%2 · %3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
        }
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("名片信息已复制", 1800);
    });
    connect(resultList, &QListWidget::itemDoubleClicked, &dialog, [openResult](QListWidgetItem*) { openResult(); });

    updateActionState();
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowCreateMenu() {
    QDialog dialog(this);
    const bool localGroupContext = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    const bool publicGroupContext = m_privateChatTarget.isEmpty();
    const QString currentSessionName = publicGroupContext
        ? QStringLiteral("公共聊天室")
        : contactDisplayName(m_privateChatTarget);
    const QString currentGroupId = localGroupContext ? m_privateChatTarget : QString();
    const QString currentGroupName = localGroupContext
        ? m_localGroupNames.value(currentGroupId, QStringLiteral("群聊"))
        : QString();

    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("workspaceCommandDialog"),
        QStringLiteral("群组与快捷工作区"),
        QSize(980, 780),
        QStringLiteral("searchDialogTitle"),
        QStringLiteral("群组与快捷工作区"),
        QStringLiteral("searchDialogSubTitle"),
        QStringLiteral("把建群、群聊管理、通知入口、媒体发送和摘要复制收在一个面板里统一处理。"),
        QStringLiteral("workspaceCommandSearch"),
        QStringLiteral("筛选建群、群聊、通知、媒体动作"),
        QStringLiteral("输入关键词筛选动作；回车可直接执行当前选中项"),
        QStringLiteral("workspaceCommandList"),
        true,
        QStringLiteral("globalActionHint"),
        QStringLiteral("先确认当前会话和群聊状态，再执行建群、邀请、公告或媒体动作。"),
        QStringLiteral("globalStatsLabel"),
        QStringLiteral("globalPreviewLabel"),
        QStringLiteral("选择动作后，这里会说明它会改哪里、复制什么，或下一步会打开哪个工作区。"),
        QStringLiteral("searchHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(18, 16, 18, 10);
    shell.headerLayout->setSpacing(10);
    shell.bodyLayout->setContentsMargins(18, 14, 18, 18);
    shell.bodyLayout->setSpacing(12);
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* actionList = shell.listWidget;
    QLabel* actionHint = shell.hintLabel;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    shell.subTitleLabel->setText(QStringLiteral("当前会话：%1 · 好友 %2 人 · 本地群 %3 个。")
                                     .arg(currentSessionName)
                                     .arg(m_friendIds.size())
                                     .arg(m_localGroupIds.size()));

    struct WorkspaceActionEntry {
        QString commandId;
        QString title;
        QString section;
        QString summary;
        QString disabledReason;
        QStyle::StandardPixmap iconType = QStyle::SP_FileDialogInfoView;
        bool primary = false;
        bool enabled = true;
    };

    const QList<WorkspaceActionEntry> actionCatalog = {
        {QStringLiteral("create-group"), QStringLiteral("创建群聊"), QStringLiteral("群组入口"), QStringLiteral("使用当前关键词或默认名称创建一个新的本地群聊并立即进入。"), QString(), QStyle::SP_FileDialogNewFolder, true, true},
        {QStringLiteral("create-group-with-friends"), QStringLiteral("创建群并拉全部好友"), QStringLiteral("群组入口"), QStringLiteral("创建群聊并自动邀请当前全部好友，适合快速拉起协作群。"), QString(), QStyle::SP_DialogYesButton, true, true},
        {QStringLiteral("show-group-notice"), QStringLiteral("打开群通知"), QStringLiteral("群组入口"), QStringLiteral("查看公共聊天室、本地群聊、群公告、成员和入群邀请材料。"), QString(), QStyle::SP_MessageBoxInformation, false, true},
        {QStringLiteral("edit-announcement"), QStringLiteral("编辑群公告"), QStringLiteral("群组入口"), QStringLiteral("修改当前群聊或公共聊天室的公告内容，并保持统一状态反馈。"), QStringLiteral("切换到公共聊天室或本地群聊后可编辑群公告。"), QStyle::SP_FileDialogDetailedView, false, localGroupContext || publicGroupContext},
        {QStringLiteral("copy-announcement"), QStringLiteral("复制群公告"), QStringLiteral("群组入口"), QStringLiteral("复制当前群聊或公共聊天室的公告内容，便于同步给成员。"), QStringLiteral("切换到公共聊天室或本地群聊后可复制公告。"), QStyle::SP_DialogSaveButton, false, localGroupContext || publicGroupContext},
        {QStringLiteral("open-global-search"), QStringLiteral("申请好友/群"), QStringLiteral("通知与搜索"), QStringLiteral("打开综合搜索，继续查找联系人、群聊或发起好友申请。"), QString(), QStyle::SP_FileDialogContentsView, false, true},
        {QStringLiteral("show-friend-notice"), QStringLiteral("打开好友通知"), QStringLiteral("通知与搜索"), QStringLiteral("进入好友通知工作区，处理待办申请、复制回复和媒体准备材料。"), QString(), QStyle::SP_DialogOpenButton, false, true},
        {QStringLiteral("focus-contact-search"), QStringLiteral("定位 QQ 搜索框"), QStringLiteral("通知与搜索"), QStringLiteral("回到主界面左侧搜索框，继续输入 QQ 号或昵称筛选联系人。"), QString(), QStyle::SP_ArrowForward, false, true},
        {QStringLiteral("refresh-contacts"), QStringLiteral("刷新联系人"), QStringLiteral("通知与搜索"), QStringLiteral("重新加载好友、群聊和群成员列表，收敛当前界面状态。"), QString(), QStyle::SP_BrowserReload, false, true},
        {QStringLiteral("clear-search"), QStringLiteral("清空搜索"), QStringLiteral("通知与搜索"), QStringLiteral("清空联系人和群成员搜索条件，让主窗口回到默认浏览状态。"), QString(), QStyle::SP_DialogResetButton, false, true},
        {QStringLiteral("copy-search-summary"), QStringLiteral("复制搜索摘要"), QStringLiteral("通知与搜索"), QStringLiteral("复制当前搜索条件、好友数、群聊数和当前会话摘要。"), QString(), QStyle::SP_FileDialogListView, false, true},
        {QStringLiteral("copy-all-contacts"), QStringLiteral("复制全部联系人"), QStringLiteral("通知与搜索"), QStringLiteral("导出好友、本地群和在线成员摘要，便于外部整理。"), QString(), QStyle::SP_FileDialogDetailedView, false, true},
        {QStringLiteral("copy-quick-guide"), QStringLiteral("复制 QQ 功能指南"), QStringLiteral("通知与搜索"), QStringLiteral("复制搜索、好友、群聊和媒体发送的简短工作说明。"), QString(), QStyle::SP_DialogHelpButton, false, true},
        {QStringLiteral("copy-chat-id"), QStringLiteral("复制当前会话号"), QStringLiteral("当前会话"), QStringLiteral("复制当前私聊 QQ、群号或公共聊天室标识。"), QString(), QStyle::SP_DialogSaveButton, false, true},
        {QStringLiteral("copy-chat-card"), QStringLiteral("复制当前会话名片"), QStringLiteral("当前会话"), QStringLiteral("复制当前会话的名称、账号和公告/成员摘要。"), QString(), QStyle::SP_FileDialogInfoView, false, true},
        {QStringLiteral("copy-current-invite"), QStringLiteral("复制当前邀请语"), QStringLiteral("当前会话"), QStringLiteral("复制当前会话适用的邀请话术，可直接发给好友或成员。"), QString(), QStyle::SP_DirLinkIcon, false, true},
        {QStringLiteral("copy-current-members"), QStringLiteral("复制当前成员列表"), QStringLiteral("当前会话"), QStringLiteral("复制当前会话可见成员，用于同步群范围和在场人员。"), QString(), QStyle::SP_FileDialogListView, false, true},
        {QStringLiteral("copy-current-online"), QStringLiteral("复制当前在线成员"), QStringLiteral("当前会话"), QStringLiteral("复制当前会话里在线成员的 QQ 和昵称。"), QString(), QStyle::SP_DialogApplyButton, false, true},
        {QStringLiteral("send-image"), QStringLiteral("发送图片/视频"), QStringLiteral("媒体发送"), QStringLiteral("打开图片/视频发送入口，把媒体动作挂回统一工作流。"), QString(), QStyle::SP_FileIcon, true, true},
        {QStringLiteral("send-file"), QStringLiteral("闪传文件"), QStringLiteral("媒体发送"), QStringLiteral("打开文件闪传入口，继续发送文档、压缩包或媒体文件。"), QString(), QStyle::SP_DriveHDIcon, true, true},
        {QStringLiteral("copy-current-media-pack"), QStringLiteral("复制当前媒体包"), QStringLiteral("媒体发送"), QStringLiteral("整理当前会话的媒体发送准备摘要、入口和查收话术。"), QString(), QStyle::SP_FileDialogDetailedView, false, true},
        {QStringLiteral("copy-media-guide"), QStringLiteral("复制上传指南"), QStringLiteral("媒体发送"), QStringLiteral("复制图片、视频和闪传文件的统一使用说明。"), QString(), QStyle::SP_DialogHelpButton, false, true},
        {QStringLiteral("copy-full-media-plan"), QStringLiteral("复制完整媒体计划"), QStringLiteral("媒体发送"), QStringLiteral("复制好友、群聊、媒体准备和查收话术的一整套执行清单。"), QString(), QStyle::SP_FileDialogContentsView, false, true},
        {QStringLiteral("invite-friend"), QStringLiteral("邀请好友入群"), QStringLiteral("当前本地群"), QStringLiteral("从好友列表中选择成员加入当前本地群聊。"), QStringLiteral("切换到本地群聊后可邀请好友。"), QStyle::SP_FileDialogNewFolder, false, localGroupContext},
        {QStringLiteral("invite-by-account"), QStringLiteral("按 QQ 号邀请"), QStringLiteral("当前本地群"), QStringLiteral("直接输入 QQ 号邀请成员入群，并按需触发好友申请。"), QStringLiteral("切换到本地群聊后可按 QQ 号邀请。"), QStyle::SP_CommandLink, false, localGroupContext},
        {QStringLiteral("invite-all-friends"), QStringLiteral("邀请全部好友"), QStringLiteral("当前本地群"), QStringLiteral("把当前全部好友批量邀请进本地群聊。"), QStringLiteral("切换到本地群聊后可批量邀请。"), QStyle::SP_DialogYesButton, false, localGroupContext},
        {QStringLiteral("rename-group"), QStringLiteral("重命名当前群聊"), QStringLiteral("当前本地群"), QStringLiteral("修改当前本地群聊名称，并同步左侧联系人列表。"), QStringLiteral("切换到本地群聊后可重命名。"), QStyle::SP_FileDialogInfoView, false, localGroupContext},
        {QStringLiteral("copy-group-card"), QStringLiteral("复制当前群名片"), QStringLiteral("当前本地群"), QStringLiteral("复制群名、群号、成员数和公告摘要。"), QStringLiteral("切换到本地群聊后可复制群名片。"), QStyle::SP_FileDialogDetailedView, false, localGroupContext},
        {QStringLiteral("copy-group-invite"), QStringLiteral("复制群邀请语"), QStringLiteral("当前本地群"), QStringLiteral("复制一段更明确的入群邀请语，适合对外发送。"), QStringLiteral("切换到本地群聊后可复制群邀请语。"), QStyle::SP_DirLinkIcon, false, localGroupContext},
        {QStringLiteral("copy-group-members"), QStringLiteral("复制群成员列表"), QStringLiteral("当前本地群"), QStringLiteral("导出当前群聊的全部成员列表。"), QStringLiteral("切换到本地群聊后可复制群成员列表。"), QStyle::SP_FileDialogListView, false, localGroupContext},
        {QStringLiteral("copy-group-online-members"), QStringLiteral("复制在线群成员"), QStringLiteral("当前本地群"), QStringLiteral("导出当前群聊里在线成员的 QQ 和昵称。"), QStringLiteral("切换到本地群聊后可复制在线群成员。"), QStyle::SP_DialogApplyButton, false, localGroupContext},
        {QStringLiteral("delete-group"), QStringLiteral("删除当前群聊"), QStringLiteral("当前本地群"), QStringLiteral("删除当前本地群聊配置，不会在此步骤直接删除聊天记录。"), QStringLiteral("切换到本地群聊后可删除。"), QStyle::SP_TrashIcon, false, localGroupContext}
    };

    auto matchesActionFilter = [](const WorkspaceActionEntry& entry, const QString& filter) {
        if (filter.isEmpty()) {
            return true;
        }
        return entry.title.contains(filter, Qt::CaseInsensitive)
            || entry.section.contains(filter, Qt::CaseInsensitive)
            || entry.summary.contains(filter, Qt::CaseInsensitive)
            || entry.commandId.contains(filter, Qt::CaseInsensitive);
    };

    auto fillActionList = [=, &dialog]() {
        const QString filter = searchEdit->text().trimmed();
        actionList->clear();
        int visibleCount = 0;
        int executableCount = 0;
        for (const WorkspaceActionEntry& entry : actionCatalog) {
            if (!matchesActionFilter(entry, filter)) {
                continue;
            }
            ++visibleCount;
            if (entry.enabled) {
                ++executableCount;
            }
            QListWidgetItem* item = new QListWidgetItem(
                dialog.style()->standardIcon(entry.iconType),
                QStringLiteral("%1\n%2 · %3").arg(entry.title, entry.section, entry.summary));
            item->setData(Qt::UserRole, entry.commandId);
            item->setData(Qt::UserRole + 1, entry.title);
            item->setData(Qt::UserRole + 2, entry.section);
            item->setData(Qt::UserRole + 3, entry.summary);
            item->setData(Qt::UserRole + 4, entry.enabled);
            item->setData(Qt::UserRole + 5, entry.disabledReason);
            item->setSizeHint(QSize(0, 78));
            item->setToolTip(entry.enabled ? entry.summary : entry.disabledReason);
            if (!entry.enabled) {
                item->setFlags(Qt::NoItemFlags);
                item->setForeground(QColor(135, 150, 165));
            } else if (entry.primary) {
                item->setForeground(QColor(24, 92, 186));
            }
            actionList->addItem(item);
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                actionList,
                QStringLiteral("没有匹配的快捷动作"),
                QStringLiteral("试试“建群”、“公告”、“成员”或“媒体”这些关键词。"),
                filter.isEmpty()
                    ? QStringLiteral("当前没有可见的快捷工作区动作。可从这里继续进入建群、通知、当前会话摘要或媒体发送入口。")
                    : QStringLiteral("当前筛选词“%1”没有匹配到快捷动作。\n试试“建群”、“公告”、“成员”或“媒体”这些关键词。").arg(filter));
        } else {
            selectPreferredListRow(actionList, 0);
        }
        statsLabel->setText(QStringLiteral("动作 %1 · 可执行 %2 · 好友 %3 · 本地群 %4")
                                .arg(visibleCount)
                                .arg(executableCount)
                                .arg(m_friendIds.size())
                                .arg(m_localGroupIds.size()));
        actionHint->setText(visibleCount == 0
                                ? (filter.isEmpty()
                                       ? QStringLiteral("当前没有可见的快捷动作。可从这里继续进入建群、通知、当前会话摘要或媒体发送入口。")
                                       : QStringLiteral("当前筛选词“%1”没有匹配到快捷动作。可调整关键词，或清空筛选后重新浏览。").arg(filter))
                                : (localGroupContext
                                       ? QStringLiteral("当前本地群：%1 · 可直接处理邀请、重命名、公告和媒体动作。").arg(currentGroupName)
                                       : QStringLiteral("当前会话：%1 · 建群、通知、复制摘要和媒体动作都从这里统一进入。").arg(currentSessionName)));
    };

    auto previewTextForItem = [=]() {
        QListWidgetItem* item = actionList->currentItem();
        const QString keyword = searchEdit->text().trimmed();
        if (!item || item->data(Qt::UserRole).toString().isEmpty()) {
            return keyword.isEmpty()
                ? QStringLiteral("选择动作后，这里会说明它会改哪里、复制什么，或下一步会打开哪个工作区。")
                : QStringLiteral("当前关键词：%1\n可继续筛选建群、公告、成员、通知或媒体动作。").arg(keyword);
        }
        const QString commandId = item->data(Qt::UserRole).toString();
        const QString title = item->data(Qt::UserRole + 1).toString();
        const QString section = item->data(Qt::UserRole + 2).toString();
        const QString summary = item->data(Qt::UserRole + 3).toString();
        const bool enabled = item->data(Qt::UserRole + 4).toBool();
        const QString disabledReason = item->data(Qt::UserRole + 5).toString();
        QString nextStep;
        if (commandId == QLatin1String("create-group") || commandId == QLatin1String("create-group-with-friends")) {
            nextStep = keyword.isEmpty()
                ? QStringLiteral("未填关键词时会使用默认群名；填了关键词就会直接拿来当群名。")
                : QStringLiteral("当前会直接使用关键词“%1”作为群名。").arg(keyword);
        } else if (commandId.startsWith(QLatin1String("copy-"))) {
            nextStep = QStringLiteral("执行后会保留在当前工作区，方便继续整理其他摘要。");
        } else if (commandId == QLatin1String("send-image") || commandId == QLatin1String("send-file")) {
            nextStep = QStringLiteral("执行后会进入发送流程，结果继续回到主窗口工作区显示。");
        } else if (commandId == QLatin1String("show-group-notice") || commandId == QLatin1String("show-friend-notice") || commandId == QLatin1String("open-global-search")) {
            nextStep = QStringLiteral("执行后会切换到对应的大弹窗工作区。");
        } else {
            nextStep = QStringLiteral("执行后会更新主窗口会话、群资料或输入/确认流程。");
        }
        return enabled
            ? QStringLiteral("%1\n区块：%2\n%3\n下一步：%4").arg(title, section, summary, nextStep)
            : QStringLiteral("%1\n区块：%2\n%3\n当前不可用：%4").arg(title, section, summary, disabledReason);
    };

    QPushButton* executeBtn = createWorkspaceButton(shell.headerFrame,
                                                    &dialog,
                                                    QStringLiteral("执行选中动作"),
                                                    QStringLiteral("globalSearchPrimaryBtn"),
                                                    QStringLiteral("执行当前选中的工作区动作"),
                                                    QStyle::SP_ArrowForward);
    QPushButton* clearFilterBtn = createWorkspaceButton(shell.headerFrame,
                                                        &dialog,
                                                        QStringLiteral("清空筛选"),
                                                        QStringLiteral("globalSearchGhostBtn"),
                                                        QStringLiteral("清空动作筛选关键词"),
                                                        QStyle::SP_DialogResetButton);
    QPushButton* openSearchBtn = createWorkspaceButton(shell.headerFrame,
                                                       &dialog,
                                                       QStringLiteral("综合搜索"),
                                                       QStringLiteral("globalSearchGhostBtn"),
                                                       QStringLiteral("打开综合搜索工作区"),
                                                       QStyle::SP_FileDialogContentsView);
    QPushButton* openGroupNoticeBtn = createWorkspaceButton(shell.headerFrame,
                                                            &dialog,
                                                            QStringLiteral("群通知"),
                                                            QStringLiteral("globalSearchGhostBtn"),
                                                            QStringLiteral("打开群通知工作区"),
                                                            QStyle::SP_MessageBoxInformation);
    shell.searchRowLayout->addWidget(executeBtn);
    shell.searchRowLayout->addWidget(clearFilterBtn);
    shell.searchRowLayout->addWidget(openSearchBtn);
    shell.searchRowLayout->addWidget(openGroupNoticeBtn);

    auto runWorkspaceCommand = [&, this](const QString& commandId) {
        if (commandId.isEmpty()) {
            return;
        }

        auto isLocalGroupWorkspaceCommand = [](const QString& id) {
            return id == QLatin1String("invite-friend")
                || id == QLatin1String("invite-by-account")
                || id == QLatin1String("invite-all-friends")
                || id == QLatin1String("rename-group")
                || id == QLatin1String("copy-group-card")
                || id == QLatin1String("copy-group-invite")
                || id == QLatin1String("copy-group-members")
                || id == QLatin1String("copy-group-online-members")
                || id == QLatin1String("delete-group");
        };

        const QString keyword = searchEdit->text().trimmed();
        if (commandId == QLatin1String("create-group")) {
            if (!keyword.isEmpty()) {
                const QString groupId = createLocalGroupSession(keyword);
                switchToLocalGroup(groupId, keyword);
                appendSystemMessage(QStringLiteral("已从群组工作区创建群聊: %1").arg(keyword));
                ui->statusbar->showMessage(QStringLiteral("已创建群聊：%1").arg(keyword), 2200);
                return;
            }
        } else if (commandId == QLatin1String("create-group-with-friends")) {
            const QString groupName = keyword.isEmpty() ? QStringLiteral("好友群聊") : keyword;
            const QStringList members = m_friendIds;
            const QString groupId = createLocalGroupSession(
                groupName,
                members,
                QStringLiteral("%1 已创建，已自动邀请全部好友。").arg(groupName));
            switchToLocalGroup(groupId, groupName);
            appendSystemMessage(QStringLiteral("已从群组工作区创建群聊并邀请 %1 位好友").arg(members.size()));
            saveHistory(groupId,
                        QStringLiteral("[%1] [系统] 已创建群聊并邀请 %2 位好友")
                            .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")))
                            .arg(members.size()));
            ui->statusbar->showMessage(QStringLiteral("已创建群聊并邀请全部好友：%1").arg(groupName), 2400);
            return;
        }

        if (isLocalGroupWorkspaceCommand(commandId)) {
            if (!localGroupContext || currentGroupId.isEmpty()) {
                ui->statusbar->showMessage(QStringLiteral("切换到本地群聊后再使用这个动作"), 2200);
                return;
            }
            handleLocalGroupContextCommand(currentGroupId, currentGroupName, commandId);
            return;
        }

        handleCreateMenuCommand(commandId);
    };

    auto shouldCloseBeforeAction = [](const QString& commandId) {
        return !commandId.startsWith(QLatin1String("copy-"))
            && commandId != QLatin1String("refresh-contacts")
            && commandId != QLatin1String("clear-search");
    };

    auto executeSelectedAction = [&, this]() {
        QListWidgetItem* item = actionList->currentItem();
        if (!item || item->data(Qt::UserRole).toString().isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("请先选择一个要执行的工作区动作"), 1800);
            return;
        }
        if (!item->data(Qt::UserRole + 4).toBool()) {
            ui->statusbar->showMessage(item->data(Qt::UserRole + 5).toString(), 2200);
            return;
        }
        const QString commandId = item->data(Qt::UserRole).toString();
        if (shouldCloseBeforeAction(commandId)) {
            dialog.accept();
        }
        runWorkspaceCommand(commandId);
        if (dialog.isVisible()) {
            fillActionList();
            previewLabel->setText(previewTextForItem());
        }
    };

    auto triggerWorkspaceAction = [&, this](const QString& commandId) {
        if (shouldCloseBeforeAction(commandId)) {
            dialog.accept();
        }
        runWorkspaceCommand(commandId);
        if (dialog.isVisible()) {
            fillActionList();
            previewLabel->setText(previewTextForItem());
        }
    };

    auto updateExecuteState = [=]() {
        QListWidgetItem* item = actionList->currentItem();
        const bool enabled = item && item->data(Qt::UserRole + 4).toBool();
        const QString title = item ? item->data(Qt::UserRole + 1).toString() : QString();
        executeBtn->setEnabled(enabled);
        executeBtn->setText(enabled
                                ? QStringLiteral("执行“%1”").arg(title)
                                : QStringLiteral("执行选中动作"));
        executeBtn->setToolTip(enabled
                                   ? QStringLiteral("执行当前选中的工作区动作：%1").arg(title)
                                   : QStringLiteral("选择一个可执行动作后再继续"));
        if ((!item || item->data(Qt::UserRole).toString().isEmpty()) && hasEnabledListRow(actionList)) {
            previewLabel->setText(QStringLiteral("选择动作后，这里会说明它会改哪里、复制什么，或下一步会打开哪个工作区。"));
        }
    };

    QPushButton* createGroupBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("创建群聊"), QStringLiteral("globalSearchPrimaryBtn"), QStringLiteral("使用关键词或默认名称创建本地群聊"), QStyle::SP_FileDialogNewFolder);
    QPushButton* createAllBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("创建群并拉全部好友"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("创建群聊并自动邀请全部好友"), QStyle::SP_DialogYesButton);
    QPushButton* editAnnouncementBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("编辑群公告"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("编辑当前群聊或公共聊天室公告"), QStyle::SP_FileDialogDetailedView);
    QPushButton* openFriendNoticeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("好友通知"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("打开好友通知工作区"), QStyle::SP_DialogOpenButton);
    QPushButton* copyChatCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制当前会话名片"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制当前会话名称、账号和摘要"), QStyle::SP_FileDialogInfoView);
    QPushButton* copyCurrentMembersBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制当前成员"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制当前会话可见成员"), QStyle::SP_FileDialogListView);
    QPushButton* copyCurrentInviteBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制当前邀请语"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制当前会话邀请话术"), QStyle::SP_DirLinkIcon);
    QPushButton* copySearchSummaryBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制搜索摘要"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制搜索条件和联系人统计"), QStyle::SP_FileDialogContentsView);
    QPushButton* sendImageBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("发送图片/视频"), QStringLiteral("globalSearchPrimaryBtn"), QStringLiteral("打开图片/视频发送入口"), QStyle::SP_FileIcon);
    QPushButton* sendFileBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("闪传文件"), QStringLiteral("globalSearchPrimaryBtn"), QStringLiteral("打开文件闪传入口"), QStyle::SP_DriveHDIcon);
    QPushButton* copyMediaPackBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制当前媒体包"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制当前会话媒体准备摘要"), QStyle::SP_FileDialogDetailedView);
    QPushButton* copyMediaGuideBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制上传指南"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制统一上传指南"), QStyle::SP_DialogHelpButton);
    QPushButton* copyFullPlanBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制完整媒体计划"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制完整媒体发送计划"), QStyle::SP_FileDialogContentsView);
    QPushButton* inviteFriendBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("邀请好友入群"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("从好友列表选择成员加入当前本地群"), QStyle::SP_FileDialogNewFolder);
    QPushButton* inviteByAccountBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("按 QQ 号邀请"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("输入 QQ 号邀请成员加入当前本地群"), QStyle::SP_CommandLink);
    QPushButton* inviteAllBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("邀请全部好友"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("把全部好友拉进当前本地群"), QStyle::SP_DialogYesButton);
    QPushButton* renameGroupBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("重命名群聊"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("修改当前本地群聊名称"), QStyle::SP_FileDialogInfoView);
    QPushButton* copyGroupMembersBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制群成员"), QStringLiteral("globalSearchGhostBtn"), QStringLiteral("复制当前本地群聊成员列表"), QStyle::SP_FileDialogListView);
    QPushButton* deleteGroupBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("删除当前群聊"), QStringLiteral("managerDangerBtn"), QStringLiteral("删除当前本地群聊配置"), QStyle::SP_TrashIcon);

    const QList<QPushButton*> localGroupButtons = {
        inviteFriendBtn, inviteByAccountBtn, inviteAllBtn, renameGroupBtn, copyGroupMembersBtn, deleteGroupBtn
    };
    for (QPushButton* button : localGroupButtons) {
        if (!button) {
            continue;
        }
        button->setEnabled(localGroupContext);
        if (!localGroupContext) {
            button->setToolTip(QStringLiteral("切换到本地群聊后可用"));
        }
    }

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("群组入口"),
        QStringLiteral("先把群聊建立起来，再进入群通知、公告和成员协作流程。"),
        {createGroupBtn, createAllBtn, editAnnouncementBtn, openGroupNoticeBtn, openFriendNoticeBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("当前会话摘要"),
        QStringLiteral("把当前会话名片、邀请语、成员和搜索统计整理出去。"),
        {copyChatCardBtn, copyCurrentMembersBtn, copyCurrentInviteBtn, copySearchSummaryBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("媒体发送"),
        QStringLiteral("统一从这里进入图片、视频、文件发送，并导出媒体准备材料。"),
        {sendImageBtn, sendFileBtn, copyMediaPackBtn, copyMediaGuideBtn, copyFullPlanBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("当前本地群"),
        localGroupContext
            ? QStringLiteral("当前群聊：%1 · 可继续邀请成员、重命名、复制群成员或直接删除配置。").arg(currentGroupName)
            : QStringLiteral("切换到本地群聊后，这里会显示邀请、重命名和删除等管理动作。"),
        {inviteFriendBtn, inviteByAccountBtn, inviteAllBtn, renameGroupBtn, copyGroupMembersBtn},
        deleteGroupBtn);

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=]() {
        fillActionList();
        previewLabel->setText(previewTextForItem());
        updateExecuteState();
    });
    connect(actionList, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        previewLabel->setText(previewTextForItem());
        updateExecuteState();
    });
    connect(actionList, &QListWidget::itemDoubleClicked, &dialog, [=](QListWidgetItem*) {
        executeSelectedAction();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, executeSelectedAction);
    connect(executeBtn, &QPushButton::clicked, &dialog, executeSelectedAction);
    connect(clearFilterBtn, &QPushButton::clicked, &dialog, [=]() {
        searchEdit->clear();
        searchEdit->setFocus();
    });
    connect(openSearchBtn, &QPushButton::clicked, &dialog, [=, &dialog]() {
        dialog.accept();
        onShowGlobalSearch();
    });
    connect(openGroupNoticeBtn, &QPushButton::clicked, &dialog, [=, &dialog]() {
        dialog.accept();
        onShowGroupNotifications();
    });
    connect(createGroupBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("create-group")); });
    connect(createAllBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("create-group-with-friends")); });
    connect(editAnnouncementBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("edit-announcement")); });
    connect(openFriendNoticeBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("show-friend-notice")); });
    connect(copyChatCardBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-chat-card")); });
    connect(copyCurrentMembersBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-current-members")); });
    connect(copyCurrentInviteBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-current-invite")); });
    connect(copySearchSummaryBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-search-summary")); });
    connect(sendImageBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("send-image")); });
    connect(sendFileBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("send-file")); });
    connect(copyMediaPackBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-current-media-pack")); });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-media-guide")); });
    connect(copyFullPlanBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-full-media-plan")); });
    connect(inviteFriendBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("invite-friend")); });
    connect(inviteByAccountBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("invite-by-account")); });
    connect(inviteAllBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("invite-all-friends")); });
    connect(renameGroupBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("rename-group")); });
    connect(copyGroupMembersBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("copy-group-members")); });
    connect(deleteGroupBtn, &QPushButton::clicked, &dialog, [=]() { triggerWorkspaceAction(QStringLiteral("delete-group")); });

    fillActionList();
    previewLabel->setText(previewTextForItem());
    updateExecuteState();
    searchEdit->setFocus();
    dialog.setStyleSheet(productDialogStyleSheet());
    dialog.exec();
}

void MainWindow::onShowGroupMemberWorkspace(const QString& initialFilter) {
    QDialog dialog(this);
    const bool localGroupContext = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    const bool removedFromPublicGroup = isCurrentUserRemovedFromPublicGroup();
    const bool serverGroupContext = !localGroupContext && !removedFromPublicGroup;
    const QString currentGroupId = localGroupContext ? m_privateChatTarget : QStringLiteral("public");
    const QString currentGroupName = localGroupContext
        ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
        : QStringLiteral("公共聊天室");
    const QString ownerId = localGroupContext
        ? groupOwnerId(m_privateChatTarget)
        : m_serverGroupOwners.value(QStringLiteral("public"));
    const QString ownerName = ownerId.isEmpty()
        ? QStringLiteral("未指定")
        : (ownerId == m_currentUserId ? m_currentUserName : contactDisplayName(ownerId));

    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("groupMemberWorkspaceDialog"),
        QStringLiteral("群成员工作区"),
        QSize(960, 760),
        QStringLiteral("managerTitle"),
        QStringLiteral("群成员工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把成员查看、邀请、备注、复制和权限动作集中处理，减少依赖右键菜单。"),
        QStringLiteral("groupMemberWorkspaceSearch"),
        QStringLiteral("搜索成员 QQ / 昵称 / 角色"),
        QStringLiteral("输入 QQ、昵称或角色筛选成员；回车执行当前主动作"),
        QStringLiteral("groupMemberWorkspaceList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先筛选成员，再决定邀请、发起好友申请、设置备注或复制群成员摘要。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("选择成员后，这里会说明当前关系、角色、下一步可做什么。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(24, 18, 24, 14);
    shell.bodyLayout->setContentsMargins(24, 20, 24, 22);
    shell.bodyLayout->setSpacing(12);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* memberList = shell.listWidget;
    QLabel* subTitleLabel = shell.subTitleLabel;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* operationGuideLabel = shell.hintLabel;

    auto isManagementEnabledForCurrentContext = [=]() {
        return localGroupContext ? isCurrentUserGroupOwner(m_privateChatTarget) : canCurrentUserManageServerGroup(QStringLiteral("public"));
    };

    auto memberGroupRoleText = [=](const QString& memberId) {
        if (memberId.isEmpty()) {
            return QString();
        }
        if (localGroupContext) {
            if (memberId == ownerId) {
                return QStringLiteral("群主");
            }
            if (memberId == m_currentUserId) {
                return QStringLiteral("我");
            }
            return QStringLiteral("成员");
        }
        const QString serverRole = m_serverGroupMemberRoles.value(QStringLiteral("public|") + memberId, QStringLiteral("member")).toLower();
        if (serverRole == QLatin1String("owner")) {
            return QStringLiteral("群主");
        }
        if (serverRole == QLatin1String("admin")) {
            return QStringLiteral("管理员");
        }
        if (memberId == m_currentUserId) {
            return QStringLiteral("我");
        }
        return QStringLiteral("成员");
    };

    struct GroupMemberWorkspaceRow {
        QString memberId;
        QString displayName;
        QString role;
        QString status;
        QString relation;
        QString preview;
        bool enabled = true;
        bool self = false;
        bool online = false;
        bool friendRelation = false;
        bool pendingRelation = false;
        bool inviteCandidate = false;
        bool searchAddCandidate = false;
        bool removable = false;
        bool localGroup = false;
    };

    auto fillMemberList = [&, this]() {
        const QString filter = searchEdit->text().trimmed();
        memberList->clear();
        QList<GroupMemberWorkspaceRow> rows;

        auto appendRowIfMatches = [&](const GroupMemberWorkspaceRow& row) {
            if (!filter.isEmpty()
                && !row.memberId.contains(filter, Qt::CaseInsensitive)
                && !row.displayName.contains(filter, Qt::CaseInsensitive)
                && !row.role.contains(filter, Qt::CaseInsensitive)
                && !row.relation.contains(filter, Qt::CaseInsensitive)) {
                return;
            }
            rows << row;
        };

        if (removedFromPublicGroup) {
            GroupMemberWorkspaceRow row;
            row.memberId = m_currentUserId;
            row.displayName = m_currentUserName;
            row.role = QStringLiteral("历史只读");
            row.status = QStringLiteral("已移出");
            row.relation = QStringLiteral("等待重新邀请");
            row.preview = QStringLiteral("公共群历史仍可查看，但当前账号已移出，成员管理不可用。");
            row.enabled = false;
            row.self = true;
            appendRowIfMatches(row);
        } else if (localGroupContext) {
            QStringList members = m_localGroupMembers.value(m_privateChatTarget);
            if (members.isEmpty()) {
                members << m_currentUserId;
            }
            for (const QString& memberId : members) {
                GroupMemberWorkspaceRow row;
                row.memberId = memberId;
                row.displayName = memberId == m_currentUserId ? m_currentUserName : contactDisplayName(memberId);
                row.self = memberId == m_currentUserId;
                row.online = row.self || isContactOnline(memberId);
                row.friendRelation = m_friendIds.contains(memberId);
                row.pendingRelation = !row.friendRelation && m_pendingOutgoingFriendRequests.contains(memberId);
                row.role = memberId == ownerId ? QStringLiteral("群主") : (row.self ? QStringLiteral("我") : QStringLiteral("成员"));
                row.status = row.online ? QStringLiteral("在线") : QStringLiteral("离线");
                row.relation = row.self ? QStringLiteral("本人")
                                        : (row.friendRelation ? QStringLiteral("好友")
                                                              : (row.pendingRelation ? QStringLiteral("申请中") : QStringLiteral("可邀请/可申请")));
                row.preview = QStringLiteral("%1 · QQ:%2 · %3 · %4").arg(row.displayName, row.memberId, row.role, row.relation);
                row.removable = isCurrentUserGroupOwner(m_privateChatTarget) && memberId != ownerId;
                row.localGroup = true;
                appendRowIfMatches(row);
            }
            if (rows.isEmpty() && !filter.isEmpty()) {
                GroupMemberWorkspaceRow inviteRow;
                inviteRow.memberId = filter;
                inviteRow.displayName = filter;
                inviteRow.role = QStringLiteral("待邀请");
                inviteRow.status = QStringLiteral("未入群");
                inviteRow.relation = QStringLiteral("按 QQ 号邀请");
                inviteRow.preview = isCurrentUserGroupOwner(m_privateChatTarget)
                    ? QStringLiteral("双击或点击邀请即可把 QQ:%1 加入当前本地群。").arg(filter)
                    : QStringLiteral("只有群主可以按 QQ 号邀请新成员入群。");
                inviteRow.enabled = isCurrentUserGroupOwner(m_privateChatTarget);
                inviteRow.inviteCandidate = true;
                inviteRow.localGroup = true;
                appendRowIfMatches(inviteRow);
            }
        } else {
            QStringList members = m_serverGroupMembers.value(QStringLiteral("public"));
            if (members.isEmpty()) {
                members << m_currentUserId;
            }
            for (const QString& memberId : members) {
                GroupMemberWorkspaceRow row;
                row.memberId = memberId;
                row.displayName = memberId == m_currentUserId
                    ? m_currentUserName
                    : m_serverGroupMemberNames.value(QStringLiteral("public|") + memberId, contactDisplayName(memberId));
                row.self = memberId == m_currentUserId;
                row.online = row.self || isContactOnline(memberId);
                row.friendRelation = m_friendIds.contains(memberId);
                row.pendingRelation = !row.friendRelation && !row.self && m_pendingOutgoingFriendRequests.contains(memberId);
                row.role = memberGroupRoleText(memberId);
                row.status = row.online ? QStringLiteral("在线") : QStringLiteral("离线");
                row.relation = row.self ? QStringLiteral("本人")
                                        : (row.friendRelation ? QStringLiteral("好友")
                                                              : (row.pendingRelation ? QStringLiteral("申请中") : QStringLiteral("可申请")));
                row.preview = QStringLiteral("%1 · QQ:%2 · %3 · %4").arg(row.displayName, row.memberId, row.role, row.relation);
                row.removable = canCurrentUserManageServerGroup(QStringLiteral("public")) && memberId != ownerId && memberId != m_currentUserId;
                appendRowIfMatches(row);
            }
            if (rows.isEmpty() && !filter.isEmpty()) {
                GroupMemberWorkspaceRow searchRow;
                searchRow.memberId = filter;
                searchRow.displayName = filter;
                searchRow.role = isManagementEnabledForCurrentContext() ? QStringLiteral("待邀请") : QStringLiteral("待搜索");
                searchRow.status = QStringLiteral("未加入");
                searchRow.relation = isManagementEnabledForCurrentContext() ? QStringLiteral("公共群邀请") : QStringLiteral("好友申请");
                searchRow.preview = isManagementEnabledForCurrentContext()
                    ? QStringLiteral("当前账号可管理公共群，双击可提交 QQ:%1 的成员变更。").arg(filter)
                    : QStringLiteral("当前账号不可直接管理公共群，双击会搜索并申请 QQ:%1。").arg(filter);
                searchRow.enabled = true;
                searchRow.searchAddCandidate = true;
                appendRowIfMatches(searchRow);
            }
        }

        int onlineCount = 0;
        int friendCount = 0;
        int pendingCount = 0;
        for (const GroupMemberWorkspaceRow& row : rows) {
            QListWidgetItem* item = new QListWidgetItem(
                QStringLiteral("%1  QQ:%2\n%3 · %4 · %5").arg(row.role, row.memberId, row.displayName, row.status, row.relation));
            item->setData(Qt::UserRole, row.memberId);
            item->setData(Qt::UserRole + 1, row.displayName);
            item->setData(Qt::UserRole + 2, row.role);
            item->setData(Qt::UserRole + 3, row.status);
            item->setData(Qt::UserRole + 4, row.relation);
            item->setData(Qt::UserRole + 5, row.preview);
            item->setData(Qt::UserRole + 6, row.enabled);
            item->setData(Qt::UserRole + 7, row.self);
            item->setData(Qt::UserRole + 8, row.online);
            item->setData(Qt::UserRole + 9, row.friendRelation);
            item->setData(Qt::UserRole + 10, row.pendingRelation);
            item->setData(Qt::UserRole + 11, row.inviteCandidate);
            item->setData(Qt::UserRole + 12, row.searchAddCandidate);
            item->setData(Qt::UserRole + 13, row.removable);
            item->setData(Qt::UserRole + 14, row.localGroup);
            item->setSizeHint(QSize(0, row.inviteCandidate || row.searchAddCandidate ? 72 : 78));
            item->setToolTip(row.preview);
            if (!row.enabled) {
                item->setFlags(Qt::NoItemFlags);
                item->setForeground(QColor(135, 150, 165));
            } else if (row.self) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.friendRelation) {
                item->setForeground(QColor(20, 92, 160));
            } else if (row.pendingRelation) {
                item->setForeground(QColor(170, 110, 20));
            } else if (row.inviteCandidate || row.searchAddCandidate) {
                item->setForeground(QColor(13, 148, 136));
            }
            memberList->addItem(item);
            if (row.online) ++onlineCount;
            if (row.friendRelation) ++friendCount;
            if (row.pendingRelation) ++pendingCount;
        }

        const int visibleCount = rows.size();
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                memberList,
                QStringLiteral("没有匹配的成员入口"),
                QStringLiteral("试试 QQ、昵称、角色或关系关键词。"),
                filter.isEmpty()
                    ? QStringLiteral("当前没有可见的成员入口。可刷新成员列表、切换会话，或在这里输入 QQ / 昵称继续筛选。")
                    : QStringLiteral("当前筛选词“%1”没有匹配到成员入口。\n试试 QQ、昵称、角色或关系关键词。").arg(filter));
        }
        statsLabel->setText(QStringLiteral("可见 %1 · 在线 %2 · 好友 %3%4")
                                .arg(visibleCount)
                                .arg(onlineCount)
                                .arg(friendCount)
                                .arg(pendingCount > 0 ? QStringLiteral(" · 申请中 %1").arg(pendingCount) : QString()));
        subTitleLabel->setText(localGroupContext
                                   ? QStringLiteral("当前群：%1 · 群主 %2 · 可管理 %3")
                                         .arg(currentGroupName)
                                         .arg(ownerName)
                                         .arg(isCurrentUserGroupOwner(m_privateChatTarget) ? QStringLiteral("是") : QStringLiteral("否"))
                                   : (removedFromPublicGroup
                                          ? QStringLiteral("公共群历史只读 · 当前账号已移出")
                                          : QStringLiteral("公共聊天室 · 群主 %1 · 可管理 %2")
                                                .arg(ownerName)
                                                .arg(isManagementEnabledForCurrentContext() ? QStringLiteral("是") : QStringLiteral("否"))));
        if (memberList->count() > 0) {
            selectPreferredListRow(memberList, 0);
        }
    };

    auto selectedMemberId = [memberList]() {
        QListWidgetItem* item = memberList->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };

    auto updatePreview = [=]() {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            previewLabel->setText(firstEnabledListRow(memberList) >= 0
                                      ? QStringLiteral("选择成员后，这里会说明当前关系、角色、下一步可做什么。")
                                      : (searchEdit->text().trimmed().isEmpty()
                                             ? QStringLiteral("当前没有可见的成员入口。可切换群聊、刷新列表，或输入 QQ / 昵称继续筛选。")
                                             : QStringLiteral("当前筛选词“%1”没有匹配到成员入口。\n试试 QQ、昵称、角色或关系关键词。").arg(searchEdit->text().trimmed())));
            return;
        }
        previewLabel->setText(item->data(Qt::UserRole + 5).toString());
    };

    QPushButton* chatBtn = createWorkspaceButton(shell.headerFrame, &dialog, QStringLiteral("打开私聊"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("打开当前选中成员的私聊会话"), QStyle::SP_ArrowForward);
    QPushButton* refreshBtn = createWorkspaceButton(shell.headerFrame, &dialog, QStringLiteral("刷新成员"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("刷新成员列表和状态"), QStyle::SP_BrowserReload);
    QPushButton* clearFilterBtn = createWorkspaceButton(shell.headerFrame, &dialog, QStringLiteral("清空筛选"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("清空成员筛选"), QStyle::SP_DialogResetButton);
    shell.searchRowLayout->addWidget(chatBtn);
    shell.searchRowLayout->addWidget(refreshBtn);
    shell.searchRowLayout->addWidget(clearFilterBtn);

    QPushButton* inviteBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("邀请/加成员"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("把当前条目加入群，或对当前成员继续发起邀请动作"), QStyle::SP_FileDialogNewFolder);
    QPushButton* addFriendBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("发起好友申请"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("向当前成员发起好友申请"), QStyle::SP_CommandLink);
    QPushButton* remarkBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("设置备注"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("修改当前成员在本地的备注名"), QStyle::SP_FileDialogInfoView);
    QPushButton* removeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("移出群聊"), QStringLiteral("managerDangerBtn"), QStringLiteral("把当前成员移出群聊"), QStyle::SP_TrashIcon);
    QPushButton* copyMemberBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制成员卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前成员资料摘要"), QStyle::SP_DialogSaveButton);
    QPushButton* copyVisibleBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制可见成员"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前筛选后的可见成员"), QStyle::SP_FileDialogListView);
    QPushButton* copyOnlineBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制在线成员"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前筛选后的在线成员"), QStyle::SP_DialogYesButton);
    QPushButton* copyInviteTextBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制邀请语"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前成员或群聊邀请话术"), QStyle::SP_DirLinkIcon);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭群成员工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("成员动作"),
        QStringLiteral("先选中成员，再决定私聊、邀请、发起好友申请、备注或移出群聊。"),
        {inviteBtn, addFriendBtn, remarkBtn, removeBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("复制与摘要"),
        QStringLiteral("把成员资料、当前可见成员、在线成员和邀请话术整理出去。"),
        {copyMemberBtn, copyVisibleBtn, copyOnlineBtn, copyInviteTextBtn});

    auto copyVisibleMembers = [&, this](bool onlineOnly) {
        QStringList rows;
        for (int i = 0; i < memberList->count(); ++i) {
            QListWidgetItem* item = memberList->item(i);
            if (!item || item->data(Qt::UserRole).toString().isEmpty()) {
                continue;
            }
            const bool online = item->data(Qt::UserRole + 8).toBool();
            if (onlineOnly && !online) {
                continue;
            }
            rows << QStringLiteral("QQ:%1 昵称:%2 角色:%3 状态:%4 关系:%5")
                        .arg(item->data(Qt::UserRole).toString(),
                             item->data(Qt::UserRole + 1).toString(),
                             item->data(Qt::UserRole + 2).toString(),
                             item->data(Qt::UserRole + 3).toString(),
                             item->data(Qt::UserRole + 4).toString());
        }
        if (rows.isEmpty()) {
            ui->statusbar->showMessage(onlineOnly ? QStringLiteral("当前没有可复制的在线成员") : QStringLiteral("当前没有可复制的成员"), 2200);
            return;
        }
        copyTextWithStatus(rows.join(QLatin1Char('\n')),
                           onlineOnly ? QStringLiteral("在线成员已复制") : QStringLiteral("可见成员已复制"),
                           2200);
    };

    auto runInviteAction = [&, this]() {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            ui->statusbar->showMessage(QStringLiteral("请先选择一个成员或邀请条目"), 1800);
            return;
        }
        const QString memberId = item->data(Qt::UserRole).toString();
        if (memberId.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前没有可邀请的目标"), 1800);
            return;
        }
        handleGroupMemberEntryActivated(item->data(Qt::UserRole + 11).toBool()
                                            ? QStringLiteral("group_invite:%1").arg(memberId)
                                            : (item->data(Qt::UserRole + 12).toBool()
                                                   ? QStringLiteral("group_search_add:%1").arg(memberId)
                                                   : memberId),
                                        &dialog);
        fillMemberList();
        updatePreview();
    };

    auto runChatAction = [&, this]() {
        const QString memberId = selectedMemberId();
        if (memberId.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("请先选择一个成员"), 1800);
            return;
        }
        if (memberId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("这是你自己，无需打开私聊"), 1800);
            return;
        }
        dialog.accept();
        if (!m_friendIds.contains(memberId) && !m_pendingOutgoingFriendRequests.contains(memberId)) {
            ensureFriendRequestQueued(memberId, QStringLiteral("已向群成员发起好友申请 QQ:%1，等待对方同意"), true);
        }
        openPrivateSession(memberId);
    };

    auto runAddFriendAction = [&, this]() {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            ui->statusbar->showMessage(QStringLiteral("请先选择一个成员"), 1800);
            return;
        }
        const QString memberId = item->data(Qt::UserRole).toString();
        if (memberId.isEmpty() || memberId == m_currentUserId) {
            ui->statusbar->showMessage(QStringLiteral("当前目标不需要发起好友申请"), 1800);
            return;
        }
        if (m_friendIds.contains(memberId)) {
            ui->statusbar->showMessage(QStringLiteral("该成员已经是你的好友"), 1800);
            return;
        }
        if (m_pendingOutgoingFriendRequests.contains(memberId)) {
            ui->statusbar->showMessage(QStringLiteral("该成员好友申请已在等待确认"), 2200);
            return;
        }
        ensureFriendRequestQueued(memberId, QStringLiteral("已向群成员发起好友申请 QQ:%1，等待对方同意"), true);
        fillMemberList();
        updatePreview();
    };

    auto updateActionState = [=]() {
        QListWidgetItem* item = memberList->currentItem();
        const bool hasItem = item != nullptr;
        const bool enabled = hasItem && item->data(Qt::UserRole + 6).toBool();
        const bool self = hasItem && item->data(Qt::UserRole + 7).toBool();
        const bool friendRelation = hasItem && item->data(Qt::UserRole + 9).toBool();
        const bool pendingRelation = hasItem && item->data(Qt::UserRole + 10).toBool();
        const bool inviteCandidate = hasItem && item->data(Qt::UserRole + 11).toBool();
        const bool searchAddCandidate = hasItem && item->data(Qt::UserRole + 12).toBool();
        const bool removable = hasItem && item->data(Qt::UserRole + 13).toBool();
        const QString memberId = hasItem ? item->data(Qt::UserRole).toString() : QString();
        const QString memberName = hasItem ? item->data(Qt::UserRole + 1).toString() : QString();

        chatBtn->setText(QStringLiteral("打开私聊"));
        chatBtn->setEnabled(enabled && !self && !inviteCandidate && !searchAddCandidate);
        chatBtn->setToolTip(chatBtn->isEnabled()
                                ? QStringLiteral("打开 %1 的私聊会话").arg(memberName)
                                : QStringLiteral("选择真实成员后可打开私聊"));
        inviteBtn->setText(inviteCandidate || searchAddCandidate ? QStringLiteral("处理当前条目") : QStringLiteral("邀请/加成员"));
        inviteBtn->setEnabled(enabled && !self);
        inviteBtn->setToolTip(localGroupContext
                                  ? (isCurrentUserGroupOwner(m_privateChatTarget)
                                         ? QStringLiteral("邀请成员加入当前本地群聊")
                                         : QStringLiteral("只有群主可以邀请新成员入群"))
                                  : (removedFromPublicGroup
                                         ? QStringLiteral("公共群历史只读，无法邀请成员")
                                         : (canCurrentUserManageServerGroup(QStringLiteral("public"))
                                                ? QStringLiteral("提交公共群成员变更")
                                                : QStringLiteral("搜索成员或发起好友申请"))));
        addFriendBtn->setText(QStringLiteral("发起好友申请"));
        addFriendBtn->setEnabled(enabled && !self && !friendRelation && !pendingRelation && !inviteCandidate);
        addFriendBtn->setToolTip(addFriendBtn->isEnabled()
                                     ? QStringLiteral("向 %1 发起好友申请").arg(memberId)
                                     : QStringLiteral("当前成员不需要重复发起好友申请"));
        remarkBtn->setText(QStringLiteral("设置备注"));
        remarkBtn->setEnabled(enabled && !inviteCandidate && !searchAddCandidate);
        removeBtn->setText(QStringLiteral("移出群聊"));
        removeBtn->setEnabled(removable);
        copyMemberBtn->setEnabled(enabled);
        copyInviteTextBtn->setEnabled(enabled);
        copyVisibleBtn->setEnabled(memberList->count() > 0 && firstEnabledListRow(memberList) >= 0);
        copyOnlineBtn->setEnabled(memberList->count() > 0 && firstEnabledListRow(memberList) >= 0);
    };

    auto runRemarkAction = [&, this]() {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            ui->statusbar->showMessage(QStringLiteral("请先选择一个成员"), 1800);
            return;
        }
        if (promptAndSetGroupMemberRemark(item->data(Qt::UserRole).toString(), &dialog)) {
            fillMemberList();
            updatePreview();
            updateActionState();
        }
    };

    auto runRemoveAction = [&, this]() {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            ui->statusbar->showMessage(QStringLiteral("请先选择要移出的成员"), 1800);
            return;
        }
        if (!item->data(Qt::UserRole + 13).toBool()) {
            ui->statusbar->showMessage(QStringLiteral("当前成员不可从群聊移除"), 2200);
            return;
        }
        if (removeGroupMemberWithConfirmation(item->data(Qt::UserRole).toString(), &dialog)) {
            fillMemberList();
            updatePreview();
            updateActionState();
        }
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=]() {
        fillMemberList();
        updatePreview();
        updateActionState();
    });
    connect(memberList, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(memberList, &QListWidget::itemDoubleClicked, &dialog, [=](QListWidgetItem*) {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            return;
        }
        if (item->data(Qt::UserRole + 11).toBool() || item->data(Qt::UserRole + 12).toBool()) {
            runInviteAction();
        } else {
            runChatAction();
        }
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, [=]() {
        QListWidgetItem* item = memberList->currentItem();
        if (item && (item->data(Qt::UserRole + 11).toBool() || item->data(Qt::UserRole + 12).toBool())) {
            runInviteAction();
        } else {
            runChatAction();
        }
    });
    connect(chatBtn, &QPushButton::clicked, &dialog, runChatAction);
    connect(refreshBtn, &QPushButton::clicked, &dialog, [=]() {
        refreshGroupMemberPanel();
        fillMemberList();
        updatePreview();
        updateActionState();
        ui->statusbar->showMessage(QStringLiteral("群成员工作区已刷新"), 1800);
    });
    connect(clearFilterBtn, &QPushButton::clicked, &dialog, [=]() {
        searchEdit->clear();
        searchEdit->setFocus();
    });
    connect(inviteBtn, &QPushButton::clicked, &dialog, runInviteAction);
    connect(addFriendBtn, &QPushButton::clicked, &dialog, runAddFriendAction);
    connect(remarkBtn, &QPushButton::clicked, &dialog, runRemarkAction);
    connect(removeBtn, &QPushButton::clicked, &dialog, runRemoveAction);
    connect(copyMemberBtn, &QPushButton::clicked, &dialog, [=, this]() {
        QListWidgetItem* item = memberList->currentItem();
        if (!item) {
            ui->statusbar->showMessage(QStringLiteral("请先选择一个成员"), 1800);
            return;
        }
        const QString text = QStringLiteral("QQ:%1\n昵称:%2\n角色:%3\n状态:%4\n关系:%5")
            .arg(item->data(Qt::UserRole).toString(),
                 item->data(Qt::UserRole + 1).toString(),
                 item->data(Qt::UserRole + 2).toString(),
                 item->data(Qt::UserRole + 3).toString(),
                 item->data(Qt::UserRole + 4).toString());
        copyTextWithStatus(text, QStringLiteral("成员卡片已复制"), 2200);
    });
    connect(copyVisibleBtn, &QPushButton::clicked, &dialog, [=]() { copyVisibleMembers(false); });
    connect(copyOnlineBtn, &QPushButton::clicked, &dialog, [=]() { copyVisibleMembers(true); });
    connect(copyInviteTextBtn, &QPushButton::clicked, &dialog, [=, this]() {
        QListWidgetItem* item = memberList->currentItem();
        QString memberId = item ? item->data(Qt::UserRole).toString() : QString();
        QString memberName = item ? item->data(Qt::UserRole + 1).toString() : QStringLiteral("朋友");
        QString inviteText = localGroupContext
            ? QStringLiteral("我邀请你加入群聊“%1”。我是 %2（QQ:%3），进群后我们可以继续沟通。")
                  .arg(currentGroupName, m_currentUserName, m_currentUserId)
            : QStringLiteral("%1，你好。我是 %2（QQ:%3），欢迎加入公共聊天室，也可以先加我好友继续沟通。")
                  .arg(memberName, m_currentUserName, m_currentUserId);
        if (!memberId.isEmpty() && !memberName.isEmpty() && !localGroupContext) {
            inviteText = QStringLiteral("%1，你好。我是 %2（QQ:%3），方便的话可以先通过好友申请，我们再继续沟通。")
                .arg(memberName, m_currentUserName, m_currentUserId);
        }
        copyTextWithStatus(inviteText, QStringLiteral("成员邀请语已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    const QString initialSearchText = initialFilter.trimmed();
    if (!initialSearchText.isEmpty()) {
        searchEdit->setText(initialSearchText);
    }
    fillMemberList();
    updatePreview();
    updateActionState();
    searchEdit->setFocus();
    dialog.setStyleSheet(productDialogStyleSheet());
    dialog.exec();
}

void MainWindow::switchToLocalGroup(const QString& groupId, const QString& groupName) {
    m_privateChatTarget = groupId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory(groupId);
    setWindowTitle(appWindowTitle(QString("群聊: %1").arg(groupName)));
    ui->chatTitleLabel->setText(groupName);
    const QString ownerId = groupOwnerId(groupId);
    const bool isOwner = isCurrentUserGroupOwner(groupId);
    ui->chatHintLabel->setText(ChatSessionManager::localGroupHint(groupId, ownerId, isOwner));
    ui->announcementTitleLabel->setText(isOwner ? "群公告 <a href=\"edit\">编辑</a>" : "群公告");
    ui->announcementBodyLabel->setText(m_localGroupAnnouncements.value(groupId, QString("%1 已创建，可继续邀请好友并发送消息。").arg(groupName)));
    refreshGroupMemberPanel();
    refreshComposerState();
}

void MainWindow::onEditGroupAnnouncement() {
    showEditGroupAnnouncementWorkspace(this);
}

void MainWindow::onInsertEmoji() {
    QMenu menu(this);
    const QStringList emojis = {"😀", "😂", "😊", "😍", "😎", "😭", "👍", "🎉", "❤️", "🔥", "👏", "🙏", "💪", "🤝", "📌", "📎"};
    for (const QString& emoji : emojis) {
        QAction* action = addMenuActionWithIcon(menu,
                                                this,
                                                emoji,
                                                QStringLiteral("把这个表情插入当前输入框"),
                                                QString(),
                                                true,
                                                QStyle::SP_DialogApplyButton);
        connect(action, &QAction::triggered, this, [this, emoji]() {
            ui->messageEdit->insertPlainText(emoji);
            ui->messageEdit->setFocus();
            ui->statusbar->showMessage(QString("已插入表情 %1").arg(emoji), 1400);
        });
    }
    menu.addSeparator();
    QMenu* commandMenu = menu.addMenu("QQ快捷指令");
    const QList<ChatContextComposerMenuAction> composerActions = ChatContextManager::composerMenuActions();
    for (const ChatContextComposerMenuAction& spec : composerActions) {
        QAction* action = addMenuActionWithIcon(*commandMenu,
                                                this,
                                                spec.title,
                                                spec.toolTip,
                                                QString(),
                                                true,
                                                QStyle::SP_ArrowForward);
        connect(action, &QAction::triggered, this, [this, commandId = spec.commandId]() {
            applyChatContextComposerCommand(commandId);
        });
    }

    const QList<ChatContextPhraseMenuPlan> phrasePlans = ChatContextManager::composerPhraseMenuPlans();
    for (const ChatContextPhraseMenuPlan& plan : phrasePlans) {
        QMenu* phraseMenu = menu.addMenu(plan.title);
        for (const QString& phrase : plan.phrases) {
            QAction* action = addMenuActionWithIcon(*phraseMenu,
                                                    this,
                                                    phrase,
                                                    plan.insertedStatusMessage,
                                                    QString(),
                                                    true,
                                                    QStyle::SP_FileDialogDetailedView);
            connect(action, &QAction::triggered, this, [this, phrase, plan]() {
                setChatDraftText(phrase, plan.insertedStatusMessage, 1400);
            });
        }
    }
    menu.exec(ui->emojiBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void MainWindow::onInsertMention() {
    QMenu menu(this);
    QStringList mentionIds;
    QMap<QString, QString> mentionNames;
    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        mentionIds = m_localGroupMembers.value(m_privateChatTarget);
    } else {
        for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
            mentionIds << it.key();
            mentionNames[it.key()] = it.value().name;
        }
    }

    for (const QString& memberId : mentionIds) {
        if (!mentionNames.contains(memberId)) {
            mentionNames[memberId] = memberId == m_currentUserId ? m_currentUserName : contactDisplayName(memberId);
        }
    }

    const ComposerMentionMenuPlan mentionPlan =
        ComposerManager::mentionMenuPlan(mentionIds, m_currentUserId, mentionNames);
    for (int i = 0; i < mentionPlan.actions.size(); ++i) {
        if (i == 1 && mentionPlan.separatorAfterAll) {
            menu.addSeparator();
        }
        const ComposerMentionAction actionPlan = mentionPlan.actions.at(i);
        QAction* action = addMenuActionWithIcon(menu,
                                                this,
                                                actionPlan.title,
                                                actionPlan.statusMessage,
                                                QString(),
                                                true,
                                                QStyle::SP_ArrowRight);
        connect(action, &QAction::triggered, this, [this, actionPlan]() {
            insertChatDraftText(actionPlan.insertText, actionPlan.statusMessage, 1400);
        });
    }
    menu.exec(ui->mentionBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

bool MainWindow::openUserTargetById(const QString& targetId) {
    if (targetId.isEmpty()) {
        return false;
    }
    if (targetId.startsWith(QStringLiteral("search_add:"))) {
        searchAndAddAccount(targetId.mid(QStringLiteral("search_add:").size()), this);
        return true;
    }
    if (targetId.startsWith(QStringLiteral("create_group:"))) {
        QString groupName = targetId.mid(QStringLiteral("create_group:").size()).trimmed();
        if (groupName.isEmpty()) {
            groupName = QStringLiteral("我的群聊");
        }
        const QString groupId = QStringLiteral("local_group_") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz"));
        m_localGroupIds << groupId;
        m_localGroupNames[groupId] = groupName;
        m_localGroupAnnouncements[groupId] = QStringLiteral("%1 已创建，可继续邀请好友并发送消息。").arg(groupName);
        m_localGroupMembers[groupId] = QStringList{m_currentUserId};
        saveLocalGroups();
        m_contactFilter.clear();
        ui->contactSearchEdit->clear();
        refreshFriendList();
        switchToLocalGroup(groupId, groupName);
        appendSystemMessage(QStringLiteral("已从联系人搜索创建群聊: ") + groupName);
        return true;
    }
    if (m_localGroupIds.contains(targetId)) {
        switchToLocalGroup(targetId, m_localGroupNames.value(targetId, QStringLiteral("群聊")));
        return true;
    }

    const QString userName = contactDisplayName(targetId);
    m_privateChatTarget = targetId;
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({QStringLiteral("聊天记录")});
    loadHistory(targetId);
    const PrivateChatUiState privateState = ChatSessionManager::privateChatState(
        targetId,
        userName,
        isContactOnline(targetId),
        m_client && m_client->hasE2ESession(targetId),
        m_client && m_client->e2eSessionNeedsRotation(targetId));
    setWindowTitle(appWindowTitle(privateState.windowSuffix));
    ui->chatTitleLabel->setText(privateState.titleText);
    ui->chatHintLabel->setText(privateState.hintText);
    refreshComposerState();
    return true;
}

void MainWindow::showProfileWorkspace() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("profileWorkspaceDialog"),
        QStringLiteral("账号工作区"),
        QSize(880, 720),
        QStringLiteral("managerTitle"),
        QStringLiteral("账号工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把当前账号、头像、搜索与好友整理入口放到一个统一工作面里。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索账号动作"),
        QStringLiteral("按账号、头像、搜索和好友整理动作筛选"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一个账号动作，再决定复制摘要、打开头像工作区、进入综合搜索或联系人工作区。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释当前账号动作会如何影响头像、联系人搜索和会话入口。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* listWidget = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    struct ProfileWorkspaceRow {
        QString id;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool accent = false;
    };

    auto buildRows = [this]() {
        QList<ProfileWorkspaceRow> rows;
        const QFileInfo avatarInfo(getAvatarFilePath());

        ProfileWorkspaceRow overview;
        overview.id = QStringLiteral("profile-overview");
        overview.title = QStringLiteral("当前账号 · %1").arg(m_currentUserName);
        overview.detail = QStringLiteral("QQ:%1 · 好友 %2 · 群聊 %3").arg(m_currentUserId).arg(m_friendIds.size()).arg(m_localGroupIds.size());
        overview.preview = QStringLiteral("账号总览\nQQ：%1\n昵称：%2\n好友：%3\n群聊：%4\n当前会话：%5")
                               .arg(m_currentUserId,
                                    m_currentUserName,
                                    QString::number(m_friendIds.size()),
                                    QString::number(m_localGroupIds.size()),
                                    m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        overview.keywords = overview.title + overview.detail + overview.preview + QStringLiteral("账号 总览");
        overview.accent = true;
        rows << overview;

        ProfileWorkspaceRow avatar;
        avatar.id = QStringLiteral("profile-avatar");
        avatar.title = QStringLiteral("头像工作区");
        avatar.detail = avatarInfo.exists()
            ? QStringLiteral("%1 · %2").arg(avatarInfo.fileName(), LocalFileManager::humanFileSize(avatarInfo.size()))
            : QStringLiteral("当前使用默认头像");
        avatar.preview = avatarInfo.exists()
            ? QStringLiteral("头像工作区\n当前头像：%1\n路径：%2\n可继续：更换头像、复制路径、打开目录")
                  .arg(avatarInfo.fileName(), avatarInfo.absoluteFilePath())
            : QStringLiteral("头像工作区\n当前使用默认头像\n可继续：更换头像、保存本地头像后再复制路径或打开目录");
        avatar.keywords = avatar.title + avatar.detail + avatar.preview + QStringLiteral("头像 路径 目录");
        rows << avatar;

        ProfileWorkspaceRow search;
        search.id = QStringLiteral("profile-search");
        search.title = QStringLiteral("综合搜索");
        search.detail = QStringLiteral("统一搜索 QQ、好友和群聊");
        search.preview = QStringLiteral("综合搜索工作区\n可从账号侧直接跳去查找 QQ、好友与群聊，并整理搜索摘要。");
        search.keywords = search.title + search.detail + search.preview + QStringLiteral("搜索 QQ 好友 群聊");
        rows << search;

        ProfileWorkspaceRow contacts;
        contacts.id = QStringLiteral("profile-contacts");
        contacts.title = QStringLiteral("联系人工作区");
        contacts.detail = QStringLiteral("整理联系人、搜索结果和群聊入口");
        contacts.preview = QStringLiteral("联系人工作区\n可查看联系人、群聊、搜索结果，并继续进入会话或发起好友申请。");
        contacts.keywords = contacts.title + contacts.detail + contacts.preview + QStringLiteral("联系人 群聊");
        rows << contacts;

        ProfileWorkspaceRow friendManager;
        friendManager.id = QStringLiteral("profile-friend-manager");
        friendManager.title = QStringLiteral("好友管理器");
        friendManager.detail = QStringLiteral("备注、邀请、删除和批量复制");
        friendManager.preview = QStringLiteral("好友管理器\n统一处理好友搜索、备注、入群邀请、删除和媒体准备。");
        friendManager.keywords = friendManager.title + friendManager.detail + friendManager.preview + QStringLiteral("好友 管理");
        rows << friendManager;

        ProfileWorkspaceRow quickAdd;
        quickAdd.id = QStringLiteral("profile-quick-add");
        quickAdd.title = QStringLiteral("好友申请工作区");
        quickAdd.detail = QStringLiteral("搜索 QQ、发送申请和整理申请话术");
        quickAdd.preview = QStringLiteral("好友申请工作区\n可搜索 QQ、批量处理推荐对象，并复制申请话术、媒体包和清单。");
        quickAdd.keywords = quickAdd.title + quickAdd.detail + quickAdd.preview + QStringLiteral("好友 申请 搜索 QQ");
        rows << quickAdd;

        return rows;
    };

    auto rows = buildRows();
    auto emptyPreviewText = [searchEdit]() {
        const QString filter = searchEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的账号入口。可从这里继续进入头像、联系人、搜索和好友整理工作区。")
            : QStringLiteral("当前筛选词“%1”没有匹配到账号动作。\n试试“头像”、“搜索”、“联系人”或“好友”这些关键词。").arg(filter);
    };

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const ProfileWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !row.keywords.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.id);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (row.accent) {
                item->setForeground(QColor(29, 78, 216));
            }
            listWidget->addItem(item);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                listWidget,
                QStringLiteral("没有匹配的账号动作"),
                QStringLiteral("试试“头像”、“搜索”、“联系人”或“好友”这些关键词。"),
                emptyPreviewText());
        }
        statsLabel->setText(QStringLiteral("可见 %1 / %2 项").arg(visibleCount).arg(rows.size()));
        selectPreferredListRow(listWidget, 0);
    };

    auto selectedRow = [=, &rows]() -> ProfileWorkspaceRow* {
        QListWidgetItem* currentItem = listWidget->currentItem();
        if (!currentItem) {
            return nullptr;
        }
        const QString id = currentItem->data(Qt::UserRole).toString();
        for (ProfileWorkspaceRow& row : rows) {
            if (row.id == id) {
                return &row;
            }
        }
        return nullptr;
    };

    auto updatePreview = [=, &rows]() {
        ProfileWorkspaceRow* row = selectedRow();
        previewLabel->setText(row ? row->preview
                                  : (firstEnabledListRow(listWidget) >= 0
                                         ? QStringLiteral("这里会解释当前账号动作会如何影响头像、联系人搜索和会话入口。")
                                         : emptyPreviewText()));
    };

    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("执行当前动作"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("执行当前账号动作"), QStyle::SP_ArrowForward);
    QPushButton* copyCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制账号卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前账号资料摘要"), QStyle::SP_DialogSaveButton);
    QPushButton* copyStatusBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区状态"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制账号工作区当前状态"), QStyle::SP_MessageBoxInformation);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭账号工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("账号动作"),
        QStringLiteral("从这里统一进入头像、搜索、联系人与好友整理入口。"),
        {openBtn, copyCardBtn, copyStatusBtn},
        closeBtn);

    auto profileCardText = [this]() {
        return QStringLiteral("账号资料卡\nQQ:%1\n昵称:%2\n好友:%3\n群聊:%4\n当前会话:%5")
            .arg(m_currentUserId,
                 m_currentUserName,
                 QString::number(m_friendIds.size()),
                 QString::number(m_localGroupIds.size()),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
    };
    auto profileStatusText = [this]() {
        const QStringList lines = {
            QStringLiteral("账号工作区状态"),
            QStringLiteral("QQ:%1").arg(m_currentUserId),
            QStringLiteral("昵称:%1").arg(m_currentUserName),
            QStringLiteral("好友:%1").arg(m_friendIds.size()),
            QStringLiteral("群聊:%1").arg(m_localGroupIds.size()),
            QStringLiteral("当前会话:%1").arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget))
        };
        return lines.join(QLatin1Char('\n'));
    };
    auto profileClipboardText = [=, &rows]() {
        ProfileWorkspaceRow* row = selectedRow();
        if (!row) {
            return profileStatusText();
        }
        if (row->id == QLatin1String("profile-overview")) {
            return profileCardText();
        }
        return row->preview.trimmed().isEmpty() ? profileStatusText() : row->preview;
    };

    auto updateActionState = [=, &rows]() {
        ProfileWorkspaceRow* row = selectedRow();
        const bool hasActionableRow = hasEnabledListRow(listWidget);
        if (!row) {
            openBtn->setEnabled(false);
            openBtn->setText(QStringLiteral("执行当前动作"));
            openBtn->setToolTip(hasActionableRow
                                    ? QStringLiteral("先选择一个账号动作后继续进入头像、搜索、联系人或好友整理入口")
                                    : QStringLiteral("当前没有可执行的账号动作"));
            copyCardBtn->setEnabled(hasActionableRow);
            copyCardBtn->setToolTip(hasActionableRow
                                        ? QStringLiteral("复制当前账号工作区总览")
                                        : QStringLiteral("当前没有可复制的账号摘要"));
            copyStatusBtn->setEnabled(true);
            copyStatusBtn->setToolTip(QStringLiteral("复制账号工作区当前状态"));
            return;
        }

        openBtn->setEnabled(true);
        if (row->id == QLatin1String("profile-overview") || row->id == QLatin1String("profile-avatar")) {
            openBtn->setText(QStringLiteral("打开头像工作区"));
            openBtn->setToolTip(row->id == QLatin1String("profile-overview")
                                    ? QStringLiteral("从当前账号总览继续进入头像工作区")
                                    : QStringLiteral("打开头像工作区并继续整理头像相关动作"));
        } else if (row->id == QLatin1String("profile-search")) {
            openBtn->setText(QStringLiteral("打开综合搜索"));
            openBtn->setToolTip(QStringLiteral("进入综合搜索工作区"));
        } else if (row->id == QLatin1String("profile-contacts")) {
            openBtn->setText(QStringLiteral("打开联系人工作区"));
            openBtn->setToolTip(QStringLiteral("进入联系人工作区"));
        } else if (row->id == QLatin1String("profile-friend-manager")) {
            openBtn->setText(QStringLiteral("打开好友管理"));
            openBtn->setToolTip(QStringLiteral("进入好友管理器"));
        } else {
            openBtn->setText(QStringLiteral("好友申请工作区"));
            openBtn->setToolTip(QStringLiteral("进入好友申请工作区"));
        }
        copyCardBtn->setEnabled(true);
        copyCardBtn->setToolTip(row->id == QLatin1String("profile-overview")
                                    ? QStringLiteral("复制当前账号资料卡")
                                    : QStringLiteral("复制当前选中账号动作卡"));
        copyStatusBtn->setEnabled(true);
        copyStatusBtn->setToolTip(QStringLiteral("复制账号工作区当前状态"));
    };

    auto runOpenSelected = [=, &rows, this, &dialog]() {
        ProfileWorkspaceRow* row = selectedRow();
        if (!row) {
            return;
        }
        if (row->id == QStringLiteral("profile-overview") || row->id == QStringLiteral("profile-avatar")) {
            dialog.accept();
            showAvatarWorkspace();
            return;
        }
        if (row->id == QStringLiteral("profile-quick-add")) {
            dialog.accept();
            onShowQuickAddFriend();
            return;
        }
        dialog.accept();
        if (row->id == QStringLiteral("profile-search")) {
            onShowGlobalSearch();
        } else if (row->id == QStringLiteral("profile-contacts")) {
            onShowContactWorkspace();
        } else if (row->id == QStringLiteral("profile-friend-manager")) {
            onShowFriendManager();
        }
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, [runOpenSelected](QListWidgetItem*) {
        runOpenSelected();
    });
    connect(openBtn, &QPushButton::clicked, &dialog, runOpenSelected);
    connect(copyCardBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        ProfileWorkspaceRow* row = selectedRow();
        copyTextWithStatus(profileClipboardText(),
                           row && row->id == QLatin1String("profile-overview")
                               ? QStringLiteral("账号资料卡已复制")
                               : QStringLiteral("账号工作区卡片已复制"),
                           2200);
    });
    connect(copyStatusBtn, &QPushButton::clicked, &dialog, [=, this]() {
        copyTextWithStatus(profileStatusText(), QStringLiteral("账号工作区状态已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(QStringLiteral("当前账号：%1 · 好友 %2 · 群聊 %3").arg(m_currentUserId).arg(m_friendIds.size()).arg(m_localGroupIds.size()));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::showUserEntryWorkspace(const QString& targetId, const QString& fallbackLabel) {
    if (targetId.isEmpty()) {
        return;
    }

    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("userEntryWorkspaceDialog"),
        QStringLiteral("对象工作区"),
        QSize(940, 760),
        QStringLiteral("managerTitle"),
        QStringLiteral("对象工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把联系人或群聊的打开、复制、加密和管理动作集中到一个稳定工作面里。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索对象动作"),
        QStringLiteral("按打开、复制、加密、好友或群聊管理动作筛选"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选一个动作，再决定进入会话、复制资料、处理加密状态，或继续做好友/群聊管理。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释当前对象动作会如何影响会话、联系人关系或群聊成员。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* listWidget = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    const bool isLocalGroup = m_localGroupIds.contains(targetId);
    const QString displayName = fallbackLabel.isEmpty() ? contactDisplayName(targetId) : fallbackLabel;

    struct UserEntryActionRow {
        QString commandId;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool accent = false;
        bool dangerous = false;
    };

    auto buildRows = [=, this]() {
        QList<UserEntryActionRow> rows;
        if (isLocalGroup) {
            const QString groupName = m_localGroupNames.value(targetId, displayName);
            const int memberCount = m_localGroupMembers.value(targetId).size();
            const QString groupNumber = targetId.mid(QStringLiteral("local_group_").size());
            rows << UserEntryActionRow{
                QStringLiteral("open-group"),
                QStringLiteral("进入群聊"),
                QStringLiteral("打开 %1").arg(groupName),
                QStringLiteral("群聊\n群名：%1\n群号：%2\n成员：%3\n公告：%4")
                    .arg(groupName,
                         groupNumber,
                         QString::number(memberCount),
                         m_localGroupAnnouncements.value(targetId, QStringLiteral("暂无公告"))),
                QStringLiteral("进入 群聊 打开"),
                true,
                false};
            rows << UserEntryActionRow{QStringLiteral("copy-group-card"), QStringLiteral("复制群名片"), QStringLiteral("复制群号、群名、成员和公告摘要"),
                                       QStringLiteral("复制群名片\n群名：%1\n群号：%2").arg(groupName, groupNumber),
                                       QStringLiteral("复制 群 名片 公告")};
            rows << UserEntryActionRow{QStringLiteral("copy-group-members"), QStringLiteral("复制成员列表"), QStringLiteral("复制当前群聊全部成员"),
                                       QStringLiteral("复制成员列表\n可用于协作同步、入群校对和状态留档。"),
                                       QStringLiteral("复制 成员 列表")};
            rows << UserEntryActionRow{QStringLiteral("invite-friend"), QStringLiteral("邀请好友"), QStringLiteral("从好友里选择成员加入当前群聊"),
                                       QStringLiteral("邀请好友\n从现有好友列表选择对象加入当前群聊。"),
                                       QStringLiteral("邀请 好友 入群")};
            rows << UserEntryActionRow{QStringLiteral("invite-by-account"), QStringLiteral("按QQ号邀请"), QStringLiteral("输入 QQ 号邀请成员入群"),
                                       QStringLiteral("按 QQ 号邀请\n可直接输入 QQ，并按需补发好友申请。"),
                                       QStringLiteral("QQ 邀请 入群")};
            rows << UserEntryActionRow{QStringLiteral("rename-group"), QStringLiteral("重命名群聊"), QStringLiteral("修改当前本地群聊名称"),
                                       QStringLiteral("重命名群聊\n会同步更新左侧群聊显示和后续会话标题。"),
                                       QStringLiteral("重命名 群聊")};
            rows << UserEntryActionRow{QStringLiteral("delete-group"), QStringLiteral("删除群聊"), QStringLiteral("删除本地群聊配置，聊天记录不会在此步骤删除"),
                                       QStringLiteral("删除群聊\n会移除本地群聊配置；历史聊天不会在此步骤删除。"),
                                       QStringLiteral("删除 群聊"), false, true};
            return rows;
        }

        const bool online = isContactOnline(targetId);
        const bool isFriend = m_friendIds.contains(targetId);
        const bool pending = m_pendingOutgoingFriendRequests.contains(targetId);
        const QString relation = isFriend ? QStringLiteral("好友") : (pending ? QStringLiteral("申请中") : QStringLiteral("联系人"));
        rows << UserEntryActionRow{
            QStringLiteral("chat"),
            QStringLiteral("打开私聊"),
            QStringLiteral("进入与 %1 的会话").arg(displayName),
            QStringLiteral("联系人\nQQ：%1\n昵称：%2\n状态：%3\n关系：%4")
                .arg(targetId,
                     displayName,
                     online ? QStringLiteral("在线") : QStringLiteral("离线"),
                     relation),
            QStringLiteral("私聊 打开 会话"),
            true,
            false};
        rows << UserEntryActionRow{QStringLiteral("copy-profile-card"), QStringLiteral("复制名片"), QStringLiteral("复制当前联系人资料卡"),
                                   QStringLiteral("复制名片\nQQ：%1\n昵称：%2\n状态：%3").arg(targetId, displayName, online ? QStringLiteral("在线") : QStringLiteral("离线")),
                                   QStringLiteral("复制 名片 资料")};
        rows << UserEntryActionRow{QStringLiteral("copy-chat-starter"), QStringLiteral("复制开聊话术"), QStringLiteral("复制适合当前关系状态的话术"),
                                   QStringLiteral("开聊话术\n会根据当前是好友、申请中还是陌生联系人生成不同的话术。"),
                                   QStringLiteral("复制 话术 开聊")};
        rows << UserEntryActionRow{QStringLiteral("copy-e2e-status"), QStringLiteral("复制加密状态"), QStringLiteral("复制当前联系人端到端加密状态"),
                                   QStringLiteral("端到端加密状态\n可复制当前会话 readiness、轮换状态和信任概览。"),
                                   QStringLiteral("加密 状态 e2e")};
        rows << UserEntryActionRow{QStringLiteral("copy-e2e-identity"), QStringLiteral("复制加密身份"), QStringLiteral("复制身份指纹与验证信息"),
                                   QStringLiteral("端到端加密身份\n可复制指纹、验证短码、首次/最近看到时间。"),
                                   QStringLiteral("加密 身份 指纹")};
        rows << UserEntryActionRow{QStringLiteral("verify-e2e-identity"), QStringLiteral("验证并信任加密身份"), QStringLiteral("输入验证短码并固定信任"),
                                   QStringLiteral("验证加密身份\n需要通过可信渠道核对短码，再将该身份固定为已验证。"),
                                   QStringLiteral("验证 信任 加密 短码")};
        if (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))) {
            rows << UserEntryActionRow{QStringLiteral("invite-current-group"), QStringLiteral("邀入当前群"), QStringLiteral("把当前联系人加入正在查看的群聊"),
                                       QStringLiteral("邀入当前群\n会补齐好友申请状态并把该联系人加入当前本地群聊。"),
                                       QStringLiteral("邀请 当前群 入群")};
        }
        if (isFriend) {
            rows << UserEntryActionRow{QStringLiteral("rename-friend"), QStringLiteral("设置备注"), QStringLiteral("修改当前好友本地备注"),
                                       QStringLiteral("设置备注\n会影响左侧联系人显示、工作区摘要和后续会话标题。"),
                                       QStringLiteral("备注 好友")};
            rows << UserEntryActionRow{QStringLiteral("remove-friend"), QStringLiteral("删除好友"), QStringLiteral("从本地好友列表删除当前好友"),
                                       QStringLiteral("删除好友\n删除后仍可重新搜索并申请。"),
                                       QStringLiteral("删除 好友"), false, true};
        } else {
            rows << UserEntryActionRow{QStringLiteral("add-friend"),
                                       pending ? QStringLiteral("好友申请待确认") : QStringLiteral("加为好友"),
                                       pending ? QStringLiteral("当前好友申请已发送，等待处理")
                                               : QStringLiteral("向当前联系人发起好友申请"),
                                       pending ? QStringLiteral("好友申请已发送\n等待对方确认后可继续私聊。")
                                               : QStringLiteral("发起好友申请\n当前联系人在线时会发起好友申请，后续可继续私聊或邀入群。"),
                                       QStringLiteral("好友 申请 添加")};
        }
        return rows;
    };

    auto rows = buildRows();
    auto emptyPreviewText = [searchEdit, isLocalGroup]() {
        const QString filter = searchEdit->text().trimmed();
        const QString scope = isLocalGroup ? QStringLiteral("群对象") : QStringLiteral("联系人对象");
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的%1动作。可调整筛选后继续进入会话、复制资料或处理管理动作。").arg(scope)
            : QStringLiteral("当前筛选词“%1”没有匹配到%2动作。\n试试“复制”、“加密”、“邀请”或“管理”这些关键词。")
                  .arg(filter, scope);
    };

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const UserEntryActionRow& row : rows) {
            if (!filter.isEmpty() && !(row.title + row.detail + row.preview + row.keywords).contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.commandId);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (row.accent) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.dangerous) {
                item->setForeground(QColor(180, 35, 24));
            }
            listWidget->addItem(item);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                listWidget,
                QStringLiteral("没有匹配的对象动作"),
                QStringLiteral("试试“复制”、“加密”、“邀请”或“管理”这些关键词。"),
                emptyPreviewText());
        }
        statsLabel->setText(QStringLiteral("可见 %1 / %2 项").arg(visibleCount).arg(rows.size()));
        selectPreferredListRow(listWidget, 0);
    };

    auto selectedRow = [=, &rows]() -> UserEntryActionRow* {
        QListWidgetItem* currentItem = listWidget->currentItem();
        if (!currentItem) {
            return nullptr;
        }
        const QString id = currentItem->data(Qt::UserRole).toString();
        for (UserEntryActionRow& row : rows) {
            if (row.commandId == id) {
                return &row;
            }
        }
        return nullptr;
    };

    auto updatePreview = [=, &rows]() {
        UserEntryActionRow* row = selectedRow();
        previewLabel->setText(row ? row->preview
                                  : (firstEnabledListRow(listWidget) >= 0
                                         ? QStringLiteral("这里会解释当前对象动作会如何影响会话、联系人关系或群聊成员。")
                                         : emptyPreviewText()));
    };

    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("执行当前动作"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("执行当前对象动作"), QStyle::SP_ArrowForward);
    QPushButton* copyCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制对象卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前对象概览"), QStyle::SP_DialogSaveButton);
    QPushButton* copyStatusBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区状态"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制对象工作区当前状态"), QStyle::SP_MessageBoxInformation);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭对象工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("对象动作"),
        QStringLiteral("把联系人或群聊的高频动作收进一个稳定工作面里，不再依赖右键菜单记忆。"),
        {openBtn, copyCardBtn, copyStatusBtn},
        closeBtn);

    auto userEntryCardText = [=]() {
        if (isLocalGroup) {
            const QString groupNumber = targetId.mid(QStringLiteral("local_group_").size());
            return QStringLiteral("群对象卡\n群名:%1\n群号:%2\n成员:%3\n公告:%4")
                .arg(m_localGroupNames.value(targetId, displayName),
                     groupNumber,
                     QString::number(m_localGroupMembers.value(targetId).size()),
                     m_localGroupAnnouncements.value(targetId, QStringLiteral("暂无公告")));
        }
        return QStringLiteral("联系人对象卡\nQQ:%1\n昵称:%2\n状态:%3\n关系:%4")
            .arg(targetId,
                 displayName,
                 isContactOnline(targetId) ? QStringLiteral("在线") : QStringLiteral("离线"),
                 m_friendIds.contains(targetId) ? QStringLiteral("好友")
                                                : (m_pendingOutgoingFriendRequests.contains(targetId) ? QStringLiteral("申请中")
                                                                                                      : QStringLiteral("联系人")));
    };
    auto userEntryStatusText = [=]() {
        QStringList lines;
        lines << QStringLiteral("对象工作区状态");
        lines << QStringLiteral("对象:%1").arg(displayName);
        lines << QStringLiteral("标识:%1").arg(targetId);
        lines << QStringLiteral("类型:%1").arg(isLocalGroup ? QStringLiteral("群聊") : QStringLiteral("联系人"));
        if (isLocalGroup) {
            lines << QStringLiteral("成员:%1").arg(m_localGroupMembers.value(targetId).size());
        } else {
            lines << QStringLiteral("状态:%1").arg(isContactOnline(targetId) ? QStringLiteral("在线") : QStringLiteral("离线"));
            lines << QStringLiteral("关系:%1").arg(m_friendIds.contains(targetId) ? QStringLiteral("好友")
                                                                                   : (m_pendingOutgoingFriendRequests.contains(targetId) ? QStringLiteral("申请中")
                                                                                                                                         : QStringLiteral("联系人")));
        }
        return lines.join(QLatin1Char('\n'));
    };
    auto userEntryClipboardText = [=, &rows]() {
        UserEntryActionRow* row = selectedRow();
        if (!row) {
            return userEntryStatusText();
        }
        if (row->commandId == QLatin1String("open-chat")
            || row->commandId == QLatin1String("open-group")
            || row->commandId == QLatin1String("copy-card")) {
            return userEntryCardText();
        }
        return row->preview.trimmed().isEmpty() ? userEntryStatusText() : row->preview;
    };

    auto updateActionState = [=, &rows]() {
        UserEntryActionRow* row = selectedRow();
        const bool hasActionableRow = hasEnabledListRow(listWidget);
        if (!row) {
            openBtn->setEnabled(false);
            openBtn->setText(QStringLiteral("执行当前动作"));
            openBtn->setToolTip(hasActionableRow
                                    ? QStringLiteral("先选择一个对象动作后继续进入会话、复制资料或处理管理动作")
                                    : QStringLiteral("当前没有可执行的对象动作"));
            copyCardBtn->setEnabled(hasActionableRow);
            copyCardBtn->setToolTip(hasActionableRow
                                        ? QStringLiteral("复制当前对象工作区总览")
                                        : QStringLiteral("当前没有可复制的对象概览"));
            copyStatusBtn->setEnabled(true);
            copyStatusBtn->setToolTip(QStringLiteral("复制对象工作区当前状态"));
            return;
        }

        openBtn->setEnabled(true);
        openBtn->setText(row->title);
        openBtn->setToolTip(row->detail);
        copyCardBtn->setEnabled(true);
        copyCardBtn->setToolTip(QStringLiteral("复制当前选中对象卡"));
        copyStatusBtn->setEnabled(true);
        copyStatusBtn->setToolTip(QStringLiteral("复制对象工作区当前状态"));
    };

    auto runSelectedCommand = [=, &rows, this, &dialog]() {
        UserEntryActionRow* row = selectedRow();
        if (!row) {
            return;
        }
        dialog.accept();
        if (isLocalGroup) {
            handleLocalGroupContextCommand(targetId, displayName, row->commandId);
        } else {
            handleContactContextCommand(targetId, row->commandId);
        }
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, [runSelectedCommand](QListWidgetItem*) {
        runSelectedCommand();
    });
    connect(openBtn, &QPushButton::clicked, &dialog, runSelectedCommand);
    connect(copyCardBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        copyTextWithStatus(userEntryClipboardText(),
                           isLocalGroup ? QStringLiteral("群对象卡已复制") : QStringLiteral("联系人对象卡已复制"),
                           2200);
    });
    connect(copyStatusBtn, &QPushButton::clicked, &dialog, [=, this]() {
        copyTextWithStatus(userEntryStatusText(), QStringLiteral("对象工作区状态已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(isLocalGroup
        ? QStringLiteral("群聊：%1 · 成员 %2").arg(displayName).arg(m_localGroupMembers.value(targetId).size())
        : QStringLiteral("联系人：%1 · %2").arg(displayName, isContactOnline(targetId) ? QStringLiteral("在线") : QStringLiteral("离线")));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::showGroupInfoWorkspace() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("groupInfoWorkspaceDialog"),
        QStringLiteral("群信息工作区"),
        QSize(940, 760),
        QStringLiteral("managerTitle"),
        QStringLiteral("群信息工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把群公告、成员、通知、邀请和群聊管理入口集中到一个统一工作面里。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索群信息动作"),
        QStringLiteral("按公告、成员、通知、邀请、重命名或公共群动作筛选"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一个群信息动作，再决定编辑公告、打开成员工作区、进入群通知或执行邀请管理。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释当前群信息动作会如何影响公告、成员、通知或当前会话。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* listWidget = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    const bool localGroupContext = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    const bool removedFromPublicGroup = isCurrentUserRemovedFromPublicGroup();
    const QString currentGroupId = localGroupContext ? m_privateChatTarget : QStringLiteral("public");
    const QString currentGroupName = localGroupContext
        ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
        : QStringLiteral("公共聊天室");
    const QString announcementText = ui->announcementBodyLabel ? ui->announcementBodyLabel->text().trimmed() : QString();
    const QString ownerId = localGroupContext ? groupOwnerId(currentGroupId) : m_serverGroupOwners.value(QStringLiteral("public"));
    const QString ownerName = ownerId.isEmpty()
        ? QStringLiteral("未指定")
        : (ownerId == m_currentUserId ? m_currentUserName : contactDisplayName(ownerId));

    struct GroupInfoWorkspaceRow {
        QString commandId;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool accent = false;
        bool dangerous = false;
    };

    auto buildRows = [=, this]() {
        QList<GroupInfoWorkspaceRow> rows;

        rows << GroupInfoWorkspaceRow{
            QStringLiteral("group-overview"),
            localGroupContext ? QStringLiteral("当前群聊 · %1").arg(currentGroupName) : QStringLiteral("公共聊天室"),
            localGroupContext
                ? QStringLiteral("群主 %1 · 成员 %2").arg(ownerName).arg(m_localGroupMembers.value(currentGroupId).size())
                : (removedFromPublicGroup
                       ? QStringLiteral("公共群历史只读")
                       : QStringLiteral("群主 %1 · 公共群成员 %2").arg(ownerName).arg(m_serverGroupMembers.value(QStringLiteral("public")).size())),
            QStringLiteral("群信息总览\n群名：%1\n群主：%2\n公告：%3")
                .arg(currentGroupName,
                     ownerName,
                     announcementText.isEmpty() ? QStringLiteral("暂无公告") : announcementText),
            QStringLiteral("群 总览 公告 成员"),
            true,
            false};

        rows << GroupInfoWorkspaceRow{
            QStringLiteral("group-announcement"),
            QStringLiteral("群公告"),
            announcementText.isEmpty() ? QStringLiteral("当前暂无群公告") : announcementText.left(42),
            QStringLiteral("群公告\n%1").arg(announcementText.isEmpty() ? QStringLiteral("暂无公告") : announcementText),
            QStringLiteral("群公告 编辑 公告"),
            false,
            false};

        rows << GroupInfoWorkspaceRow{
            QStringLiteral("group-members"),
            QStringLiteral("群成员工作区"),
            localGroupContext
                ? QStringLiteral("当前群成员 %1").arg(m_localGroupMembers.value(currentGroupId).size())
                : QStringLiteral("当前可见成员 %1").arg(m_serverGroupMembers.value(QStringLiteral("public")).size()),
            QStringLiteral("群成员工作区\n集中处理成员查看、邀请、备注、复制和权限动作。"),
            QStringLiteral("群成员 工作区 邀请 备注"),
            false,
            false};

        rows << GroupInfoWorkspaceRow{
            QStringLiteral("group-notices"),
            QStringLiteral("群通知"),
            QStringLiteral("查看群创建、群名片、公告和成员摘要"),
            QStringLiteral("群通知工作区\n统一查看群入口、群公告、成员复制、媒体包和批量计划。"),
            QStringLiteral("群通知 公告 成员"),
            false,
            false};

        if (localGroupContext) {
            rows << GroupInfoWorkspaceRow{
                QStringLiteral("group-invite-friend"),
                QStringLiteral("邀请好友"),
                QStringLiteral("从现有好友里选择成员加入当前群聊"),
                QStringLiteral("邀请好友\n从好友列表里选人加入当前群聊，保持当前群上下文。"),
                QStringLiteral("邀请 好友 入群"),
                false,
                false};
            rows << GroupInfoWorkspaceRow{
                QStringLiteral("group-invite-account"),
                QStringLiteral("按QQ号邀请"),
                QStringLiteral("输入 QQ 号邀请成员入群"),
                QStringLiteral("按 QQ 号邀请\n可邀请还不是好友的人，并按需补发好友申请。"),
                QStringLiteral("QQ 邀请 成员"),
                false,
                false};
            rows << GroupInfoWorkspaceRow{
                QStringLiteral("group-rename"),
                QStringLiteral("重命名群聊"),
                QStringLiteral("调整当前本地群聊名称"),
                QStringLiteral("重命名群聊\n会同步更新左侧列表、会话标题和后续邀请上下文。"),
                QStringLiteral("重命名 群聊"),
                false,
                false};
        } else {
            rows << GroupInfoWorkspaceRow{
                QStringLiteral("group-back-public"),
                QStringLiteral("返回公共群主会话"),
                QStringLiteral("回到公共聊天室当前会话视图"),
                QStringLiteral("公共群主会话\n可恢复公共聊天室视图、公告和成员面板。"),
                QStringLiteral("公共群 返回 会话"),
                false,
                false};
        }

        if (localGroupContext) {
            rows << GroupInfoWorkspaceRow{
                QStringLiteral("group-delete"),
                QStringLiteral("删除群聊"),
                QStringLiteral("删除本地群聊配置，聊天记录不会在此步骤删除"),
                QStringLiteral("删除群聊\n会移除本地群聊配置；历史聊天记录不会在此步骤删除。"),
                QStringLiteral("删除 群聊"),
                false,
                true};
        }

        return rows;
    };

    auto rows = buildRows();
    auto emptyPreviewText = [searchEdit]() {
        const QString filter = searchEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的群信息动作。可从这里继续进入公告、成员、群通知和邀请管理入口。")
            : QStringLiteral("当前筛选词“%1”没有匹配到群信息动作。\n试试“公告”、“成员”、“通知”或“邀请”这些关键词。").arg(filter);
    };

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const GroupInfoWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !(row.title + row.detail + row.preview + row.keywords).contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.commandId);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (row.accent) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.dangerous) {
                item->setForeground(QColor(180, 35, 24));
            }
            listWidget->addItem(item);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                listWidget,
                QStringLiteral("没有匹配的群信息动作"),
                QStringLiteral("试试“公告”、“成员”、“通知”或“邀请”这些关键词。"),
                emptyPreviewText());
        }
        statsLabel->setText(QStringLiteral("可见 %1 / %2 项").arg(visibleCount).arg(rows.size()));
        selectPreferredListRow(listWidget, 0);
    };

    auto selectedRow = [=, &rows]() -> GroupInfoWorkspaceRow* {
        QListWidgetItem* currentItem = listWidget->currentItem();
        if (!currentItem) {
            return nullptr;
        }
        const QString id = currentItem->data(Qt::UserRole).toString();
        for (GroupInfoWorkspaceRow& row : rows) {
            if (row.commandId == id) {
                return &row;
            }
        }
        return nullptr;
    };

    auto updatePreview = [=, &rows]() {
        GroupInfoWorkspaceRow* row = selectedRow();
        previewLabel->setText(row ? row->preview
                                  : (firstEnabledListRow(listWidget) >= 0
                                         ? QStringLiteral("这里会解释当前群信息动作会如何影响公告、成员、通知或当前会话。")
                                         : emptyPreviewText()));
    };

    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("执行当前动作"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("执行当前群信息动作"), QStyle::SP_ArrowForward);
    QPushButton* copyCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制群信息卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前群信息摘要"), QStyle::SP_DialogSaveButton);
    QPushButton* copyStatusBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区状态"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制群信息工作区当前状态"), QStyle::SP_MessageBoxInformation);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭群信息工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("群信息动作"),
        QStringLiteral("把公告、成员、通知、邀请和管理入口收进一个统一群信息面。"),
        {openBtn, copyCardBtn, copyStatusBtn},
        closeBtn);

    auto groupInfoCardText = [=]() {
        return QStringLiteral("群信息卡\n群名:%1\n群标识:%2\n群主:%3\n成员:%4\n公告:%5")
            .arg(currentGroupName,
                 currentGroupId,
                 ownerName,
                 localGroupContext
                     ? QString::number(m_localGroupMembers.value(currentGroupId).size())
                     : QString::number(m_serverGroupMembers.value(QStringLiteral("public")).size()),
                 announcementText.isEmpty() ? QStringLiteral("暂无公告") : announcementText);
    };
    auto groupInfoStatusText = [=]() {
        QStringList lines;
        lines << QStringLiteral("群信息工作区状态");
        lines << QStringLiteral("群名:%1").arg(currentGroupName);
        lines << QStringLiteral("群标识:%1").arg(currentGroupId);
        lines << QStringLiteral("群主:%1").arg(ownerName);
        lines << QStringLiteral("公告:%1").arg(announcementText.isEmpty() ? QStringLiteral("暂无公告") : announcementText);
        lines << QStringLiteral("上下文:%1").arg(localGroupContext ? QStringLiteral("本地群聊") : (removedFromPublicGroup ? QStringLiteral("公共群历史只读") : QStringLiteral("公共聊天室")));
        return lines.join(QLatin1Char('\n'));
    };
    auto groupInfoClipboardText = [=, &rows]() {
        GroupInfoWorkspaceRow* row = selectedRow();
        if (!row) {
            return groupInfoStatusText();
        }
        if (row->commandId == QLatin1String("group-overview")) {
            return groupInfoCardText();
        }
        if (row->commandId == QLatin1String("group-announcement")) {
            return QStringLiteral("群公告\n%1").arg(announcementText.isEmpty() ? QStringLiteral("暂无公告") : announcementText);
        }
        return row->preview.trimmed().isEmpty() ? groupInfoStatusText() : row->preview;
    };

    auto updateActionState = [=, &rows]() {
        GroupInfoWorkspaceRow* row = selectedRow();
        const bool hasActionableRow = hasEnabledListRow(listWidget);
        if (!row) {
            openBtn->setEnabled(false);
            openBtn->setText(QStringLiteral("执行当前动作"));
            openBtn->setToolTip(hasActionableRow
                                    ? QStringLiteral("先选择一个群信息动作后继续查看公告、成员、通知或邀请管理")
                                    : QStringLiteral("当前没有可执行的群信息动作"));
            copyCardBtn->setEnabled(hasActionableRow);
            copyCardBtn->setToolTip(hasActionableRow
                                        ? QStringLiteral("复制当前群信息工作区总览")
                                        : QStringLiteral("当前没有可复制的群信息摘要"));
            copyStatusBtn->setEnabled(true);
            copyStatusBtn->setToolTip(QStringLiteral("复制群信息工作区当前状态"));
            return;
        }

        openBtn->setEnabled(true);
        if (row->commandId == QLatin1String("group-overview")
            || row->commandId == QLatin1String("group-members")) {
            openBtn->setText(QStringLiteral("打开群成员工作区"));
            openBtn->setToolTip(QStringLiteral("进入群成员工作区继续查看成员、复制摘要或处理邀请动作"));
        } else if (row->commandId == QLatin1String("group-announcement")) {
            openBtn->setText(QStringLiteral("编辑群公告"));
            openBtn->setToolTip(QStringLiteral("打开群公告编辑入口"));
        } else if (row->commandId == QLatin1String("group-notices")) {
            openBtn->setText(QStringLiteral("打开群通知"));
            openBtn->setToolTip(QStringLiteral("进入群通知工作区"));
        } else if (row->commandId == QLatin1String("group-invite-friend")) {
            openBtn->setText(QStringLiteral("邀请好友入群"));
            openBtn->setToolTip(QStringLiteral("从好友列表里选择成员加入当前群聊"));
        } else if (row->commandId == QLatin1String("group-invite-account")) {
            openBtn->setText(QStringLiteral("按 QQ 号邀请"));
            openBtn->setToolTip(QStringLiteral("输入 QQ 号邀请成员入群"));
        } else if (row->commandId == QLatin1String("group-rename")) {
            openBtn->setText(QStringLiteral("重命名群聊"));
            openBtn->setToolTip(QStringLiteral("调整当前本地群聊名称"));
        } else if (row->commandId == QLatin1String("group-back-public")) {
            openBtn->setText(QStringLiteral("返回公共会话"));
            openBtn->setToolTip(QStringLiteral("回到公共聊天室当前会话视图"));
        } else {
            openBtn->setText(QStringLiteral("删除群聊"));
            openBtn->setToolTip(QStringLiteral("删除当前本地群聊配置"));
        }
        copyCardBtn->setEnabled(true);
        copyCardBtn->setToolTip(row->commandId == QLatin1String("group-overview")
                                    ? QStringLiteral("复制当前群信息卡")
                                    : (row->commandId == QLatin1String("group-announcement")
                                           ? QStringLiteral("复制当前群公告摘要")
                                           : QStringLiteral("复制当前选中群信息卡")));
        copyStatusBtn->setEnabled(true);
        copyStatusBtn->setToolTip(QStringLiteral("复制群信息工作区当前状态"));
    };

    auto runSelectedCommand = [=, &rows, this, &dialog]() {
        GroupInfoWorkspaceRow* row = selectedRow();
        if (!row) {
            return;
        }
        const QString commandId = row->commandId;
        if (commandId == QLatin1String("group-overview")
            || commandId == QLatin1String("group-members")) {
            dialog.accept();
            onShowGroupMemberWorkspace();
            return;
        }
        if (commandId == QLatin1String("group-announcement")) {
            dialog.accept();
            onEditGroupAnnouncement();
            return;
        }
        if (commandId == QLatin1String("group-notices")) {
            dialog.accept();
            onShowGroupNotifications();
            return;
        }
        if (commandId == QLatin1String("group-invite-friend")) {
            dialog.accept();
            showInviteFriendToGroupWorkspace(currentGroupId, this);
            return;
        }
        if (commandId == QLatin1String("group-invite-account")) {
            dialog.accept();
            showInviteAccountToGroupWorkspace(currentGroupId, this);
            return;
        }
        if (commandId == QLatin1String("group-rename")) {
            dialog.accept();
            showRenameGroupWorkspace(currentGroupId, this);
            return;
        }
        if (commandId == QLatin1String("group-back-public")) {
            dialog.accept();
            onBackToGroupChat();
            return;
        }
        if (commandId == QLatin1String("group-delete")) {
            dialog.accept();
            handleLocalGroupContextCommand(currentGroupId, currentGroupName, QStringLiteral("delete-group"));
            return;
        }
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, [runSelectedCommand](QListWidgetItem*) {
        runSelectedCommand();
    });
    connect(openBtn, &QPushButton::clicked, &dialog, runSelectedCommand);
    connect(copyCardBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        GroupInfoWorkspaceRow* row = selectedRow();
        copyTextWithStatus(groupInfoClipboardText(),
                           row && row->commandId == QLatin1String("group-announcement")
                               ? QStringLiteral("群公告摘要已复制")
                               : QStringLiteral("群信息卡已复制"),
                           2200);
    });
    connect(copyStatusBtn, &QPushButton::clicked, &dialog, [=, this]() {
        copyTextWithStatus(groupInfoStatusText(), QStringLiteral("群信息工作区状态已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(localGroupContext
        ? QStringLiteral("当前群：%1 · 群主 %2 · 成员 %3").arg(currentGroupName, ownerName).arg(m_localGroupMembers.value(currentGroupId).size())
        : QStringLiteral("公共聊天室 · 群主 %1 · 上下文 %2")
              .arg(ownerName, removedFromPublicGroup ? QStringLiteral("历史只读") : QStringLiteral("当前主会话")));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowQuickAddFriend() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("quickAddWorkspaceDialog"),
        QStringLiteral("好友申请工作区"),
        QSize(920, 760),
        QStringLiteral("managerTitle"),
        QStringLiteral("好友申请工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把 QQ 搜索、推荐申请、申请话术和通过后的媒体准备统一收进一个稳定工作面。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("输入对方 QQ 号"),
        QStringLiteral("输入 QQ 号后回车即可搜索并申请"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("可直接输入 QQ 搜索并申请，也可以从推荐列表里双击、批量申请或复制当前申请资料。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会显示当前申请预览、推荐摘要和后续媒体准备说明。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* accountEdit = shell.searchEdit;
    QListWidget* suggestionList = shell.listWidget;
    QLabel* hintLabel = shell.hintLabel;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* cardLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    auto emptyQuickAddPreviewText = [accountEdit]() {
        const QString filter = accountEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的推荐对象。可直接输入 QQ 号搜索并申请，或稍后等待更多在线用户出现。")
            : QStringLiteral("当前输入“%1”没有匹配到推荐对象。\n可直接回车搜索这个 QQ，或清空输入后浏览推荐列表。").arg(filter);
    };
    auto fillSuggestions = [this, accountEdit, suggestionList, statsLabel, cardLabel, subTitleLabel, emptyQuickAddPreviewText]() {
        suggestionList->clear();
        const FriendQuickAddSuggestionUiState suggestionState =
            FriendManager::quickAddSuggestionUiState(m_currentUserId,
                                                     m_currentUserName,
                                                     m_friendIds,
                                                     m_pendingOutgoingFriendRequests,
                                                     m_knownUsers,
                                                     m_friendNames,
                                                     accountEdit->text(),
                                                     5);
        statsLabel->setText(suggestionState.statsText);
        cardLabel->setText(suggestionState.previewText);
        subTitleLabel->setText(QStringLiteral("当前账号：%1 · 推荐候选 %2").arg(m_currentUserId).arg(suggestionState.entries.size()));
        for (const FriendQuickAddSuggestionEntryUiState& entry : suggestionState.entries) {
            QListWidgetItem* item = new QListWidgetItem(entry.text);
            item->setData(Qt::UserRole, entry.entryId);
            item->setSizeHint(QSize(0, entry.rowHeight));
            if (!entry.enabled) {
                item->setFlags(Qt::NoItemFlags);
            }
            if (entry.muted) {
                item->setForeground(QColor(135, 150, 165));
            }
            suggestionList->addItem(item);
        }
        if (!hasEnabledListRow(suggestionList)) {
            cardLabel->setText(emptyQuickAddPreviewText());
        }
        selectPreferredListRow(suggestionList, 0);
    };
    fillSuggestions();

    QPushButton* cancelBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭好友申请工作区"), QStyle::SP_DialogCloseButton);
    QPushButton* searchBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("搜索并申请"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("按输入的 QQ 号搜索在线账号并发起好友申请"), QStyle::SP_FileDialogContentsView);
    searchBtn->setDefault(true);
    QPushButton* recommendBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("批量申请推荐"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("向当前推荐列表里的可申请用户批量发起好友申请"), QStyle::SP_CommandLink);
    QPushButton* copyPreviewBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制预览"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前申请预览卡片"), QStyle::SP_FileDialogDetailedView);
    QPushButton* copyRequestBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制申请话术"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制适合当前 QQ 号或推荐用户的好友申请话术"), QStyle::SP_MessageBoxInformation);
    QPushButton* copySearchCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制搜索卡片"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前好友申请搜索条件和推荐结果"), QStyle::SP_FileDialogInfoView);
    QPushButton* copyFriendMediaPackBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制好友媒体包"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制加好友后发送图片、视频或文件的准备摘要"), QStyle::SP_FileIcon);
    QPushButton* copyAddChecklistBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制申请清单"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制好友申请前后的操作检查清单"), QStyle::SP_DriveHDIcon);
    QPushButton* copyMediaGuideBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制上传指南"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制通过好友申请后发送媒体和文件的简短指南"), QStyle::SP_DialogHelpButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("主操作"),
        QStringLiteral("确认 QQ 后直接搜索并申请，也可以批量处理推荐对象。"),
        {searchBtn, recommendBtn},
        cancelBtn);
    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("复制与说明"),
        QStringLiteral("先整理预览和申请话术，再发给对方或自己留档。"),
        {copyPreviewBtn, copyRequestBtn, copySearchCardBtn});
    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("媒体与清单"),
        QStringLiteral("为好友通过后的图片、视频、文件发送准备材料和检查项。"),
        {copyFriendMediaPackBtn, copyAddChecklistBtn, copyMediaGuideBtn});

    auto currentQuickAddSelectionId = [suggestionList]() {
        QListWidgetItem* item = suggestionList->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    auto updateQuickAddPreview = [this, accountEdit, suggestionList, cardLabel, emptyQuickAddPreviewText]() {
        QListWidgetItem* item = suggestionList->currentItem();
        const QString manualAccount = accountEdit->text().trimmed();
        const QString selectedId = item ? item->data(Qt::UserRole).toString() : QString();
        if (!selectedId.isEmpty()) {
            const QString displayName = contactDisplayName(selectedId);
            cardLabel->setText(QStringLiteral("邀请预览：%1（QQ:%2）\n你好，我是 %3（QQ:%4），方便加个好友吗？")
                                   .arg(displayName.isEmpty() ? selectedId : displayName,
                                        selectedId,
                                        m_currentUserName,
                                        m_currentUserId));
            return;
        }
        if (!manualAccount.isEmpty()) {
            cardLabel->setText(QStringLiteral("邀请预览：待搜索好友（QQ:%1）\n你好，我是 %2（QQ:%3），方便加个好友吗？")
                                   .arg(manualAccount, m_currentUserName, m_currentUserId));
            return;
        }
        if (hasEnabledListRow(suggestionList)) {
            cardLabel->setText(QStringLiteral("选择推荐对象后，可直接发起申请或复制申请话术、媒体准备和检查清单。"));
            return;
        }
        cardLabel->setText(emptyQuickAddPreviewText());
    };
    auto updateQuickAddActionState = [=]() {
        const QString manualAccount = accountEdit->text().trimmed();
        const QString selectedId = currentQuickAddSelectionId();
        const bool hasManualAccount = !manualAccount.isEmpty();
        const bool hasActionableSuggestion = hasEnabledListRow(suggestionList);
        const bool hasRequestContext = !selectedId.isEmpty() || hasManualAccount;

        searchBtn->setEnabled(hasManualAccount);
        searchBtn->setText(QStringLiteral("搜索并申请"));
        searchBtn->setToolTip(hasManualAccount
                                  ? QStringLiteral("搜索 QQ:%1 并发起好友申请").arg(manualAccount)
                                  : QStringLiteral("先输入对方 QQ 号再搜索并申请"));
        recommendBtn->setEnabled(hasActionableSuggestion);
        recommendBtn->setText(QStringLiteral("批量申请推荐"));
        recommendBtn->setToolTip(hasActionableSuggestion
                                     ? QStringLiteral("向当前推荐列表里的可申请用户批量发起好友申请")
                                     : QStringLiteral("当前没有可批量申请的推荐对象"));
        copyPreviewBtn->setEnabled(hasRequestContext || hasActionableSuggestion);
        copyPreviewBtn->setToolTip(hasRequestContext || hasActionableSuggestion
                                       ? QStringLiteral("复制当前申请预览卡片")
                                       : QStringLiteral("输入 QQ 或选择推荐对象后可复制预览"));
        copyRequestBtn->setEnabled(hasRequestContext || hasActionableSuggestion);
        copyRequestBtn->setToolTip(hasRequestContext || hasActionableSuggestion
                                       ? QStringLiteral("复制适合当前 QQ 号或推荐用户的好友申请话术")
                                       : QStringLiteral("输入 QQ 或选择推荐对象后可复制申请话术"));
        copySearchCardBtn->setEnabled(hasRequestContext || hasActionableSuggestion);
        copySearchCardBtn->setToolTip(hasRequestContext || hasActionableSuggestion
                                          ? QStringLiteral("复制当前好友申请搜索条件和推荐结果")
                                          : QStringLiteral("当前没有可整理的搜索条件或推荐结果"));
        copyFriendMediaPackBtn->setEnabled(hasRequestContext);
        copyFriendMediaPackBtn->setToolTip(hasRequestContext
                                               ? QStringLiteral("复制加好友后发送图片、视频或文件的准备摘要")
                                               : QStringLiteral("输入 QQ 或选择推荐对象后可复制好友媒体包"));
        copyAddChecklistBtn->setEnabled(hasRequestContext || hasActionableSuggestion);
        copyAddChecklistBtn->setToolTip(hasRequestContext || hasActionableSuggestion
                                            ? QStringLiteral("复制好友申请前后的操作检查清单")
                                            : QStringLiteral("当前没有可整理的申请检查清单"));
        copyMediaGuideBtn->setEnabled(true);
        copyMediaGuideBtn->setToolTip(QStringLiteral("复制通过好友申请后发送媒体和文件的简短指南"));
    };

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto runQuickAdd = [this, accountEdit, hintLabel, &dialog]() {
        QString account = accountEdit->text().trimmed();
        if (account.isEmpty()) {
            hintLabel->setText("请输入对方 QQ 账号");
            ui->statusbar->showMessage("请输入对方 QQ 账号", 2500);
            return;
        }
        searchAndAddAccount(account, &dialog);
        dialog.accept();
    };
    auto quickAddCandidateTargets = [this, suggestionList]() {
        QList<FriendManagerVisibleTargetSummary> targets;
        for (int i = 0; i < suggestionList->count(); ++i) {
            QListWidgetItem* item = suggestionList->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                continue;
            }
            FriendManagerVisibleTargetSummary target;
            target.userId = id;
            target.displayName = contactDisplayName(id);
            target.online = isContactOnline(id);
            targets << target;
        }
        return targets;
    };
    auto selectedQuickAddTarget = [this, accountEdit, suggestionList]() {
        FriendManagerVisibleTargetSummary target;
        target.userId = accountEdit->text().trimmed();
        if (target.userId.isEmpty()) {
            QListWidgetItem* item = suggestionList->currentItem();
            if (item) {
                target.userId = item->data(Qt::UserRole).toString();
            }
        }
        if (!target.userId.isEmpty()) {
            target.displayName = contactDisplayName(target.userId);
            target.online = isContactOnline(target.userId);
        }
        return target;
    };
    connect(accountEdit, &QLineEdit::textChanged, &dialog, [fillSuggestions, updateQuickAddPreview, updateQuickAddActionState]() {
        fillSuggestions();
        updateQuickAddPreview();
        updateQuickAddActionState();
    });
    connect(suggestionList, &QListWidget::currentItemChanged, &dialog, [updateQuickAddPreview, updateQuickAddActionState](QListWidgetItem*, QListWidgetItem*) {
        updateQuickAddPreview();
        updateQuickAddActionState();
    });
    connect(copyPreviewBtn, &QPushButton::clicked, &dialog, [this, cardLabel]() {
        QApplication::clipboard()->setText(cardLabel->text());
        ui->statusbar->showMessage("好友申请预览已复制", 2200);
    });
    connect(copyRequestBtn, &QPushButton::clicked, &dialog, [this, accountEdit, suggestionList, cardLabel]() {
        QString account = accountEdit->text().trimmed();
        if (account.isEmpty()) {
            QListWidgetItem* item = suggestionList->currentItem();
            if (item) account = item->data(Qt::UserRole).toString();
        }
        QString name = account.isEmpty() ? "朋友" : contactDisplayName(account);
        QString text = QString("%1，你好，我是 %2（QQ:%3）。我通过 QQ 搜索看到你，想加你为好友继续沟通。\n%4")
            .arg(name, m_currentUserName, m_currentUserId, cardLabel->text());
        QApplication::clipboard()->setText(text);
        ui->statusbar->showMessage("好友申请话术已复制", 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, accountEdit, quickAddCandidateTargets]() {
        const GlobalSearchSelectionCopyState state = FriendManager::quickAddSearchSummaryCardState(
            m_currentUserId,
            m_currentUserName,
            accountEdit->text().trimmed(),
            quickAddCandidateTargets());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友申请搜索卡片已复制", 2200);
    });
    connect(copyFriendMediaPackBtn, &QPushButton::clicked, &dialog, [this, selectedQuickAddTarget]() {
        const GlobalSearchSelectionCopyState state = FriendManager::quickAddMediaPackState(
            m_currentUserId,
            m_currentUserName,
            selectedQuickAddTarget());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友媒体包已复制", 2200);
    });
    connect(copyAddChecklistBtn, &QPushButton::clicked, &dialog, [this, selectedQuickAddTarget]() {
        const GlobalSearchSelectionCopyState state = FriendManager::quickAddChecklistState(
            m_currentUserId,
            m_currentUserName,
            m_friendIds.size(),
            selectedQuickAddTarget());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友申请清单已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, accountEdit]() {
        QApplication::clipboard()->setText(FriendManager::quickAddMediaGuideText(
            m_currentUserId,
            m_currentUserName,
            accountEdit->text().trimmed()));
        ui->statusbar->showMessage("好友申请上传指南已复制", 2200);
    });
    connect(suggestionList, &QListWidget::itemDoubleClicked, &dialog, [accountEdit, runQuickAdd](QListWidgetItem* item) {
        QString account = item->data(Qt::UserRole).toString();
        if (account.isEmpty()) return;
        accountEdit->setText(account);
        runQuickAdd();
    });
    connect(recommendBtn, &QPushButton::clicked, &dialog, [this, suggestionList, hintLabel, fillSuggestions, &dialog]() {
        QStringList addIds;
        for (int i = 0; i < suggestionList->count(); ++i) {
            QListWidgetItem* item = suggestionList->item(i);
            QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty() || id == m_currentUserId || m_friendIds.contains(id) || m_pendingOutgoingFriendRequests.contains(id)) continue;
            if (!addIds.contains(id)) {
                addIds << id;
            }
        }
        if (addIds.isEmpty()) {
            hintLabel->setText("暂无可申请的推荐好友，可输入 QQ 号搜索");
            ui->statusbar->showMessage("暂无可申请的推荐好友", 2200);
            return;
        }
        if (!confirmAction(QStringLiteral("发起推荐好友申请"),
                           QStringLiteral("确定向 %1 位推荐用户发起好友申请吗？").arg(addIds.size()),
                           QStringLiteral("已取消发起推荐申请"),
                           1600,
                           &dialog)) {
            return;
        }
        QStringList sentNames;
        QStringList failedNames;
        for (const QString& id : addIds) {
            const QString displayName = contactDisplayName(id);
            if (m_client->sendFriendRequest(id)) {
                m_friendNames[id] = displayName;
                if (!m_pendingOutgoingFriendRequests.contains(id)) {
                    m_pendingOutgoingFriendRequests << id;
                }
                sentNames << QString("%1(%2)").arg(displayName, id);
            } else {
                failedNames << QString("%1(%2)").arg(displayName, id);
            }
        }
        if (sentNames.isEmpty()) {
            hintLabel->setText("好友申请发起失败，请检查连接后重试。");
            ui->statusbar->showMessage("推荐好友申请发起失败", 2600);
            return;
        }
        refreshFriendList();
        fillSuggestions();
        appendSystemMessage(QString("已向推荐用户发起好友申请：%1").arg(sentNames.join("、")));
        if (!failedNames.isEmpty()) {
            appendSystemMessage(QString("以下推荐好友申请发起失败：%1").arg(failedNames.join("、")));
        }
        ui->statusbar->showMessage(QString("已发送 %1 个推荐好友申请，等待确认").arg(sentNames.size()), 2500);
        dialog.accept();
    });
    connect(searchBtn, &QPushButton::clicked, &dialog, runQuickAdd);
    connect(accountEdit, &QLineEdit::returnPressed, &dialog, runQuickAdd);

    dialog.setStyleSheet(productDialogStyleSheet());
    updateQuickAddPreview();
    updateQuickAddActionState();
    accountEdit->setFocus();
    accountEdit->selectAll();
    dialog.exec();
}

void MainWindow::onShowContactWorkspace(const QString& initialFilter) {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("contactWorkspaceDialog"),
        QStringLiteral("联系人工作区"),
        QSize(980, 780),
        QStringLiteral("managerTitle"),
        QStringLiteral("联系人工作区"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把账号资料、QQ 搜索、联系人入口和常用复制动作收进同一个侧栏工作面。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索联系人 / QQ / 群聊动作"),
        QStringLiteral("按联系人、群聊、QQ 搜索动作或工作区动作关键词筛选"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一项，再决定进入会话、打开管理器、复制资料，或发起搜索与建群。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释当前联系人、群聊或侧栏入口会如何影响主界面、会话和后续操作。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* listWidget = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    struct ContactWorkspaceRow {
        QString rowId;
        QString actionKey;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool accent = false;
        bool muted = false;
    };

    auto buildRows = [this]() {
        QList<ContactWorkspaceRow> rows;

        ContactWorkspaceRow profileRow;
        profileRow.rowId = QStringLiteral("profile-overview");
        profileRow.actionKey = QStringLiteral("profile-overview");
        profileRow.title = QStringLiteral("当前账号 · %1").arg(m_currentUserName);
        profileRow.detail = QStringLiteral("QQ:%1 · 好友 %2 · 群聊 %3").arg(m_currentUserId).arg(m_friendIds.size()).arg(m_localGroupIds.size());
        profileRow.preview = QStringLiteral("当前账号\nQQ：%1\n昵称：%2\n好友：%3\n群聊：%4\n当前会话：%5")
                                 .arg(m_currentUserId,
                                      m_currentUserName,
                                      QString::number(m_friendIds.size()),
                                      QString::number(m_localGroupIds.size()),
                                      m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        profileRow.keywords = profileRow.title + profileRow.detail + profileRow.preview + QStringLiteral("账号 资料 头像 联系人 搜索");
        rows << profileRow;

        ContactWorkspaceRow quickAddRow;
        quickAddRow.rowId = QStringLiteral("action-quick-add");
        quickAddRow.actionKey = QStringLiteral("action-quick-add");
        quickAddRow.title = QStringLiteral("好友申请工作区");
        quickAddRow.detail = QStringLiteral("打开申请工作区，按 QQ 搜索并申请");
        quickAddRow.preview = QStringLiteral("好友申请工作区\n可输入 QQ 搜索、复制申请话术、整理申请前后的媒体和检查清单。");
        quickAddRow.keywords = quickAddRow.title + quickAddRow.detail + quickAddRow.preview + QStringLiteral("好友申请 添加好友 QQ 搜索");
        quickAddRow.accent = true;
        rows << quickAddRow;

        ContactWorkspaceRow globalSearchRow;
        globalSearchRow.rowId = QStringLiteral("action-global-search");
        globalSearchRow.actionKey = QStringLiteral("action-global-search");
        globalSearchRow.title = QStringLiteral("综合搜索");
        globalSearchRow.detail = QStringLiteral("打开综合搜索工作区，统一搜索 QQ、好友和群聊");
        globalSearchRow.preview = QStringLiteral("综合搜索工作区\n可查看匹配、复制搜索摘要，并继续跳转到好友、群聊和媒体准备动作。");
        globalSearchRow.keywords = globalSearchRow.title + globalSearchRow.detail + globalSearchRow.preview + QStringLiteral("搜索 好友 群聊");
        rows << globalSearchRow;

        ContactWorkspaceRow friendManagerRow;
        friendManagerRow.rowId = QStringLiteral("action-friend-manager");
        friendManagerRow.actionKey = QStringLiteral("action-friend-manager");
        friendManagerRow.title = QStringLiteral("好友管理器");
        friendManagerRow.detail = QStringLiteral("打开好友管理工作区，做备注、邀请、删除和批量复制");
        friendManagerRow.preview = QStringLiteral("好友管理器\n统一处理好友筛选、备注、入群邀请、删除和媒体准备动作。");
        friendManagerRow.keywords = friendManagerRow.title + friendManagerRow.detail + friendManagerRow.preview + QStringLiteral("好友 管理 备注");
        rows << friendManagerRow;

        const QString pendingSearch = ui->contactSearchEdit ? ui->contactSearchEdit->text().trimmed() : QString();
        if (!pendingSearch.isEmpty()) {
            ContactWorkspaceRow pendingSearchRow;
            pendingSearchRow.rowId = QStringLiteral("pending-search");
            pendingSearchRow.actionKey = QStringLiteral("pending-search");
            pendingSearchRow.title = QStringLiteral("QQ 搜索框 · %1").arg(pendingSearch);
            pendingSearchRow.detail = QStringLiteral("立即用当前输入搜索账号，或创建同名群聊");
            pendingSearchRow.preview = QStringLiteral("搜索框当前内容：%1\n可直接搜索 QQ、打开综合搜索，或创建同名群聊。").arg(pendingSearch);
            pendingSearchRow.keywords = pendingSearchRow.title + pendingSearchRow.detail + pendingSearchRow.preview + QStringLiteral("搜索 建群");
            pendingSearchRow.accent = true;
            rows << pendingSearchRow;
        }

        const QList<QStandardItem*> visibleItems = m_userListModel ? m_userListModel->findItems(QStringLiteral("*"), Qt::MatchWildcard) : QList<QStandardItem*>();
        for (QStandardItem* item : visibleItems) {
            if (!item) {
                continue;
            }
            const QString targetId = item->data(Qt::UserRole + 1).toString();
            if (targetId.isEmpty()) {
                continue;
            }

            ContactWorkspaceRow row;
            row.rowId = targetId;
            row.actionKey = QStringLiteral("open-target");
            row.title = contactDisplayName(targetId);
            if (targetId.startsWith(QStringLiteral("search_add:"))) {
                const QString account = targetId.mid(QStringLiteral("search_add:").size());
                row.title = QStringLiteral("搜索并申请 · %1").arg(account);
                row.detail = QStringLiteral("对这个 QQ 发起搜索并申请");
                row.preview = QStringLiteral("搜索入口\nQQ：%1\n动作：搜索在线账号并准备发起好友申请").arg(account);
                row.accent = true;
            } else if (targetId.startsWith(QStringLiteral("create_group:"))) {
                const QString groupName = targetId.mid(QStringLiteral("create_group:").size()).trimmed();
                row.title = QStringLiteral("创建群聊 · %1").arg(groupName.isEmpty() ? QStringLiteral("我的群聊") : groupName);
                row.detail = QStringLiteral("按当前搜索词创建本地群聊");
                row.preview = QStringLiteral("建群入口\n群名：%1\n动作：创建群聊并切到该群继续邀请好友和发消息。")
                                  .arg(groupName.isEmpty() ? QStringLiteral("我的群聊") : groupName);
                row.accent = true;
            } else if (m_localGroupIds.contains(targetId)) {
                const QString groupName = m_localGroupNames.value(targetId, row.title);
                row.title = QStringLiteral("群聊 · %1").arg(groupName);
                row.detail = QStringLiteral("群号:%1 · 成员 %2").arg(targetId).arg(m_localGroupMembers.value(targetId).size());
                row.preview = QStringLiteral("本地群聊\n群名：%1\n群号：%2\n成员：%3\n公告：%4")
                                  .arg(groupName,
                                       targetId,
                                       QString::number(m_localGroupMembers.value(targetId).size()),
                                       m_localGroupAnnouncements.value(targetId, QStringLiteral("暂无公告")));
            } else {
                const bool online = isContactOnline(targetId);
                const bool isFriend = m_friendIds.contains(targetId);
                const bool pending = m_pendingOutgoingFriendRequests.contains(targetId);
                row.detail = QStringLiteral("QQ:%1 · %2 · %3")
                                 .arg(targetId,
                                      online ? QStringLiteral("在线") : QStringLiteral("离线"),
                                      isFriend ? QStringLiteral("好友")
                                               : (pending ? QStringLiteral("申请中") : QStringLiteral("联系人")));
                row.preview = QStringLiteral("联系人\nQQ：%1\n昵称：%2\n状态：%3\n关系：%4\n当前可继续：打开私聊、复制名片、查看加密状态或发起好友动作。")
                                  .arg(targetId,
                                       contactDisplayName(targetId),
                                       online ? QStringLiteral("在线") : QStringLiteral("离线"),
                                       isFriend ? QStringLiteral("好友")
                                                : (pending ? QStringLiteral("申请中") : QStringLiteral("联系人")));
                row.accent = online;
                row.muted = !online;
            }
            row.keywords = row.rowId + row.title + row.detail + row.preview;
            rows << row;
        }

        return rows;
    };

    auto rows = buildRows();
    auto emptyPreviewText = [searchEdit]() {
        const QString filter = searchEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的联系人入口。可从这里继续进入联系人、群聊、搜索和好友管理入口。")
            : QStringLiteral("当前筛选词“%1”没有匹配到联系人入口。\n试试 QQ、昵称、“群聊”、“申请”或“管理”这些关键词。").arg(filter);
    };

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const ContactWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !row.keywords.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.rowId);
            item->setData(Qt::UserRole + 1, row.actionKey);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (row.accent) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.muted) {
                item->setForeground(QColor(100, 116, 139));
            }
            listWidget->addItem(item);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                listWidget,
                QStringLiteral("没有匹配的联系人入口"),
                QStringLiteral("试试 QQ、昵称、“群聊”、“申请”或“管理”这些关键词。"),
                emptyPreviewText());
        }
        statsLabel->setText(QStringLiteral("可见 %1 / %2 项").arg(visibleCount).arg(rows.size()));
        selectPreferredListRow(listWidget, 0);
    };

    auto selectedRow = [=, &rows]() -> ContactWorkspaceRow* {
        QListWidgetItem* currentItem = listWidget->currentItem();
        if (!currentItem) {
            return nullptr;
        }
        const QString rowId = currentItem->data(Qt::UserRole).toString();
        const QString actionKey = currentItem->data(Qt::UserRole + 1).toString();
        for (ContactWorkspaceRow& row : rows) {
            if (row.rowId == rowId && row.actionKey == actionKey) {
                return &row;
            }
        }
        return nullptr;
    };

    auto updatePreview = [=, &rows]() {
        ContactWorkspaceRow* row = selectedRow();
        previewLabel->setText(row ? row->preview
                                  : (firstEnabledListRow(listWidget) >= 0
                                         ? QStringLiteral("这里会解释当前联系人、群聊或侧栏入口会如何影响主界面、会话和后续操作。")
                                         : emptyPreviewText()));
    };

    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("执行当前动作"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("进入当前联系人、群聊或工作区入口"), QStyle::SP_ArrowForward);
    QPushButton* searchBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("搜索当前QQ"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("把当前选中 QQ 带入搜索或申请流程"), QStyle::SP_FileDialogContentsView);
    QPushButton* copyCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制资料卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前账号、联系人或群聊摘要"), QStyle::SP_FileDialogDetailedView);
    QPushButton* copyStatusBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区状态"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前侧栏工作区摘要"), QStyle::SP_MessageBoxInformation);
    QPushButton* friendManagerBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("打开好友管理"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("跳转到好友管理器"), QStyle::SP_FileDialogDetailedView);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭联系人工作区"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("主动作"),
        QStringLiteral("统一打开联系人、群聊或搜索入口，减少左侧列表和右键之间的来回切换。"),
        {openBtn, searchBtn, friendManagerBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("复制与记录"),
        QStringLiteral("把联系人、群聊和侧栏状态整理成可复制的摘要，便于反馈、留档和继续处理。"),
        {copyCardBtn, copyStatusBtn});

    auto contactStatusText = [=, &rows]() {
        QStringList lines;
        lines << QStringLiteral("联系人工作区状态");
        lines << QStringLiteral("当前账号:%1 (%2)").arg(m_currentUserName, m_currentUserId);
        lines << QStringLiteral("好友:%1").arg(m_friendIds.size());
        lines << QStringLiteral("群聊:%1").arg(m_localGroupIds.size());
        lines << QStringLiteral("搜索框:%1").arg(ui->contactSearchEdit->text().trimmed().isEmpty() ? QStringLiteral("空") : ui->contactSearchEdit->text().trimmed());
        ContactWorkspaceRow* row = selectedRow();
        if (row) {
            lines << QStringLiteral("当前选中:%1").arg(row->title);
            lines << QStringLiteral("当前详情:%1").arg(row->detail);
        }
        return lines.join(QLatin1Char('\n'));
    };
    auto contactClipboardText = [=, &rows]() {
        ContactWorkspaceRow* row = selectedRow();
        if (!row) {
            return contactStatusText();
        }
        return row->preview.trimmed().isEmpty() ? contactStatusText() : row->preview;
    };

    auto updateActionState = [=, &rows]() {
        ContactWorkspaceRow* row = selectedRow();
        const bool hasSelection = row != nullptr;
        const bool hasActionableRow = hasEnabledListRow(listWidget);
        openBtn->setEnabled(hasSelection);
        friendManagerBtn->setEnabled(true);
        friendManagerBtn->setToolTip(QStringLiteral("直接跳转到好友管理器"));
        if (!hasSelection) {
            openBtn->setText(QStringLiteral("执行当前动作"));
            openBtn->setToolTip(hasActionableRow
                                    ? QStringLiteral("先选择一个联系人、群聊或入口动作后继续进入会话或工作区")
                                    : QStringLiteral("当前没有可执行的联系人或群聊入口"));
            searchBtn->setEnabled(false);
            searchBtn->setToolTip(QStringLiteral("先选择一个可搜索的 QQ，或直接进入好友申请工作区"));
            copyCardBtn->setEnabled(hasActionableRow);
            copyCardBtn->setToolTip(hasActionableRow
                                        ? QStringLiteral("复制当前联系人工作区总览")
                                        : QStringLiteral("当前没有可复制的联系人摘要"));
            copyStatusBtn->setEnabled(true);
            copyStatusBtn->setToolTip(QStringLiteral("复制联系人工作区当前状态"));
            return;
        }
        if (row->rowId == QLatin1String("profile-overview")) {
            openBtn->setText(QStringLiteral("打开账号工作区"));
        } else if (row->rowId == QLatin1String("action-quick-add")) {
            openBtn->setText(QStringLiteral("好友申请工作区"));
        } else if (row->rowId == QLatin1String("action-global-search")) {
            openBtn->setText(QStringLiteral("打开综合搜索"));
        } else if (row->rowId == QLatin1String("action-friend-manager")) {
            openBtn->setText(QStringLiteral("打开好友管理"));
        } else if (row->rowId == QLatin1String("pending-search") || row->rowId.startsWith(QStringLiteral("search_add:"))) {
            openBtn->setText(QStringLiteral("搜索并申请"));
        } else if (row->rowId.startsWith(QStringLiteral("create_group:"))) {
            openBtn->setText(QStringLiteral("创建群聊"));
        } else if (m_localGroupIds.contains(row->rowId)) {
            openBtn->setText(QStringLiteral("进入群聊"));
        } else {
            openBtn->setText(QStringLiteral("打开私聊"));
        }
        openBtn->setToolTip(row->detail);
        const bool searchCapable = row->rowId.startsWith(QStringLiteral("search_add:"))
            || (!m_localGroupIds.contains(row->rowId)
                && row->actionKey == QStringLiteral("open-target")
                && !row->rowId.startsWith(QStringLiteral("create_group:"))
                && row->rowId != QStringLiteral("profile-overview")
                && !row->rowId.startsWith(QStringLiteral("action-"))
                && row->rowId != QStringLiteral("pending-search"));
        searchBtn->setEnabled(searchCapable || row->rowId == QStringLiteral("pending-search"));
        searchBtn->setToolTip(row->rowId == QStringLiteral("pending-search")
                                  ? QStringLiteral("用当前搜索框内容发起搜索和好友申请")
                                  : (searchCapable
                                         ? QStringLiteral("把当前选中 QQ 带入搜索或申请流程")
                                         : QStringLiteral("当前项不支持直接搜索 QQ")));
        copyCardBtn->setEnabled(true);
        copyCardBtn->setToolTip(QStringLiteral("复制当前选中联系人、群聊或入口卡"));
        copyStatusBtn->setEnabled(true);
        copyStatusBtn->setToolTip(QStringLiteral("复制联系人工作区当前状态"));
    };

    auto runOpenSelected = [=, &rows, this, &dialog]() {
        ContactWorkspaceRow* row = selectedRow();
        if (!row) {
            ui->statusbar->showMessage(QStringLiteral("请先选择联系人或工作区入口"), 1800);
            return;
        }
        if (row->rowId == QStringLiteral("profile-overview")) {
            dialog.accept();
            showProfileWorkspace();
            return;
        }
        if (row->rowId == QStringLiteral("action-quick-add")) {
            dialog.accept();
            onShowQuickAddFriend();
            return;
        }
        if (row->rowId == QStringLiteral("action-global-search")) {
            dialog.accept();
            onShowGlobalSearch();
            return;
        }
        if (row->rowId == QStringLiteral("action-friend-manager")) {
            dialog.accept();
            onShowFriendManager();
            return;
        }
        if (row->rowId == QStringLiteral("pending-search")) {
            const QString text = ui->contactSearchEdit->text().trimmed();
            if (text.isEmpty()) {
                ui->statusbar->showMessage(QStringLiteral("当前搜索框为空"), 1600);
                return;
            }
            dialog.accept();
            searchAndAddAccount(text, this);
            return;
        }
        dialog.accept();
        openUserTargetById(row->rowId);
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, [runOpenSelected](QListWidgetItem*) {
        runOpenSelected();
    });
    connect(openBtn, &QPushButton::clicked, &dialog, runOpenSelected);
    connect(friendManagerBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowFriendManager();
    });
    connect(searchBtn, &QPushButton::clicked, &dialog, [this, &rows, &dialog, selectedRow]() {
        ContactWorkspaceRow* row = selectedRow();
        if (!row) {
            return;
        }
        QString account;
        if (row->rowId == QStringLiteral("pending-search")) {
            account = ui->contactSearchEdit->text().trimmed();
        } else if (row->rowId.startsWith(QStringLiteral("search_add:"))) {
            account = row->rowId.mid(QStringLiteral("search_add:").size());
        } else if (!m_localGroupIds.contains(row->rowId)
                   && row->actionKey == QStringLiteral("open-target")
                   && !row->rowId.startsWith(QStringLiteral("create_group:"))
                   && !row->rowId.startsWith(QStringLiteral("action-"))
                   && row->rowId != QStringLiteral("profile-overview")) {
            account = row->rowId;
        }
        if (account.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("当前项不支持直接搜索 QQ"), 1800);
            return;
        }
        dialog.accept();
        searchAndAddAccount(account, this);
    });
    connect(copyCardBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        copyTextWithStatus(contactClipboardText(), QStringLiteral("联系人工作区资料卡已复制"), 2200);
    });
    connect(copyStatusBtn, &QPushButton::clicked, &dialog, [=, &rows, this]() {
        copyTextWithStatus(contactStatusText(), QStringLiteral("联系人工作区状态已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    const QString initialSearchText = initialFilter.trimmed();
    if (!initialSearchText.isEmpty()) {
        searchEdit->setText(initialSearchText);
    }
    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(QStringLiteral("当前账号：%1 · 好友 %2 · 群聊 %3").arg(m_currentUserId).arg(m_friendIds.size()).arg(m_localGroupIds.size()));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    if (!initialSearchText.isEmpty()) {
        searchEdit->selectAll();
    }
    dialog.exec();
}

void MainWindow::onShowNotificationWorkspace() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("notificationWorkspaceDialog"),
        QStringLiteral("通知控制台"),
        QSize(960, 760),
        QStringLiteral("managerTitle"),
        QStringLiteral("通知控制台"),
        QStringLiteral("managerSubTitle"),
        QStringLiteral("把好友通知、群通知、批量处理和通知摘要统一收进同一个稳定工作面。"),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索通知动作"),
        QStringLiteral("按好友通知、群通知、批量处理或通知摘要关键词筛选"),
        QStringLiteral("managerList"),
        true,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先选中一项，再决定进入好友通知、群通知，或复制当前通知控制台摘要与处理计划。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("这里会解释当前通知动作会如何影响好友申请、群聊通知和批量处理流程。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    moveWorkspaceShellStatsToHeader(shell);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* listWidget = shell.listWidget;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* previewLabel = shell.previewLabel;
    QLabel* subTitleLabel = shell.subTitleLabel;

    struct NotificationWorkspaceRow {
        QString id;
        QString title;
        QString detail;
        QString preview;
        QString keywords;
        bool accent = false;
        bool dangerous = false;
    };

    auto notificationSummaryText = [this]() {
        const FriendNoticeUiState friendState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
        const GroupNoticeUiState groupState = m_groupManager.noticeUiState(m_localGroupIds.size());
        const int publicGroupMembers = publicGroupNoticeMemberIds().size();
        QStringList lines;
        lines << QStringLiteral("通知控制台摘要");
        lines << QStringLiteral("我的QQ:%1 · 昵称:%2").arg(m_currentUserId, m_currentUserName);
        lines << QStringLiteral("当前会话:%1").arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室")
                                                                                  : contactDisplayName(m_privateChatTarget));
        lines << QStringLiteral("好友申请:%1 · 已有好友:%2").arg(m_pendingFriendRequests.size()).arg(m_friendIds.size());
        lines << QStringLiteral("群通知入口:%1 · 公共聊天室在线:%2").arg(m_localGroupIds.size() + 1).arg(publicGroupMembers);
        lines << QStringLiteral("好友通知按钮:%1").arg(friendState.text);
        lines << QStringLiteral("群通知按钮:%1").arg(groupState.text);
        return lines.join(QLatin1Char('\n'));
    };

    auto notificationFriendTargets = [this]() {
        QList<FriendManagerVisibleTargetSummary> targets;
        QStringList seenIds;
        for (const QString& pendingId : m_pendingFriendRequests) {
            const QString trimmedId = pendingId.trimmed();
            if (trimmedId.isEmpty() || seenIds.contains(trimmedId)) {
                continue;
            }
            seenIds << trimmedId;
            targets << friendNoticeVisibleTarget(trimmedId);
        }
        return targets;
    };
    auto notificationFriendPlanText = [this, notificationFriendTargets]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendNoticeBatchPlanState(
            m_currentUserId,
            m_currentUserName,
            m_pendingFriendRequests.size(),
            QString(),
            notificationFriendTargets());
        return state.text;
    };
    auto notificationGroupTargets = [this]() {
        QList<GroupNoticeBatchTargetInput> targets;

        GroupNoticeBatchTargetInput publicTarget;
        publicTarget.entryId = QString();
        publicTarget.groupName = QStringLiteral("公共聊天室");
        publicTarget.groupNumber = QStringLiteral("公共聊天室");
        publicTarget.memberCount = publicGroupNoticeMemberIds().size();
        publicTarget.onlineCount = publicTarget.memberCount;
        targets << publicTarget;

        for (const QString& groupId : m_localGroupIds) {
            GroupNoticeBatchTargetInput target;
            target.entryId = groupId;
            target.groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
            target.groupNumber = groupId.mid(QStringLiteral("local_group_").size());
            const QStringList members = groupNoticeMemberIds(groupId);
            int groupOnline = 0;
            for (const QString& memberId : members) {
                if (memberId == m_currentUserId || isContactOnline(memberId)) {
                    ++groupOnline;
                }
            }
            target.memberCount = qMax(1, members.size());
            target.onlineCount = groupOnline;
            targets << target;
        }
        return targets;
    };
    auto notificationGroupPlanText = [this, notificationGroupTargets]() {
        const GroupNoticeBatchPlanState state = NotificationPanelManager::groupNoticeBatchPlanState(
            QString(),
            notificationGroupTargets(),
            m_currentUserName,
            m_currentUserId);
        return state.text;
    };

    auto buildRows = [this, notificationSummaryText, notificationFriendPlanText, notificationGroupPlanText]() {
        QList<NotificationWorkspaceRow> rows;
        const FriendNoticeUiState friendState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
        const GroupNoticeUiState groupState = m_groupManager.noticeUiState(m_localGroupIds.size());

        rows << NotificationWorkspaceRow{
            QStringLiteral("notice-overview"),
            QStringLiteral("通知总览"),
            QStringLiteral("好友申请 %1 · 群通知入口 %2").arg(m_pendingFriendRequests.size()).arg(m_localGroupIds.size() + 1),
            notificationSummaryText(),
            QStringLiteral("通知 总览 控制台 好友 群聊"),
            true,
            false};
        rows << NotificationWorkspaceRow{
            QStringLiteral("notice-friend"),
            QStringLiteral("好友通知"),
            friendState.toolTip,
            QStringLiteral("好友通知工作区\n集中处理待通过好友申请、批量回复、申请话术和媒体准备。\n\n%1")
                .arg(notificationFriendPlanText()),
            QStringLiteral("好友通知 好友申请 批量回复"),
            m_pendingFriendRequests.size() > 0,
            false};
        rows << NotificationWorkspaceRow{
            QStringLiteral("notice-group"),
            QStringLiteral("群通知"),
            groupState.toolTip,
            QStringLiteral("群通知工作区\n集中处理群入口、群公告、成员复制、媒体包和批量计划。\n\n%1")
                .arg(notificationGroupPlanText()),
            QStringLiteral("群通知 群聊 公告 成员"),
            false,
            false};
        rows << NotificationWorkspaceRow{
            QStringLiteral("notice-copy-summary"),
            QStringLiteral("复制通知摘要"),
            QStringLiteral("复制好友申请、群聊入口和当前通知按钮状态"),
            QStringLiteral("复制通知摘要\n适合留档、反馈当前待处理量和入口状态。"),
            QStringLiteral("复制 通知 摘要"),
            false,
            false};
        rows << NotificationWorkspaceRow{
            QStringLiteral("notice-copy-friend-plan"),
            QStringLiteral("复制好友处理计划"),
            QStringLiteral("汇总当前好友申请和通过后的媒体准备"),
            QStringLiteral("好友处理计划\n把当前待处理好友申请、回复流程和媒体发送准备整理成一份摘要。"),
            QStringLiteral("好友 处理 计划 媒体"),
            false,
            false};
        rows << NotificationWorkspaceRow{
            QStringLiteral("notice-copy-group-plan"),
            QStringLiteral("复制群处理计划"),
            QStringLiteral("汇总当前群入口、成员复制和群内媒体计划"),
            QStringLiteral("群处理计划\n把当前群入口、成员同步和群内媒体发送准备整理成一份摘要。"),
            QStringLiteral("群 处理 计划 成员 媒体"),
            false,
            false};

        return rows;
    };

    auto rows = buildRows();
    auto emptyPreviewText = [searchEdit]() {
        const QString filter = searchEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有可见的通知动作。可从这里继续进入好友通知、群通知或复制处理计划。")
            : QStringLiteral("当前筛选词“%1”没有匹配到通知动作。\n试试“好友通知”、“群通知”、“计划”或“摘要”这些关键词。").arg(filter);
    };

    auto fillList = [=, &rows]() {
        const QString filter = searchEdit->text().trimmed();
        listWidget->clear();
        int visibleCount = 0;
        for (const NotificationWorkspaceRow& row : rows) {
            if (!filter.isEmpty() && !row.keywords.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(row.title, row.detail));
            item->setData(Qt::UserRole, row.id);
            item->setToolTip(row.preview);
            item->setSizeHint(QSize(0, 78));
            if (row.accent) {
                item->setForeground(QColor(29, 78, 216));
            } else if (row.dangerous) {
                item->setForeground(QColor(180, 35, 24));
            }
            listWidget->addItem(item);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            addWorkspaceEmptyStateItem(
                listWidget,
                QStringLiteral("没有匹配的通知动作"),
                QStringLiteral("试试“好友通知”、“群通知”、“计划”或“摘要”这些关键词。"),
                emptyPreviewText());
        }
        statsLabel->setText(QStringLiteral("可见 %1 / %2 项").arg(visibleCount).arg(rows.size()));
        selectPreferredListRow(listWidget, 0);
    };

    auto selectedRow = [=, &rows]() -> NotificationWorkspaceRow* {
        QListWidgetItem* currentItem = listWidget->currentItem();
        if (!currentItem) {
            return nullptr;
        }
        const QString id = currentItem->data(Qt::UserRole).toString();
        for (NotificationWorkspaceRow& row : rows) {
            if (row.id == id) {
                return &row;
            }
        }
        return nullptr;
    };

    auto updatePreview = [=, &rows]() {
        NotificationWorkspaceRow* row = selectedRow();
        previewLabel->setText(row ? row->preview
                                  : (firstEnabledListRow(listWidget) >= 0
                                         ? QStringLiteral("这里会解释当前通知动作会如何影响好友申请、群聊通知和批量处理流程。")
                                         : emptyPreviewText()));
    };
    auto rowClipboardText = [=, &rows]() {
        NotificationWorkspaceRow* row = selectedRow();
        if (!row) {
            return notificationSummaryText();
        }
        if (row->id == QLatin1String("notice-copy-friend-plan")) {
            return notificationFriendPlanText();
        }
        if (row->id == QLatin1String("notice-copy-group-plan")) {
            return notificationGroupPlanText();
        }
        if (row->id == QLatin1String("notice-friend")) {
            return QStringLiteral("%1\n\n%2").arg(notificationSummaryText(), notificationFriendPlanText());
        }
        if (row->id == QLatin1String("notice-group")) {
            return QStringLiteral("%1\n\n%2").arg(notificationSummaryText(), notificationGroupPlanText());
        }
        return row->preview.trimmed().isEmpty() ? notificationSummaryText() : row->preview;
    };
    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("执行当前动作"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("执行当前通知动作"), QStyle::SP_ArrowForward);
    QPushButton* copyCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制通知卡"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前通知摘要"), QStyle::SP_DialogSaveButton);
    QPushButton* copyStatusBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制工作区状态"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制通知控制台当前状态"), QStyle::SP_MessageBoxInformation);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭通知控制台"), QStyle::SP_DialogCloseButton);

    auto updateActionState = [=, &rows]() {
        NotificationWorkspaceRow* row = selectedRow();
        const bool hasActionableRow = hasEnabledListRow(listWidget);
        if (!row) {
            openBtn->setEnabled(false);
            openBtn->setText(QStringLiteral("执行当前动作"));
            openBtn->setToolTip(hasActionableRow
                                    ? QStringLiteral("先选择一个通知动作后继续进入好友通知、群通知或复制处理计划")
                                    : QStringLiteral("当前没有可执行的通知动作"));
            copyCardBtn->setEnabled(hasActionableRow);
            copyCardBtn->setToolTip(hasActionableRow
                                        ? QStringLiteral("复制当前通知控制台总览")
                                        : QStringLiteral("当前没有可复制的通知卡"));
            copyStatusBtn->setEnabled(true);
            copyStatusBtn->setToolTip(QStringLiteral("复制通知控制台当前状态"));
            return;
        }
        openBtn->setEnabled(true);
        if (row->id == QLatin1String("notice-friend")) {
            openBtn->setText(QStringLiteral("进入好友通知"));
            openBtn->setToolTip(QStringLiteral("打开好友通知工作区"));
        } else if (row->id == QLatin1String("notice-group")) {
            openBtn->setText(QStringLiteral("进入群通知"));
            openBtn->setToolTip(QStringLiteral("打开群通知工作区"));
        } else if (row->id == QLatin1String("notice-copy-friend-plan")) {
            openBtn->setText(QStringLiteral("复制好友计划"));
            openBtn->setToolTip(QStringLiteral("复制好友通知处理计划"));
        } else if (row->id == QLatin1String("notice-copy-group-plan")) {
            openBtn->setText(QStringLiteral("复制群计划"));
            openBtn->setToolTip(QStringLiteral("复制群通知处理计划"));
        } else {
            openBtn->setText(QStringLiteral("复制通知摘要"));
            openBtn->setToolTip(QStringLiteral("复制通知控制台摘要"));
        }
        copyCardBtn->setEnabled(true);
        copyCardBtn->setToolTip(QStringLiteral("复制当前选中通知卡"));
        copyStatusBtn->setEnabled(true);
        copyStatusBtn->setToolTip(QStringLiteral("复制通知控制台当前状态"));
    };

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("通知动作"),
        QStringLiteral("统一进入好友通知、群通知和批量处理摘要，减少入口来回切换。"),
        {openBtn, copyCardBtn, copyStatusBtn},
        closeBtn);

    auto runSelectedCommand = [=, &rows, this, &dialog]() {
        NotificationWorkspaceRow* row = selectedRow();
        if (!row) {
            return;
        }
        const QString commandId = row->id;
        if (commandId == QLatin1String("notice-friend")) {
            dialog.accept();
            onShowFriendNotifications();
            return;
        }
        if (commandId == QLatin1String("notice-group")) {
            dialog.accept();
            onShowGroupNotifications();
            return;
        }
        if (commandId == QLatin1String("notice-overview")
            || commandId == QLatin1String("notice-copy-summary")) {
            copyTextWithStatus(notificationSummaryText(), QStringLiteral("通知摘要已复制"), 2200);
            return;
        }
        if (commandId == QLatin1String("notice-copy-friend-plan")) {
            copyTextWithStatus(notificationFriendPlanText(), QStringLiteral("好友处理计划已复制"), 2200);
            return;
        }
        if (commandId == QLatin1String("notice-copy-group-plan")) {
            copyTextWithStatus(notificationGroupPlanText(), QStringLiteral("群处理计划已复制"), 2200);
            return;
        }
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [=, &rows](const QString&) {
        rows = buildRows();
        fillList();
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem*, QListWidgetItem*) {
        updatePreview();
        updateActionState();
    });
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, [runSelectedCommand](QListWidgetItem*) {
        runSelectedCommand();
    });
    connect(openBtn, &QPushButton::clicked, &dialog, runSelectedCommand);
    connect(copyCardBtn, &QPushButton::clicked, &dialog, [=, this]() {
        copyTextWithStatus(rowClipboardText(), QStringLiteral("通知卡已复制"), 2200);
    });
    connect(copyStatusBtn, &QPushButton::clicked, &dialog, [=, this]() {
        QStringList lines;
        lines << notificationSummaryText();
        lines << QString();
        lines << notificationFriendPlanText();
        lines << QString();
        lines << notificationGroupPlanText();
        copyTextWithStatus(lines.join(QLatin1Char('\n')), QStringLiteral("通知控制台状态已复制"), 2200);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    rows = buildRows();
    fillList();
    updatePreview();
    updateActionState();
    subTitleLabel->setText(QStringLiteral("好友申请 %1 · 群通知入口 %2 · 当前会话 %3")
                               .arg(m_pendingFriendRequests.size())
                               .arg(m_localGroupIds.size() + 1)
                               .arg(m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室")
                                                                  : contactDisplayName(m_privateChatTarget)));
    dialog.setStyleSheet(productDialogStyleSheet());
    searchEdit->setFocus();
    dialog.exec();
}

void MainWindow::onShowFriendManager() {
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("friendManagerDialog"),
        QStringLiteral("好友管理器"),
        QSize(900, 700),
        QStringLiteral("managerTitle"),
        QStringLiteral("好友管理器"),
        QStringLiteral("managerSubTitle"),
        QString("当前 QQ：%1 · 好友 %2 人").arg(m_currentUserId).arg(m_friendIds.size()),
        QStringLiteral("managerSearch"),
        QStringLiteral("搜索好友 QQ 号 / 昵称"),
        QStringLiteral("按 QQ 号或昵称筛选好友；无结果时可直接搜索并申请。"),
        QStringLiteral("managerList"),
        false,
        QStringLiteral("managerOperationGuide"),
        QStringLiteral("先筛选好友，再决定发消息、加备注、邀入群或发起新的好友申请。"),
        QStringLiteral("managerStats"),
        QStringLiteral("managerSelectionPreview"),
        QStringLiteral("选择好友后可直接发消息，也能整理名片、邀请语和群聊操作。"),
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(26, 18, 26, 16);
    shell.bodyLayout->setContentsMargins(24, 22, 24, 22);
    shell.bodyLayout->setSpacing(12);

    QLineEdit* searchEdit = shell.searchEdit;
    QListWidget* friendList = shell.listWidget;
    QLabel* subTitleLabel = shell.subTitleLabel;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* selectionPreviewLabel = shell.previewLabel;
    QLabel* operationGuideLabel = shell.hintLabel;
    const QString friendManagerEmptyPreviewText = QStringLiteral("当前没有可见的好友入口。可直接发起好友申请，或稍后刷新好友列表。");
    const QString friendManagerNoSelectionPreviewText =
        m_friendManager.managerSelectionPreviewUiState(QString(), QString(), false, false).text;

    auto fillList = [this, friendList, subTitleLabel, statsLabel, friendManagerEmptyPreviewText](const QString& filter = QString()) {
        friendList->clear();
        const FriendManagerListRenderUiState listState = m_friendManager.managerListRenderUiState(
            m_currentUserId,
            m_friendIds,
            m_localGroupIds,
            m_friendNames,
            m_knownUsers,
            filter);
        for (const FriendManagerListEntryUiState& entry : listState.entries) {
            QListWidgetItem* item = new QListWidgetItem(entry.text);
            item->setData(Qt::UserRole, entry.entryId);
            item->setSizeHint(QSize(0, entry.rowHeight));
            if (entry.muted) {
                item->setForeground(QColor(135, 150, 165));
            }
            friendList->addItem(item);
        }
        if (listState.entries.isEmpty()) {
            addWorkspaceEmptyStateItem(
                friendList,
                QStringLiteral("没有匹配的好友入口"),
                QStringLiteral("试试 QQ、昵称，或直接搜索并申请。"),
                filter.trimmed().isEmpty()
                    ? friendManagerEmptyPreviewText
                    : QStringLiteral("当前筛选词“%1”没有匹配到好友入口。\n试试 QQ、昵称，或直接搜索并申请。").arg(filter.trimmed()));
        } else {
            selectPreferredListRow(friendList, 0);
        }
        subTitleLabel->setText(listState.summary.subTitle);
        statsLabel->setText(listState.summary.statsText);
    };
    fillList();
    operationGuideLabel->setWordWrap(true);

    QPushButton* addBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("好友申请工作区"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("打开好友申请工作区，输入 QQ 号后搜索并申请"), QStyle::SP_FileDialogNewFolder);
    QPushButton* searchAddBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("搜索并申请"), QStringLiteral("managerPrimaryBtn"), QStringLiteral("使用当前搜索框内容搜索 QQ 并发起好友申请"), QStyle::SP_FileDialogContentsView);
    QPushButton* clearSearchBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("清空搜索"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("清空筛选条件并显示全部好友"), QStyle::SP_DialogResetButton);
    QPushButton* chatBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("发消息"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("打开当前选中好友的私聊会话"), QStyle::SP_MessageBoxInformation);
    QPushButton* copyBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制QQ"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前选中好友的 QQ 号"), QStyle::SP_DialogSaveButton);
    QPushButton* copyAllBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制可见列表"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前筛选出的好友列表"), QStyle::SP_FileDialogListView);
    QPushButton* profileBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制名片"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前选中好友的 QQ、昵称和在线状态"), QStyle::SP_FileDialogInfoView);
    QPushButton* inviteTextBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制邀请语"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制一段邀请当前好友加入群聊的话术"), QStyle::SP_DirLinkIcon);
    QPushButton* copyStatsBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制好友统计"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制好友数量、在线状态和群聊统计"), QStyle::SP_FileDialogDetailedView);
    QPushButton* copyOnlineBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制在线"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前可见列表中的在线好友"), QStyle::SP_DialogYesButton);
    QPushButton* copySearchCardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制搜索卡片"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前搜索条件、选中好友和可见结果摘要"), QStyle::SP_FileDialogContentsView);
    QPushButton* copyFriendMediaPackBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制好友媒体包"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制给好友发送图片、视频或文件前的准备摘要"), QStyle::SP_FileIcon);
    QPushButton* copyBatchMediaPlanBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制批量媒体计划"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制当前可见好友的批量媒体发送计划"), QStyle::SP_DriveHDIcon);
    QPushButton* copyMediaGuideBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("复制上传指南"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("复制好友私聊中发送图片、视频和文件的简短指南"), QStyle::SP_DialogHelpButton);
    QPushButton* remarkBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("备注"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("修改当前选中好友在本地显示的备注名"), QStyle::SP_FileDialogDetailedView);
    QPushButton* inviteBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("邀入群"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("把当前选中好友邀请进最近的本地群聊"), QStyle::SP_DialogOpenButton);
    QPushButton* inviteVisibleBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("邀请可见"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("把当前筛选出的可见好友批量邀请进群聊"), QStyle::SP_CommandLink);
    QPushButton* deleteBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("删除好友"), QStringLiteral("managerDangerBtn"), QStringLiteral("从本地好友列表中删除当前选中好友"), QStyle::SP_TrashIcon);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, QStringLiteral("关闭"), QStringLiteral("managerSecondaryBtn"), QStringLiteral("关闭好友管理器"), QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("主操作"),
        QStringLiteral("先打开会话或直接发起好友申请，再决定群聊邀请和备注管理。"),
        {addBtn, searchAddBtn, clearSearchBtn, chatBtn, remarkBtn, inviteBtn, inviteVisibleBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("复制与统计"),
        QStringLiteral("把当前好友、可见列表、搜索摘要和邀请话术整理出去。"),
        {copyBtn, copyAllBtn, profileBtn, inviteTextBtn, copyStatsBtn, copyOnlineBtn, copySearchCardBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("媒体与批量计划"),
        QStringLiteral("为好友私聊里的图片、视频和文件发送准备媒体包、批量计划和上传指南。"),
        {copyFriendMediaPackBtn, copyBatchMediaPlanBtn, copyMediaGuideBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("危险操作"),
        QStringLiteral("删除好友会移出本地好友列表，但后续仍可重新搜索并申请。"),
        {deleteBtn});

    dialog.setStyleSheet(productDialogStyleSheet());

    auto openSelectedFriend = [this, &dialog, friendList]() {
        QListWidgetItem* selected = friendList->currentItem();
        if (!selected) {
            ui->statusbar->showMessage("请先选择要发消息的好友", 1800);
            return;
        }
        QString id = selected->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            ui->statusbar->showMessage("暂无可打开的好友会话", 1800);
            return;
        }
        if (id.startsWith("search_add:")) {
            dialog.accept();
            searchAndAddAccount(id.mid(QString("search_add:").size()), this);
            return;
        }
        dialog.accept();
        openPrivateSession(id);
        ui->statusbar->showMessage(QString("已打开与 %1 的私聊").arg(contactDisplayName(id)), 1800);
    };

    auto updateSelectionPreview = [this, friendList, selectionPreviewLabel, friendManagerNoSelectionPreviewText, friendManagerEmptyPreviewText]() {
        QListWidgetItem* selected = friendList->currentItem();
        if (!selected) {
            selectionPreviewLabel->setText(firstEnabledListRow(friendList) >= 0
                                               ? friendManagerNoSelectionPreviewText
                                               : friendManagerEmptyPreviewText);
            return;
        }
        const QString id = selected->data(Qt::UserRole).toString();
        selectionPreviewLabel->setText(
            m_friendManager.managerSelectionPreviewUiState(
                id,
                id.isEmpty() ? QString() : contactDisplayName(id),
                !id.isEmpty() && isContactOnline(id),
                m_privateChatTarget.startsWith("local_group_")).text);
    };
    updateSelectionPreview();

    auto updateActionState = [=, this]() {
        const QString searchText = searchEdit->text().trimmed();
        QListWidgetItem* selected = friendList->currentItem();
        const QString entryId = selected ? selected->data(Qt::UserRole).toString() : QString();
        const bool hasSelection = !entryId.isEmpty();
        const bool searchAddSelection = entryId.startsWith(QStringLiteral("search_add:"));
        const QString friendId = searchAddSelection ? entryId.mid(QStringLiteral("search_add:").size()) : entryId;
        const bool validFriendSelection = hasSelection && !searchAddSelection;
        const QString friendName = friendId.isEmpty() ? QString() : contactDisplayName(friendId);
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        bool hasVisibleOnline = false;
        for (const QString& visibleId : visibleIds) {
            if (isContactOnline(visibleId)) {
                hasVisibleOnline = true;
                break;
            }
        }

        addBtn->setText(QStringLiteral("好友申请工作区"));
        addBtn->setToolTip(QStringLiteral("打开好友申请工作区"));
        searchAddBtn->setText(QStringLiteral("搜索并申请"));
        searchAddBtn->setEnabled(!searchText.isEmpty());
        searchAddBtn->setToolTip(searchText.isEmpty()
                                     ? QStringLiteral("先输入 QQ 账号再搜索并申请")
                                     : QStringLiteral("搜索当前输入的 QQ 并发起好友申请"));
        clearSearchBtn->setEnabled(!searchText.isEmpty());
        clearSearchBtn->setToolTip(searchText.isEmpty()
                                       ? QStringLiteral("当前没有需要清空的筛选条件")
                                       : QStringLiteral("清空当前好友筛选"));

        if (!hasSelection) {
            chatBtn->setText(QStringLiteral("打开私聊"));
            chatBtn->setEnabled(false);
            chatBtn->setToolTip(QStringLiteral("请先选择一个好友或搜索建议项"));
            copyBtn->setEnabled(false);
            copyBtn->setToolTip(QStringLiteral("请先选择一个好友再复制 QQ"));
            profileBtn->setEnabled(false);
            profileBtn->setToolTip(QStringLiteral("请先选择一个好友再复制名片"));
            remarkBtn->setText(QStringLiteral("设置备注"));
            remarkBtn->setEnabled(false);
            remarkBtn->setToolTip(QStringLiteral("请先选择一个好友再设置备注"));
            inviteBtn->setText(QStringLiteral("邀请入群"));
            inviteBtn->setEnabled(false);
            inviteBtn->setToolTip(QStringLiteral("请先选择一个好友再邀请入群"));
            deleteBtn->setEnabled(false);
            deleteBtn->setToolTip(QStringLiteral("请先选择一个好友再删除"));
            inviteTextBtn->setEnabled(!searchText.isEmpty() || !visibleIds.isEmpty());
            inviteTextBtn->setToolTip(inviteTextBtn->isEnabled()
                                          ? QStringLiteral("复制当前筛选上下文的邀请话术")
                                          : QStringLiteral("先选中好友或输入搜索条件再复制邀请语"));
            copyAllBtn->setEnabled(!visibleIds.isEmpty());
            copyAllBtn->setToolTip(copyAllBtn->isEnabled()
                                       ? QStringLiteral("复制当前筛选出的好友列表")
                                       : QStringLiteral("当前没有可复制的可见好友列表"));
            copyStatsBtn->setEnabled(!visibleIds.isEmpty() || !searchText.isEmpty() || !m_friendIds.isEmpty());
            copyStatsBtn->setToolTip(QStringLiteral("复制好友数量、在线状态和当前可见列表统计"));
            copyOnlineBtn->setEnabled(hasVisibleOnline);
            copyOnlineBtn->setToolTip(hasVisibleOnline
                                          ? QStringLiteral("复制当前可见列表中的在线好友")
                                          : QStringLiteral("当前没有可复制的在线好友"));
            copySearchCardBtn->setEnabled(!searchText.isEmpty() || !visibleIds.isEmpty());
            copySearchCardBtn->setToolTip(copySearchCardBtn->isEnabled()
                                              ? QStringLiteral("复制当前搜索条件、选中好友和可见结果摘要")
                                              : QStringLiteral("先输入搜索条件或筛出好友再复制搜索卡片"));
            copyFriendMediaPackBtn->setEnabled(!searchText.isEmpty());
            copyFriendMediaPackBtn->setToolTip(copyFriendMediaPackBtn->isEnabled()
                                                   ? QStringLiteral("复制当前搜索或筛选上下文下的好友媒体准备摘要")
                                                   : QStringLiteral("先输入搜索条件再复制好友媒体包"));
            copyBatchMediaPlanBtn->setEnabled(!visibleIds.isEmpty());
            copyBatchMediaPlanBtn->setToolTip(copyBatchMediaPlanBtn->isEnabled()
                                                  ? QStringLiteral("复制当前可见好友的批量媒体发送计划")
                                                  : QStringLiteral("当前没有可复制的批量媒体计划"));
            copyMediaGuideBtn->setEnabled(true);
            copyMediaGuideBtn->setToolTip(QStringLiteral("复制好友私聊中发送图片、视频和文件的简短指南"));
            return;
        }

        chatBtn->setText(searchAddSelection ? QStringLiteral("搜索并申请") : QStringLiteral("打开私聊"));
        chatBtn->setEnabled(true);
        chatBtn->setToolTip(searchAddSelection
                                ? QStringLiteral("搜索 QQ:%1 并发起好友申请").arg(friendId)
                                : QStringLiteral("打开 %1 的私聊会话").arg(friendName));
        copyBtn->setEnabled(validFriendSelection);
        copyBtn->setToolTip(validFriendSelection
                                ? QStringLiteral("复制 %1 的 QQ 号").arg(friendName)
                                : QStringLiteral("搜索建议项没有可直接复制的好友 QQ"));
        profileBtn->setEnabled(validFriendSelection);
        profileBtn->setToolTip(validFriendSelection
                                   ? QStringLiteral("复制 %1 的名片").arg(friendName)
                                   : QStringLiteral("搜索建议项没有可直接复制的好友名片"));
        remarkBtn->setText(QStringLiteral("设置备注"));
        remarkBtn->setEnabled(validFriendSelection);
        remarkBtn->setToolTip(validFriendSelection
                                  ? QStringLiteral("修改 %1 的本地备注").arg(friendName)
                                  : QStringLiteral("搜索建议项不能直接设置备注"));
        inviteBtn->setText(QStringLiteral("邀请入群"));
        inviteBtn->setEnabled(validFriendSelection);
        inviteBtn->setToolTip(validFriendSelection
                                  ? QStringLiteral("把 %1 邀请进最近的本地群聊").arg(friendName)
                                  : QStringLiteral("搜索建议项不能直接邀请入群"));
        deleteBtn->setEnabled(validFriendSelection);
        deleteBtn->setToolTip(validFriendSelection
                                  ? QStringLiteral("从本地好友列表删除 %1").arg(friendName)
                                  : QStringLiteral("搜索建议项不能直接删除好友"));
        inviteTextBtn->setEnabled(!friendId.isEmpty());
        inviteTextBtn->setToolTip(friendId.isEmpty()
                                      ? QStringLiteral("先选中好友或输入搜索条件再复制邀请语")
                                      : QStringLiteral("复制面向 %1 的邀请话术").arg(friendName.isEmpty() ? friendId : friendName));
        copyAllBtn->setEnabled(!visibleIds.isEmpty());
        copyAllBtn->setToolTip(copyAllBtn->isEnabled()
                                   ? QStringLiteral("复制当前筛选出的好友列表")
                                   : QStringLiteral("当前没有可复制的可见好友列表"));
        copyStatsBtn->setEnabled(true);
        copyStatsBtn->setToolTip(QStringLiteral("复制好友数量、在线状态和当前可见列表统计"));
        copyOnlineBtn->setEnabled(hasVisibleOnline);
        copyOnlineBtn->setToolTip(hasVisibleOnline
                                      ? QStringLiteral("复制当前可见列表中的在线好友")
                                      : QStringLiteral("当前没有可复制的在线好友"));
        copySearchCardBtn->setEnabled(!searchText.isEmpty() || !visibleIds.isEmpty());
        copySearchCardBtn->setToolTip(copySearchCardBtn->isEnabled()
                                          ? QStringLiteral("复制当前搜索条件、选中好友和可见结果摘要")
                                          : QStringLiteral("先输入搜索条件或筛出好友再复制搜索卡片"));
        copyFriendMediaPackBtn->setEnabled(!friendId.isEmpty() || !searchText.isEmpty());
        copyFriendMediaPackBtn->setToolTip(copyFriendMediaPackBtn->isEnabled()
                                               ? QStringLiteral("复制给当前好友或当前筛选上下文准备的媒体摘要")
                                               : QStringLiteral("先选中好友或输入搜索条件再复制好友媒体包"));
        copyBatchMediaPlanBtn->setEnabled(!visibleIds.isEmpty());
        copyBatchMediaPlanBtn->setToolTip(copyBatchMediaPlanBtn->isEnabled()
                                              ? QStringLiteral("复制当前可见好友的批量媒体发送计划")
                                              : QStringLiteral("当前没有可复制的批量媒体计划"));
        copyMediaGuideBtn->setEnabled(true);
        copyMediaGuideBtn->setToolTip(QStringLiteral("复制好友私聊中发送图片、视频和文件的简短指南"));
    };

    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillList, updateSelectionPreview, updateActionState](const QString& text) {
        fillList(text.trimmed());
        updateSelectionPreview();
        updateActionState();
    });
    connect(addBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        onShowQuickAddFriend();
    });
    connect(searchAddBtn, &QPushButton::clicked, &dialog, [this, searchEdit, &dialog]() {
        QString account = searchEdit->text().trimmed();
        if (account.isEmpty()) {
            searchEdit->setFocus();
            ui->statusbar->showMessage("请输入 QQ 账号后搜索并申请", 2200);
            return;
        }
        dialog.accept();
        searchAndAddAccount(account, this);
    });
    connect(clearSearchBtn, &QPushButton::clicked, &dialog, [searchEdit, fillList, updateSelectionPreview, updateActionState]() {
        searchEdit->clear();
        fillList();
        updateSelectionPreview();
        updateActionState();
        searchEdit->setFocus();
    });
    connect(chatBtn, &QPushButton::clicked, &dialog, openSelectedFriend);
    connect(friendList, &QListWidget::currentItemChanged, &dialog, [updateSelectionPreview, updateActionState](QListWidgetItem*, QListWidgetItem*) {
        updateSelectionPreview();
        updateActionState();
    });
    connect(friendList, &QListWidget::itemDoubleClicked, &dialog, [openSelectedFriend](QListWidgetItem*) { openSelectedFriend(); });
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要复制 QQ 的好友", &id)) {
            return;
        }
        copyTextWithStatus(id, "QQ 号已复制: " + id, 2500);
    });
    auto friendCopyInputs = [this](const QStringList& friendIds) {
        QList<FriendManagerContactCopyInput> inputs;
        for (const QString& id : friendIds) {
            FriendManagerContactCopyInput input;
            input.userId = id;
            input.displayName = contactDisplayName(id);
            input.online = isContactOnline(id);
            inputs << input;
        }
        return inputs;
    };
    auto friendManagerVisibleTargets = [this, friendList]() {
        QList<FriendManagerVisibleTargetSummary> targets;
        for (int i = 0; i < friendList->count(); ++i) {
            QListWidgetItem* item = friendList->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                continue;
            }
            FriendManagerVisibleTargetSummary target;
            target.userId = id;
            target.online = isContactOnline(id);
            if (!id.startsWith("search_add:")) {
                target.displayName = contactDisplayName(id);
            }
            targets << target;
        }
        return targets;
    };
    auto selectedFriendManagerVisibleTarget = [this, friendList, searchEdit]() {
        FriendManagerVisibleTargetSummary target;
        target.userId = selectedFriendManagerTargetId(friendList);
        if (target.userId.isEmpty()) {
            target.userId = searchEdit->text().trimmed();
        }
        target.online = isContactOnline(target.userId);
        if (!target.userId.isEmpty() && !target.userId.startsWith("search_add:")) {
            target.displayName = contactDisplayName(target.userId);
        }
        return target;
    };
    connect(copyAllBtn, &QPushButton::clicked, &dialog, [this, friendList, friendCopyInputs]() {
        const FriendManagerContactCopyState state =
            FriendManager::managerContactCopyState(friendCopyInputs(visibleFriendManagerIds(friendList)), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        copyTextWithStatus(state.rows.join('\n'), state.copiedStatusMessage, 2200);
    });
    connect(profileBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要复制名片的好友", &id)) {
            return;
        }
        QString card = QString("QQ:%1\n昵称:%2\n状态:%3").arg(id, contactDisplayName(id), isContactOnline(id) ? "在线" : "离线");
        copyTextWithStatus(card, "好友名片已复制");
    });
    connect(inviteTextBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString id = selectedFriendManagerTargetId(friendList);
        QString targetName = id.isEmpty() ? "朋友" : contactDisplayName(id);
        QString groupName = m_privateChatTarget.startsWith("local_group_") ? m_localGroupNames.value(m_privateChatTarget, "群聊") : "群聊";
        QString text = QString("%1，你好，我是 %2（QQ:%3）。方便的话加个好友，我也可以邀请你加入 %4 一起沟通。")
            .arg(targetName, m_currentUserName, m_currentUserId, groupName);
        copyTextWithStatus(text, "好友邀请话术已复制", 2200);
    });
    connect(copyStatsBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        int visibleCount = 0;
        int visibleOnline = 0;
        int visibleOffline = 0;
        QStringList visibleRows;
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        for (const QString& id : visibleIds) {
            bool online = isContactOnline(id);
            ++visibleCount;
            if (online) ++visibleOnline; else ++visibleOffline;
            visibleRows << QString("%1(QQ:%2,%3)").arg(contactDisplayName(id), id, online ? "在线" : "离线");
        }
        QString text = QString("好友统计\n我的QQ:%1\n全部好友:%2\n可见好友:%3\n可见在线:%4\n可见离线:%5\n本地群:%6\n可见列表:%7")
            .arg(m_currentUserId)
            .arg(m_friendIds.size())
            .arg(visibleCount)
            .arg(visibleOnline)
            .arg(visibleOffline)
            .arg(m_localGroupIds.size())
            .arg(visibleRows.isEmpty() ? "无" : visibleRows.join("、"));
        copyTextWithStatus(text, "好友统计已复制", 2200);
    });
    connect(copyOnlineBtn, &QPushButton::clicked, &dialog, [this, friendList, friendCopyInputs]() {
        const FriendManagerContactCopyState state =
            FriendManager::managerContactCopyState(friendCopyInputs(visibleFriendManagerIds(friendList)), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        copyTextWithStatus(state.rows.join('\n'), state.copiedStatusMessage, 2200);
    });
    connect(copySearchCardBtn, &QPushButton::clicked, &dialog, [this, friendManagerVisibleTargets, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendManagerSearchSummaryCardState(
            m_currentUserId,
            m_currentUserName,
            searchEdit->text().trimmed(),
            m_friendIds.size(),
            m_localGroupIds.size(),
            friendManagerVisibleTargets());
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage("好友管理搜索卡片已复制", 2200);
    });
    connect(copyFriendMediaPackBtn, &QPushButton::clicked, &dialog, [this, selectedFriendManagerVisibleTarget, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendManagerMediaPackState(
            m_currentUserId,
            m_currentUserName,
            searchEdit->text().trimmed(),
            m_friendIds.size(),
            selectedFriendManagerVisibleTarget());
        copyTextWithStatus(state.text, "好友管理媒体包已复制", 2200);
    });
    connect(copyBatchMediaPlanBtn, &QPushButton::clicked, &dialog, [this, friendManagerVisibleTargets, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendManagerBatchMediaPlanState(
            m_currentUserId,
            m_currentUserName,
            searchEdit->text().trimmed(),
            friendManagerVisibleTargets());
        copyTextWithStatus(state.text, "好友批量媒体计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, friendList, searchEdit]() {
        const int visibleCount = visibleFriendManagerIds(friendList).size();
        copyTextWithStatus(
            FriendManager::friendManagerMediaGuideText(
                m_currentUserId,
                m_currentUserName,
                searchEdit->text().trimmed(),
                visibleCount,
                m_friendIds.size()),
            "好友管理上传指南已复制",
            2200);
    });
    connect(remarkBtn, &QPushButton::clicked, &dialog, [this, friendList, fillList, searchEdit, &dialog]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要备注的好友", &id)) {
            return;
        }
        const QString oldRemark = contactDisplayName(id);
        bool ok = false;
        const QString remark = promptTextValue(QStringLiteral("设置备注"),
                                               QStringLiteral("备注名称:"),
                                               oldRemark,
                                               &ok,
                                               &dialog);
        if (!ok) return;
        if (remark.isEmpty()) {
            ui->statusbar->showMessage("备注名称不能为空", 1800);
            return;
        }
        if (remark == oldRemark) {
            ui->statusbar->showMessage("备注未改变", 1600);
            return;
        }
        m_friendNames[id] = remark;
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        fillList(searchEdit->text().trimmed());
        ui->statusbar->showMessage(QString("已设置备注：%1").arg(remark), 2200);
        appendSystemMessage(QString("已设置 %1 的备注为 %2").arg(id, remark));
    });
    connect(inviteBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        QString friendId;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要邀请入群的好友", &friendId)) {
            return;
        }
        if (m_localGroupIds.isEmpty()) {
            createLocalGroupSession(QStringLiteral("我的群聊"));
        }
        QString targetGroup = m_privateChatTarget.startsWith("local_group_") ? m_privateChatTarget : m_localGroupIds.last();
        if (appendMembersToLocalGroup(targetGroup, QStringList{friendId}) == 0) {
            ui->statusbar->showMessage(QString("%1 已在目标群聊中").arg(contactDisplayName(friendId)), 1800);
            return;
        }
        switchToLocalGroup(targetGroup, m_localGroupNames.value(targetGroup, "群聊"));
        appendSystemMessage(QString("已邀请 %1 加入群聊").arg(contactDisplayName(friendId)));
        saveHistory(targetGroup, QString("[%1] [系统] 已邀请 %2 加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), contactDisplayName(friendId)));
    });
    connect(inviteVisibleBtn, &QPushButton::clicked, &dialog, [this, friendList]() {
        const bool willCreateGroup = m_localGroupIds.isEmpty();
        QString targetGroup = m_privateChatTarget.startsWith("local_group_")
            ? m_privateChatTarget
            : (willCreateGroup ? QString("local_group_" + QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")) : m_localGroupIds.last());
        QString groupName = willCreateGroup ? "好友群聊" : m_localGroupNames.value(targetGroup, "群聊");
        QStringList currentMembers = willCreateGroup ? QStringList{m_currentUserId} : m_localGroupMembers.value(targetGroup);
        QStringList inviteIds;
        const QStringList visibleIds = visibleFriendManagerIds(friendList);
        for (const QString& friendId : visibleIds) {
            if (currentMembers.contains(friendId) || inviteIds.contains(friendId)) continue;
            inviteIds << friendId;
        }
        if (inviteIds.isEmpty()) {
            ui->statusbar->showMessage("当前没有可邀请的可见好友", 2200);
            return;
        }
        if (!confirmAction(QStringLiteral("邀请可见好友"),
                           QString("确定邀请 %1 位可见好友加入群聊“%2”吗？").arg(inviteIds.size()).arg(groupName),
                           QStringLiteral("已取消邀请可见好友"))) {
            return;
        }
        if (willCreateGroup) {
            targetGroup = createLocalGroupSession(QStringLiteral("好友群聊"));
        }
        appendMembersToLocalGroup(targetGroup, inviteIds);
        switchToLocalGroup(targetGroup, m_localGroupNames.value(targetGroup, "群聊"));
        appendSystemMessage(QString("已邀请 %1 位可见好友加入群聊").arg(inviteIds.size()));
        saveHistory(targetGroup, QString("[%1] [系统] 已邀请 %2 位可见好友加入群聊").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(inviteIds.size()));
    });
    connect(deleteBtn, &QPushButton::clicked, &dialog, [this, friendList, fillList, searchEdit, &dialog]() {
        QString id;
        if (!trySelectedValidFriendId(friendList, ui->statusbar, "请先选择要删除的好友", &id)) {
            return;
        }
        QString displayName = contactDisplayName(id);
        if (!confirmAction(QStringLiteral("删除好友"),
                           QString("确定删除好友“%1”（QQ:%2）吗？删除后可重新搜索并申请。").arg(displayName, id),
                           QStringLiteral("已取消删除好友"),
                           1600,
                           &dialog)) {
            return;
        }
        m_friendIds.removeAll(id);
        m_friendNames.remove(id);
        saveFriends();
        refreshFriendList();
        fillList(searchEdit->text().trimmed());
        ui->statusbar->showMessage(QString("已删除好友：%1").arg(displayName), 2200);
        appendSystemMessage(QString("已删除好友: %1（QQ:%2）").arg(displayName, id));
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    updateActionState();
    dialog.exec();
}

void MainWindow::onUploadAvatar() {
    const LocalAvatarSelectionPlan selectionPlan = LocalFileManager::avatarSelectionPlan();
    const QString selectedPath = selectOpenFilePath(selectionPlan.dialogTitle,
                                                    LocalFileManager::lastAvatarDirectory(),
                                                    selectionPlan.filters,
                                                    this);
    const LocalFileSelectionResult selection = LocalFileManager::selectAvatarFile(selectedPath);
    applyAvatarSelection(selection);
}

void MainWindow::onBackToGroupChat() {
    m_privateChatTarget.clear();
    m_chatModel->clear();
    m_chatModel->setHorizontalHeaderLabels({"聊天记录"});
    loadHistory("group");
    setWindowTitle(appWindowTitle(m_currentUserName));
    ui->chatTitleLabel->setText("公共聊天室");
    if (isCurrentUserRemovedFromPublicGroup()) {
        const QJsonObject removedInfo = m_removedServerGroups.value("public");
        const QString removedBy = removedInfo["removedByName"].toString(removedInfo["removedBy"].toString());
        const QString removedAt = removedInfo["removedAt"].toString();
        const QString removedDetail = removedBy.isEmpty()
            ? QStringLiteral("等待群主或管理员重新邀请")
            : QStringLiteral("由 %1 移出%2，等待重新邀请")
                .arg(removedBy, removedAt.isEmpty() ? QString() : QStringLiteral("于 %1").arg(removedAt));
        ui->chatHintLabel->setText(QString("公共会话工作区受限 · 当前账号 %1 已不在公共群 · %2").arg(m_currentUserId, removedDetail));
        ui->announcementTitleLabel->setText("群公告");
        ui->announcementBodyLabel->setText(QString("当前账号已不在公共群，%1。你仍可查看本机历史记录；重新邀请后会自动恢复群公告和成员列表。").arg(removedDetail));
    } else {
        ui->chatHintLabel->setText(QString("公共会话工作区 · 当前账号 QQ %1 · 双击左侧成员即可切换私聊").arg(m_currentUserId));
        ui->announcementTitleLabel->setText(canCurrentUserManageServerGroup("public")
            ? "群公告 <a href=\"edit\">编辑</a>"
            : "群公告");
        ui->announcementBodyLabel->setText(m_serverGroupAnnouncements.value(
            "public",
            "欢迎来到公共聊天室，支持 QQ 号搜索、好友、私聊和文件发送。"));
    }
    refreshGroupMemberPanel();
    refreshComposerState();
}

void MainWindow::onFriendRequestReceived(const QString& senderId, const QString& senderName) {
    if (senderId.isEmpty() || senderId == m_currentUserId) return;
    const QString displayName = senderName.isEmpty() ? senderId : senderName;
    m_friendNames[senderId] = displayName;
    m_pendingFriendRequests.removeAll(senderId);
    if (m_friendIds.contains(senderId)) {
        m_client->sendFriendResponse(senderId, true);
        appendSystemMessage(QString("%1 已是好友，已自动确认好友申请").arg(displayName));
        return;
    }
    m_pendingFriendRequests << senderId;
    saveFriends();
    ui->friendNoticeBtn->setText(QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
    ui->friendNoticeBtn->setToolTip(QString("有 %1 个好友申请待处理").arg(m_pendingFriendRequests.size()));
    appendSystemMessage(QString("收到好友申请：%1（QQ:%2），请在好友通知中处理").arg(displayName, senderId));
    ui->statusbar->showMessage(QString("新的好友申请 · %1").arg(displayName), 3500);
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage("新的好友申请",
                                QString("%1（QQ:%2）请求加为好友").arg(displayName, senderId),
                                QSystemTrayIcon::Information,
                                3000);
    }
}

void MainWindow::onFriendSearchResult(const QString& account, const QString& userId, const QString& userName, bool found, bool online, bool exactMatch, int matchCount, const QString& matchReason) {
    if (!found) {
        ui->statusbar->showMessage(QString("没有找到 QQ 或昵称：%1").arg(account), 3000);
        appendSystemMessage(QString("没有找到 QQ 或昵称: %1，可尝试输入更完整的 QQ 号或昵称关键词").arg(account));
        return;
    }
    if (userId == m_currentUserId) {
        ui->statusbar->showMessage("不能添加自己为好友", 2500);
        return;
    }
    if (m_friendIds.contains(userId)) {
        ui->statusbar->showMessage(QString("QQ 账号 %1 已经是你的好友").arg(userId), 2500);
        return;
    }
    if (m_pendingOutgoingFriendRequests.contains(userId)) {
        ui->statusbar->showMessage(QString("已向 QQ 账号 %1 发送过好友申请，等待对方处理").arg(userId), 3000);
        return;
    }

    const QString displayName = userName.isEmpty() ? userId : userName;
    const QString relation = m_friendIds.contains(userId)
        ? "好友"
        : (m_pendingOutgoingFriendRequests.contains(userId) ? "申请中" : "陌生人");
    const QString profileCard = QString("搜索资料卡\nQQ:%1\n昵称:%2\n状态:%3\n关系:%4\n匹配:%5 · 共%6个结果")
        .arg(userId,
             displayName,
             online ? "在线" : "离线",
             relation,
             matchReason.isEmpty() ? (exactMatch ? "QQ号精确匹配" : "模糊匹配") : matchReason,
             QString::number(matchCount));

    m_friendNames[userId] = displayName;
    QString compactProfileCard = profileCard;
    appendSystemMessage(compactProfileCard.replace('\n', " · "));

    if (!exactMatch) {
        if (!online) {
            ui->statusbar->showMessage(QString("模糊匹配到 %1（QQ:%2），但当前离线").arg(displayName, userId), 3500);
            appendSystemMessage(QString("模糊匹配到 %1（QQ:%2），对方离线，暂不能发起好友申请").arg(displayName, userId));
            return;
        }

        if (!confirmAction(QStringLiteral("确认模糊匹配"),
                           QStringLiteral("%1\n\n是否向该用户发起好友申请？").arg(profileCard),
                           QStringLiteral("已查看资料卡，未发起好友申请：%1").arg(displayName),
                           2600,
                           this)) {
            ui->statusbar->showMessage(QString("已查看资料卡，未发起好友申请：%1").arg(displayName), 2600);
            return;
        }
    }

    if (online) {
        if (!m_client->sendFriendRequest(userId)) {
            appendSystemMessage(QString("好友申请发起失败 QQ:%1，请检查连接后重试").arg(userId));
            ui->statusbar->showMessage(QString("好友申请发起失败：%1").arg(displayName), 3000);
            return;
        }
        if (!m_pendingOutgoingFriendRequests.contains(userId)) {
            m_pendingOutgoingFriendRequests << userId;
        }
        saveFriends();
        appendSystemMessage(QString("已发起好友申请 QQ:%1，等待对方同意 · %2").arg(userId, exactMatch ? "精确匹配" : "模糊匹配确认"));
        ui->statusbar->showMessage(QString("已向 %1 发起好友申请").arg(displayName), 2500);
        refreshFriendList();
    } else {
        ui->statusbar->showMessage(QString("QQ 账号 %1 当前离线，暂不能发起好友申请").arg(userId), 3000);
        appendSystemMessage(QString("QQ:%1 当前离线，未加入好友列表，可稍后重试").arg(userId));
    }
}

void MainWindow::onFriendRequestSent(const QString& receiverId, bool delivered) {
    QString userName = contactDisplayName(receiverId);
    if (delivered) {
        if (!m_pendingOutgoingFriendRequests.contains(receiverId)) {
            m_pendingOutgoingFriendRequests << receiverId;
        }
        appendSystemMessage(QString("好友申请已送达 QQ:%1，等待对方处理").arg(receiverId));
        ui->statusbar->showMessage(QString("好友申请已送达 %1").arg(userName), 2400);
    } else {
        m_pendingOutgoingFriendRequests.removeAll(receiverId);
        appendSystemMessage(QString("好友申请未送达 QQ:%1，对方当前离线").arg(receiverId));
        ui->statusbar->showMessage(QString("%1 当前离线，好友申请未送达").arg(userName), 3000);
    }
    saveFriends();
    refreshFriendList();
    refreshGroupMemberPanel();
}

void MainWindow::onFriendResponseReceived(const QString& senderId, const QString& senderName, bool accepted) {
    const QString displayName = senderName.isEmpty() ? contactDisplayName(senderId) : senderName;
    m_pendingOutgoingFriendRequests.removeAll(senderId);
    if (accepted) {
        if (!m_friendIds.contains(senderId)) {
            m_friendIds << senderId;
            m_friendNames[senderId] = displayName;
            QFile file(getFriendFilePath());
            if (file.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&file);
                out << senderId << "|" << displayName << "\n";
            }
        }
        refreshFriendList();
        refreshGroupMemberPanel();
        m_pendingFriendRequests.removeAll(senderId);
        saveFriends();
        const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
        ui->friendNoticeBtn->setText(noticeState.text);
        ui->friendNoticeBtn->setToolTip(noticeState.toolTip);
        appendSystemMessage(displayName + " 已同意你的好友申请");
        ui->statusbar->showMessage(QString("%1 已同意好友申请").arg(displayName), 2800);
    } else {
        m_friendIds.removeAll(senderId);
        m_friendNames.remove(senderId);
        m_pendingFriendRequests.removeAll(senderId);
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->friendNoticeBtn->setText(m_pendingFriendRequests.isEmpty() ? "好友通知" : QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
        ui->friendNoticeBtn->setToolTip(m_pendingFriendRequests.isEmpty() ? "查看并处理好友申请" : QString("有 %1 个好友申请待处理").arg(m_pendingFriendRequests.size()));
        appendSystemMessage(displayName + " 已拒绝你的好友申请");
        ui->statusbar->showMessage(QString("%1 已拒绝好友申请").arg(displayName), 2800);
    }
}

bool MainWindow::handleLocalGroupContextCommand(const QString& groupId,
                                                const QString& groupLabel,
                                                const QString& commandId) {
    auto groupMemberCopyInputs = [this](const QStringList& memberIds) {
        QList<GroupNoticeMemberInput> inputs;
        for (const QString& id : memberIds) {
            GroupNoticeMemberInput input;
            input.userId = id;
            input.displayName = contactDisplayName(id);
            input.self = id == m_currentUserId;
            input.online = isContactOnline(id);
            inputs << input;
        }
        return inputs;
    };

    if (commandId == QLatin1String("open-group")) {
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        return true;
    }
    if (commandId == QLatin1String("copy-group-id")) {
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        copyTextWithStatus(groupNumber, QStringLiteral("群号已复制: ") + groupNumber, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-group-card")) {
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        const QString card = QStringLiteral("群聊 QQ:%1\n%2\n成员:%3\n公告:%4")
            .arg(groupNumber,
                 m_localGroupNames.value(groupId, QStringLiteral("群聊")),
                 QString::number(m_localGroupMembers.value(groupId).size()),
                 m_localGroupAnnouncements.value(groupId,
                                                 QStringLiteral("%1 已创建，可继续邀请好友并发送消息。")
                                                     .arg(m_localGroupNames.value(groupId, QStringLiteral("群聊")))));
        copyTextWithStatus(card, QStringLiteral("群名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-group-invite")) {
        const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        const QString groupNumber = groupId.mid(QStringLiteral("local_group_").size());
        const QString inviteText = QStringLiteral("我邀请你加入群聊“%1”（群号:%2）。我是 %3（QQ:%4），进群后我们一起沟通。")
            .arg(groupName, groupNumber, m_currentUserName, m_currentUserId);
        copyTextWithStatus(inviteText, QStringLiteral("群邀请语已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-group-members")) {
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupMemberCopyInputs(m_localGroupMembers.value(groupId)), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return true;
        }
        copyTextWithStatus(state.rows.join(QLatin1Char('\n')), state.copiedStatusMessage, 2200);
        return true;
    }
    if (commandId == QLatin1String("invite-friend")) {
        if (m_friendIds.isEmpty()) {
            appendSystemMessage(QStringLiteral("当前没有好友可邀请"));
            return true;
        }
        showInviteFriendToGroupWorkspace(groupId, this);
        return true;
    }
    if (commandId == QLatin1String("invite-by-account")) {
        showInviteAccountToGroupWorkspace(groupId, this);
        return true;
    }
    if (commandId == QLatin1String("invite-all-friends")) {
        QStringList inviteIds;
        for (const QString& friendId : m_friendIds) {
            if (!m_localGroupMembers[groupId].contains(friendId)) {
                inviteIds << friendId;
            }
        }
        if (inviteIds.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("全部好友已在该群聊中"), 1800);
            return true;
        }
        const QString groupName = m_localGroupNames.value(groupId, QStringLiteral("群聊"));
        if (!confirmAction(QStringLiteral("邀请全部好友"),
                           QStringLiteral("确定邀请 %1 位好友加入群聊“%2”吗？").arg(inviteIds.size()).arg(groupName),
                           QStringLiteral("已取消邀请全部好友"))) {
            return true;
        }
        for (const QString& friendId : inviteIds) {
            m_localGroupMembers[groupId] << friendId;
        }
        saveLocalGroups();
        switchToLocalGroup(groupId, m_localGroupNames.value(groupId, QStringLiteral("群聊")));
        appendSystemMessage(QStringLiteral("已自动邀请 %1 位好友加入群聊").arg(inviteIds.size()));
        saveHistory(groupId,
                    QStringLiteral("[%1] [系统] 已自动邀请 %2 位好友加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")))
                        .arg(inviteIds.size()));
        return true;
    }
    if (commandId == QLatin1String("copy-group-online-members")) {
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupMemberCopyInputs(m_localGroupMembers.value(groupId)), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return true;
        }
        copyTextWithStatus(state.rows.join(QLatin1Char('\n')), state.copiedStatusMessage, 2200);
        return true;
    }
    if (commandId == QLatin1String("rename-group")) {
        showRenameGroupWorkspace(groupId, this);
        return true;
    }
    if (commandId == QLatin1String("delete-group")) {
        const QString groupName = m_localGroupNames.value(groupId, groupLabel);
        const int memberCount = qMax(1, m_localGroupMembers.value(groupId).size());
        if (!confirmAction(QStringLiteral("删除群聊"),
                           QStringLiteral("确定删除群聊“%1”吗？本地群成员 %2 人，聊天记录不会在此步骤删除。")
                               .arg(groupName)
                               .arg(memberCount),
                           QStringLiteral("已取消删除群聊"))) {
            return true;
        }
        m_localGroupIds.removeAll(groupId);
        m_localGroupNames.remove(groupId);
        m_localGroupAnnouncements.remove(groupId);
        m_localGroupMembers.remove(groupId);
        saveLocalGroups();
        refreshFriendList();
        if (m_privateChatTarget == groupId) {
            onBackToGroupChat();
        }
        appendSystemMessage(QStringLiteral("已删除群聊: ") + groupName);
        return true;
    }

    return false;
}

bool MainWindow::handleContactContextCommand(const QString& userId,
                                             const QString& commandId) {
    if (commandId == QLatin1String("copy-account")) {
        copyTextWithStatus(userId, QStringLiteral("QQ 号已复制: ") + userId, 2500);
        return true;
    }
    if (commandId == QLatin1String("copy-profile-card")) {
        const QString card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3")
            .arg(userId,
                 contactDisplayName(userId),
                 isContactOnline(userId) ? QStringLiteral("在线") : QStringLiteral("离线"));
        copyTextWithStatus(card, QStringLiteral("联系人名片已复制"), 1800);
        return true;
    }
    if (commandId == QLatin1String("copy-add-text")) {
        const QString text = QStringLiteral("你好，我是 %1（QQ:%2），通过 QQ 搜索看到你。方便的话加个好友，我们可以私聊或一起进群沟通。")
            .arg(m_currentUserName, m_currentUserId);
        copyTextWithStatus(text, QStringLiteral("好友申请话术已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-online-card")) {
        const QString card = QStringLiteral("QQ:%1\n昵称:%2\n状态:%3\n关系:%4\n当前会话:%5")
            .arg(userId,
                 contactDisplayName(userId),
                 isContactOnline(userId) ? QStringLiteral("在线") : QStringLiteral("离线"),
                 m_friendIds.contains(userId) ? QStringLiteral("好友")
                                              : (m_pendingOutgoingFriendRequests.contains(userId) ? QStringLiteral("申请中")
                                                                                                  : QStringLiteral("陌生人")),
                 m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget));
        copyTextWithStatus(card, QStringLiteral("在线名片已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-chat-starter")) {
        const QString text = m_friendIds.contains(userId)
            ? QStringLiteral("%1，在吗？我是 %2（QQ:%3），想和你私聊确认一下刚才的消息。")
                  .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
            : m_pendingOutgoingFriendRequests.contains(userId)
                ? QStringLiteral("%1，你好，我是 %2（QQ:%3），我已经发起好友申请了，通过后我们可以继续私聊。")
                      .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId)
                : QStringLiteral("你好 %1，我是 %2（QQ:%3）。通过 QQ 搜索看到你，方便通过好友申请后再聊吗？")
                      .arg(contactDisplayName(userId), m_currentUserName, m_currentUserId);
        copyTextWithStatus(text, QStringLiteral("开聊话术已复制"), 2200);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-status")) {
        copyE2ESessionStatus(userId);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-identity")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString text = identity.value(QStringLiteral("configured")).toBool(false)
            ? QStringLiteral("端到端加密身份\n对端QQ：%1\n信任状态：%2\n验证状态：%3\n公钥指纹：%4\n验证短码：%5\n首次看到：%6\n最近看到：%7")
                  .arg(userId,
                       identity.value(QStringLiteral("trustState")).toString(),
                       identity.value(QStringLiteral("verified")).toBool(false) ? QStringLiteral("verified")
                                                                                 : QStringLiteral("unverified"),
                       identity.value(QStringLiteral("publicKeyFingerprintSha256")).toString(),
                       identity.value(QStringLiteral("verificationCodeDisplay")).toString(),
                       identity.value(QStringLiteral("firstSeenAt")).toString(),
                       identity.value(QStringLiteral("lastSeenAt")).toString())
            : QStringLiteral("端到端加密身份\n对端QQ：%1\n信任状态：unknown\n说明：尚未收到该联系人的身份公告").arg(userId);
        copyTextWithStatus(text, QStringLiteral("端到端加密身份指纹已复制"), 2400);
        return true;
    }
    if (commandId == QLatin1String("copy-e2e-verification")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString code = identity.value(QStringLiteral("verificationCodeDisplay")).toString();
        const QString text = identity.value(QStringLiteral("configured")).toBool(false) && !code.isEmpty()
            ? QStringLiteral("端到端加密验证短码\n对端QQ：%1\n短码：%2\n核对方式：请通过另一个可信渠道与对方屏幕上的短码一致后再验证信任。")
                  .arg(userId, code)
            : QStringLiteral("端到端加密验证短码\n对端QQ：%1\n说明：尚未收到该联系人的身份公告").arg(userId);
        copyTextWithStatus(text, QStringLiteral("端到端加密验证短码已复制"), 2400);
        return true;
    }
    if (commandId == QLatin1String("trust-e2e-identity")) {
        QString rejectReason;
        if (m_client && m_client->pinE2EPeerIdentity(userId, QString(), &rejectReason)) {
            appendSystemMessage(QStringLiteral("已固定 %1 的端到端加密身份指纹；完成验证短码核对前不会启用默认加密")
                                    .arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份已固定，等待验证"), 2600);
        } else {
            appendSystemMessage(QStringLiteral("信任端到端加密身份失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("信任端到端加密身份失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("verify-e2e-identity")) {
        const QJsonObject identity = m_client ? m_client->e2ePeerIdentityStatus(userId) : QJsonObject();
        const QString suggestedCode = identity.value(QStringLiteral("verificationCodeDisplay")).toString();
        bool ok = false;
        const QString code = promptTextValue(QStringLiteral("验证加密身份"),
                                             QStringLiteral("请输入与 %1 通过可信渠道核对一致的验证短码:")
                                                 .arg(contactDisplayName(userId)),
                                             suggestedCode,
                                             &ok);
        if (!ok) {
            ui->statusbar->showMessage(QStringLiteral("已取消端到端加密身份验证"), 1800);
            return true;
        }
        QString rejectReason;
        if (m_client && m_client->verifyAndPinE2EPeerIdentity(userId, code, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已验证并信任 %1 的端到端加密身份").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份已验证"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("验证端到端加密身份失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("验证端到端加密身份失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-identity-trust")) {
        QString rejectReason;
        if (m_client && m_client->clearE2EPeerIdentityPin(userId, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已清除 %1 的端到端加密身份信任固定").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密身份信任已清除"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("清除端到端加密身份信任失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("清除端到端加密身份信任失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("request-e2e-rotation")) {
        QString rejectReason;
        if (m_client && m_client->requestE2ESessionRotation(userId, &rejectReason)) {
            appendSystemMessage(QStringLiteral("已向 %1 发送端到端加密轮换请求").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("端到端加密轮换请求已发送"), 2400);
        } else {
            appendSystemMessage(QStringLiteral("端到端加密轮换请求发送失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("端到端加密轮换请求发送失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-session")) {
        if (m_client) {
            m_client->clearE2ESessionKey(userId);
            appendSystemMessage(QStringLiteral("已关闭 %1 的本机端到端加密会话").arg(contactDisplayName(userId)));
            ui->statusbar->showMessage(QStringLiteral("本机端到端加密会话已关闭"), 2400);
        }
        return true;
    }
    if (commandId == QLatin1String("clear-e2e-backend-migration")) {
        QString rejectReason;
        if (m_client && m_client->clearE2EBackendMigrationState(&rejectReason)) {
            appendSystemMessage(QStringLiteral("已清理本机端到端加密后端迁移状态；需要重新接收身份公告、核对短码并建立会话后才能继续默认加密"));
            ui->statusbar->showMessage(QStringLiteral("端到端加密迁移状态已清理"), 3200);
        } else {
            appendSystemMessage(QStringLiteral("清理端到端加密迁移状态失败：%1")
                                    .arg(rejectReason.isEmpty() ? QStringLiteral("unknown") : rejectReason));
            ui->statusbar->showMessage(QStringLiteral("清理端到端加密迁移状态失败"), 3000);
        }
        return true;
    }
    if (commandId == QLatin1String("invite-current-group")) {
        QString requestNote;
        if (!m_friendIds.contains(userId)) {
            if (m_pendingOutgoingFriendRequests.contains(userId)) {
                requestNote = QStringLiteral("，好友申请已在等待确认");
            } else if (m_client && m_client->sendFriendRequest(userId)) {
                m_friendNames[userId] = contactDisplayName(userId);
                m_pendingOutgoingFriendRequests << userId;
                requestNote = QStringLiteral("，好友申请等待确认");
            } else {
                requestNote = QStringLiteral("，好友申请发起失败");
                ui->statusbar->showMessage(QStringLiteral("已邀请入群，但好友申请发起失败：%1").arg(contactDisplayName(userId)), 3000);
            }
        }
        if (!m_localGroupMembers[m_privateChatTarget].contains(userId)) {
            m_localGroupMembers[m_privateChatTarget] << userId;
            saveLocalGroups();
        }
        refreshFriendList();
        refreshGroupMemberPanel();
        appendSystemMessage(QStringLiteral("已邀请 %1 加入当前群聊%2").arg(contactDisplayName(userId), requestNote));
        saveHistory(m_privateChatTarget,
                    QStringLiteral("[%1] [系统] 已邀请 %2 加入群聊")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), contactDisplayName(userId)));
        return true;
    }
    if (commandId == QLatin1String("rename-friend")) {
        bool ok = false;
        const QString oldRemark = contactDisplayName(userId);
        const QString remark = promptTextValue(QStringLiteral("设置备注"),
                                               QStringLiteral("备注名称:"),
                                               oldRemark,
                                               &ok);
        if (!ok) {
            return true;
        }
        if (remark.isEmpty()) {
            ui->statusbar->showMessage(QStringLiteral("备注名称不能为空"), 1800);
            return true;
        }
        if (remark == oldRemark) {
            ui->statusbar->showMessage(QStringLiteral("备注未改变"), 1600);
            return true;
        }
        m_friendNames[userId] = remark;
        saveFriends();
        refreshFriendList();
        refreshGroupMemberPanel();
        ui->statusbar->showMessage(QStringLiteral("已设置备注：%1").arg(remark), 2200);
        appendSystemMessage(QStringLiteral("已设置 %1 的备注为 %2").arg(userId, remark));
        return true;
    }
    if (commandId == QLatin1String("add-friend")) {
        if (m_pendingOutgoingFriendRequests.contains(userId)) {
            ui->statusbar->showMessage(QStringLiteral("已向 %1 发送过好友申请，等待对方处理").arg(contactDisplayName(userId)), 2500);
            return true;
        }
        if (!m_friendIds.contains(userId)) {
            const QString displayName = contactDisplayName(userId);
            if (!m_client || !m_client->sendFriendRequest(userId)) {
                ui->statusbar->showMessage(QStringLiteral("好友申请发起失败：%1").arg(displayName), 3000);
                appendSystemMessage(QStringLiteral("好友申请发起失败 QQ:%1，请检查连接后重试").arg(userId));
                return true;
            }
            m_friendNames[userId] = displayName;
            m_pendingOutgoingFriendRequests << userId;
            refreshFriendList();
            appendSystemMessage(QStringLiteral("已发起好友申请：%1（QQ:%2），等待对方同意").arg(displayName, userId));
        }
        return true;
    }
    if (commandId == QLatin1String("remove-friend")) {
        const QString displayName = contactDisplayName(userId);
        if (!confirmAction(QStringLiteral("删除好友"),
                           QStringLiteral("确定删除好友“%1”（QQ:%2）吗？删除后可重新搜索并申请。").arg(displayName, userId),
                           QStringLiteral("已取消删除好友"))) {
            return true;
        }
        m_friendIds.removeAll(userId);
        m_friendNames.remove(userId);
        saveFriends();
        refreshFriendList();
        appendSystemMessage(QStringLiteral("已删除好友: %1（QQ:%2）").arg(displayName, userId));
        return true;
    }
    return false;
}

void MainWindow::onUserContextMenu(const QPoint& pos) {
    QModelIndex index = ui->userListView->indexAt(pos);
    if (!index.isValid()) return;

    QString userId = index.data(Qt::UserRole + 1).toString();
    if (userId.isEmpty()) return;
    QString userName = index.data().toString();
    userName.remove(QRegularExpression("^[★☆○]\\s*"));
    userName.remove(QRegularExpression("^群聊\\s+QQ:[^\\n]+\\n\\s*"));
    userName.remove(QRegularExpression("\\s*\\[(在线|离线|本地)\\]$"));
    const QString displayName = userName.trimmed().isEmpty() ? contactDisplayName(userId) : userName.trimmed();
    QMenu menu(this);
    QAction* contactWorkspaceAction = addMenuActionWithIcon(menu,
                                                            this,
                                                            QStringLiteral("打开联系人工作区"),
                                                            QStringLiteral("带当前联系人上下文进入联系人工作区，统一处理资料、搜索和跳转"),
                                                            QString(),
                                                            true,
                                                            QStyle::SP_FileDialogDetailedView);
    QAction* objectWorkspaceAction = addMenuActionWithIcon(menu,
                                                           this,
                                                           QStringLiteral("打开对象工作区"),
                                                           QStringLiteral("带当前联系人或群聊对象进入对象工作区，集中处理当前项动作"),
                                                           QString(),
                                                           true,
                                                           QStyle::SP_FileDialogInfoView);
    QAction* selected = menu.exec(ui->userListView->viewport()->mapToGlobal(pos));
    if (selected == contactWorkspaceAction) {
        onShowContactWorkspace(QStringLiteral("%1 %2").arg(displayName, userId).trimmed());
        return;
    }
    if (selected == objectWorkspaceAction) {
        showUserEntryWorkspace(userId, displayName);
    }
}

void MainWindow::onShowFriendNotifications() {
    const FriendNoticeDialogChrome chrome = NotificationPanelManager::friendNoticeDialogChrome();
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("noticeDialog"),
        chrome.windowTitle,
        chrome.dialogSize,
        QStringLiteral("noticeTitle"),
        chrome.titleText,
        QStringLiteral("noticeSubTitle"),
        QString(),
        QStringLiteral("noticeSearch"),
        chrome.searchPlaceholder,
        chrome.searchToolTip,
        QStringLiteral("noticeList"),
        true,
        QStringLiteral("globalActionHint"),
        QStringLiteral("先确认筛选范围，再处理单条申请、批量回复或整理发送材料。"),
        QStringLiteral("globalStatsLabel"),
        QStringLiteral("globalPreviewLabel"),
        chrome.previewPlaceholder,
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(28, 24, 28, 18);
    shell.bodyLayout->setContentsMargins(28, 0, 28, 24);
    shell.bodyLayout->setSpacing(14);

    QListWidget* noticeList = shell.listWidget;
    QLineEdit* searchEdit = shell.searchEdit;
    QLabel* statsLabel = shell.statsLabel;
    QLabel* actionHint = shell.hintLabel;
    QLabel* requestPreviewLabel = shell.previewLabel;

    auto emptyFriendNoticePreviewText = [searchEdit]() {
        const QString filter = searchEdit->text().trimmed();
        return filter.isEmpty()
            ? QStringLiteral("当前没有待处理的好友申请。可在这里输入 QQ 号直接搜索并申请，或稍后回来处理新的通知。")
            : QStringLiteral("当前筛选词“%1”没有匹配到待处理申请。\n可直接搜索这个 QQ，或清空搜索后查看全部申请。").arg(filter);
    };
    auto fillList = [this, noticeList, statsLabel, searchEdit, actionHint, emptyFriendNoticePreviewText]() {
        noticeList->clear();
        const FriendNoticeListRenderUiState renderState =
            NotificationPanelManager::friendNoticeListRenderUiState(m_pendingFriendRequests,
                                                                    m_friendNames,
                                                                    m_friendIds.size(),
                                                                    searchEdit->text());
        statsLabel->setText(renderState.statsText);
        for (const FriendNoticeListEntryUiState& entry : renderState.entries) {
            QListWidgetItem* item = new QListWidgetItem(entry.text);
            item->setData(Qt::UserRole, entry.entryId);
            item->setSizeHint(QSize(0, entry.rowHeight));
            item->setToolTip(entry.toolTip);
            if (!entry.enabled) {
                item->setFlags(Qt::NoItemFlags);
            }
            if (entry.muted) {
                item->setForeground(QColor(135, 150, 165));
            } else if (entry.accent) {
                item->setForeground(QColor(18, 150, 247));
            }
            noticeList->addItem(item);
        }
        if (!hasEnabledListRow(noticeList)) {
            actionHint->setText(emptyFriendNoticePreviewText());
        }
        selectPreferredListRow(noticeList, 0);
    };
    fillList();
    QPushButton* acceptBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.acceptButton, QStyle::SP_DialogApplyButton);
    QPushButton* acceptAllBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.acceptAllButton, QStyle::SP_DialogYesButton);
    QPushButton* rejectBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.rejectButton, QStyle::SP_DialogCancelButton);
    QPushButton* rejectAllBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.rejectAllButton, QStyle::SP_TrashIcon);
    QPushButton* copyBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyCardButton, QStyle::SP_FileDialogInfoView);
    QPushButton* copyInviteBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyInviteButton, QStyle::SP_DirLinkIcon);
    QPushButton* copyAllBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyAllButton, QStyle::SP_FileDialogListView);
    QPushButton* copyRequestMediaPackBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyMediaPackButton, QStyle::SP_FileIcon);
    QPushButton* copyRequestBatchPlanBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyBatchPlanButton, QStyle::SP_DriveHDIcon);
    QPushButton* copyMediaGuideBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyMediaGuideButton, QStyle::SP_DialogHelpButton);
    QPushButton* clearBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.clearButton, QStyle::SP_DialogResetButton);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.closeButton, QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("主操作"),
        QStringLiteral("同意、拒绝和批量处理都会跟随当前选中项或筛选范围联动启用。"),
        {acceptBtn, acceptAllBtn, rejectBtn, rejectAllBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("复制与回复"),
        QStringLiteral("把申请人名片、回复话术和当前筛选结果整理出去，减少重复操作。"),
        {copyBtn, copyInviteBtn, copyAllBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("媒体与处理计划"),
        QStringLiteral("为通过好友后的图片、视频、文件发送提前准备摘要、批量计划和上传说明。"),
        {copyRequestMediaPackBtn, copyRequestBatchPlanBtn, copyMediaGuideBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("清理与收尾"),
        QStringLiteral("只清空当前待处理队列，不会自动代你回复对方。"),
        {clearBtn});

    dialog.setStyleSheet(productDialogStyleSheet() + NotificationPanelManager::friendNoticeDialogStyleSheet());

    auto updateBadge = [this]() {
        const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
        ui->friendNoticeBtn->setText(noticeState.text);
        ui->friendNoticeBtn->setToolTip(noticeState.toolTip);
    };
    auto currentRequestId = [noticeList]() -> QString {
        return selectedFriendNoticeEntryId(noticeList);
    };
    auto selectedFriendNoticeTarget = [this, noticeList, searchEdit]() {
        return friendNoticeVisibleTarget(selectedFriendNoticeTargetId(noticeList, searchEdit));
    };
    auto visibleFriendNoticeTargetsFn = [this, noticeList]() {
        return visibleFriendNoticeTargets(noticeList);
    };
    auto updateRequestActionState = [=]() {
        const FriendNoticeActionState state = NotificationPanelManager::friendNoticeActionState(
            currentRequestId(),
            !m_pendingFriendRequests.isEmpty(),
            !searchEdit->text().trimmed().isEmpty());
        const QString currentId = currentRequestId();
        const bool hasRealRequest = NotificationPanelManager::isRealFriendNoticeRequestId(currentId);
        const bool hasSearchEntry = NotificationPanelManager::isSearchAddEntryId(currentId);
        const bool hasActionableRow = hasEnabledListRow(noticeList);
        acceptBtn->setEnabled(state.acceptEnabled);
        acceptBtn->setText(state.acceptText);
        acceptBtn->setToolTip(state.acceptToolTip);
        rejectBtn->setEnabled(state.rejectEnabled);
        rejectBtn->setToolTip(state.rejectToolTip);
        copyBtn->setEnabled(state.copyCardEnabled);
        copyBtn->setToolTip(state.copyCardToolTip);
        copyInviteBtn->setEnabled(state.copyInviteEnabled);
        copyInviteBtn->setToolTip(state.copyInviteToolTip);
        copyAllBtn->setEnabled(state.copyAllEnabled);
        copyAllBtn->setToolTip(state.copyAllToolTip);
        copyRequestMediaPackBtn->setEnabled(state.copyMediaPackEnabled);
        copyRequestMediaPackBtn->setToolTip(state.copyMediaPackToolTip);
        copyRequestBatchPlanBtn->setEnabled(state.copyBatchPlanEnabled);
        copyRequestBatchPlanBtn->setToolTip(state.copyBatchPlanToolTip);
        copyMediaGuideBtn->setEnabled(state.copyMediaGuideEnabled);
        copyMediaGuideBtn->setToolTip(state.copyMediaGuideToolTip);
        acceptAllBtn->setEnabled(state.acceptAllEnabled);
        acceptAllBtn->setToolTip(state.acceptAllToolTip);
        rejectAllBtn->setEnabled(state.rejectAllEnabled);
        rejectAllBtn->setToolTip(state.rejectAllToolTip);
        clearBtn->setEnabled(state.clearEnabled);
        clearBtn->setToolTip(state.clearToolTip);
        actionHint->setText(!hasActionableRow
                                ? emptyFriendNoticePreviewText()
                                : (hasRealRequest
                                       ? QStringLiteral("当前申请可立即同意、拒绝、复制名片或整理通过后的媒体计划。")
                                       : (hasSearchEntry
                                              ? QStringLiteral("当前是搜索建议项，可直接搜索并申请，或复制回复与媒体准备材料。")
                                              : QStringLiteral("先确认筛选范围，再处理单条申请、批量回复或整理发送材料。"))));
    };
    auto updateRequestPreview = [this, noticeList, searchEdit, requestPreviewLabel, emptyFriendNoticePreviewText]() {
        const FriendNoticeSelectionSnapshot snapshot = currentFriendNoticeSelectionSnapshot(noticeList, searchEdit);
        requestPreviewLabel->setText(snapshot.currentId.isEmpty()
                                         ? (hasEnabledListRow(noticeList)
                                                ? QStringLiteral("先选中申请，再决定同意、回复整理或复制申请人名片。")
                                                : emptyFriendNoticePreviewText())
                                         : snapshot.previewText);
    };
    updateRequestPreview();
    updateRequestActionState();
    connect(noticeList, &QListWidget::currentItemChanged, &dialog, [updateRequestPreview, updateRequestActionState](QListWidgetItem*, QListWidgetItem*) {
        updateRequestPreview();
        updateRequestActionState();
    });
    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillList, updateRequestPreview, updateRequestActionState]() {
        fillList();
        updateRequestPreview();
        updateRequestActionState();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, [this, searchEdit]() {
        QString account = searchEdit->text().trimmed();
        if (account.isEmpty()) {
            searchEdit->setFocus();
            ui->statusbar->showMessage("请输入申请人 QQ 号后搜索", 1800);
            return;
        }
        searchAndAddAccount(account, this);
    });
    connect(acceptBtn, &QPushButton::clicked, &dialog, [this, noticeList, fillList, updateBadge, updateRequestActionState]() {
        const QString id = selectedFriendNoticeEntryId(noticeList);
        if (id.startsWith("search_add:")) {
            searchAndAddAccount(id.mid(QString("search_add:").size()), this);
            return;
        }
        if (id.isEmpty()) {
            ui->statusbar->showMessage("暂无可同意的好友申请", 1800);
            return;
        }
        QString name = m_friendNames.value(id, id);
        m_client->sendFriendResponse(id, true);
        if (!m_friendIds.contains(id)) {
            m_friendIds << id;
        }
        m_friendNames[id] = name;
        m_pendingFriendRequests.removeAll(id);
        saveFriends();
        refreshFriendList();
        updateBadge();
        fillList();
        updateRequestActionState();
        const FriendNoticeRequestDecisionState state =
            FriendManager::friendNoticeRequestDecisionState(id, name, true);
        ui->statusbar->showMessage(state.statusMessage, 2200);
        appendSystemMessage(state.systemMessage);
    });
    connect(acceptAllBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        QStringList pending = m_pendingFriendRequests;
        const FriendNoticeBulkActionState actionState =
            FriendManager::friendNoticeBulkActionState(FriendNoticeBulkActionKind::AcceptAll,
                                                       pending.size());
        if (pending.isEmpty()) {
            ui->statusbar->showMessage(actionState.emptyStatusMessage, 1800);
            return;
        }
        if (!confirmAction(actionState.title,
                           actionState.questionText,
                           actionState.cancelledStatusMessage,
                           1600,
                           &dialog)) {
            return;
        }
        for (const QString& id : pending) {
            if (id.isEmpty()) continue;
            QString name = m_friendNames.value(id, id);
            m_client->sendFriendResponse(id, true);
            if (!m_friendIds.contains(id)) {
                m_friendIds << id;
            }
            m_friendNames[id] = name;
        }
        m_pendingFriendRequests.clear();
        saveFriends();
        refreshFriendList();
        updateBadge();
        fillList();
        updateRequestActionState();
        ui->statusbar->showMessage(actionState.successStatusMessage, 2200);
        appendSystemMessage(actionState.systemMessage);
    });
    connect(rejectBtn, &QPushButton::clicked, &dialog, [this, noticeList, fillList, updateBadge, updateRequestActionState]() {
        QString id;
        if (!trySelectedRealFriendNoticeId(noticeList,
                                           ui->statusbar,
                                           "请先选择要拒绝的好友申请",
                                           "这是搜索占位项，可先搜索并申请",
                                           &id)) {
            return;
        }
        m_client->sendFriendResponse(id, false);
        m_pendingFriendRequests.removeAll(id);
        saveFriends();
        updateBadge();
        fillList();
        updateRequestActionState();
        const FriendNoticeRequestDecisionState state =
            FriendManager::friendNoticeRequestDecisionState(id, QString(), false);
        ui->statusbar->showMessage(state.statusMessage, 2200);
        appendSystemMessage(state.systemMessage);
    });
    connect(rejectAllBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        QStringList pending = m_pendingFriendRequests;
        const FriendNoticeBulkActionState actionState =
            FriendManager::friendNoticeBulkActionState(FriendNoticeBulkActionKind::RejectAll,
                                                       pending.size());
        if (pending.isEmpty()) {
            ui->statusbar->showMessage(actionState.emptyStatusMessage, 1800);
            return;
        }
        if (!confirmAction(actionState.title,
                           actionState.questionText,
                           actionState.cancelledStatusMessage,
                           1600,
                           &dialog)) {
            return;
        }
        for (const QString& id : pending) {
            if (!id.isEmpty()) {
                m_client->sendFriendResponse(id, false);
            }
        }
        m_pendingFriendRequests.clear();
        saveFriends();
        updateBadge();
        fillList();
        updateRequestActionState();
        ui->statusbar->showMessage(actionState.successStatusMessage, 2200);
        appendSystemMessage(actionState.systemMessage);
    });
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString id;
        if (!trySelectedRealFriendNoticeId(noticeList,
                                           ui->statusbar,
                                           "请先选择要复制的好友申请",
                                           "这是搜索占位项，请先搜索这个 QQ",
                                           &id)) {
            return;
        }
        copyTextWithStatus(
            FriendManager::friendNoticeApplicantCardText(id, m_friendNames.value(id, id)),
            "申请人名片已复制");
    });
    connect(copyInviteBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString id = selectedFriendNoticeTargetId(noticeList, searchEdit);
        QString name = id.isEmpty() ? "朋友" : m_friendNames.value(id, contactDisplayName(id));
        copyTextWithStatus(
            FriendManager::friendNoticeReplyText(name, m_currentUserName, m_currentUserId),
            "申请回复话术已复制",
            2200);
    });
    connect(copyAllBtn, &QPushButton::clicked, &dialog, [this]() {
        const QString bulkText = FriendManager::friendNoticeBulkCopyText(
            m_pendingFriendRequests,
            m_friendNames,
            m_currentUserName,
            m_currentUserId);
        if (bulkText.isEmpty()) {
            ui->statusbar->showMessage("暂无好友申请可复制", 2200);
            return;
        }
        copyTextWithStatus(
            bulkText,
            QString("已复制 %1 条好友申请").arg(m_pendingFriendRequests.size()),
            2200);
    });
    connect(copyRequestMediaPackBtn, &QPushButton::clicked, &dialog, [this, selectedFriendNoticeTarget]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendNoticeMediaPackState(
            m_currentUserId,
            m_currentUserName,
            m_pendingFriendRequests.size(),
            selectedFriendNoticeTarget());
        copyTextWithStatus(state.text, "好友申请媒体包已复制", 2200);
    });
    connect(copyRequestBatchPlanBtn, &QPushButton::clicked, &dialog, [this, visibleFriendNoticeTargetsFn, searchEdit]() {
        const GlobalSearchSelectionCopyState state = FriendManager::friendNoticeBatchPlanState(
            m_currentUserId,
            m_currentUserName,
            m_pendingFriendRequests.size(),
            searchEdit->text().trimmed(),
            visibleFriendNoticeTargetsFn());
        copyTextWithStatus(state.text, "好友申请处理计划已复制", 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, selectedFriendNoticeTarget]() {
        copyTextWithStatus(
            FriendManager::friendNoticeMediaGuideText(
                m_currentUserId,
                m_currentUserName,
                selectedFriendNoticeTarget()),
            "好友申请上传指南已复制",
            2200);
    });
    connect(clearBtn, &QPushButton::clicked, &dialog, [this, fillList, updateBadge, updateRequestActionState, &dialog]() {
        const FriendNoticeBulkActionState actionState =
            FriendManager::friendNoticeBulkActionState(FriendNoticeBulkActionKind::ClearAll,
                                                       m_pendingFriendRequests.size());
        if (m_pendingFriendRequests.isEmpty()) {
            ui->statusbar->showMessage(actionState.emptyStatusMessage, 1600);
            return;
        }
        if (!confirmAction(actionState.title,
                           actionState.questionText,
                           actionState.cancelledStatusMessage,
                           1600,
                           &dialog)) {
            return;
        }
        m_pendingFriendRequests.clear();
        saveFriends();
        updateBadge();
        fillList();
        updateRequestActionState();
        ui->statusbar->showMessage(actionState.successStatusMessage, 1800);
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::onShowGroupNotifications() {
    const GroupNoticeDialogChrome chrome = NotificationPanelManager::groupNoticeDialogChrome();
    QDialog dialog(this);
    WorkspaceDialogShell shell = createWorkspaceDialogShell(
        dialog,
        QStringLiteral("noticeDialog"),
        chrome.windowTitle,
        chrome.dialogSize,
        QStringLiteral("noticeTitle"),
        chrome.titleText,
        QStringLiteral("noticeSubTitle"),
        QString(),
        QStringLiteral("noticeSearch"),
        chrome.searchPlaceholder,
        chrome.searchToolTip,
        QStringLiteral("noticeList"),
        true,
        QStringLiteral("globalActionHint"),
        chrome.hintPlaceholder,
        QStringLiteral("globalStatsLabel"),
        QStringLiteral("globalPreviewLabel"),
        chrome.previewPlaceholder,
        QStringLiteral("managerHeader"),
        QStringLiteral("managerBody"));
    shell.headerLayout->setContentsMargins(28, 24, 28, 18);
    shell.bodyLayout->setContentsMargins(28, 0, 28, 24);
    shell.bodyLayout->setSpacing(14);
    shell.statsLabel->setText(chrome.countTextTemplate.arg(m_localGroupIds.size() + 1));
    moveWorkspaceShellStatsToHeader(shell);

    QListWidget* noticeList = shell.listWidget;
    QLineEdit* searchEdit = shell.searchEdit;
    QLabel* countLabel = shell.statsLabel;
    QLabel* hintLabel = shell.hintLabel;
    QLabel* groupPreviewLabel = shell.previewLabel;
    auto emptyGroupNoticePreviewText = [searchEdit]() {
        const QString keyword = searchEdit->text().trimmed();
        return keyword.isEmpty()
            ? QStringLiteral("当前没有额外群聊筛选压力。可进入公共聊天室、浏览已有群聊，或输入关键词创建新群。")
            : QStringLiteral("当前筛选词“%1”没有匹配到群聊。\n可直接用这个关键词创建新群，或清空搜索后查看全部群聊。").arg(keyword);
    };

    auto fillGroups = [this, noticeList, countLabel, searchEdit]() {
        fillGroupNoticeList(noticeList, countLabel, searchEdit);
    };
    fillGroups();

    QPushButton* openBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.openButton, QStyle::SP_ArrowForward);
    QPushButton* copyBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyIdButton, QStyle::SP_DialogSaveButton);
    QPushButton* cardBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyCardButton, QStyle::SP_FileDialogInfoView);
    QPushButton* announceBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyAnnouncementButton, QStyle::SP_MessageBoxInformation);
    QPushButton* inviteTextBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyInviteButton, QStyle::SP_DirLinkIcon);
    QPushButton* memberBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyMembersButton, QStyle::SP_FileDialogListView);
    QPushButton* onlineMemberBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyOnlineMembersButton, QStyle::SP_DialogYesButton);
    QPushButton* copyGroupMediaPackBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyMediaPackButton, QStyle::SP_FileIcon);
    QPushButton* copyGroupBatchPlanBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyBatchPlanButton, QStyle::SP_DriveHDIcon);
    QPushButton* copyMediaGuideBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.copyMediaGuideButton, QStyle::SP_DialogHelpButton);
    QPushButton* closeBtn = createWorkspaceButton(shell.bodyFrame, &dialog, chrome.closeButton, QStyle::SP_DialogCloseButton);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("主操作"),
        QStringLiteral("先进入选中群聊，再继续复制资料、发邀请或展开后续群内动作。"),
        {openBtn},
        closeBtn);

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("复制与群信息"),
        QStringLiteral("把群号、群名片、公告和邀请话术整理成一套可直接发送的上下文。"),
        {copyBtn, cardBtn, announceBtn, inviteTextBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("成员与在线状态"),
        QStringLiteral("成员清单和在线成员会跟随当前群聊切换，方便快速同步给协作者。"),
        {memberBtn, onlineMemberBtn});

    addWorkspaceSectionCard(
        shell.bodyLayout,
        shell.bodyFrame,
        QStringLiteral("媒体与批量计划"),
        QStringLiteral("为后续群内图片、视频、文件发送准备媒体包、批量计划和上传说明。"),
        {copyGroupMediaPackBtn, copyGroupBatchPlanBtn, copyMediaGuideBtn});

    auto openSelectedGroup = [this, noticeList, &dialog]() {
        openSelectedGroupNoticeEntry(noticeList, &dialog);
    };
    dialog.setStyleSheet(productDialogStyleSheet() + NotificationPanelManager::groupNoticeDialogStyleSheet());
    connect(openBtn, &QPushButton::clicked, &dialog, openSelectedGroup);
    auto updateGroupActionState = [=]() {
        const QString currentId = selectedGroupNoticeEntryId(noticeList);
        const bool hasSelection = noticeList->currentItem() != nullptr;
        const bool createEntry = isGroupCreateEntryId(currentId);
        const bool hasActionableGroup = hasEnabledListRow(noticeList);
        const GroupNoticeActionState state = NotificationPanelManager::groupNoticeActionState(
            currentId,
            hasSelection,
            !searchEdit->text().trimmed().isEmpty(),
            noticeList->count());
        openBtn->setEnabled(state.openEnabled);
        openBtn->setText(state.openText);
        openBtn->setToolTip(state.openToolTip);
        copyBtn->setEnabled(state.copyIdEnabled);
        copyBtn->setToolTip(state.copyIdToolTip);
        cardBtn->setEnabled(state.copyCardEnabled);
        cardBtn->setToolTip(state.copyCardToolTip);
        announceBtn->setEnabled(state.copyAnnouncementEnabled);
        announceBtn->setToolTip(state.copyAnnouncementToolTip);
        memberBtn->setEnabled(state.copyMembersEnabled);
        memberBtn->setToolTip(state.copyMembersToolTip);
        onlineMemberBtn->setEnabled(state.copyOnlineMembersEnabled);
        onlineMemberBtn->setToolTip(state.copyOnlineMembersToolTip);
        inviteTextBtn->setEnabled(state.copyInviteEnabled);
        inviteTextBtn->setToolTip(state.copyInviteToolTip);
        copyGroupMediaPackBtn->setEnabled(state.copyMediaPackEnabled);
        copyGroupMediaPackBtn->setToolTip(state.copyMediaPackToolTip);
        copyGroupBatchPlanBtn->setEnabled(state.copyBatchPlanEnabled);
        copyGroupBatchPlanBtn->setToolTip(state.copyBatchPlanToolTip);
        copyMediaGuideBtn->setEnabled(state.copyMediaGuideEnabled);
        copyMediaGuideBtn->setToolTip(state.copyMediaGuideToolTip);
        hintLabel->setText(!hasActionableGroup
                               ? emptyGroupNoticePreviewText()
                               : (createEntry
                                      ? QStringLiteral("当前是建群建议项，可直接创建并进入，再继续邀请好友或整理群资料。")
                                      : (currentId.isEmpty()
                                             ? QStringLiteral("当前选中公共聊天室，可先进入，再复制成员与发送指南。")
                                             : state.hintText)));
    };
    auto updateGroupPreviewState = [this, noticeList, searchEdit, groupPreviewLabel, emptyGroupNoticePreviewText]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        groupPreviewLabel->setText(noticeList->currentItem()
                                       ? snapshot.previewText
                                       : (hasEnabledListRow(noticeList)
                                              ? QStringLiteral("选择群聊后可进入、复制群资料、查看成员或整理媒体计划。")
                                              : emptyGroupNoticePreviewText()));
    };
    updateGroupPreviewState();
    updateGroupActionState();
    connect(noticeList, &QListWidget::currentItemChanged, &dialog, [updateGroupPreviewState, updateGroupActionState](QListWidgetItem*, QListWidgetItem*) {
        updateGroupPreviewState();
        updateGroupActionState();
    });
    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillGroups, updateGroupPreviewState, updateGroupActionState]() {
        fillGroups();
        updateGroupPreviewState();
        updateGroupActionState();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, openSelectedGroup);
    connect(copyBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制群号的群聊",
                                                 "待创建群聊还没有群号，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(snapshot.copyId);
        ui->statusbar->showMessage("群号已复制: " + snapshot.copyId, 2500);
    });
    connect(cardBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制名片的群聊",
                                                 "待创建群聊还没有名片，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(snapshot.cardText);
        ui->statusbar->showMessage("群名片已复制", 1800);
    });
    connect(announceBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制公告的群聊",
                                                 "待创建群聊还没有公告，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(snapshot.announcementText);
        ui->statusbar->showMessage("群公告已复制", 1800);
    });
    connect(inviteTextBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(NotificationPanelManager::groupNoticeInviteText(
            snapshot.copyContext.groupName,
            snapshot.copyContext.groupNumber,
            m_currentUserName,
            m_currentUserId));
        ui->statusbar->showMessage("入群邀请话术已复制", 2200);
    });
    connect(memberBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制成员的群聊",
                                                 "待创建群聊还没有成员列表，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const QStringList members = groupNoticeMemberIds(groupId);
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupNoticeMemberCopyInputs(members), false);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(onlineMemberBtn, &QPushButton::clicked, &dialog, [this, noticeList]() {
        QString groupId;
        if (!trySelectedInspectableGroupNoticeId(noticeList,
                                                 ui->statusbar,
                                                 "请先选择要复制在线成员的群聊",
                                                 "待创建群聊还没有在线成员，请先进入创建",
                                                 &groupId)) {
            return;
        }
        const QStringList members = groupNoticeMemberIds(groupId);
        const GroupNoticeMemberCopyState state =
            NotificationPanelManager::groupMemberCopyState(groupNoticeMemberCopyInputs(members), true);
        if (state.rows.isEmpty()) {
            ui->statusbar->showMessage(state.emptyStatusMessage, 2200);
            return;
        }
        QApplication::clipboard()->setText(state.rows.join('\n'));
        ui->statusbar->showMessage(state.copiedStatusMessage, 2200);
    });
    connect(copyGroupMediaPackBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(NotificationPanelManager::groupNoticeMediaPackText(
            snapshot.copyContext.groupName,
            snapshot.copyContext.groupNumber,
            snapshot.copyContext.memberCount,
            m_currentUserName,
            m_currentUserId));
        ui->statusbar->showMessage("群媒体包已复制", 2200);
    });
    connect(copyGroupBatchPlanBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeBatchPlanState state = NotificationPanelManager::groupNoticeBatchPlanState(
            searchEdit->text().trimmed(),
            visibleGroupNoticeBatchTargets(noticeList),
            m_currentUserName,
            m_currentUserId);
        QApplication::clipboard()->setText(state.text);
        ui->statusbar->showMessage(state.statusMessage, 2200);
    });
    connect(copyMediaGuideBtn, &QPushButton::clicked, &dialog, [this, noticeList, searchEdit]() {
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        QApplication::clipboard()->setText(NotificationPanelManager::groupNoticeMediaGuideText(
            snapshot.copyContext.groupName,
            snapshot.copyContext.groupNumber,
            snapshot.copyContext.memberCount,
            m_currentUserName,
            m_currentUserId));
        ui->statusbar->showMessage("群上传指南已复制", 2200);
    });
    connect(noticeList, &QListWidget::itemDoubleClicked, &dialog, [openSelectedGroup](QListWidgetItem*) { openSelectedGroup(); });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::appendMessage(const Message& msg) {
    onNewMessage(msg);
}

void MainWindow::appendSystemMessage(const QString& text) {
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString line = QString("[%1] [系统] %2").arg(timeStr, text);
    QStandardItem* item = createChatMessageItem(line,
                                                QStringLiteral("system"),
                                                QStringLiteral("系统"),
                                                false,
                                                Qt::darkGray,
                                                QColor(245, 247, 250));
    m_chatModel->appendRow(item);
    ui->chatListView->scrollToBottom();
}

bool MainWindow::ensureTransferTargetReady(const QString& kind, const QString& targetName, bool isLocalGroup) {
    if (m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup()) {
        const TransferSendUiState state = m_transferManager.publicGroupRemovedState(kind);
        applyTransferSendState(state);
        refreshComposerState();
        return false;
    }
    if (!isLocalGroup && (!m_client || !m_client->isConnected())) {
        const TransferSendUiState state = m_transferManager.disconnectedSendState(kind, targetName);
        applyTransferSendState(state);
        refreshComposerState();
        return false;
    }
    return true;
}

bool MainWindow::selectTransferFile(const TransferSelectionPlan& selectionPlan,
                                    QString* filePath,
                                    QFileInfo* fileInfo,
                                    QString* fileSize) {
    if (!filePath || !fileInfo) {
        return false;
    }

    const QString selectedPath = selectOpenFilePath(selectionPlan.dialogTitle,
                                                    LocalFileManager::lastTransferDirectory(),
                                                    selectionPlan.filters,
                                                    this);
    TransferSelectionUiState selectionState =
        m_transferManager.transferSelectionUiState(selectionPlan, selectedPath);
    if (!handleTransferSelectionUiState(&selectionState)) {
        return false;
    }

    *filePath = selectionState.filePath;
    *fileInfo = selectionState.fileInfo;
    if (fileSize) {
        *fileSize = selectionState.fileSize;
    }
    return true;
}

bool MainWindow::selectTransferFileContext(const TransferSelectionPlan& selectionPlan,
                                           SelectedTransferFile* selectedFile) {
    if (!selectedFile) {
        return false;
    }

    return selectTransferFile(selectionPlan,
                              &selectedFile->filePath,
                              &selectedFile->info,
                              &selectedFile->fileSize);
}

bool MainWindow::handleTransferSelectionUiState(TransferSelectionUiState* selectionState) {
    if (!selectionState) {
        return false;
    }

    while (true) {
        const TransferSelectionFeedbackPlan feedbackPlan =
            m_transferManager.transferSelectionFeedbackPlan(*selectionState);
        const TransferSendUiState workspaceState =
            m_transferManager.selectionFeedbackWorkspaceState(*selectionState, feedbackPlan);
        if (!workspaceState.workspaceTitle.trimmed().isEmpty()
            || !workspaceState.workspaceDetail.trimmed().isEmpty()) {
            setTransferWorkspaceSendState(workspaceState);
        }

        if (feedbackPlan.dialogKind == TransferSelectionFeedbackPlan::DialogKind::Warning) {
            showWarningDialog(feedbackPlan.dialogTitle, feedbackPlan.dialogMessage, this);
        }

        if (!feedbackPlan.hintText.isEmpty()) {
            ui->chatHintLabel->setText(feedbackPlan.hintText);
        }
        if (!feedbackPlan.statusMessage.isEmpty()) {
            ui->statusbar->showMessage(feedbackPlan.statusMessage, feedbackPlan.statusTimeoutMs);
        }

        if (feedbackPlan.requiresConfirmation) {
            const bool confirmed = confirmAction(feedbackPlan.dialogTitle,
                                                 feedbackPlan.dialogMessage,
                                                 feedbackPlan.statusMessage,
                                                 feedbackPlan.statusTimeoutMs,
                                                 this);
            *selectionState = m_transferManager.resolveTransferSelectionUiState(*selectionState, confirmed);
            continue;
        }

        return !feedbackPlan.stopSelection && selectionState->accepted;
    }
}

void MainWindow::applyTransferSendState(const TransferSendUiState& state) {
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
    setTransferWorkspaceSendState(state);
}

void MainWindow::appendTransferCompletionState(const TransferSendUiState& state,
                                               bool includeSystemMessage,
                                               bool includeCard,
                                               const QColor& cardForeground,
                                               const QColor& cardBackground,
                                               const QString& mediaKind,
                                               const QString& openPath) {
    if (includeSystemMessage && !state.systemMessage.isEmpty()) {
        appendSystemMessage(state.systemMessage);
    }

    if (includeCard && !state.cardText.isEmpty()) {
        QStandardItem* cardItem = createChatMessageItem(state.cardText,
                                                        m_currentUserId,
                                                        m_currentUserName,
                                                        true,
                                                        cardForeground,
                                                        cardBackground,
                                                        mediaKind,
                                                        openPath);
        m_chatModel->appendRow(cardItem);
    }

    if (!state.receiptText.isEmpty()) {
        QStandardItem* receiptItem = new QStandardItem(state.receiptText);
        receiptItem->setEditable(false);
        receiptItem->setForeground(QColor(86, 116, 130));
        receiptItem->setBackground(QColor(246, 251, 253));
        receiptItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(receiptItem);
    }

    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
    setTransferWorkspaceSendState(state);
    ui->chatListView->scrollToBottom();
}

void MainWindow::appendLocalGroupFileTransferCompletion(const TransferSelectionPlan& selectionPlan,
                                                        const QFileInfo& info,
                                                        const QString& fileSize,
                                                        const QString& targetName,
                                                        const QString& completedAt) {
    const TransferSendUiState completedState = m_transferManager.localSendCompletedState(
        selectionPlan.preparingKind,
        info.fileName(),
        fileSize,
        targetName,
        completedAt);
    const QString line = QString("[%1] <%2> 发送了文件: %3 · %4")
        .arg(completedAt, m_currentUserName, info.fileName(), fileSize);
    saveHistory(m_privateChatTarget, line);
    QStandardItem* item = createChatMessageItem(line,
                                                m_currentUserId,
                                                m_currentUserName,
                                                true,
                                                QColor(20, 92, 160),
                                                QColor(218, 241, 255),
                                                QStringLiteral("file"),
                                                info.absoluteFilePath());
    m_chatModel->appendRow(item);
    appendTransferCompletionState(completedState,
                                  true,
                                  true,
                                  QColor(0, 121, 107),
                                  QColor(232, 248, 245),
                                  QStringLiteral("file"),
                                  info.absoluteFilePath());
}

void MainWindow::appendLocalGroupMediaTransferCompletion(const QString& filePath,
                                                         const QFileInfo& info,
                                                         const QString& fileSize,
                                                         const QString& mediaType,
                                                         bool isVideo,
                                                         const QString& targetName,
                                                         const QString& completedAt) {
    const TransferSendUiState completedState = m_transferManager.localSendCompletedState(
        mediaType,
        info.fileName(),
        fileSize,
        targetName,
        completedAt);
    const QString line = QString("[%1] <%2> [%3] %4 · %5")
        .arg(completedAt, m_currentUserName, mediaType, info.fileName(), fileSize);
    saveHistory(m_privateChatTarget, line);
    QStandardItem* item = createChatMessageItem(line,
                                                m_currentUserId,
                                                m_currentUserName,
                                                true,
                                                QColor(20, 92, 160),
                                                QColor(218, 241, 255),
                                                isVideo ? QStringLiteral("video") : QStringLiteral("image"),
                                                filePath,
                                                isVideo ? QPixmap() : QPixmap(filePath));
    m_chatModel->appendRow(item);

    QPixmap pixmap;
    if (!isVideo) {
        pixmap.load(filePath);
    }
    if ((!isVideo && !pixmap.isNull()) || isVideo) {
        const TransferMediaPreviewPlan previewPlan = m_transferManager.localMediaPreviewPlan(
            info.fileName(),
            fileSize,
            isVideo);
        appendMediaPreviewItem(previewPlan.text,
                               pixmap,
                               previewPlan.isVideo,
                               previewPlan.alignRight,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
    }
    appendTransferCompletionState(completedState, true, false, QColor(), QColor());
}

void MainWindow::appendRemoteMediaTransferCompletion(const QString& filePath,
                                                     const TransferSendUiState& completedState,
                                                     bool isVideo) {
    appendSystemMessage(completedState.systemMessage);
    const TransferMediaPreviewPlan previewPlan = m_transferManager.remoteMediaPreviewPlan(completedState.cardText, isVideo);
    if (!isVideo) {
        QPixmap pixmap(filePath);
        if (!pixmap.isNull()) {
            appendMediaPreviewItem(previewPlan.text,
                                   pixmap,
                                   previewPlan.isVideo,
                                   previewPlan.alignRight,
                                   filePath,
                                   m_currentUserId,
                                   m_currentUserName);
        }
    } else {
        appendMediaPreviewItem(previewPlan.text,
                               QPixmap(),
                               previewPlan.isVideo,
                               previewPlan.alignRight,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
    }
    appendTransferCompletionState(completedState, false, false, QColor(), QColor());
}

void MainWindow::handleRemoteTransferResult(bool ok,
                                            bool transferCanceled,
                                            const QString& filePath,
                                            const QFileInfo& info,
                                            const QString& fileSize,
                                            const QString& targetName,
                                            const QString& kind,
                                            bool media,
                                            bool isVideo,
                                            const QString& transferSummary) {
    if (ok) {
        const TransferSendUiState completedState = m_transferManager.remoteSendCompletedState(
            kind,
            info.fileName(),
            fileSize,
            targetName,
            QDateTime::currentDateTime().toString("hh:mm:ss"),
            transferSummary);
        if (media) {
            appendRemoteMediaTransferCompletion(filePath, completedState, isVideo);
        } else {
            appendTransferCompletionState(completedState,
                                          true,
                                          true,
                                          QColor(0, 121, 107),
                                          QColor(232, 248, 245),
                                          QStringLiteral("file"),
                                          filePath);
        }
        return;
    }

    if (transferCanceled) {
        appendSystemMessage(QString("已取消发送%1: %2 · 到 %3").arg(kind, info.fileName(), targetName));
        const TransferSendUiState state = m_transferManager.canceledSendState(kind, info.fileName());
        ui->chatHintLabel->setText(QString("%1 · %2").arg(state.hintText, targetName));
        ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
        setTransferWorkspaceSendState(state);
        refreshComposerState();
        return;
    }

    const TransferSendUiState state = m_transferManager.failedSendState(kind, info.fileName(), fileSize, targetName);
    applyTransferSendState(state);
    showWarningDialog(state.warningTitle, state.warningMessage, this);
    refreshComposerState();
}

void MainWindow::appendMediaPreviewItem(const QString& text,
                                        const QPixmap& pixmap,
                                        bool isVideo,
                                        bool alignRight,
                                        const QString& openPath,
                                        const QString& senderId,
                                        const QString& senderName) {
    QStandardItem* previewItem = createChatMessageItem(text,
                                                       senderId.isEmpty() ? m_currentUserId : senderId,
                                                       senderName.isEmpty() ? m_currentUserName : senderName,
                                                       alignRight,
                                                       isVideo ? QColor(126, 87, 194) : QColor(38, 50, 56),
                                                       isVideo ? QColor(245, 240, 255) : QColor(246, 250, 253),
                                                       isVideo ? QStringLiteral("video") : QStringLiteral("image"),
                                                       openPath,
                                                       pixmap);
    m_chatModel->appendRow(previewItem);
}

void MainWindow::applyReceivedTransferRenderPlan(const TransferReceiveRenderPlan& plan,
                                                 const QString& fileName,
                                                 const QString& transferId,
                                                 qint64 receivedBytes,
                                                 qint64 totalBytes) {
    for (const TransferChatListItemUiState& itemState : plan.chatItems) {
        appendTransferChatListItem(itemState);
    }
    showFileTransferStatusEvent(fileName,
                                transferId,
                                plan.eventReason,
                                receivedBytes,
                                totalBytes);
    ui->chatHintLabel->setText(plan.hintText);
    ui->statusbar->showMessage(plan.statusMessage, plan.statusTimeoutMs);
}

void MainWindow::showReceivedTransferWorkspace(const ReceivedTransferContext& context,
                                               const QString& displayName,
                                               bool saved) {
    const TransferReceivedWorkspaceState workspace =
        m_transferManager.receivedTransferWorkspaceState(context.kind,
                                                         context.receivedName,
                                                         context.receivedSize,
                                                         displayName,
                                                         context.manifestSuffix,
                                                         context.integrityText,
                                                         context.integritySuffix,
                                                         context.savePath,
                                                         saved);

    const LocalSavedFileState savedState =
        LocalFileManager::savedFileStateFromChatText(QStringLiteral("保存路径：") + context.savePath);
    if (!context.savePath.trimmed().isEmpty()) {
        setTransferWorkspaceSavedFileState(savedState, workspace.previewText);
    }

    TransferSendUiState workspaceState;
    workspaceState.workspaceTitle = workspace.workspaceTitle;
    workspaceState.workspaceDetail = QStringLiteral("%1\n%2\n%3")
                                         .arg(workspace.workspaceDetail,
                                              workspace.nextStep,
                                              workspace.preservedState);
    workspaceState.hintText = saved
        ? QStringLiteral("接收结果已同步到文件工作区 · %1").arg(context.receivedName)
        : QStringLiteral("接收保存失败需排查 · %1").arg(context.receivedName);
    workspaceState.statusMessage = saved
        ? QStringLiteral("已同步接收结果到文件工作区：") + context.receivedName
        : QStringLiteral("接收保存失败已同步到文件工作区：") + context.receivedName;
    workspaceState.statusTone = workspace.statusTone;
    setTransferWorkspaceSendState(workspaceState);
}

bool MainWindow::persistReceivedTransferPayload(const ReceivedTransferContext& context,
                                                const QString& displayName,
                                                const QString& transferId,
                                                const QByteArray& fileData,
                                                qint64 totalBytes) {
    const bool saved = LocalFileManager::writeReceivedTransferPayload(context.savePath, fileData);
    showReceivedTransferWorkspace(context, displayName, saved);
    applyReceivedTransferRenderPlan(receivedTransferPersistencePlan(context, displayName, saved),
                                    context.receivedName,
                                    transferId,
                                    fileData.size(),
                                    totalBytes);
    return saved;
}

bool MainWindow::handleReceivedTransferMessage(const Message& msg,
                                               const QString& displayName) {
    if (msg.fileData.isEmpty()) {
        return false;
    }

    const bool image = msg.type == MessageType::Image;
    const ReceivedTransferContext context = receivedTransferContext(
        msg,
        image ? QStringLiteral("图片") : QStringLiteral("文件"),
        image ? QStringLiteral("received_image") : QStringLiteral("received_file"),
        image ? QStringLiteral("Images") : QStringLiteral("Files"),
        displayName);
    if (image) {
        QPixmap pixmap;
        if (pixmap.loadFromData(msg.fileData)) {
            const TransferMediaPreviewPlan previewPlan = m_transferManager.receivedMediaPreviewPlan(
                context.receivedName,
                context.receivedSize,
                context.manifestSuffix);
            appendMediaPreviewItem(previewPlan.text,
                                   pixmap,
                                   previewPlan.isVideo,
                                   previewPlan.alignRight,
                                   context.savePath,
                                   msg.senderId,
                                   displayName);
        }
    }
    return persistReceivedTransferPayload(context,
                                          displayName,
                                          msg.transferId,
                                          msg.fileData,
                                          msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
}

TransferReceiveRenderPlan MainWindow::receivedTransferPersistencePlan(const ReceivedTransferContext& context,
                                                                      const QString& displayName,
                                                                      bool saved) const {
    return m_transferManager.receivedTransferPersistenceRenderPlan(context.kind,
                                                                  context.receivedName,
                                                                  context.receivedSize,
                                                                  displayName,
                                                                  context.manifestSuffix,
                                                                  context.integrityText,
                                                                  context.integritySuffix,
                                                                  context.savePath,
                                                                  saved);
}

void MainWindow::appendTransferChatListItem(const TransferChatListItemUiState& itemState) {
    if (itemState.text.isEmpty()) {
        return;
    }

    QStandardItem* item = TransferChatItemRenderer::createItem(itemState);
    m_chatModel->appendRow(item);
}

MainWindow::ReceivedTransferContext MainWindow::receivedTransferContext(const Message& msg,
                                                                        const QString& kind,
                                                                        const QString& fallbackName,
                                                                        const QString& downloadSubdir,
                                                                        const QString& displayName) const {
    ReceivedTransferContext context;
    context.kind = kind;
    const LocalReceivedTransferPlan localPlan = LocalFileManager::receivedTransferPlan(msg.fileName,
                                                                                       fallbackName,
                                                                                       msg.fileData.size(),
                                                                                       downloadSubdir);
    context.receivedName = localPlan.receivedName;
    context.receivedSize = localPlan.receivedSize;
    context.savePath = localPlan.savePath;
    context.integrityText = transferIntegritySummary(msg);
    context.integritySuffix = context.integrityText.isEmpty()
        ? QString()
        : QString(" · %1").arg(context.integrityText);
    const QString manifestText = m_transferManager.sendingPreparedState(kind,
                                                                        context.receivedName,
                                                                        displayName,
                                                                        msg.fileSize > 0 ? msg.fileSize : msg.fileData.size(),
                                                                        msg.chunkSize,
                                                                        msg.chunkCount,
                                                                        msg.fileHash).manifestSummary;
    context.manifestSuffix = manifestText.isEmpty() ? QString() : QString(" · %1").arg(manifestText);
    return context;
}

void MainWindow::loadHistory(const QString& peerId) {
    if (peerId.isEmpty()) return;

    const QStringList rows = m_historyService.recentRows(peerId, MAX_HISTORY_LINES);
    const QRegularExpression senderPattern(QStringLiteral("<([^>]+)>"));
    for (const QString& line : rows) {
        const QRegularExpressionMatch match = senderPattern.match(line);
        const QString senderName = match.hasMatch() ? match.captured(1) : QStringLiteral("历史");
        QStandardItem* item = createChatMessageItem(line,
                                                    QString(),
                                                    senderName,
                                                    false,
                                                    Qt::gray,
                                                    QColor(250, 252, 254));
        m_chatModel->appendRow(item);
    }
}

void MainWindow::saveHistory(const QString& peerId, const QString& content) {
    saveHistory(peerId, content, QStringLiteral("plaintext"));
}

void MainWindow::saveHistory(const QString& peerId,
                             const QString& content,
                             const QString& encryptionState,
                             const QString& e2eKeyId,
                             const QString& e2eKeyFingerprint) {
    m_historyService.save(peerId, content, encryptionState, e2eKeyId, e2eKeyFingerprint);
}

bool MainWindow::ensureClientDatabase() const {
    const QString connectionName = "client_history_init_" + QString::number(reinterpret_cast<quintptr>(this));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(clientDbPath());
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("CREATE TABLE IF NOT EXISTS friends ("
                            "user_id TEXT PRIMARY KEY, "
                            "display_name TEXT NOT NULL, "
                            "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS local_groups ("
                                "group_id TEXT PRIMARY KEY, "
                                "group_name TEXT NOT NULL, "
                                "members TEXT NOT NULL, "
                                "announcement TEXT, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS profile ("
                                "user_id TEXT PRIMARY KEY, "
                                "user_name TEXT NOT NULL, "
                                "avatar_path TEXT, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP)");
            }
            if (ok) {
                ok = query.exec("CREATE TABLE IF NOT EXISTS friend_requests ("
                                "request_id TEXT NOT NULL, "
                                "display_name TEXT NOT NULL, "
                                "direction TEXT NOT NULL, "
                                "status TEXT NOT NULL, "
                                "updated_at TEXT DEFAULT CURRENT_TIMESTAMP, "
                                "PRIMARY KEY(request_id, direction))");
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

QString MainWindow::clientDbPath() const {
    return m_historyService.databasePath();
}

bool MainWindow::saveProfileToSqlite() const {
    if (m_currentUserId.isEmpty() || !ensureClientDatabase()) return false;
    const QString avatarPath = QFileInfo::exists(getAvatarFilePath()) ? getAvatarFilePath() : QString();
    return m_clientStorage.saveProfileToSqlite(clientDbPath(), m_currentUserId, m_currentUserName, avatarPath);
}

QStandardItem* MainWindow::findUserItem(const QString& userId) {
    for (int i = 0; i < m_userListModel->rowCount(); ++i) {
        QStandardItem* item = m_userListModel->item(i);
        if (item->data(Qt::UserRole + 1).toString() == userId) {
            return item;
        }
    }
    return nullptr;
}

void MainWindow::refreshFriendList() {
    m_userListModel->clear();
    m_userListModel->setHorizontalHeaderLabels({"好友 / 在线"});

    bool loadedFriendsFromSqlite = false;
    if (m_friendIds.isEmpty() && ensureClientDatabase()) {
        const QString connectionName = "client_friends_read_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(clientDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT user_id, display_name FROM friends ORDER BY user_id ASC")) {
                    while (query.next()) {
                        const QString id = query.value(0).toString();
                        const QString name = query.value(1).toString();
                        if (!id.isEmpty() && !m_friendIds.contains(id)) {
                            m_friendIds << id;
                            loadedFriendsFromSqlite = true;
                        }
                        if (!id.isEmpty() && !name.isEmpty()) {
                            m_friendNames[id] = name;
                        }
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    if (!loadedFriendsFromSqlite
        && m_friendIds.isEmpty()
        && m_clientStorage.readLegacyFriends(&m_friendIds, &m_friendNames)) {
        saveFriends();
    }

    if (m_pendingFriendRequests.isEmpty() && m_pendingOutgoingFriendRequests.isEmpty() && ensureClientDatabase()) {
        const QString connectionName = "client_requests_read_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(clientDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT request_id, display_name, direction FROM friend_requests WHERE status = 'pending' ORDER BY updated_at ASC")) {
                    while (query.next()) {
                        const QString id = query.value(0).toString();
                        const QString name = query.value(1).toString();
                        const QString direction = query.value(2).toString();
                        if (id.isEmpty()) continue;
                        if (!name.isEmpty()) m_friendNames[id] = name;
                        if (direction == "incoming" && !m_pendingFriendRequests.contains(id) && !m_friendIds.contains(id)) {
                            m_pendingFriendRequests << id;
                        } else if (direction == "outgoing" && !m_pendingOutgoingFriendRequests.contains(id) && !m_friendIds.contains(id)) {
                            m_pendingOutgoingFriendRequests << id;
                        }
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        ui->friendNoticeBtn->setText(m_pendingFriendRequests.isEmpty() ? "好友通知" : QString("好友通知 %1").arg(m_pendingFriendRequests.size()));
        ui->friendNoticeBtn->setToolTip(m_pendingFriendRequests.isEmpty() ? "查看并处理好友申请" : QString("有 %1 个好友申请待处理").arg(m_pendingFriendRequests.size()));
    }

    bool loadedGroupsFromSqlite = false;
    if (m_localGroupIds.isEmpty() && ensureClientDatabase()) {
        const QString connectionName = "client_groups_read_" + QString::number(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            db.setDatabaseName(clientDbPath());
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec("SELECT group_id, group_name, members, announcement FROM local_groups ORDER BY group_id ASC")) {
                    while (query.next()) {
                        const QString id = query.value(0).toString();
                        const QString name = query.value(1).toString();
                        const QStringList members = query.value(2).toString().split(',', Qt::SkipEmptyParts);
                        const QString announcement = query.value(3).toString();
                        if (!id.isEmpty() && !m_localGroupIds.contains(id)) {
                            m_localGroupIds << id;
                            loadedGroupsFromSqlite = true;
                        }
                        if (!id.isEmpty() && !name.isEmpty()) {
                            m_localGroupNames[id] = name;
                        }
                        if (!id.isEmpty()) {
                            m_localGroupMembers[id] = members.isEmpty() ? QStringList{m_currentUserId} : members;
                            m_localGroupAnnouncements[id] = announcement.isEmpty()
                                ? QString("%1 已创建，可继续邀请好友并发送消息。").arg(m_localGroupNames.value(id, "群聊"))
                                : announcement;
                        }
                    }
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    if (!loadedGroupsFromSqlite
        && m_localGroupIds.isEmpty()
        && m_clientStorage.readLegacyLocalGroups(m_currentUserId,
                                                 &m_localGroupIds,
                                                 &m_localGroupNames,
                                                 &m_localGroupMembers,
                                                 &m_localGroupAnnouncements)) {
        saveLocalGroups();
    }

    const FriendNoticeUiState noticeState = m_friendManager.noticeUiState(m_pendingFriendRequests.size());
    const GroupNoticeUiState groupNoticeState = m_groupManager.noticeUiState(m_localGroupIds.size());
    ui->friendNoticeBtn->setText(noticeState.text);
    ui->friendNoticeBtn->setToolTip(noticeState.toolTip);
    ui->groupNoticeBtn->setText(groupNoticeState.text);
    ui->groupNoticeBtn->setToolTip(groupNoticeState.toolTip);

    auto appendSection = [this](const QString& title) {
        QStandardItem* section = new QStandardItem(title);
        section->setEditable(false);
        section->setEnabled(false);
        section->setForeground(QColor(93, 109, 126));
        section->setBackground(QColor(236, 243, 249));
        m_userListModel->appendRow(section);
    };

    int visibleCount = 0;
    int visibleFriends = 0;
    int visibleGroups = 0;
    int visibleOnlineUsers = 0;
    int onlineFriendCount = 0;
    int visiblePendingOutgoing = 0;
    int visibleStrangers = 0;
    auto matchesFilter = [this](const QString& id, const QString& name) {
        return m_friendManager.matchesFilter(id, name, m_contactFilter);
    };

    appendSection("我的好友");
    for (const QString& friendId : m_friendIds) {
        if (m_knownUsers.contains(friendId)) continue;
        QString name = m_friendNames.value(friendId, friendId);
        if (!matchesFilter(friendId, name)) continue;
        QStandardItem* item = new QStandardItem(QString("☆ QQ:%1\n   %2 [离线]").arg(friendId, name));
        item->setData(friendId, Qt::UserRole + 1);
        item->setForeground(QColor(77, 98, 118));
        item->setData(chatAvatarPixmap(friendId, name, 32), Qt::DecorationRole);
        m_userListModel->appendRow(item);
        ++visibleCount;
        ++visibleFriends;
    }

    appendSection("群聊");
    for (const QString& groupId : m_localGroupIds) {
        QString groupName = m_localGroupNames.value(groupId, "群聊");
        if (!matchesFilter(groupId, groupName)) continue;
        QStandardItem* item = new QStandardItem(QString("群聊 QQ:%1\n   %2 [本地]").arg(groupId.mid(QString("local_group_").size()), groupName));
        item->setData(groupId, Qt::UserRole + 1);
        item->setForeground(QColor(29, 78, 216));
        m_userListModel->appendRow(item);
        ++visibleCount;
        ++visibleGroups;
    }

    appendSection("在线成员");
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        const ChatUser& user = it.value();
        if (user.name == m_currentUserName) continue;
        if (!matchesFilter(user.id, user.name)) continue;
        bool isFriend = m_friendIds.contains(user.id);
        if (isFriend) {
            m_friendNames[user.id] = user.name;
            ++onlineFriendCount;
        }
        const bool isPending = m_pendingOutgoingFriendRequests.contains(user.id);
        if (isPending && !isFriend) ++visiblePendingOutgoing;
        const QString marker = isFriend ? "★" : "○";
        const QString stateSuffix = isFriend ? " [在线]" : (isPending ? " [申请中]" : "");
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n   %3%4").arg(marker, user.id, user.name, stateSuffix));
        item->setData(user.id, Qt::UserRole + 1);
        item->setForeground(isFriend ? QColor(15, 23, 42)
                                     : (isPending ? QColor(170, 110, 20) : QColor(60, 78, 96)));
        item->setData(chatAvatarPixmap(user.id, user.name, 32), Qt::DecorationRole);
        m_userListModel->appendRow(item);
        ++visibleCount;
        ++visibleOnlineUsers;
        if (!isFriend && !isPending) ++visibleStrangers;
    }

    const QString pendingPart = visiblePendingOutgoing > 0 ? QString(" · 申请中%1").arg(visiblePendingOutgoing) : QString();
    ui->onlineTitleLabel->setText(m_contactFilter.isEmpty()
        ? QString("联系人工作区 · 好友在线 %1/%2 · 群聊 %3%4 · 可发现 %5")
              .arg(onlineFriendCount)
              .arg(m_friendIds.size())
              .arg(visibleGroups)
              .arg(pendingPart)
              .arg(visibleStrangers)
        : QString("联系人工作区 · 匹配 %1 项 · 好友 %2 · 群聊 %3 · 在线 %4%5 · 可发现 %6")
              .arg(visibleCount)
              .arg(visibleFriends + onlineFriendCount)
              .arg(visibleGroups)
              .arg(visibleOnlineUsers)
              .arg(pendingPart)
              .arg(visibleStrangers));

    if (visibleCount == 0 && !m_contactFilter.isEmpty()) {
        QStandardItem* addItem = new QStandardItem(QString("搜索并申请 QQ:%1\n   回车或双击查找好友").arg(m_contactFilter));
        addItem->setData("search_add:" + m_contactFilter, Qt::UserRole + 1);
        addItem->setForeground(QColor(29, 78, 216));
        addItem->setBackground(QColor(232, 240, 254));
        m_userListModel->appendRow(addItem);
        QStandardItem* groupItem = new QStandardItem(QString("创建群聊:%1\n   双击立即建群并进入").arg(m_contactFilter));
        groupItem->setData("create_group:" + m_contactFilter, Qt::UserRole + 1);
        groupItem->setForeground(QColor(13, 148, 136));
        groupItem->setBackground(QColor(229, 248, 244));
        m_userListModel->appendRow(groupItem);
        ui->onlineTitleLabel->setText(QString("联系人工作区 · 未匹配结果 · 可搜索 QQ 或创建群聊：%1").arg(m_contactFilter));
    }
}

void MainWindow::onContactSearchChanged(const QString& text) {
    m_contactFilter = text.trimmed();
    refreshFriendList();
    if (!m_contactFilter.isEmpty()) {
        ui->statusbar->showMessage(QString("QQ搜索:%1 · 无结果可双击搜索并申请或建群").arg(m_contactFilter), 1800);
    } else {
        ui->statusbar->showMessage(QString("联系人已显示 · 好友%1 · 本地群%2 · 在线%3").arg(m_friendIds.size()).arg(m_localGroupIds.size()).arg(m_knownUsers.size()), 1200);
    }
}

void MainWindow::refreshGroupMemberPanel() {
    if (!ui->groupMemberListView || !m_groupMemberModel) return;
    QString filter = ui->memberSearchEdit ? ui->memberSearchEdit->text().trimmed() : QString();
    m_groupMemberModel->clear();
    m_groupMemberModel->setHorizontalHeaderLabels({"群成员"});

    if (!m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_")) {
        QStringList members = m_localGroupMembers.value(m_privateChatTarget);
        if (members.isEmpty()) members << m_currentUserId;
        const QString ownerId = members.first();
        const QString ownerName = ownerId == m_currentUserId ? m_currentUserName : contactDisplayName(ownerId);
        int visibleMembers = 0;
        int onlineMembers = 0;
        int friendMembers = 0;
        int pendingMembers = 0;
        for (const QString& memberId : members) {
            QString name = memberId == m_currentUserId ? m_currentUserName : m_friendNames.value(memberId, memberId);
            bool online = isContactOnline(memberId) || memberId == m_currentUserId;
            bool isFriend = m_friendIds.contains(memberId);
            bool isPending = !isFriend && m_pendingOutgoingFriendRequests.contains(memberId);
            if (online) ++onlineMembers;
            if (isFriend) ++friendMembers;
            if (isPending) ++pendingMembers;
            if (!filter.isEmpty()
                && !memberId.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
            const GroupMemberDisplayState display = m_groupManager.memberDisplayState(
                memberId, m_currentUserId, ownerId, QString(), isFriend, isPending, online, false);
            QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5")
                .arg(display.role, memberId, name, display.state, display.actionText));
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            item->setForeground(memberId == m_currentUserId ? QColor(29, 78, 216)
                                                            : (isFriend ? QColor(20, 92, 160)
                                                                        : (isPending ? QColor(170, 110, 20) : QColor(60, 78, 96))));
            m_groupMemberModel->appendRow(item);
            ++visibleMembers;
        }
        QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
        ui->memberTitleLabel->setText(QString("群成员工作区 · 共 %1 人 · 群主 %2 · 在线 %3 · 好友 %4%5")
            .arg(members.size())
            .arg(ownerName)
            .arg(onlineMembers)
            .arg(friendMembers)
            .arg(pendingPart));
        if (visibleMembers == 0 && !filter.isEmpty()) {
            QStandardItem* addItem = new QStandardItem(QString("邀请 QQ:%1\n双击自动加入当前群聊").arg(filter));
            addItem->setData("group_invite:" + filter, Qt::UserRole + 1);
            addItem->setEditable(false);
            addItem->setForeground(QColor(29, 78, 216));
            addItem->setEnabled(isCurrentUserGroupOwner(m_privateChatTarget));
            addItem->setToolTip(isCurrentUserGroupOwner(m_privateChatTarget) ? "双击邀请该 QQ 入群" : "只有群主可以邀请新成员入群");
            m_groupMemberModel->appendRow(addItem);
            ui->memberTitleLabel->setText(QString("群成员工作区 · 共 %1 人 · 群主 %2 · 在线 %3 · 好友 %4%5 · %6 QQ:%7")
                .arg(members.size())
                .arg(ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(isCurrentUserGroupOwner(m_privateChatTarget) ? "可邀请" : "无权限邀请")
                .arg(filter));
            refreshMainWorkbenchChrome();
        } else if (!filter.isEmpty()) {
            ui->memberTitleLabel->setText(QString("群成员工作区 · 共 %1 人 · 群主 %2 · 在线 %3 · 好友 %4%5 · 匹配 %6")
                .arg(members.size())
                .arg(ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(visibleMembers));
        }
        refreshMainWorkbenchChrome();
        return;
    }

    const QStringList serverPublicMembers = m_serverGroupMembers.value("public");
    if (isCurrentUserRemovedFromPublicGroup()) {
        const QJsonObject removedInfo = m_removedServerGroups.value("public");
        const QString removedBy = removedInfo["removedByName"].toString(removedInfo["removedBy"].toString());
        const QString removedAt = removedInfo["removedAt"].toString();
        const QString detail = removedBy.isEmpty()
            ? QStringLiteral("只读历史 · 等待重新邀请")
            : QStringLiteral("只读历史 · %1 移出%2")
                .arg(removedBy, removedAt.isEmpty() ? QString() : QStringLiteral(" · %1").arg(removedAt));
        QStandardItem* removedItem = new QStandardItem(QString("已不在公共群 QQ:%1\n%2").arg(m_currentUserId, detail));
        removedItem->setData(m_currentUserId, Qt::UserRole + 1);
        removedItem->setEditable(false);
        removedItem->setEnabled(false);
        removedItem->setForeground(QColor(170, 110, 20));
        removedItem->setToolTip("服务端已移出当前账号；本机聊天历史仍可查看，重新邀请后会自动恢复群成员列表");
        if (filter.isEmpty()
            || m_currentUserId.contains(filter, Qt::CaseInsensitive)
            || QString("等待邀请 只读 历史").contains(filter, Qt::CaseInsensitive)) {
            m_groupMemberModel->appendRow(removedItem);
        } else {
            delete removedItem;
        }
        ui->memberTitleLabel->setText("公共群工作区 · 当前账号已移出 · 历史只读");
        refreshMainWorkbenchChrome();
        return;
    }

    if (!serverPublicMembers.isEmpty()) {
        const bool canManagePublicGroup = canCurrentUserManageServerGroup("public");
        const QString ownerId = m_serverGroupOwners.value("public");
        const QString ownerName = ownerId == m_currentUserId
            ? m_currentUserName
            : m_serverGroupMemberNames.value("public|" + ownerId, contactDisplayName(ownerId));
        int memberCount = 0;
        int visibleMembers = 0;
        int friendMembers = 0;
        int pendingMembers = 0;
        int onlineMembers = 0;

        for (const QString& memberId : serverPublicMembers) {
            if (memberId.trimmed().isEmpty()) continue;
            const QString name = memberId == m_currentUserId
                ? m_currentUserName
                : m_serverGroupMemberNames.value("public|" + memberId, contactDisplayName(memberId));
            const bool online = memberId == m_currentUserId || isContactOnline(memberId);
            const bool isFriend = m_friendIds.contains(memberId);
            const bool isPending = !isFriend && memberId != m_currentUserId && m_pendingOutgoingFriendRequests.contains(memberId);
            ++memberCount;
            if (online) ++onlineMembers;
            if (isFriend) ++friendMembers;
            if (isPending) ++pendingMembers;
            if (!filter.isEmpty()
                && !memberId.contains(filter, Qt::CaseInsensitive)
                && !name.contains(filter, Qt::CaseInsensitive)) {
                continue;
            }

            const QString serverRole = m_serverGroupMemberRoles.value("public|" + memberId, "member").toLower();
            const GroupMemberDisplayState display = m_groupManager.memberDisplayState(
                memberId, m_currentUserId, ownerId, serverRole, isFriend, isPending, online, true);
            QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · %4 · %5")
                .arg(display.role, memberId, name, display.state, display.actionText));
            item->setData(memberId, Qt::UserRole + 1);
            item->setEditable(false);
            item->setForeground(display.isOwner ? QColor(156, 98, 0)
                                                : (memberId == m_currentUserId ? QColor(29, 78, 216)
                                                                               : (isFriend ? QColor(20, 92, 160)
                                                                                           : (isPending ? QColor(170, 110, 20) : QColor(60, 78, 96)))));
            m_groupMemberModel->appendRow(item);
            ++visibleMembers;
        }

        if (visibleMembers == 0 && !filter.isEmpty()) {
            QStandardItem* addItem = new QStandardItem(canManagePublicGroup
                ? QString("邀请 QQ:%1 加入公共群\n双击提交服务端成员变更").arg(filter)
                : QString("搜索并申请 QQ:%1\n双击查找好友").arg(filter));
            addItem->setData("group_search_add:" + filter, Qt::UserRole + 1);
            addItem->setEditable(false);
            addItem->setForeground(canManagePublicGroup ? QColor(13, 148, 136) : QColor(29, 78, 216));
            addItem->setToolTip(canManagePublicGroup ? "双击后由服务端校验群主/管理员权限并添加成员" : "双击查找该 QQ 并发起好友申请");
            m_groupMemberModel->appendRow(addItem);
            ui->memberTitleLabel->setText(QString("公共群工作区 · 共 %1 人 · 群主 %2 · 在线 %3 · %4 QQ:%5")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(canManagePublicGroup ? "可邀请" : "可搜索")
                .arg(filter));
            refreshMainWorkbenchChrome();
            return;
        }

        QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
        int auditVisibleCount = 0;
        const QJsonArray auditEvents = m_serverGroupAuditEvents.value("public");
        if (!auditEvents.isEmpty() && (filter.isEmpty() || QStringLiteral("审计").contains(filter, Qt::CaseInsensitive))) {
            QStandardItem* auditHeader = new QStandardItem(QString("最近群审计 · %1 条\n服务端同步公告、成员和管理员变更").arg(auditEvents.size()));
            auditHeader->setEditable(false);
            auditHeader->setEnabled(false);
            auditHeader->setForeground(QColor(92, 107, 120));
            m_groupMemberModel->appendRow(auditHeader);
            const int start = qMax(0, auditEvents.size() - 3);
            for (int i = start; i < auditEvents.size(); ++i) {
                const QJsonObject event = auditEvents.at(i).toObject();
                QStandardItem* auditItem = new QStandardItem(QString("审计 · %1").arg(serverGroupAuditSummary(event)));
                auditItem->setEditable(false);
                auditItem->setEnabled(false);
                auditItem->setForeground(QColor(92, 107, 120));
                auditItem->setToolTip(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
                m_groupMemberModel->appendRow(auditItem);
                ++auditVisibleCount;
            }
        }
        ui->memberTitleLabel->setText(filter.isEmpty()
            ? QString("公共群工作区 · 共 %1 人 · 群主 %2 · 在线 %3 · 好友 %4%5%6")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(auditEvents.isEmpty() ? QString() : QString(" · 审计%1").arg(auditEvents.size()))
            : QString("公共群工作区 · 共 %1 人 · 群主 %2 · 在线 %3 · 好友 %4%5 · 匹配 %6%7")
                .arg(memberCount)
                .arg(ownerName.isEmpty() ? "未指定" : ownerName)
                .arg(onlineMembers)
                .arg(friendMembers)
                .arg(pendingPart)
                .arg(visibleMembers)
                .arg(auditVisibleCount > 0 ? QString(" · 审计%1").arg(auditVisibleCount) : QString()));
        refreshMainWorkbenchChrome();
        return;
    }

    QStandardItem* selfItem = new QStandardItem(QString("我  QQ:%1\n%2 · 在线").arg(m_currentUserId, m_currentUserName));
    selfItem->setData(m_currentUserId, Qt::UserRole + 1);
    selfItem->setEditable(false);
    selfItem->setForeground(QColor(29, 78, 216));
    selfItem->setData(chatAvatarPixmap(m_currentUserId, m_currentUserName, 32), Qt::DecorationRole);
    if (filter.isEmpty() || m_currentUserId.contains(filter, Qt::CaseInsensitive) || m_currentUserName.contains(filter, Qt::CaseInsensitive)) {
        m_groupMemberModel->appendRow(selfItem);
    } else {
        delete selfItem;
    }

    int memberCount = 1;
    int visibleMembers = 0;
    int friendMembers = 0;
    int pendingMembers = 0;
    int onlineMembers = 1;
    for (auto it = m_knownUsers.begin(); it != m_knownUsers.end(); ++it) {
        const ChatUser& user = it.value();
        if (user.id == m_currentUserId) continue;
        ++memberCount;
        if (!filter.isEmpty()
            && !user.id.contains(filter, Qt::CaseInsensitive)
            && !user.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        bool isFriend = m_friendIds.contains(user.id);
        bool isPending = !isFriend && m_pendingOutgoingFriendRequests.contains(user.id);
        if (isFriend) ++friendMembers;
        if (isPending) ++pendingMembers;
        ++onlineMembers;
        QStandardItem* item = new QStandardItem(QString("%1 QQ:%2\n%3 · 在线 · %4").arg(isFriend ? "好友" : (isPending ? "申请中" : "成员"), user.id, user.name, isFriend ? "已是好友" : (isPending ? "等待确认" : "双击发送申请")));
        item->setData(user.id, Qt::UserRole + 1);
        item->setEditable(false);
        item->setForeground(isFriend ? QColor(20, 92, 160) : (isPending ? QColor(170, 110, 20) : QColor(60, 78, 96)));
        item->setData(chatAvatarPixmap(user.id, user.name, 32), Qt::DecorationRole);
        m_groupMemberModel->appendRow(item);
        ++visibleMembers;
    }
    if (visibleMembers == 0 && !filter.isEmpty()) {
        QStandardItem* addItem = new QStandardItem(QString("搜索并申请 QQ:%1\n双击查找好友").arg(filter));
        addItem->setData("group_search_add:" + filter, Qt::UserRole + 1);
        addItem->setEditable(false);
        addItem->setForeground(QColor(29, 78, 216));
        m_groupMemberModel->appendRow(addItem);
        ui->memberTitleLabel->setText(QString("群成员工作区 · 共 %1 人 · 在线 %2 · 可搜索 QQ:%3").arg(memberCount).arg(onlineMembers).arg(filter));
        refreshMainWorkbenchChrome();
        return;
    }
    QString pendingPart = pendingMembers > 0 ? QString(" · 申请中%1").arg(pendingMembers) : QString();
    ui->memberTitleLabel->setText(filter.isEmpty()
        ? QString("群成员工作区 · 共 %1 人 · 在线 %2 · 好友 %3%4").arg(memberCount).arg(onlineMembers).arg(friendMembers).arg(pendingPart)
        : QString("群成员工作区 · 共 %1 人 · 在线 %2 · 好友 %3%4 · 匹配 %5").arg(memberCount).arg(onlineMembers).arg(friendMembers).arg(pendingPart).arg(visibleMembers));
    refreshMainWorkbenchChrome();
}

void MainWindow::loadAvatar() {
    const QString avatarPath = getAvatarFilePath();
    QPixmap pixmap(avatarPath);
    if (!pixmap.isNull()) {
        const QPixmap square = squareAvatarPixmap(pixmap, ui->avatarLabel->width());
        ui->avatarLabel->setPixmap(square);
        if (m_client) {
            QByteArray avatarBytes;
            QBuffer buffer(&avatarBytes);
            buffer.open(QIODevice::WriteOnly);
            squareAvatarPixmap(pixmap, 256).save(&buffer, "PNG");
            m_client->sendAvatarUpdate(avatarBytes);
        }
        QFileInfo info(avatarPath);
        const QString avatarTip = QString("当前头像：本地头像 · %1；点击“换头像”重新选择")
                                      .arg(LocalFileManager::humanFileSize(info.size()));
        ui->avatarLabel->setToolTip(avatarTip);
        ui->uploadAvatarBtn->setToolTip(avatarTip);
    }
    refreshAvatarWorkspaceCard();
}

QString MainWindow::contactDisplayName(const QString& userId) const {
    return m_friendManager.contactDisplayName(userId, m_knownUsers, m_friendNames);
}

bool MainWindow::isContactOnline(const QString& userId) const {
    return m_friendManager.isContactOnline(userId, m_knownUsers);
}

bool MainWindow::isCurrentUserRemovedFromPublicGroup() const {
    return m_hasServerGroupSnapshot
        && !m_currentUserId.isEmpty()
        && m_removedServerGroups.contains("public")
        && !m_serverGroupMembers.value("public").contains(m_currentUserId);
}

QString MainWindow::groupOwnerId(const QString& groupId) const {
    return m_groupManager.localGroupOwnerId(m_localGroupMembers.value(groupId), m_currentUserId);
}

bool MainWindow::isCurrentUserGroupOwner(const QString& groupId) const {
    return m_groupManager.isLocalGroupOwner(groupId, m_localGroupMembers.value(groupId), m_currentUserId);
}

bool MainWindow::canCurrentUserManageServerGroup(const QString& groupId) const {
    return m_groupManager.canManageServerGroup(groupId, m_currentUserId, m_serverGroupOwners, m_serverGroupMemberRoles);
}

bool MainWindow::requestServerGroupMemberUpdate(const QString& memberId, const QString& action) {
    const QStringList members = m_serverGroupMembers.value("public");
    const ServerGroupMemberUpdateDecision decision = m_groupManager.serverGroupMemberUpdateDecision(
        memberId,
        action,
        m_client && m_client->isConnected(),
        QStringLiteral("public"),
        m_currentUserId,
        members,
        m_serverGroupOwners,
        m_serverGroupMemberRoles);
    if (!decision.allowed) {
        ui->statusbar->showMessage(decision.statusMessage, decision.statusTimeoutMs);
        if (!decision.auditMessage.isEmpty()) {
            appendSystemMessage(decision.auditMessage);
        }
        return false;
    }
    if (!m_client->sendServerGroupMemberUpdate("public", decision.targetId, decision.normalizedAction)) {
        ui->statusbar->showMessage("公共群成员变更提交失败", 2600);
        return false;
    }

    const QString displayName = m_serverGroupMemberNames.value("public|" + decision.targetId, contactDisplayName(decision.targetId));
    const QString actionText = decision.normalizedAction == "add"
        ? QStringLiteral("邀请")
        : (decision.normalizedAction == "remove"
            ? QStringLiteral("移出")
            : (decision.normalizedAction == "promote_admin" ? QStringLiteral("设置管理员") : QStringLiteral("取消管理员")));
    appendSystemMessage(QString("已提交公共群%1成员请求：%2（QQ:%3），等待服务端同步").arg(actionText, displayName, decision.targetId));
    ui->statusbar->showMessage(QString("公共群%1请求已提交，等待服务端同步").arg(actionText), 2400);
    return true;
}

QString MainWindow::e2eSessionStatusText(const QString& peerId) const {
    if (!m_client) {
        return QStringLiteral("端到端加密状态：客户端未就绪");
    }
    const QJsonObject status = m_client->e2eSessionStatus(peerId);
    const QJsonObject identity = m_client->e2ePeerIdentityStatus(peerId);
    const QString identityLine = identity.value("configured").toBool(false)
        ? QString("\n身份信任：%1\n跨设备验证：%2\n验证短码：%3\n身份指纹：%4\n信任持久化：%5")
            .arg(identity.value("trustState").toString(),
                 identity.value("verified").toBool(false) ? QStringLiteral("verified") : QStringLiteral("unverified"),
                 identity.value("verificationCodeDisplay").toString(),
                 identity.value("publicKeyFingerprintSha256").toString().left(16),
                 identity.value("pinPersisted").toBool(false) ? QStringLiteral("yes") : QStringLiteral("no"))
        : QStringLiteral("\n身份信任：unknown\n身份指纹：未收到");
    const QString state = status.value("state").toString();
    if (state == QLatin1String("ready")) {
        return QString("端到端加密状态：已就绪\n对端QQ：%1%7\nkeyId：%2\n指纹：%3\n已加密发送：%4\n已解密接收：%5\n轮换阈值：%6")
            .arg(peerId,
                 status.value("keyId").toString(),
                 status.value("keyFingerprintSha256").toString().left(16),
                 status.value("encryptedMessages").toString(),
                 status.value("decryptedMessages").toString(),
                 QString::number(status.value("messageLimit").toInt()),
                 identityLine);
    }
    if (state == QLatin1String("rotation-required")) {
        return QString("端到端加密状态：需要轮换\n对端QQ：%1%6\nkeyId：%2\n指纹：%3\n已加密发送：%4\n轮换阈值：%5")
            .arg(peerId,
                 status.value("keyId").toString(),
                 status.value("keyFingerprintSha256").toString().left(16),
                 status.value("encryptedMessages").toString(),
                 QString::number(status.value("messageLimit").toInt()),
                 identityLine);
    }
    return QString("端到端加密状态：未就绪\n对端QQ：%1%2\n说明：当前本机没有可用于该联系人的会话密钥").arg(peerId, identityLine);
}

void MainWindow::copyE2ESessionStatus(const QString& peerId) {
    const QString text = e2eSessionStatusText(peerId);
    QApplication::clipboard()->setText(text);
    ui->statusbar->showMessage("端到端加密状态已复制", 2200);
}

QString MainWindow::getFriendFilePath() const {
    return m_clientStorage.friendFilePath();
}

QString MainWindow::getGroupFilePath() const {
    return m_clientStorage.groupFilePath();
}

QString MainWindow::getAvatarFilePath() const {
    return m_clientStorage.avatarFilePath();
}

void MainWindow::saveFriends() const {
    if (ensureClientDatabase()) {
        m_clientStorage.saveFriendsToSqlite(clientDbPath(),
                                            m_friendIds,
                                            m_friendNames,
                                            m_pendingFriendRequests,
                                            m_pendingOutgoingFriendRequests);
    }
    m_clientStorage.writeLegacyFriends(m_friendIds, m_friendNames);
}

void MainWindow::saveLocalGroups() const {
    if (ensureClientDatabase()) {
        m_clientStorage.saveLocalGroupsToSqlite(clientDbPath(),
                                                m_currentUserId,
                                                m_localGroupIds,
                                                m_localGroupNames,
                                                m_localGroupMembers,
                                                m_localGroupAnnouncements);
    }
    m_clientStorage.writeLegacyLocalGroups(m_currentUserId,
                                           m_localGroupIds,
                                           m_localGroupNames,
                                           m_localGroupMembers,
                                           m_localGroupAnnouncements);
}

void MainWindow::updateUnreadState() {
    const WindowChromeState state = WindowStateManager::unreadState(
        QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING),
        m_unreadCount);
    setWindowTitle(state.windowTitle);
    m_trayIcon->setToolTip(state.trayToolTip);
}

void MainWindow::clearUnreadState() {
    m_unreadCount = 0;
    const WindowChromeState state = WindowStateManager::clearedState(
        QString::fromLatin1(QTNETWORKCHAT_VERSION_STRING),
        !m_privateChatTarget.isEmpty(),
        m_currentUserName,
        ui->chatTitleLabel->text());
    setWindowTitle(state.windowTitle);
    m_trayIcon->setToolTip(state.trayToolTip);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
        clearUnreadState();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!m_isQuitting && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    m_client->disconnectFromServer();
    event->accept();
}

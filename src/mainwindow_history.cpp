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

using namespace MainWindowSupport;

void MainWindow::onViewHistory() {
    const QString peerId = m_privateChatTarget.isEmpty() ? QStringLiteral("group") : m_privateChatTarget;
    const QString sessionName = m_privateChatTarget.isEmpty()
        ? QStringLiteral("公共聊天室")
        : (m_privateChatTarget.startsWith(QStringLiteral("local_group_"))
            ? m_localGroupNames.value(m_privateChatTarget, QStringLiteral("群聊"))
            : contactDisplayName(m_privateChatTarget));
    const QStringList rows = m_historyService.rowsForExport(peerId);
    QDialog panel(this);
    panel.setObjectName(QStringLiteral("historyWindow"));
    panel.setWindowTitle(sessionName + QStringLiteral(" - 聊天记录"));
    panel.setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    panel.resize(760, 680);
    panel.setMinimumSize(620, 520);
    auto* root = new QVBoxLayout(&panel);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto* header = new QFrame(&panel);
    header->setObjectName(QStringLiteral("historyWindowHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(4, 0, 4, 6);
    headerLayout->setSpacing(3);
    auto* title = new QLabel(sessionName, header);
    title->setObjectName(QStringLiteral("historyWindowTitle"));
    auto* meta = new QLabel(QStringLiteral("聊天记录 · 共 %1 条").arg(QString::number(rows.size())), header);
    meta->setObjectName(QStringLiteral("historyWindowMeta"));
    headerLayout->addWidget(title);
    headerLayout->addWidget(meta);
    root->addWidget(header);

    auto* search = new QLineEdit(&panel);
    search->setObjectName(QStringLiteral("historyWindowSearch"));
    search->setPlaceholderText(QStringLiteral("搜索"));
    search->setClearButtonEnabled(true);
    search->setFixedHeight(34);
    root->addWidget(search);

    auto* filters = new QFrame(&panel);
    auto* filtersLayout = new QHBoxLayout(filters);
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(22);
    auto* filterGroup = new QButtonGroup(filters);
    filterGroup->setExclusive(true);
    const QList<QPair<QString, QString>> filterChoices = {
        {QStringLiteral("全部"), QStringLiteral("all")},
        {QStringLiteral("图片/视频"), QStringLiteral("media")},
        {QStringLiteral("表情"), QStringLiteral("emoji")},
        {QStringLiteral("文件"), QStringLiteral("file")},
        {QStringLiteral("链接"), QStringLiteral("link")}
    };
    for (int index = 0; index < filterChoices.size(); ++index) {
        auto* button = new QPushButton(filterChoices.at(index).first, filters);
        button->setObjectName(QStringLiteral("historyWindowFilter"));
        button->setProperty("mode", filterChoices.at(index).second);
        button->setCheckable(true);
        button->setChecked(index == 0);
        button->setFlat(true);
        filterGroup->addButton(button, index);
        filtersLayout->addWidget(button);
    }
    filtersLayout->addStretch();
    auto* filterHint = new QPushButton(QStringLiteral("筛选"), filters);
    filterHint->setObjectName(QStringLiteral("historyWindowFilterHint"));
    filterHint->setFlat(true);
    filtersLayout->addWidget(filterHint);
    root->addWidget(filters);

    auto* scroll = new QScrollArea(&panel);
    scroll->setObjectName(QStringLiteral("historyWindowScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll, 1);

    auto rebuildList = [this, &scroll, &rows, search, filterGroup]() {
        QWidget* oldList = scroll->takeWidget();
        if (oldList) oldList->deleteLater();
        auto* list = new QWidget(scroll);
        list->setObjectName(QStringLiteral("historyWindowList"));
        auto* listLayout = new QVBoxLayout(list);
        listLayout->setContentsMargins(4, 0, 4, 0);
        listLayout->setSpacing(0);
        const QString keyword = search->text().trimmed();
        const QString mode = filterGroup->checkedButton()
            ? filterGroup->checkedButton()->property("mode").toString() : QStringLiteral("all");
        QString currentDate;
        int visibleCount = 0;
        for (const QString& row : rows) {
            const StoredChatMessage stored = parseStoredChatMessage(row);
            const QString bodyLower = stored.body.toLower();
            const bool media = stored.body.contains(QStringLiteral("图片")) || stored.body.contains(QStringLiteral("视频"));
            const bool file = stored.body.contains(QStringLiteral("文件"));
            const bool link = bodyLower.contains(QStringLiteral("http://")) || bodyLower.contains(QStringLiteral("https://"));
            const bool emoji = stored.body.contains(QRegularExpression(QStringLiteral("[\\x{1F300}-\\x{1FAFF}]")));
            const bool modeMatches = mode == QLatin1String("all")
                || (mode == QLatin1String("media") && media)
                || (mode == QLatin1String("file") && file)
                || (mode == QLatin1String("link") && link)
                || (mode == QLatin1String("emoji") && emoji);
            const bool keywordMatches = keyword.isEmpty()
                || stored.senderName.contains(keyword, Qt::CaseInsensitive)
                || stored.body.contains(keyword, Qt::CaseInsensitive);
            if (!modeMatches || !keywordMatches) continue;

            const QRegularExpressionMatch dateMatch = QRegularExpression(QStringLiteral(R"(^(\d{4}-\d{2}-\d{2}))")).match(row);
            const QString date = dateMatch.hasMatch() ? dateMatch.captured(1).replace(QLatin1Char('-'), QLatin1Char('/'))
                                                       : QStringLiteral("更早记录");
            if (date != currentDate) {
                currentDate = date;
                auto* dateLabel = new QLabel(date, list);
                dateLabel->setObjectName(QStringLiteral("historyWindowDate"));
                listLayout->addWidget(dateLabel);
            }

            const QString senderName = stored.system ? QStringLiteral("系统消息")
                : (stored.senderName.isEmpty() ? QStringLiteral("未知成员") : stored.senderName);
            QString senderId;
            if (!stored.system) {
                if (senderName == m_currentUserName) senderId = m_currentUserId;
                else {
                    for (auto user = m_knownUsers.cbegin(); user != m_knownUsers.cend(); ++user) {
                        if (user.value().name == senderName) { senderId = user.key(); break; }
                    }
                    if (senderId.isEmpty()) {
                        for (auto member = m_serverGroupMemberNames.cbegin(); member != m_serverGroupMemberNames.cend(); ++member) {
                            if (member.value() == senderName) { senderId = member.key().section(QLatin1Char('|'), 1); break; }
                        }
                    }
                }
            }
            auto* card = new QFrame(list);
            card->setObjectName(QStringLiteral("historyWindowRow"));
            auto* rowLayout = new QHBoxLayout(card);
            rowLayout->setContentsMargins(0, 12, 0, 12);
            rowLayout->setSpacing(10);
            auto* avatar = new AvatarLabel(card, 36);
            const QString avatarPath = senderId.isEmpty() ? QString() : chatAvatarPath(senderId);
            QPixmap avatarPixmap;
            if (!avatarPath.isEmpty()) avatarPixmap.load(avatarPath);
            if (avatarPixmap.isNull() && senderId == m_currentUserId) avatarPixmap.load(getAvatarFilePath());
            if (!avatarPixmap.isNull()) avatar->setPixmap(avatarPixmap);
            else avatar->setTextAvatar(senderName, ThemeManager::instance()->primaryColor());
            rowLayout->addWidget(avatar, 0, Qt::AlignTop);
            auto* content = new QWidget(card);
            auto* contentLayout = new QVBoxLayout(content);
            contentLayout->setContentsMargins(0, 0, 0, 0);
            contentLayout->setSpacing(4);
            auto* cardHeader = new QHBoxLayout();
            cardHeader->setContentsMargins(0, 0, 0, 0);
            auto* sender = new QLabel(senderName, content);
            sender->setObjectName(QStringLiteral("historyWindowSender"));
            auto* timestamp = new QLabel(bubbleTimestamp(stored.timestamp), content);
            timestamp->setObjectName(QStringLiteral("historyWindowTime"));
            cardHeader->addWidget(sender, 1);
            cardHeader->addWidget(timestamp);
            auto* body = new QLabel(stored.body, content);
            body->setObjectName(QStringLiteral("historyWindowBody"));
            body->setWordWrap(true);
            body->setMaximumHeight(96);
            contentLayout->addLayout(cardHeader);
            contentLayout->addWidget(body);
            rowLayout->addWidget(content, 1);
            listLayout->addWidget(card);
            ++visibleCount;
        }
        if (visibleCount == 0) {
            auto* empty = new QLabel(QStringLiteral("暂无聊天记录"), list);
            empty->setObjectName(QStringLiteral("historyWindowEmpty"));
            empty->setAlignment(Qt::AlignCenter);
            listLayout->addWidget(empty, 1);
        }
        listLayout->addStretch();
        scroll->setWidget(list);
    };
    rebuildList();
    connect(search, &QLineEdit::textChanged, &panel, [rebuildList]() { rebuildList(); });
    connect(filterGroup, qOverload<QAbstractButton*>(&QButtonGroup::buttonClicked), &panel,
            [rebuildList](QAbstractButton*) { rebuildList(); });
    auto* footer = new QFrame(&panel);
    footer->setObjectName(QStringLiteral("historyWindowFooter"));
    footer->setFixedHeight(56);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 10, 12, 10);
    auto* clear = new QPushButton(QStringLiteral("清空聊天记录"), footer);
    clear->setObjectName(QStringLiteral("historyWindowClear"));
    clear->setFixedHeight(36);
    clear->setEnabled(!rows.isEmpty());
    footerLayout->addWidget(clear);
    root->addWidget(footer);
    connect(clear, &QPushButton::clicked, &panel, [&panel, this]() {
        panel.accept();
        onClearHistory();
    });
    panel.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#historyWindow { background:%1; }"
        "QFrame#historyWindowFooter { border-top:1px solid %2; }"
        "QLineEdit#historyWindowSearch { background:%3; color:%4; border:1px solid %2; border-radius:7px; padding:0 12px; font-size:13px; }"
        "QLineEdit#historyWindowSearch:focus { border-color:%6; }"
        "QPushButton#historyWindowFilter { color:%4; border:none; background:transparent; padding:6px 2px; border-radius:0; font-size:13px; }"
        "QPushButton#historyWindowFilter:checked { color:%6; border-bottom:2px solid %6; font-weight:600; }"
        "QPushButton#historyWindowFilterHint { color:%4; border:none; background:transparent; padding:6px 4px; font-size:12px; }"
        "QScrollArea#historyWindowScroll,QWidget#historyWindowList { background:%1; }"
        "QLabel#historyWindowTitle { color:%4; font-size:15px; font-weight:600; }"
        "QLabel#historyWindowMeta,QLabel#historyWindowTime { color:%5; font-size:12px; }"
        "QLabel#historyWindowDate { color:%4; font-size:13px; padding:12px 0 7px 0; }"
        "QLabel#historyWindowSender { color:%5; font-size:13px; }"
        "QLabel#historyWindowBody { color:%4; font-size:14px; }"
        "QFrame#historyWindowRow { background:transparent; border:none; border-bottom:1px solid %2; }"
        "QLabel#historyWindowEmpty { color:%5; font-size:12px; }"
        "QPushButton#historyWindowClear { min-height:36px; max-height:36px; padding:0 14px; color:%7; background:transparent; border:1px solid %2; border-radius:5px; font-size:13px; }"
        "QPushButton#historyWindowClear:hover { background:%8; border-color:%7; }"
        "QPushButton#historyWindowClear:disabled { color:%5; border-color:%2; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->backgroundSecondaryColor().name(), ThemeManager::instance()->textColor().name(),
             ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->primaryColor().name(),
             ThemeManager::instance()->dangerColor().name(), ThemeManager::instance()->backgroundTertiaryColor().name()));
    panel.exec();
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

    if (QMessageBox::question(this,
                              "清空聊天记录",
                              QString("确定清空“%1”的本地聊天记录吗？此操作不会删除对方设备上的记录。").arg(sessionName),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        ui->statusbar->showMessage("已取消清空聊天记录", 1600);
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
        const StoredChatMessage stored = parseStoredChatMessage(row);
        const bool outgoing = !stored.system && !m_currentUserName.isEmpty()
            && stored.senderName == m_currentUserName;
        QStandardItem* item = new QStandardItem(stored.body);
        item->setEditable(false);
        if (!stored.timestamp.isEmpty()) {
            item->setData(bubbleTimestamp(stored.timestamp), ChatBubbleTimestampRole);
        }
        decorateChatItem(item,
                         outgoing ? m_currentUserId : QString(),
                         stored.senderName,
                         outgoing,
                         stored.system);
        m_chatModel->appendRow(item);
    }
    ui->chatHintLabel->setText(QString("%1 · %2 · 已筛选 %3 条记录")
        .arg(sessionName, selectedDate.toString("yyyy-MM-dd"), QString::number(rows.size())));
    ui->statusbar->showMessage(QString("已筛选 %1 条聊天记录").arg(rows.size()), 2400);
    scrollActiveChatToBottom();
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
    const QString savePath = QFileDialog::getSaveFileName(this,
                                                          exportPlan.dialogTitle,
                                                          exportPlan.defaultPath,
                                                          exportPlan.filters);
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
        QMessageBox::warning(this, exportResult.failureTitle, exportResult.failureMessage);
        ui->statusbar->showMessage(exportResult.failureStatusMessage, exportResult.failureStatusTimeoutMs);
        return;
    }

    ui->statusbar->showMessage(exportResult.successStatusMessage, exportResult.successStatusTimeoutMs);
    appendSystemMessage(exportResult.systemMessage);
}

void MainWindow::loadHistory(const QString& peerId) {
    if (peerId.isEmpty()) return;

    const QStringList rows = m_historyService.recentRows(peerId, MAX_HISTORY_LINES);
    for (const QString& line : rows) {
        const StoredChatMessage stored = parseStoredChatMessage(line);
        const bool outgoing = !stored.system && !m_currentUserName.isEmpty()
            && stored.senderName == m_currentUserName;
        QStandardItem* item = new QStandardItem(stored.body);
        item->setEditable(false);
        if (!stored.timestamp.isEmpty()) {
            item->setData(bubbleTimestamp(stored.timestamp), ChatBubbleTimestampRole);
        }
        decorateChatItem(item,
                         outgoing ? m_currentUserId : QString(),
                         stored.senderName,
                         outgoing,
                         stored.system);
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

void MainWindow::refreshFavoritesView() {
    if (!m_favoritesView) return;
    QStandardItemModel* model = m_favoritesView->model();
    if (!model) return;
    model->clear();

    if (!QQNTBackendService::isCommand(QStringLiteral("get_local_favorite_messages"))) {
        return;
    }
    QJsonObject payload;
    QJsonObject response;
    QString errorCode;
    QString errorMessage;
    if (!QQNTBackendService::handle(QStringLiteral("get_local_favorite_messages"),
                                    payload, &response, &errorCode, &errorMessage)) {
        qDebug() << "get_local_favorite_messages failed:" << errorCode << errorMessage;
        m_favoritesView->setEmptyStateVisible(true);
        return;
    }

    const QJsonArray messages = response.value(QStringLiteral("messages")).toArray();
    QSet<QString> addedSessionHeaders;
    for (const QJsonValue& value : messages) {
        const QJsonObject msg = value.toObject();
        const QString sessionId = msg.value(QStringLiteral("sessionId")).toString();
        const QString messageId = msg.value(QStringLiteral("messageId")).toString(msg.value(QStringLiteral("id")).toString());
        QString senderName = msg.value(QStringLiteral("senderName")).toString().trimmed();
        QString content = msg.value(QStringLiteral("content")).toString().trimmed();
        const QString timestampRaw = msg.value(QStringLiteral("timestamp")).toString();

        // Older favorite snapshots preserve the pre-QQNT archive line in
        // content. Reuse the chat-history parser so FavoritesList receives the
        // same sender/body separation as tauri-qqnt.
        const StoredChatMessage stored = parseStoredChatMessage(content);
        if (!stored.senderName.isEmpty() || stored.system) {
            if (senderName.isEmpty()) {
                senderName = stored.senderName;
            }
            content = stored.body;
        }

        const QString sessionName = sessionId == QStringLiteral("public")
            ? QStringLiteral("公共聊天室")
            : (m_localGroupIds.contains(sessionId)
                   ? m_localGroupNames.value(sessionId, QStringLiteral("群聊"))
                   : contactDisplayName(sessionId));

        if (!addedSessionHeaders.contains(sessionId)) {
            QStandardItem* header = new QStandardItem(sessionName.isEmpty() ? sessionId : sessionName);
            header->setEditable(false);
            header->setData(true, Qt::UserRole + 6);
            header->setData(sessionName.isEmpty() ? sessionId : sessionName, Qt::UserRole + 5);
            header->setData(QStringLiteral("%1 %2").arg(sessionName, content), Qt::UserRole + 8);
            model->appendRow(header);
            addedSessionHeaders.insert(sessionId);
        }

        const QString type = msg.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("image")) {
            content = QStringLiteral("[图片]");
        } else if (type == QStringLiteral("file")) {
            const QString fileName = msg.value(QStringLiteral("fileInfo")).toObject().value(QStringLiteral("name")).toString();
            content = QStringLiteral("[文件] %1").arg(fileName.isEmpty() ? content : fileName);
        }
        QDateTime timestamp = QDateTime::fromString(timestampRaw, Qt::ISODate);
        if (!timestamp.isValid()) timestamp = QDateTime::fromString(timestampRaw, Qt::ISODateWithMs);
        QString displayTime = timestamp.isValid()
            ? timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"))
            : timestampRaw;
        if (displayTime.isEmpty() && !stored.timestamp.isEmpty()) {
            displayTime = stored.timestamp;
        }

        QStandardItem* item = new QStandardItem();
        item->setEditable(false);
        item->setData(sessionId, Qt::UserRole);
        item->setData(messageId, Qt::UserRole + 1);
        item->setData(senderName.isEmpty() ? QStringLiteral("未知用户") : senderName, Qt::UserRole + 2);
        item->setData(content, Qt::UserRole + 3);
        item->setData(displayTime, Qt::UserRole + 4);
        item->setData(sessionName, Qt::UserRole + 5);
        item->setData(false, Qt::UserRole + 6);
        item->setData(msg, Qt::UserRole + 7);
        item->setData(QStringLiteral("%1 %2 %3 %4").arg(sessionName, senderName, content, displayTime), Qt::UserRole + 8);
        item->setToolTip(QStringLiteral("%1\n%2").arg(content, displayTime));
        model->appendRow(item);
    }
    m_favoritesView->setEmptyStateVisible(messages.isEmpty());
}

void MainWindow::onFavoriteSelected(const QString& sessionId, const QString& messageId) {
    // Switch to the originating session, then jump to the message page so the user
    // lands on the conversation that holds the favorited message.
    if (sessionId == QStringLiteral("public") || sessionId.isEmpty()) {
        onBackToGroupChat();
    } else if (m_localGroupIds.contains(sessionId)) {
        switchToLocalGroup(sessionId, m_localGroupNames.value(sessionId, QStringLiteral("群聊")));
    } else {
        openPrivateSession(sessionId);
    }

    if (m_viewStack) {
        m_viewStack->setCurrentIndex(0);
    }
    if (m_appNav) {
        m_appNav->setCurrentIndex(0);
    }

    // Best-effort locate: highlight the matching row if it is present in the loaded history.
    if (!messageId.isEmpty() && m_chatModel && m_messagesView) {
        for (int row = 0; row < m_chatModel->rowCount(); ++row) {
            QStandardItem* item = m_chatModel->item(row);
            if (item && item->data(ChatBubbleMessageIdRole).toString() == messageId) {
                const QModelIndex idx = m_chatModel->index(row, 0);
                if (QListView* view = m_messagesView->chatListView()) {
                    view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                    view->setCurrentIndex(idx);
                }
                break;
            }
        }
    }
    ui->statusbar->showMessage(QStringLiteral("已定位收藏消息所在会话"), 1800);
}

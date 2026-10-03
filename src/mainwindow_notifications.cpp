#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "notificationpanelmanager.h"
#include "theme/thememanager.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QStatusBar>
#include <QVBoxLayout>

namespace {
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
    if (noticeList->count() > 0) {
        noticeList->setCurrentRow(0);
    }
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

void MainWindow::onShowFriendNotifications() {
    const FriendNoticeDialogChrome chrome = NotificationPanelManager::friendNoticeDialogChrome();
    QDialog dialog(this);
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle(chrome.windowTitle);
    dialog.setFixedSize(chrome.dialogSize);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    QHBoxLayout* titleLayout = new QHBoxLayout;
    QLabel* titleLabel = new QLabel(chrome.titleText, &dialog);
    titleLabel->setObjectName("noticeTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    QPushButton* clearBtn = new QPushButton(chrome.clearButton.text, &dialog);
    clearBtn->setObjectName(chrome.clearButton.objectName);
    clearBtn->setToolTip(chrome.clearButton.toolTip);
    titleLayout->addWidget(clearBtn);
    layout->addLayout(titleLayout);

    QLabel* statsLabel = new QLabel(&dialog);
    statsLabel->setObjectName("noticeSubTitle");
    layout->addWidget(statsLabel);

    QLineEdit* searchEdit = new QLineEdit(&dialog);
    searchEdit->setObjectName("noticeSearch");
    searchEdit->setPlaceholderText(chrome.searchPlaceholder);
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setToolTip(chrome.searchToolTip);
    layout->addWidget(searchEdit);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    noticeList->setWordWrap(true);
    layout->addWidget(noticeList, 1);

    auto fillList = [this, noticeList, statsLabel, searchEdit]() {
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
        for (int i = 0; i < noticeList->count(); ++i) {
            if (noticeList->item(i)->flags().testFlag(Qt::ItemIsEnabled)) {
                noticeList->setCurrentRow(i);
                break;
            }
        }
    };
    fillList();

    QLabel* requestPreviewLabel = new QLabel(chrome.previewPlaceholder, &dialog);
    requestPreviewLabel->setObjectName("noticePreviewLabel");
    layout->addWidget(requestPreviewLabel);

    QVBoxLayout* buttonLayout = new QVBoxLayout;
    buttonLayout->setSpacing(8);
    QHBoxLayout* decisionButtonLayout = new QHBoxLayout;
    decisionButtonLayout->setSpacing(8);
    QHBoxLayout* copyButtonLayout = new QHBoxLayout;
    copyButtonLayout->setSpacing(8);
    QHBoxLayout* mediaButtonLayout = new QHBoxLayout;
    mediaButtonLayout->setSpacing(8);
    QPushButton* acceptBtn = new QPushButton(chrome.acceptButton.text, &dialog);
    acceptBtn->setObjectName(chrome.acceptButton.objectName);
    acceptBtn->setToolTip(chrome.acceptButton.toolTip);
    QPushButton* acceptAllBtn = new QPushButton(chrome.acceptAllButton.text, &dialog);
    acceptAllBtn->setObjectName(chrome.acceptAllButton.objectName);
    acceptAllBtn->setToolTip(chrome.acceptAllButton.toolTip);
    QPushButton* rejectBtn = new QPushButton(chrome.rejectButton.text, &dialog);
    rejectBtn->setObjectName(chrome.rejectButton.objectName);
    rejectBtn->setToolTip(chrome.rejectButton.toolTip);
    QPushButton* rejectAllBtn = new QPushButton(chrome.rejectAllButton.text, &dialog);
    rejectAllBtn->setObjectName(chrome.rejectAllButton.objectName);
    rejectAllBtn->setToolTip(chrome.rejectAllButton.toolTip);
    QPushButton* copyBtn = new QPushButton(chrome.copyCardButton.text, &dialog);
    copyBtn->setObjectName(chrome.copyCardButton.objectName);
    copyBtn->setToolTip(chrome.copyCardButton.toolTip);
    QPushButton* copyInviteBtn = new QPushButton(chrome.copyInviteButton.text, &dialog);
    copyInviteBtn->setObjectName(chrome.copyInviteButton.objectName);
    copyInviteBtn->setToolTip(chrome.copyInviteButton.toolTip);
    QPushButton* copyAllBtn = new QPushButton(chrome.copyAllButton.text, &dialog);
    copyAllBtn->setObjectName(chrome.copyAllButton.objectName);
    copyAllBtn->setToolTip(chrome.copyAllButton.toolTip);
    QPushButton* copyRequestMediaPackBtn = new QPushButton(chrome.copyMediaPackButton.text, &dialog);
    copyRequestMediaPackBtn->setObjectName(chrome.copyMediaPackButton.objectName);
    copyRequestMediaPackBtn->setToolTip(chrome.copyMediaPackButton.toolTip);
    QPushButton* copyRequestBatchPlanBtn = new QPushButton(chrome.copyBatchPlanButton.text, &dialog);
    copyRequestBatchPlanBtn->setObjectName(chrome.copyBatchPlanButton.objectName);
    copyRequestBatchPlanBtn->setToolTip(chrome.copyBatchPlanButton.toolTip);
    QPushButton* copyMediaGuideBtn = new QPushButton(chrome.copyMediaGuideButton.text, &dialog);
    copyMediaGuideBtn->setObjectName(chrome.copyMediaGuideButton.objectName);
    copyMediaGuideBtn->setToolTip(chrome.copyMediaGuideButton.toolTip);
    QPushButton* closeBtn = new QPushButton(chrome.closeButton.text, &dialog);
    closeBtn->setObjectName(chrome.closeButton.objectName);
    closeBtn->setToolTip(chrome.closeButton.toolTip);
    decisionButtonLayout->addWidget(acceptBtn);
    decisionButtonLayout->addWidget(acceptAllBtn);
    decisionButtonLayout->addWidget(rejectBtn);
    decisionButtonLayout->addWidget(rejectAllBtn);
    decisionButtonLayout->addStretch();
    decisionButtonLayout->addWidget(closeBtn);
    copyButtonLayout->addWidget(copyBtn);
    copyButtonLayout->addWidget(copyInviteBtn);
    copyButtonLayout->addWidget(copyAllBtn);
    copyButtonLayout->addStretch();
    mediaButtonLayout->addWidget(copyRequestMediaPackBtn);
    mediaButtonLayout->addWidget(copyRequestBatchPlanBtn);
    mediaButtonLayout->addWidget(copyMediaGuideBtn);
    mediaButtonLayout->addStretch();
    buttonLayout->addLayout(decisionButtonLayout);
    buttonLayout->addLayout(copyButtonLayout);
    buttonLayout->addLayout(mediaButtonLayout);
    layout->addLayout(buttonLayout);

    dialog.setStyleSheet(NotificationPanelManager::friendNoticeDialogStyleSheet());

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
    };
    auto updateRequestPreview = [this, noticeList, searchEdit, requestPreviewLabel]() {
        const FriendNoticeSelectionSnapshot snapshot = currentFriendNoticeSelectionSnapshot(noticeList, searchEdit);
        requestPreviewLabel->setText(snapshot.previewText);
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
        if (QMessageBox::question(&dialog,
                                  actionState.title,
                                  actionState.questionText,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage(actionState.cancelledStatusMessage, 1600);
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
                                           "这是搜索占位项，可先搜索并发送申请",
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
        if (QMessageBox::question(&dialog,
                                  actionState.title,
                                  actionState.questionText,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage(actionState.cancelledStatusMessage, 1600);
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
                                           "这是搜索占位项，请先搜索申请人",
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
        if (QMessageBox::question(&dialog,
                                  actionState.title,
                                  actionState.questionText,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            ui->statusbar->showMessage(actionState.cancelledStatusMessage, 1600);
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
    dialog.setObjectName("noticeDialog");
    dialog.setWindowTitle(chrome.windowTitle);
    dialog.setFixedSize(chrome.dialogSize);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    QHBoxLayout* titleLayout = new QHBoxLayout;
    QLabel* titleLabel = new QLabel(chrome.titleText, &dialog);
    titleLabel->setObjectName("noticeTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    QLabel* countLabel = new QLabel(chrome.countTextTemplate.arg(m_localGroupIds.size() + 1), &dialog);
    countLabel->setObjectName("noticeSubTitle");
    titleLayout->addWidget(countLabel);
    layout->addLayout(titleLayout);

    QLineEdit* searchEdit = new QLineEdit(&dialog);
    searchEdit->setObjectName("noticeSearch");
    searchEdit->setPlaceholderText(chrome.searchPlaceholder);
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setToolTip(chrome.searchToolTip);
    layout->addWidget(searchEdit);

    QListWidget* noticeList = new QListWidget(&dialog);
    noticeList->setObjectName("noticeList");
    noticeList->setWordWrap(true);

    auto fillGroups = [this, noticeList, countLabel, searchEdit]() {
        fillGroupNoticeList(noticeList, countLabel, searchEdit);
        const QString keyword = searchEdit->text().trimmed();
        for (auto it = m_pendingGroupJoinApplications.constBegin(); it != m_pendingGroupJoinApplications.constEnd(); ++it) {
            const QJsonObject application = it.value();
            const QString groupName = application.value("groupName").toString(QStringLiteral("群聊"));
            const QString applicantName = application.value("applicantName").toString(application.value("applicantId").toString());
            const QString message = application.value("message").toString();
            const QString searchable = groupName + applicantName + application.value("applicantId").toString() + message;
            if (!keyword.isEmpty() && !searchable.contains(keyword, Qt::CaseInsensitive)) continue;
            auto* item = new QListWidgetItem(
                QStringLiteral("入群申请 · %1\n%2 申请加入 · %3%4")
                    .arg(groupName, applicantName, application.value("applicantId").toString(),
                         message.isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(message)), noticeList);
            item->setData(Qt::UserRole, QStringLiteral("join_request:") + it.key());
            item->setSizeHint(QSize(0, 70));
            item->setForeground(QColor(18, 150, 247));
            item->setToolTip(QStringLiteral("选择后可同意或拒绝该入群申请"));
            noticeList->insertItem(0, item);
        }
        for (auto it = m_pendingOutgoingGroupJoinApplications.constBegin(); it != m_pendingOutgoingGroupJoinApplications.constEnd(); ++it) {
            const QJsonObject application = it.value();
            const QString groupName = application.value("groupName").toString(QStringLiteral("群聊"));
            const QString message = application.value("message").toString();
            const QString searchable = groupName + application.value("groupId").toString() + message;
            if (!keyword.isEmpty() && !searchable.contains(keyword, Qt::CaseInsensitive)) continue;
            auto* item = new QListWidgetItem(
                QStringLiteral("入群申请中 · %1\n群号：%2 · 等待群主或管理员同意%3")
                    .arg(groupName, application.value("groupId").toString(),
                         message.isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(message)), noticeList);
            item->setData(Qt::UserRole, QStringLiteral("join_pending:") + it.key());
            item->setSizeHint(QSize(0, 64));
            item->setToolTip(QStringLiteral("入群申请已提交，等待群主或管理员同意"));
            noticeList->insertItem(0, item);
        }
        for (auto it = m_rejectedOutgoingGroupJoinApplications.constBegin(); it != m_rejectedOutgoingGroupJoinApplications.constEnd(); ++it) {
            const QJsonObject application = it.value();
            const QString groupName = application.value("groupName").toString(QStringLiteral("群聊"));
            const QString reviewer = application.value("reviewerName").toString(QStringLiteral("群主或管理员"));
            const QString searchable = groupName + application.value("groupId").toString() + reviewer;
            if (!keyword.isEmpty() && !searchable.contains(keyword, Qt::CaseInsensitive)) continue;
            auto* item = new QListWidgetItem(
                QStringLiteral("入群申请被拒绝 · %1\n%2 拒绝了你的加群申请")
                    .arg(groupName, reviewer), noticeList);
            item->setData(Qt::UserRole, QStringLiteral("join_rejected:") + it.key());
            item->setSizeHint(QSize(0, 56));
            item->setForeground(ThemeManager::instance()->dangerColor());
            item->setToolTip(QStringLiteral("入群申请已被群主或管理员拒绝"));
            noticeList->insertItem(0, item);
        }
        countLabel->setText(QStringLiteral("%1 个群聊 · %2 条待处理 · %3 条申请中")
                            .arg(m_localGroupIds.size() + 1)
                            .arg(m_pendingGroupJoinApplications.size())
                            .arg(m_pendingOutgoingGroupJoinApplications.size()));
        if (noticeList->count() > 0) noticeList->setCurrentRow(0);
    };
    fillGroups();

    QLabel* groupPreviewLabel = new QLabel(chrome.previewPlaceholder, &dialog);
    groupPreviewLabel->setObjectName("noticePreviewLabel");
    layout->addWidget(groupPreviewLabel);
    layout->addWidget(noticeList, 1);

    QVBoxLayout* actionLayout = new QVBoxLayout;
    actionLayout->setSpacing(8);
    QHBoxLayout* hintLayout = new QHBoxLayout;
    QHBoxLayout* groupMainActionLayout = new QHBoxLayout;
    groupMainActionLayout->setSpacing(8);
    QHBoxLayout* groupMemberActionLayout = new QHBoxLayout;
    groupMemberActionLayout->setSpacing(8);
    QHBoxLayout* groupMediaActionLayout = new QHBoxLayout;
    groupMediaActionLayout->setSpacing(8);
    QLabel* hintLabel = new QLabel(chrome.hintPlaceholder, &dialog);
    hintLabel->setObjectName("noticeHint");
    hintLayout->addWidget(hintLabel);
    hintLayout->addStretch();
    QPushButton* openBtn = new QPushButton(chrome.openButton.text, &dialog);
    openBtn->setObjectName(chrome.openButton.objectName);
    openBtn->setToolTip(chrome.openButton.toolTip);
    QPushButton* copyBtn = new QPushButton(chrome.copyIdButton.text, &dialog);
    copyBtn->setObjectName(chrome.copyIdButton.objectName);
    copyBtn->setToolTip(chrome.copyIdButton.toolTip);
    QPushButton* cardBtn = new QPushButton(chrome.copyCardButton.text, &dialog);
    cardBtn->setObjectName(chrome.copyCardButton.objectName);
    cardBtn->setToolTip(chrome.copyCardButton.toolTip);
    QPushButton* announceBtn = new QPushButton(chrome.copyAnnouncementButton.text, &dialog);
    announceBtn->setObjectName(chrome.copyAnnouncementButton.objectName);
    announceBtn->setToolTip(chrome.copyAnnouncementButton.toolTip);
    QPushButton* inviteTextBtn = new QPushButton(chrome.copyInviteButton.text, &dialog);
    inviteTextBtn->setObjectName(chrome.copyInviteButton.objectName);
    inviteTextBtn->setToolTip(chrome.copyInviteButton.toolTip);
    QPushButton* memberBtn = new QPushButton(chrome.copyMembersButton.text, &dialog);
    memberBtn->setObjectName(chrome.copyMembersButton.objectName);
    memberBtn->setToolTip(chrome.copyMembersButton.toolTip);
    QPushButton* onlineMemberBtn = new QPushButton(chrome.copyOnlineMembersButton.text, &dialog);
    onlineMemberBtn->setObjectName(chrome.copyOnlineMembersButton.objectName);
    onlineMemberBtn->setToolTip(chrome.copyOnlineMembersButton.toolTip);
    QPushButton* copyGroupMediaPackBtn = new QPushButton(chrome.copyMediaPackButton.text, &dialog);
    copyGroupMediaPackBtn->setObjectName(chrome.copyMediaPackButton.objectName);
    copyGroupMediaPackBtn->setToolTip(chrome.copyMediaPackButton.toolTip);
    QPushButton* copyGroupBatchPlanBtn = new QPushButton(chrome.copyBatchPlanButton.text, &dialog);
    copyGroupBatchPlanBtn->setObjectName(chrome.copyBatchPlanButton.objectName);
    copyGroupBatchPlanBtn->setToolTip(chrome.copyBatchPlanButton.toolTip);
    QPushButton* copyMediaGuideBtn = new QPushButton(chrome.copyMediaGuideButton.text, &dialog);
    copyMediaGuideBtn->setObjectName(chrome.copyMediaGuideButton.objectName);
    copyMediaGuideBtn->setToolTip(chrome.copyMediaGuideButton.toolTip);
    QPushButton* closeBtn = new QPushButton(chrome.closeButton.text, &dialog);
    closeBtn->setObjectName(chrome.closeButton.objectName);
    closeBtn->setToolTip(chrome.closeButton.toolTip);
    QPushButton* approveJoinBtn = new QPushButton(QStringLiteral("同意"), &dialog);
    approveJoinBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    approveJoinBtn->setToolTip(QStringLiteral("同意选中的入群申请"));
    QPushButton* rejectJoinBtn = new QPushButton(QStringLiteral("拒绝"), &dialog);
    rejectJoinBtn->setObjectName(QStringLiteral("dialogDangerBtn"));
    rejectJoinBtn->setToolTip(QStringLiteral("拒绝选中的入群申请"));
    QPushButton* leaveGroupBtn = new QPushButton(QStringLiteral("退群"), &dialog);
    leaveGroupBtn->setObjectName(QStringLiteral("dialogDangerBtn"));
    leaveGroupBtn->setToolTip(QStringLiteral("退出选中的服务器群聊"));
    groupMainActionLayout->addWidget(openBtn);
    groupMainActionLayout->addWidget(copyBtn);
    groupMainActionLayout->addWidget(cardBtn);
    groupMainActionLayout->addWidget(announceBtn);
    groupMainActionLayout->addWidget(approveJoinBtn);
    groupMainActionLayout->addWidget(rejectJoinBtn);
    groupMainActionLayout->addWidget(leaveGroupBtn);
    groupMainActionLayout->addStretch();
    groupMainActionLayout->addWidget(closeBtn);
    groupMemberActionLayout->addWidget(inviteTextBtn);
    groupMemberActionLayout->addWidget(memberBtn);
    groupMemberActionLayout->addWidget(onlineMemberBtn);
    groupMemberActionLayout->addStretch();
    groupMediaActionLayout->addWidget(copyGroupMediaPackBtn);
    groupMediaActionLayout->addWidget(copyGroupBatchPlanBtn);
    groupMediaActionLayout->addWidget(copyMediaGuideBtn);
    groupMediaActionLayout->addStretch();
    actionLayout->addLayout(hintLayout);
    actionLayout->addLayout(groupMainActionLayout);
    actionLayout->addLayout(groupMemberActionLayout);
    actionLayout->addLayout(groupMediaActionLayout);
    layout->addLayout(actionLayout);

    auto openSelectedGroup = [this, noticeList, &dialog]() {
        openSelectedGroupNoticeEntry(noticeList, &dialog);
    };
    dialog.setStyleSheet(NotificationPanelManager::groupNoticeDialogStyleSheet());
    connect(openBtn, &QPushButton::clicked, &dialog, openSelectedGroup);
    auto updateGroupPreview = [this, noticeList, searchEdit, groupPreviewLabel]() {
        const QString entryId = selectedGroupNoticeEntryId(noticeList);
        if (entryId.startsWith(QStringLiteral("join_request:"))) {
            const QJsonObject application = m_pendingGroupJoinApplications.value(entryId.mid(QStringLiteral("join_request:").size()));
            const QString applicant = application.value("applicantName").toString(application.value("applicantId").toString());
            const QString message = application.value("message").toString();
            groupPreviewLabel->setText(QStringLiteral("%1 申请加入“%2”\nQQ:%3%4")
                .arg(applicant,
                     application.value("groupName").toString(QStringLiteral("群聊")),
                     application.value("applicantId").toString(),
                     message.isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(message)));
            return;
        }
        if (entryId.startsWith(QStringLiteral("join_pending:"))) {
            const QJsonObject application = m_pendingOutgoingGroupJoinApplications.value(entryId.mid(QStringLiteral("join_pending:").size()));
            groupPreviewLabel->setText(QStringLiteral("已申请加入“%1”\n群号：%2\n当前状态：等待群主或管理员同意%3")
                .arg(application.value("groupName").toString(QStringLiteral("群聊")),
                     application.value("groupId").toString(),
                     application.value("message").toString().isEmpty() ? QString() : QStringLiteral("\n验证信息：%1").arg(application.value("message").toString())));
            return;
        }
        if (entryId.startsWith(QStringLiteral("join_rejected:"))) {
            const QJsonObject application = m_rejectedOutgoingGroupJoinApplications.value(entryId.mid(QStringLiteral("join_rejected:").size()));
            groupPreviewLabel->setText(QStringLiteral("“%1”已拒绝你的加群申请\n群名：%2\n群号：%3")
                .arg(application.value("reviewerName").toString(QStringLiteral("群主或管理员")),
                     application.value("groupName").toString(QStringLiteral("群聊")),
                     application.value("groupId").toString()));
            return;
        }
        const GroupNoticeSelectionSnapshot snapshot = currentGroupNoticeSelectionSnapshot(noticeList, searchEdit);
        groupPreviewLabel->setText(snapshot.previewText);
    };
    auto updateGroupActionState = [=]() {
        const bool joinRequestSelected = selectedGroupNoticeEntryId(noticeList).startsWith(QStringLiteral("join_request:"));
        const bool joinPendingSelected = selectedGroupNoticeEntryId(noticeList).startsWith(QStringLiteral("join_pending:"));
        const bool joinRejectedSelected = selectedGroupNoticeEntryId(noticeList).startsWith(QStringLiteral("join_rejected:"));
        const GroupNoticeActionState state = NotificationPanelManager::groupNoticeActionState(
            selectedGroupNoticeEntryId(noticeList),
            noticeList->currentItem() != nullptr,
            !searchEdit->text().trimmed().isEmpty(),
            noticeList->count());
        openBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.openEnabled);
        openBtn->setText(state.openText);
        openBtn->setToolTip(state.openToolTip);
        copyBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyIdEnabled);
        copyBtn->setToolTip(state.copyIdToolTip);
        cardBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyCardEnabled);
        cardBtn->setToolTip(state.copyCardToolTip);
        announceBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyAnnouncementEnabled);
        announceBtn->setToolTip(state.copyAnnouncementToolTip);
        memberBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyMembersEnabled);
        memberBtn->setToolTip(state.copyMembersToolTip);
        onlineMemberBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyOnlineMembersEnabled);
        onlineMemberBtn->setToolTip(state.copyOnlineMembersToolTip);
        inviteTextBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyInviteEnabled);
        inviteTextBtn->setToolTip(state.copyInviteToolTip);
        copyGroupMediaPackBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyMediaPackEnabled);
        copyGroupMediaPackBtn->setToolTip(state.copyMediaPackToolTip);
        copyGroupBatchPlanBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyBatchPlanEnabled);
        copyGroupBatchPlanBtn->setToolTip(state.copyBatchPlanToolTip);
        copyMediaGuideBtn->setEnabled(!joinRequestSelected && !joinPendingSelected && !joinRejectedSelected && state.copyMediaGuideEnabled);
        copyMediaGuideBtn->setToolTip(state.copyMediaGuideToolTip);
        approveJoinBtn->setVisible(joinRequestSelected);
        rejectJoinBtn->setVisible(joinRequestSelected);
        const QString entryId = selectedGroupNoticeEntryId(noticeList);
        const bool canLeaveGroup = !joinRequestSelected
            && m_joinedServerSearchGroups.values().contains(entryId)
            && m_serverGroupOwners.value(m_joinedServerSearchGroups.key(entryId)) != m_currentUserId;
        leaveGroupBtn->setVisible(canLeaveGroup);
        hintLabel->setText(joinRequestSelected ? QStringLiteral("请选择同意或拒绝处理入群申请")
            : (joinPendingSelected ? QStringLiteral("入群申请处理中，等待群主或管理员同意")
            : (joinRejectedSelected ? QStringLiteral("该入群申请已被拒绝") : state.hintText)));
    };
    updateGroupPreview();
    updateGroupActionState();
    connect(noticeList, &QListWidget::currentItemChanged, &dialog, [updateGroupPreview, updateGroupActionState](QListWidgetItem*, QListWidgetItem*) {
        updateGroupPreview();
        updateGroupActionState();
    });
    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillGroups, updateGroupPreview, updateGroupActionState]() {
        fillGroups();
        updateGroupPreview();
        updateGroupActionState();
    });
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, openSelectedGroup);
    auto respondToSelectedJoinRequest = [this, noticeList, fillGroups, updateGroupPreview, updateGroupActionState](bool accepted) {
        const QString entryId = selectedGroupNoticeEntryId(noticeList);
        static const QString prefix = QStringLiteral("join_request:");
        if (!entryId.startsWith(prefix)) {
            ui->statusbar->showMessage(QStringLiteral("请先选择要处理的入群申请"), 1800);
            return;
        }
        const QString requestId = entryId.mid(prefix.size());
        if (!m_client || !m_client->respondServerGroupJoinRequest(requestId, accepted)) {
            ui->statusbar->showMessage(QStringLiteral("入群申请处理发送失败，请检查连接"), 2800);
            return;
        }
        fillGroups();
        updateGroupPreview();
        updateGroupActionState();
        ui->statusbar->showMessage(accepted ? QStringLiteral("已同意入群申请") : QStringLiteral("已拒绝入群申请"), 2200);
        ui->groupNoticeBtn->setText(m_pendingGroupJoinApplications.isEmpty()
            ? QStringLiteral("群通知")
            : QStringLiteral("群通知 %1").arg(m_pendingGroupJoinApplications.size()));
    };
    connect(approveJoinBtn, &QPushButton::clicked, &dialog, [respondToSelectedJoinRequest]() { respondToSelectedJoinRequest(true); });
    connect(rejectJoinBtn, &QPushButton::clicked, &dialog, [respondToSelectedJoinRequest]() { respondToSelectedJoinRequest(false); });
    connect(leaveGroupBtn, &QPushButton::clicked, &dialog, [this, noticeList, &dialog]() {
        const QString localId = selectedGroupNoticeEntryId(noticeList);
        const QString serverId = m_joinedServerSearchGroups.key(localId);
        if (serverId.isEmpty() || !m_client) return;
        if (QMessageBox::question(&dialog, QStringLiteral("退出群聊"),
                                  QStringLiteral("确定退出“%1”吗？").arg(m_localGroupNames.value(localId, QStringLiteral("群聊"))),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            m_client->leaveServerGroup(serverId);
        }
    });
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

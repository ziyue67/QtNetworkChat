#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

void MainWindow::onShowGlobalSearch() {
    QDialog dialog(this);
    dialog.setObjectName("globalSearchDialog");
    dialog.setWindowTitle("综合搜索");
    dialog.setFixedSize(940, 740);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QFrame* header = new QFrame(&dialog);
    header->setObjectName("searchHeader");
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(18, 14, 18, 8);
    headerLayout->setSpacing(10);

    QHBoxLayout* searchLayout = new QHBoxLayout;
    QLineEdit* searchEdit = new QLineEdit(header);
    searchEdit->setObjectName("globalSearchInput");
    searchEdit->setPlaceholderText("输入 QQ 号 / 昵称搜索");
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setToolTip("输入 QQ 号、昵称或群名；回车可搜索或打开匹配结果");
    QPushButton* searchBtn = new QPushButton("搜索", header);
    searchBtn->setObjectName("globalSearchPrimaryBtn");
    searchBtn->setToolTip("按当前关键词刷新综合搜索结果");
    QPushButton* clearBtn = new QPushButton("清空", header);
    clearBtn->setObjectName("globalSearchGhostBtn");
    clearBtn->setToolTip("清空搜索关键词并恢复全部结果");
    QPushButton* quickAddBtn = new QPushButton("好友申请", header);
    quickAddBtn->setObjectName("globalSearchGhostBtn");
    quickAddBtn->setToolTip("打开好友申请窗口，按 QQ 号搜索并发送申请");
    QPushButton* friendManagerBtn = new QPushButton("好友管理", header);
    friendManagerBtn->setObjectName("globalSearchGhostBtn");
    friendManagerBtn->setToolTip("打开好友管理器，查看、搜索和整理好友");
    searchLayout->addWidget(searchEdit, 1);
    searchLayout->addWidget(searchBtn);
    searchLayout->addWidget(clearBtn);
    searchLayout->addWidget(quickAddBtn);
    searchLayout->addWidget(friendManagerBtn);
    headerLayout->addLayout(searchLayout);

    QHBoxLayout* tabLayout = new QHBoxLayout;
    const QStringList tabs = {"全部", "用户", "群聊", "小程序", "机器人"};
    for (const QString& tab : tabs) {
        QLabel* label = new QLabel(tab, header);
        label->setObjectName(tab == "全部" ? "activeSearchTab" : "searchTab");
        label->setAlignment(Qt::AlignCenter);
        tabLayout->addWidget(label);
    }
    tabLayout->addStretch();
    headerLayout->addLayout(tabLayout);
    layout->addWidget(header);

    QListWidget* resultList = new QListWidget(&dialog);
    resultList->setObjectName("globalResultList");
    layout->addWidget(resultList, 1);

    QHBoxLayout* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(18, 10, 18, 18);
    QLabel* actionHint = new QLabel("双击结果可聊天、进群或发送好友申请", &dialog);
    actionHint->setObjectName("globalActionHint");
    QLabel* statsLabel = new QLabel(&dialog);
    statsLabel->setObjectName("globalStatsLabel");
    QLabel* previewLabel = new QLabel("选择结果后可复制QQ、名片、邀请卡或直接打开", &dialog);
    previewLabel->setObjectName("globalPreviewLabel");
    QPushButton* openBtn = new QPushButton("打开/申请", &dialog);
    openBtn->setObjectName("globalSearchPrimaryBtn");
    openBtn->setToolTip("打开当前结果；陌生用户会尝试发送好友申请");
    QPushButton* createGroupBtn = new QPushButton("用搜索创建群", &dialog);
    createGroupBtn->setObjectName("globalSearchGhostBtn");
    createGroupBtn->setToolTip("使用当前搜索关键词创建一个本地群聊");
    QPushButton* inviteVisibleBtn = new QPushButton("可见用户建群", &dialog);
    inviteVisibleBtn->setObjectName("globalSearchGhostBtn");
    inviteVisibleBtn->setToolTip("用当前可见用户创建群聊，并邀请可申请用户");
    QPushButton* addVisibleBtn = new QPushButton("申请可见用户", &dialog);
    addVisibleBtn->setObjectName("globalSearchGhostBtn");
    addVisibleBtn->setToolTip("向当前列表里可申请的在线用户批量发送好友申请");
    QPushButton* copyBtn = new QPushButton("复制QQ", &dialog);
    copyBtn->setObjectName("globalSearchGhostBtn");
    copyBtn->setToolTip("复制当前选中结果的 QQ 号或群号");
    QPushButton* copyListBtn = new QPushButton("复制结果列表", &dialog);
    copyListBtn->setObjectName("globalSearchGhostBtn");
    copyListBtn->setToolTip("复制当前可见搜索结果列表");
    QPushButton* copyAddTextBtn = new QPushButton("复制申请话术", &dialog);
    copyAddTextBtn->setObjectName("globalSearchGhostBtn");
    copyAddTextBtn->setToolTip("复制适合当前选中用户的好友申请话术");
    QPushButton* copyInviteCardBtn = new QPushButton("复制邀请卡", &dialog);
    copyInviteCardBtn->setObjectName("globalSearchGhostBtn");
    copyInviteCardBtn->setToolTip("复制当前用户或群聊的邀请卡片");
    QPushButton* copySearchCardBtn = new QPushButton("复制搜索卡片", &dialog);
    copySearchCardBtn->setObjectName("globalSearchGhostBtn");
    copySearchCardBtn->setToolTip("复制当前搜索条件和结果摘要");
    QPushButton* copySearchMediaPackBtn = new QPushButton("复制搜索媒体包", &dialog);
    copySearchMediaPackBtn->setObjectName("globalSearchGhostBtn");
    copySearchMediaPackBtn->setToolTip("复制搜索场景下发送图片、视频或文件的准备摘要");
    QPushButton* copyBatchMediaPlanBtn = new QPushButton("复制批量媒体计划", &dialog);
    copyBatchMediaPlanBtn->setObjectName("globalSearchGhostBtn");
    copyBatchMediaPlanBtn->setToolTip("复制当前搜索结果的批量媒体发送计划");
    QPushButton* copyMediaGuideBtn = new QPushButton("复制上传指南", &dialog);
    copyMediaGuideBtn->setObjectName("globalSearchGhostBtn");
    copyMediaGuideBtn->setToolTip("复制搜索后发送图片、视频和文件的简短指南");
    QPushButton* copyOnlineBtn = new QPushButton("复制在线", &dialog);
    copyOnlineBtn->setObjectName("globalSearchGhostBtn");
    copyOnlineBtn->setToolTip("复制当前可见结果中的在线用户");
    QPushButton* profileBtn = new QPushButton("复制名片", &dialog);
    profileBtn->setObjectName("globalSearchGhostBtn");
    profileBtn->setToolTip("复制当前选中结果的资料名片");
    actionLayout->addWidget(actionHint);
    actionLayout->addWidget(statsLabel);
    actionLayout->addWidget(previewLabel);
    actionLayout->addStretch();
    actionLayout->addWidget(createGroupBtn);
    actionLayout->addWidget(inviteVisibleBtn);
    actionLayout->addWidget(addVisibleBtn);
    actionLayout->addWidget(copyBtn);
    actionLayout->addWidget(copyListBtn);
    actionLayout->addWidget(copyAddTextBtn);
    actionLayout->addWidget(copyInviteCardBtn);
    actionLayout->addWidget(copySearchCardBtn);
    actionLayout->addWidget(copySearchMediaPackBtn);
    actionLayout->addWidget(copyBatchMediaPlanBtn);
    actionLayout->addWidget(copyMediaGuideBtn);
    actionLayout->addWidget(copyOnlineBtn);
    actionLayout->addWidget(profileBtn);
    actionLayout->addWidget(openBtn);
    layout->addLayout(actionLayout);

    auto updatePreview = [this, resultList, previewLabel]() {
        QListWidgetItem* item = resultList->currentItem();
        if (!item) {
            previewLabel->setText("选择结果后可复制QQ、名片、邀请卡或直接打开");
            return;
        }
        QString id = item->data(Qt::UserRole).toString();
        if (id.startsWith("search_add:")) {
            QString account = id.mid(QString("search_add:").size());
            previewLabel->setText(QString("准备搜索 QQ:%1 并发送好友申请").arg(account));
        } else if (id.startsWith("local_group_")) {
            previewLabel->setText(QString("群聊 · %1 · 群号:%2 · 成员%3人").arg(m_localGroupNames.value(id, "群聊"), id.mid(QString("local_group_").size())).arg(m_localGroupMembers.value(id).size()));
        } else if (!id.isEmpty()) {
            QString relation = m_friendIds.contains(id) ? "好友" : (m_pendingOutgoingFriendRequests.contains(id) ? "申请中" : "可申请");
            previewLabel->setText(QString("联系人 · %1 · QQ:%2 · %3 · %4")
                .arg(contactDisplayName(id), id, isContactOnline(id) ? "在线" : "离线", relation));
        } else {
            previewLabel->setText("输入 QQ 号后可继续搜索并发送申请");
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
            QListWidgetItem* searchItem = new QListWidgetItem(QString("搜索 QQ 账号：%1\n双击或点击搜索可从服务器查找并发送好友申请").arg(filter));
            searchItem->setData(Qt::UserRole, "search_add:" + filter);
            searchItem->setForeground(QColor(92, 110, 128));
            searchItem->setSizeHint(QSize(0, 58));
            resultList->addItem(searchItem);
        }
        if (resultList->count() == 0) {
            QListWidgetItem* emptyItem = new QListWidgetItem("输入 QQ 号搜索用户并发送好友申请");
            emptyItem->setFlags(Qt::NoItemFlags);
            emptyItem->setForeground(QColor(135, 150, 165));
            resultList->addItem(emptyItem);
        }
        int directResultCount = friendCount + userCount + groupCount;
        actionHint->setText(filter.isEmpty()
            ? QString("双击结果可聊天、进群或发送好友申请 · 共%1项").arg(directResultCount)
            : QString("匹配%1项 · 可继续搜索QQ:%2").arg(directResultCount).arg(filter));
        statsLabel->setText(QString("好友%1 · 用户%2 · 申请中%3 · 群聊%4").arg(friendCount).arg(userCount).arg(pendingCount).arg(groupCount));
        if (resultList->count() > 0) resultList->setCurrentRow(0);
        updatePreview();
    };
    fillResults();


    connect(searchEdit, &QLineEdit::textChanged, &dialog, [fillResults](const QString& text) {
        fillResults(text.trimmed());
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
            ensureFriendRequestQueued(id, QStringLiteral("已从综合搜索向 %1（QQ:%2）发送好友申请"));
        }
        dialog.accept();
        openPrivateSession(id);
        ui->statusbar->showMessage(QString("已打开与 %1 的私聊").arg(contactDisplayName(id)), 1800);
    };

    connect(searchBtn, &QPushButton::clicked, &dialog, runServerSearch);
    connect(resultList, &QListWidget::currentItemChanged, &dialog, [updatePreview](QListWidgetItem*, QListWidgetItem*) { updatePreview(); });
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
                           QStringLiteral("确定创建群聊“%1”并邀请 %2 位可见用户吗？其中 %3 位会同时发送好友申请。")
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
        if (!sentNames.isEmpty()) detail += QString("，已发送好友申请 %1 个").arg(sentNames.size());
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
        if (!confirmAction(QStringLiteral("发送可见用户申请"),
                           QString("确定向 %1 位可见用户发送好友申请吗？").arg(addIds.size()),
                           QStringLiteral("已取消发送可见用户申请"),
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
            ui->statusbar->showMessage("可见用户好友申请发送失败", 2600);
            searchEdit->setFocus();
            return;
        }
        refreshFriendList();
        fillResults(searchEdit->text().trimmed());
        QString detail = QString("已向 %1 个可见用户发送好友申请").arg(sentNames.size());
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
            input.localGroup = id.startsWith("local_group_");
            input.friendContact = m_friendIds.contains(id);
            input.online = isContactOnline(id);
            input.memberCount = input.localGroup ? m_localGroupMembers.value(id).size() : 0;
            input.displayName = input.localGroup ? m_localGroupNames.value(id, "群聊") : contactDisplayName(id);
            inputs << input;
        }
        return inputs;
    };
    auto currentGlobalSearchCopyInput = [this, resultList, searchEdit]() {
        GlobalSearchResultCopyInput input;
        QListWidgetItem* item = resultList->currentItem();
        const QString rawId = item ? item->data(Qt::UserRole).toString() : searchEdit->text().trimmed();
        input.entryId = rawId;
        input.localGroup = rawId.startsWith("local_group_");
        input.friendContact = m_friendIds.contains(rawId);
        input.online = isContactOnline(rawId);
        input.memberCount = input.localGroup ? m_localGroupMembers.value(rawId).size() : 0;
        if (input.localGroup) {
            input.displayName = m_localGroupNames.value(rawId, "群聊");
        } else if (!rawId.startsWith("search_add:")) {
            input.displayName = contactDisplayName(rawId);
        }
        return input;
    };
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
            text = QString("QQ:%1\n一键搜索并发送好友申请").arg(id);
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

    searchEdit->setFocus();
    dialog.exec();
}

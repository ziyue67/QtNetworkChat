#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "group_info_panel_ui.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "views/messagesview.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QPair<bool, QString> promptGroupText(QDialog& panel, const QString& title, const QString& label,
                                     const QString& initialValue, bool multiline = false)
{
    QDialog dialog(&panel);
    dialog.setObjectName(QStringLiteral("groupInlineEditor"));
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(344, multiline ? 268 : 190);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 16);
    layout->setSpacing(0);
    auto* bar = new DialogTitleBar(&dialog, title);
    QObject::connect(bar, &DialogTitleBar::closeRequested, &dialog, &QDialog::reject);
    layout->addWidget(bar);
    auto* content = new QWidget(&dialog);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 14, 18, 0);
    contentLayout->setSpacing(8);
    auto* prompt = new QLabel(label, content);
    prompt->setObjectName(QStringLiteral("groupInlineEditorHint"));
    contentLayout->addWidget(prompt);
    QLineEdit* lineEdit = nullptr;
    QPlainTextEdit* textEdit = nullptr;
    if (multiline) {
        textEdit = new QPlainTextEdit(initialValue, content);
        textEdit->setObjectName(QStringLiteral("groupInlineEditorText"));
        textEdit->setPlaceholderText(label);
        textEdit->setFixedHeight(108);
        contentLayout->addWidget(textEdit);
    } else {
        lineEdit = new QLineEdit(initialValue, content);
        lineEdit->setObjectName(QStringLiteral("groupInlineEditorInput"));
        lineEdit->setPlaceholderText(label);
        lineEdit->setClearButtonEnabled(true);
        contentLayout->addWidget(lineEdit);
    }
    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), content);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    auto* save = new QPushButton(QStringLiteral("保存"), content);
    save->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    actions->addWidget(cancel);
    actions->addWidget(save);
    contentLayout->addLayout(actions);
    layout->addWidget(content, 1);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#groupInlineEditor { background:%1; border:1px solid %2; }"
        "QLabel#groupInlineEditorHint { color:%3; font-size:12px; }"
        "QLineEdit#groupInlineEditorInput,QPlainTextEdit#groupInlineEditorText { background:%4; color:%5; border:1px solid %2; border-radius:6px; padding:8px 10px; }"
        "QLineEdit#groupInlineEditorInput:focus,QPlainTextEdit#groupInlineEditorText:focus { border-color:%6; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
             ThemeManager::instance()->textColor().name(), ThemeManager::instance()->primaryColor().name()));
    if (lineEdit) {
        lineEdit->setFocus();
    } else if (textEdit) {
        textEdit->setFocus();
    }
    if (dialog.exec() != QDialog::Accepted) return qMakePair(false, QString());
    return qMakePair(true, multiline ? textEdit->toPlainText().trimmed() : lineEdit->text().trimmed());
}

bool confirmGroupDanger(QDialog& panel, const QString& title, const QString& detail,
                        const QString& actionText)
{
    QDialog dialog(&panel);
    dialog.setObjectName(QStringLiteral("groupDangerConfirm"));
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(344, 196);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 16);
    layout->setSpacing(0);
    auto* bar = new DialogTitleBar(&dialog, title);
    QObject::connect(bar, &DialogTitleBar::closeRequested, &dialog, &QDialog::reject);
    layout->addWidget(bar);
    auto* content = new QWidget(&dialog);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 16, 18, 0);
    auto* detailLabel = new QLabel(detail, content);
    detailLabel->setObjectName(QStringLiteral("groupDangerConfirmDetail"));
    detailLabel->setWordWrap(true);
    contentLayout->addWidget(detailLabel, 1);
    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), content);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    auto* accept = new QPushButton(actionText, content);
    accept->setObjectName(QStringLiteral("dialogDangerBtn"));
    actions->addWidget(cancel);
    actions->addWidget(accept);
    contentLayout->addLayout(actions);
    layout->addWidget(content, 1);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#groupDangerConfirm { background:%1; border:1px solid %2; }"
        "QLabel#groupDangerConfirmDetail { color:%3; font-size:13px; line-height:1.45; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->textSecondaryColor().name()));
    return dialog.exec() == QDialog::Accepted;
}

QString chooseGroupSetting(QDialog& panel, const QString& title, const QString& section,
                           const QList<QPair<QString, QString>>& choices, const QString& currentKey)
{
    QDialog chooser(&panel);
    chooser.setObjectName(QStringLiteral("groupSettingChooser"));
    chooser.setWindowTitle(title);
    chooser.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    chooser.setFixedSize(336, qBound(230, 122 + choices.size() * 52, 440));
    auto* layout = new QVBoxLayout(&chooser);
    layout->setContentsMargins(0, 0, 0, 14);
    layout->setSpacing(0);
    auto* titleBar = new DialogTitleBar(&chooser, title);
    QObject::connect(titleBar, &DialogTitleBar::closeRequested, &chooser, &QDialog::reject);
    layout->addWidget(titleBar);
    auto* sectionLabel = new QLabel(section, &chooser);
    sectionLabel->setObjectName(QStringLiteral("groupSettingChooserCaption"));
    sectionLabel->setContentsMargins(16, 12, 16, 6);
    layout->addWidget(sectionLabel);
    auto* optionCard = new QFrame(&chooser);
    optionCard->setObjectName(QStringLiteral("groupSettingChooserCard"));
    auto* optionLayout = new QVBoxLayout(optionCard);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(0);
    for (const auto& choice : choices) {
        auto* row = new QPushButton(optionCard);
        row->setObjectName(QStringLiteral("groupSettingChoice"));
        row->setCursor(Qt::PointingHandCursor);
        row->setProperty("settingValue", choice.second);
        row->setProperty("selected", choice.second == currentKey);
        row->setFixedHeight(52);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 14, 0);
        auto* selectionBar = new QFrame(row);
        selectionBar->setObjectName(QStringLiteral("groupSettingChoiceBar"));
        selectionBar->setFixedSize(3, 26);
        selectionBar->setVisible(choice.second == currentKey);
        rowLayout->addWidget(selectionBar);
        auto* label = new QLabel(choice.first, row);
        label->setObjectName(QStringLiteral("groupSettingChoiceLabel"));
        label->setContentsMargins(11, 0, 0, 0);
        rowLayout->addWidget(label);
        rowLayout->addStretch();
        auto* checked = new QLabel(QStringLiteral("✓"), row);
        checked->setObjectName(QStringLiteral("groupSettingChoiceCheck"));
        checked->setAlignment(Qt::AlignCenter);
        checked->setFixedSize(20, 20);
        checked->setVisible(choice.second == currentKey);
        rowLayout->addWidget(checked);
        QObject::connect(row, &QPushButton::clicked, &chooser, [&chooser, row]() {
            chooser.setProperty("selectedValue", row->property("settingValue"));
            chooser.accept();
        });
        optionLayout->addWidget(row);
    }
    layout->addWidget(optionCard);
    layout->addStretch();
    chooser.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#groupSettingChooser { background:%1; }"
        "QLabel#groupSettingChooserCaption { color:%5; font-size:12px; }"
        "QFrame#groupSettingChooserCard { background:%2; border:1px solid %3; border-radius:7px; }"
        "QPushButton#groupSettingChoice { color:%4; background:%2; border:none; border-bottom:1px solid %3; text-align:left; }"
        "QPushButton#groupSettingChoice:last-child { border-bottom:none; }"
        "QPushButton#groupSettingChoice:hover { background:%8; }"
        "QPushButton#groupSettingChoice:pressed { background:%3; }"
        "QPushButton#groupSettingChoice[selected=\"true\"] { background:%2; }"
        "QLabel#groupSettingChoiceLabel { color:%4; font-size:13px; }"
        "QPushButton#groupSettingChoice[selected=\"true\"] QLabel#groupSettingChoiceLabel { color:%7; font-weight:500; }"
        "QFrame#groupSettingChoiceBar { background:%7; border-radius:1px; }"
        "QLabel#groupSettingChoiceCheck { color:%2; background:%7; border-radius:10px; font-size:13px; font-weight:600; }")
        .arg(ThemeManager::instance()->backgroundColor().name(),
             ThemeManager::instance()->backgroundSecondaryColor().name(),
             ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->textColor().name(),
             ThemeManager::instance()->textSecondaryColor().name(),
             ThemeManager::instance()->borderColor().name(),
             ThemeManager::instance()->primaryColor().name(),
             ThemeManager::instance()->backgroundTertiaryColor().name()));
    if (chooser.exec() != QDialog::Accepted) return QString();
    return chooser.property("selectedValue").toString();
}

} // namespace

void MainWindow::editGroupProfile(const QString& groupId, const QString& groupName, bool serverGroup)
{
    QDialog editor(this);
    editor.setObjectName(QStringLiteral("groupProfileEditor"));
    editor.setWindowTitle(QStringLiteral("编辑群资料"));
    editor.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    editor.setFixedSize(360, 270);
    auto* editorLayout = new QVBoxLayout(&editor);
    editorLayout->setContentsMargins(0, 0, 0, 16);
    editorLayout->setSpacing(0);
    auto* editorBar = new DialogTitleBar(&editor, QStringLiteral("编辑群资料"));
    connect(editorBar, &DialogTitleBar::closeRequested, &editor, &QDialog::reject);
    editorLayout->addWidget(editorBar);
    auto* editorContent = new QWidget(&editor);
    auto* contentLayout = new QVBoxLayout(editorContent);
    contentLayout->setContentsMargins(18, 14, 18, 0);
    contentLayout->setSpacing(10);
    auto* editorHint = new QLabel(QStringLiteral("修改后将同步给当前群成员"), editorContent);
    editorHint->setObjectName(QStringLiteral("groupProfileEditorHint"));
    contentLayout->addWidget(editorHint);

    auto* profileRowLayout = new QHBoxLayout();
    profileRowLayout->setSpacing(12);
    auto* preview = new AvatarLabel(editorContent, 56);
    QByteArray avatarBytes;
    if (serverGroup) {
        avatarBytes = QByteArray::fromBase64(m_serverGroupSettings.value(groupId)
                                                .value(QStringLiteral("avatar")).toString().toUtf8());
    } else {
        const QString path = m_localGroupAvatarPaths.value(groupId);
        if (!path.isEmpty()) {
            QFile avatarFile(path);
            if (avatarFile.open(QIODevice::ReadOnly)) avatarBytes = avatarFile.readAll();
        }
    }
    QPixmap currentAvatar;
    currentAvatar.loadFromData(avatarBytes);
    if (currentAvatar.isNull()) preview->setTextAvatar(groupName, ThemeManager::instance()->primaryColor());
    else preview->setPixmap(currentAvatar);
    profileRowLayout->addWidget(preview);
    auto* profileFields = new QVBoxLayout();
    profileFields->setSpacing(6);
    auto* nameInput = new QLineEdit(groupName, editorContent);
    nameInput->setObjectName(QStringLiteral("groupProfileNameInput"));
    nameInput->setMaxLength(80);
    nameInput->setPlaceholderText(QStringLiteral("输入群名称"));
    auto* chooseAvatar = new QPushButton(QStringLiteral("从本地选择图片"), editorContent);
    chooseAvatar->setObjectName(QStringLiteral("groupProfileAvatarButton"));
    chooseAvatar->setCursor(Qt::PointingHandCursor);
    profileFields->addWidget(nameInput);
    profileFields->addWidget(chooseAvatar, 0, Qt::AlignLeft);
    profileRowLayout->addLayout(profileFields, 1);
    contentLayout->addLayout(profileRowLayout);
    connect(chooseAvatar, &QPushButton::clicked, &editor, [this, &editor, preview, chooseAvatar, &avatarBytes]() {
        const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString avatarFilePath = QFileDialog::getOpenFileName(&editor, QStringLiteral("选择本地群头像"), picturesDirectory,
            QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (avatarFilePath.isEmpty()) return;
        QImageReader reader(avatarFilePath);
        reader.setAutoTransform(true);
        const QImage original = reader.read();
        if (original.isNull()) {
            ui->statusbar->showMessage(QStringLiteral("图片读取失败，请选择有效的图片文件"), 2200);
            return;
        }
        QImage normalized = original.scaled(256, 256, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int left = qMax(0, (normalized.width() - 256) / 2);
        const int top = qMax(0, (normalized.height() - 256) / 2);
        normalized = normalized.copy(left, top, qMin(256, normalized.width()), qMin(256, normalized.height()));
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly) || !normalized.save(&buffer, "PNG")) {
            ui->statusbar->showMessage(QStringLiteral("群头像处理失败"), 2200);
            return;
        }
        if (encoded.size() > 768 * 1024) {
            ui->statusbar->showMessage(QStringLiteral("图片内容过大，请选择更简单的图片"), 2400);
            return;
        }
        avatarBytes = encoded;
        preview->setPixmap(QPixmap::fromImage(normalized));
        chooseAvatar->setText(QStringLiteral("已选择本地图片"));
    });

    auto* editorActions = new QHBoxLayout();
    editorActions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), editorContent);
    cancel->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    auto* save = new QPushButton(QStringLiteral("保存"), editorContent);
    save->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    save->setDefault(true);
    editorActions->addWidget(cancel);
    editorActions->addWidget(save);
    contentLayout->addLayout(editorActions);
    editorLayout->addWidget(editorContent, 1);
    connect(cancel, &QPushButton::clicked, &editor, &QDialog::reject);
    connect(save, &QPushButton::clicked, &editor, [&editor, nameInput]() {
        if (nameInput->text().trimmed().isEmpty()) {
            nameInput->setFocus();
            return;
        }
        editor.accept();
    });
    editor.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#groupProfileEditor { background:%1; border:1px solid %5; }"
        "QLabel#groupProfileEditorHint { color:%3; font-size:12px; }"
        "QLineEdit#groupProfileNameInput { background:%4; color:%2; border:1px solid %5; border-radius:6px; padding:7px 9px; }"
        "QLineEdit#groupProfileNameInput:focus { border-color:%6; }"
        "QPushButton#groupProfileAvatarButton { color:%6; background:transparent; border:none; padding:2px 0; }"
        "QPushButton#groupProfileAvatarButton:hover { color:%7; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->textColor().name(),
             ThemeManager::instance()->textSecondaryColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
             ThemeManager::instance()->borderColor().name(), ThemeManager::instance()->primaryColor().name(),
             ThemeManager::instance()->primaryHoverColor().name()));
    if (editor.exec() != QDialog::Accepted) return;
    const QString newName = nameInput->text().trimmed();
    QJsonObject update{{QStringLiteral("groupName"), newName}};
    if (!avatarBytes.isEmpty()) update[QStringLiteral("avatar")] = QString::fromUtf8(avatarBytes.toBase64());
    if (serverGroup) {
        if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, update)) {
            ui->statusbar->showMessage(QStringLiteral("群资料提交失败，请检查连接"), 2600);
        } else {
            ui->statusbar->showMessage(QStringLiteral("群资料已提交，正在同步"), 1800);
        }
    } else {
        m_localGroupNames[groupId] = newName;
        if (!avatarBytes.isEmpty()) {
            const QString avatarDirectory = QDir(ClientStorage::appDataRootDirectory())
                .filePath(QStringLiteral("group_avatars"));
            QDir().mkpath(avatarDirectory);
            const QString localAvatarPath = QDir(avatarDirectory)
                .filePath(groupId + QStringLiteral(".png"));
            QPixmap avatar;
            if (avatar.loadFromData(avatarBytes) && avatar.save(localAvatarPath, "PNG")) {
                m_localGroupAvatarPaths[groupId] = localAvatarPath;
            }
        }
        saveLocalGroups();
        refreshFriendList();
        ui->statusbar->showMessage(QStringLiteral("群资料已保存"), 1800);
    }
}

void MainWindow::addGroupMemberSummary(QDialog& panel, QVBoxLayout* contentLayout,
                                       const QString& groupId, const QStringList& members,
                                       bool serverGroup, bool manager)
{
    auto* memberCard = new QFrame(&panel);
    memberCard->setObjectName(QStringLiteral("groupInfoMemberCard"));
    auto* memberLayout = new QVBoxLayout(memberCard);
    memberLayout->setContentsMargins(14, 12, 14, 12);
    memberLayout->setSpacing(9);
    auto* memberHeader = new QHBoxLayout();
    auto* memberTitle = new QLabel(QStringLiteral("群成员  %1").arg(members.size()), memberCard);
    memberTitle->setObjectName(QStringLiteral("groupInfoMemberTitle"));
    memberHeader->addWidget(memberTitle);
    memberHeader->addStretch();
    auto* allMembers = new QPushButton(QStringLiteral("查看全部"), memberCard);
    allMembers->setObjectName(QStringLiteral("groupInfoLinkButton"));
    allMembers->setToolTip(QStringLiteral("打开群成员列表"));
    memberHeader->addWidget(allMembers);
    memberLayout->addLayout(memberHeader);
    auto* memberGrid = new QGridLayout();
    memberGrid->setHorizontalSpacing(9);
    memberGrid->setVerticalSpacing(8);
    const int visibleMembers = qMin(10, members.size());
    for (int index = 0; index < visibleMembers; ++index) {
        const QString memberId = members.at(index);
        const QString display = memberId == m_currentUserId ? m_currentUserName
            : (serverGroup ? m_serverGroupMemberNames.value(groupId + QStringLiteral("|") + memberId,
                                                             contactDisplayName(memberId))
                           : contactDisplayName(memberId));
        auto* memberItem = new QWidget(memberCard);
        auto* itemLayout = new QVBoxLayout(memberItem);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(3);
        auto* avatar = new AvatarLabel(memberItem, 34);
        const QString memberAvatarPath = peerAvatarPath(memberId);
        if (!memberAvatarPath.isEmpty() && QFileInfo::exists(memberAvatarPath)) {
            avatar->setPixmap(QPixmap(memberAvatarPath));
        } else {
            avatar->setTextAvatar(display, ThemeManager::instance()->primaryColor());
        }
        avatar->setToolTip(display);
        auto* memberName = new QLabel(display.left(4), memberItem);
        memberName->setObjectName(QStringLiteral("groupInfoMemberName"));
        memberName->setAlignment(Qt::AlignCenter);
        itemLayout->addWidget(avatar, 0, Qt::AlignHCenter);
        itemLayout->addWidget(memberName);
        memberGrid->addWidget(memberItem, index / 5, index % 5);
    }
    if (manager) {
        auto* invite = new QToolButton(memberCard);
        invite->setObjectName(QStringLiteral("groupInfoMemberAction"));
        invite->setText(QStringLiteral("+"));
        invite->setToolTip(QStringLiteral("邀请群成员"));
        invite->setAccessibleName(QStringLiteral("邀请群成员"));
        invite->setFixedSize(34, 34);
        memberGrid->addWidget(invite, visibleMembers / 5, visibleMembers % 5, Qt::AlignHCenter);
        connect(invite, &QToolButton::clicked, this, [this, serverGroup, groupId, &panel]() {
            const auto result = promptGroupText(panel, QStringLiteral("邀请群成员"),
                                                QStringLiteral("输入对方 QQ 号"), QString());
            const QString account = result.second;
            if (!result.first || account.isEmpty()) return;
            if (serverGroup) {
                requestServerGroupMemberUpdate(account, QStringLiteral("add"), groupId);
                return;
            }
            if (!isCurrentUserGroupOwner(groupId)) return;
            if (!m_localGroupMembers[groupId].contains(account)) {
                m_localGroupMembers[groupId].append(account);
                saveLocalGroups();
                refreshGroupMemberSidebar();
            }
        });
    }
    memberLayout->addLayout(memberGrid);
    contentLayout->addWidget(memberCard);
    connect(allMembers, &QPushButton::clicked, this, [this, &panel]() {
        if (m_messagesView) {
            refreshGroupMemberSidebar();
            m_messagesView->setGroupMemberSidebarVisible(true);
        }
        panel.accept();
    });
}

void MainWindow::showGroupInfoPanel()
{
    const bool publicGroup = m_privateChatTarget.isEmpty();
    const QString serverGroupId = publicGroup ? QStringLiteral("public")
        : m_joinedServerSearchGroups.key(m_privateChatTarget);
    const bool serverGroup = !serverGroupId.isEmpty();
    // Joined searchable server groups deliberately use a local session id.  The
    // server mapping must win here, otherwise a server group is treated as a
    // local one and its owner/admin permissions disappear from the panel.
    const bool localGroup = !serverGroup
        && m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    if (!localGroup && !publicGroup && !serverGroup) {
        ui->statusbar->showMessage(QStringLiteral("请先进入群聊"), 1800);
        return;
    }

    const QString groupId = serverGroup ? serverGroupId : m_privateChatTarget;
    const QString groupName = localGroup ? m_localGroupNames.value(groupId, QStringLiteral("群聊"))
        : m_serverGroupNames.value(groupId, QStringLiteral("公共聊天室"));
    const QString announcement = localGroup ? m_localGroupAnnouncements.value(groupId)
        : m_serverGroupAnnouncements.value(groupId);
    const QStringList members = localGroup ? m_localGroupMembers.value(groupId)
        : m_serverGroupMembers.value(groupId);
    const bool manager = localGroup ? isCurrentUserGroupOwner(groupId) : canCurrentUserManageServerGroup(groupId);
    const bool owner = localGroup ? isCurrentUserGroupOwner(groupId)
        : (m_serverGroupOwners.value(groupId) == m_currentUserId);
    const QJsonObject serverSettings = m_serverGroupSettings.value(groupId);

    QDialog panel(this);
    panel.setObjectName(QStringLiteral("groupInfoDialog"));
    panel.setWindowTitle(QStringLiteral("群聊资料"));
    panel.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    panel.setFixedSize(384, qMin(736, qMax(592, height() - 28)));
    auto* root = new QVBoxLayout(&panel);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* titleBar = new DialogTitleBar(&panel, QStringLiteral("群聊资料"));
    connect(titleBar, &DialogTitleBar::closeRequested, &panel, &QDialog::reject);
    root->addWidget(titleBar);

    auto* scroll = new QScrollArea(&panel);
    scroll->setObjectName(QStringLiteral("groupInfoScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("groupInfoContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 12, 16, 22);
    contentLayout->setSpacing(9);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    GroupInfoPanelUi panelUi(panel, *contentLayout);

    const QString avatarPath = localGroup ? m_localGroupAvatarPaths.value(groupId) : QString();
    QPixmap serverAvatar;
    if (serverGroup) serverAvatar.loadFromData(QByteArray::fromBase64(serverSettings.value(QStringLiteral("avatar")).toString().toUtf8()));
    const QPixmap avatarPixmap = !avatarPath.isEmpty() && QFileInfo::exists(avatarPath)
        ? QPixmap(avatarPath) : serverAvatar;
    const auto overview = panelUi.addOverview(groupId, groupName, members.size(),
                                              serverGroup, manager, avatarPixmap);
    connect(overview.shareButton, &QToolButton::clicked, this, [groupId, this]() {
        QApplication::clipboard()->setText(groupId);
        ui->statusbar->showMessage(QStringLiteral("群号已复制"), 1600);
    });
    if (manager) {
        connect(overview.avatarButton, &QToolButton::clicked, this,
                [this, serverGroup, groupId, groupAvatar = overview.avatar,
                 avatarButton = overview.avatarButton]() {
            const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            const QString sourcePath = QFileDialog::getOpenFileName(
                this, QStringLiteral("选择本地群头像"), picturesDirectory,
                QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (sourcePath.isEmpty()) return;

            QImageReader reader(sourcePath);
            reader.setAutoTransform(true);
            const QImage original = reader.read();
            if (original.isNull()) {
                ui->statusbar->showMessage(QStringLiteral("图片读取失败，请选择有效的图片文件"), 2200);
                return;
            }
            QImage normalized = original.scaled(256, 256, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const int left = qMax(0, (normalized.width() - 256) / 2);
            const int top = qMax(0, (normalized.height() - 256) / 2);
            normalized = normalized.copy(left, top, qMin(256, normalized.width()), qMin(256, normalized.height()));
            QByteArray encoded;
            QBuffer buffer(&encoded);
            if (!buffer.open(QIODevice::WriteOnly) || !normalized.save(&buffer, "PNG") || encoded.size() > 768 * 1024) {
                ui->statusbar->showMessage(QStringLiteral("群头像处理失败，请选择更简单的图片"), 2400);
                return;
            }

            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(
                        groupId, QJsonObject{{QStringLiteral("avatar"), QString::fromUtf8(encoded.toBase64())}})) {
                    ui->statusbar->showMessage(QStringLiteral("群头像提交失败，请检查连接"), 2600);
                    return;
                }
                m_serverGroupSettings[groupId][QStringLiteral("avatar")] = QString::fromUtf8(encoded.toBase64());
                ui->statusbar->showMessage(QStringLiteral("群头像已提交，正在同步"), 1800);
            } else {
                const QString avatarDirectory = QDir(ClientStorage::appDataRootDirectory())
                    .filePath(QStringLiteral("group_avatars"));
                QDir().mkpath(avatarDirectory);
                const QString avatarPath = QDir(avatarDirectory).filePath(groupId + QStringLiteral(".png"));
                if (!normalized.save(avatarPath, "PNG")) {
                    ui->statusbar->showMessage(QStringLiteral("群头像保存失败"), 2200);
                    return;
                }
                m_localGroupAvatarPaths[groupId] = avatarPath;
                saveLocalGroups();
                refreshFriendList();
                ui->statusbar->showMessage(QStringLiteral("群头像已更新"), 1800);
            }
            groupAvatar->setPixmap(QPixmap::fromImage(normalized));
            avatarButton->setToolTip(QStringLiteral("点击从本地选择群头像"));
        });
    }
    if (QPushButton* editAnnouncement = panelUi.addAnnouncement(announcement, manager)) {
        connect(editAnnouncement, &QPushButton::clicked, this, [this, &panel]() {
            onEditGroupAnnouncement();
            panel.accept();
        });
    }

    addGroupMemberSummary(panel, contentLayout, groupId, members, serverGroup, manager);
    addGroupInfoManagerSettings(panel, panelUi, groupId, groupName, serverGroup, manager);
    addGroupInfoPersonalSettings(panel, panelUi, groupId, serverGroup);
    addGroupInfoExitAction(panel, contentLayout, groupId, groupName, serverGroup, localGroup, owner);

    GroupInfoPanelUi::applyStyle(panel);
    const QPoint panelOrigin = mapToGlobal(QPoint(qMax(0, width() - panel.width() - 10), 36));
    panel.move(panelOrigin);
    panel.exec();
}

void MainWindow::addGroupInfoManagerSettings(QDialog& panel, GroupInfoPanelUi& panelUi,
                                            const QString& groupId, const QString& groupName,
                                            bool serverGroup, bool manager)
{
    const QJsonObject serverSettings = m_serverGroupSettings.value(groupId);
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    auto stableSettingKey = [groupId, serverGroup]() {
        return QStringLiteral("groupInfo/%1/%2/")
            .arg(serverGroup ? QStringLiteral("server") : QStringLiteral("local"), groupId);
    };
    auto chooseSetting = [&panel](const QString& title, const QString& section,
                                  const QList<QPair<QString, QString>>& choices, const QString& currentKey) {
        return chooseGroupSetting(panel, title, section, choices, currentKey);
    };

    if (manager) {
        panelUi.addCaption(QStringLiteral("资料管理"));
        QPushButton* profileRow = panelUi.addRow(QStringLiteral("群资料设置"), QStringLiteral("群名称、头像"), true);
        connect(profileRow, &QPushButton::clicked, this, [this, serverGroup, groupId, groupName]() {
            editGroupProfile(groupId, groupName, serverGroup);
        });
        panelUi.addCaption(QStringLiteral("发言权限"));
        auto* muteToggle = panelUi.addToggleRow(QStringLiteral("全员禁言"),
                                                 QStringLiteral("仅允许管理员和群主发言"), 48);
        const QString allMuteKey = stableSettingKey() + QStringLiteral("allMuted");
        muteToggle->setChecked(serverGroup ? serverSettings.value(QStringLiteral("allMuted")).toBool(false)
                                           : settings.value(allMuteKey, false).toBool());
        connect(muteToggle, &QCheckBox::toggled, this, [this, manager, allMuteKey, serverGroup, groupId](bool enabled) {
            if (!manager) return;
            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("allMuted"), enabled}})) {
                    ui->statusbar->showMessage(QStringLiteral("全员禁言提交失败，请检查连接"), 2600);
                }
                return;
            }
            QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
            localSettings.setValue(allMuteKey, enabled);
            ui->statusbar->showMessage(enabled ? QStringLiteral("已保存全员禁言状态") : QStringLiteral("已解除全员禁言状态"), 2200);
        });
        const QString speakingRule = serverGroup ? serverSettings.value(QStringLiteral("speakingRule")).toString(QStringLiteral("unrestricted"))
                                                 : settings.value(stableSettingKey() + QStringLiteral("speakingRule"), QStringLiteral("unrestricted")).toString();
        const QMap<QString, QString> speakingTexts = {
            {QStringLiteral("unrestricted"), QStringLiteral("不限制发言")},
            {QStringLiteral("per_minute_10"), QStringLiteral("每分钟 10 条")},
            {QStringLiteral("per_minute_5"), QStringLiteral("每分钟 5 条")},
            {QStringLiteral("new_members_24h"), QStringLiteral("新成员 24 小时后可发言")}
        };
        QPushButton* speakingRow = panelUi.addRow(QStringLiteral("发言限制"), speakingTexts.value(speakingRule, QStringLiteral("不限制发言")), true);
        connect(speakingRow, &QPushButton::clicked, this, [this, speakingRow, stableSettingKey, serverGroup, groupId, chooseSetting, speakingTexts]() {
            const QString current = serverGroup
                ? m_serverGroupSettings.value(groupId).value(QStringLiteral("speakingRule")).toString(QStringLiteral("unrestricted"))
                : QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).value(stableSettingKey() + QStringLiteral("speakingRule"), QStringLiteral("unrestricted")).toString();
            const QList<QPair<QString, QString>> choices = {
                {QStringLiteral("不限制发言"), QStringLiteral("unrestricted")},
                {QStringLiteral("每分钟 10 条"), QStringLiteral("per_minute_10")},
                {QStringLiteral("每分钟 5 条"), QStringLiteral("per_minute_5")},
                {QStringLiteral("新成员 24 小时后可发言"), QStringLiteral("new_members_24h")}
            };
            const QString selected = chooseSetting(QStringLiteral("发言限制"), QStringLiteral("选择群成员发言频率"), choices, current);
            if (selected.isEmpty() || selected == current) return;
            if (serverGroup && (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("speakingRule"), selected}}))) {
                ui->statusbar->showMessage(QStringLiteral("发言限制提交失败，请检查连接"), 2600);
                return;
            }
            if (!serverGroup) {
                QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("speakingRule"), selected);
            }
            speakingRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(speakingTexts.value(selected));
        });
        panelUi.addCaption(QStringLiteral("开放设置"));
        const QString joinPolicy = serverGroup ? serverSettings.value(QStringLiteral("joinPolicy")).toString(QStringLiteral("approval"))
                                               : settings.value(stableSettingKey() + QStringLiteral("joinPolicy"), QStringLiteral("approval")).toString();
        const QMap<QString, QString> joinTexts = {
            {QStringLiteral("open"), QStringLiteral("允许任何人加群")},
            {QStringLiteral("approval"), QStringLiteral("需要身份验证")},
            {QStringLiteral("disabled"), QStringLiteral("不允许任何人加群")}
        };
        QPushButton* joinRow = panelUi.addRow(QStringLiteral("加群方式"), joinTexts.value(joinPolicy, QStringLiteral("需要身份验证")), true);
        connect(joinRow, &QPushButton::clicked, this, [this, joinRow, serverGroup, groupId, stableSettingKey, chooseSetting, joinTexts]() {
            const QString current = serverGroup
                ? m_serverGroupSettings.value(groupId).value(QStringLiteral("joinPolicy")).toString(QStringLiteral("approval"))
                : QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).value(stableSettingKey() + QStringLiteral("joinPolicy"), QStringLiteral("approval")).toString();
            const QString selected = chooseSetting(QStringLiteral("加群方式"), QStringLiteral("选择加入当前群聊的方式"), {
                {QStringLiteral("允许任何人加群"), QStringLiteral("open")},
                {QStringLiteral("需要身份验证"), QStringLiteral("approval")},
                {QStringLiteral("不允许任何人加群"), QStringLiteral("disabled")}
            }, current);
            if (selected.isEmpty() || selected == current) return;
            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("joinPolicy"), selected}})) {
                    ui->statusbar->showMessage(QStringLiteral("加群方式提交失败，请检查连接"), 2600);
                    return;
                }
            } else {
                QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("joinPolicy"), selected);
            }
            joinRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(joinTexts.value(selected));
        });
        const QString searchMode = serverGroup
            ? serverSettings.value(QStringLiteral("searchMode")).toString(QStringLiteral("id_and_keyword"))
            : settings.value(stableSettingKey() + QStringLiteral("searchMode"), QStringLiteral("id_and_keyword")).toString();
        const QMap<QString, QString> searchTexts = {
            {QStringLiteral("id_and_keyword"), QStringLiteral("通过群号及关键词搜索")},
            {QStringLiteral("id_only"), QStringLiteral("通过群号搜索")},
            {QStringLiteral("private"), QStringLiteral("私密")}
        };
        QPushButton* searchRow = panelUi.addRow(QStringLiteral("群搜索方式"), searchTexts.value(searchMode), true);
        connect(searchRow, &QPushButton::clicked, this, [this, searchRow, serverGroup, groupId, stableSettingKey, chooseSetting, searchTexts]() {
            const QString current = serverGroup
                ? m_serverGroupSettings.value(groupId).value(QStringLiteral("searchMode")).toString(QStringLiteral("id_and_keyword"))
                : QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).value(stableSettingKey() + QStringLiteral("searchMode"), QStringLiteral("id_and_keyword")).toString();
            const QString selected = chooseSetting(QStringLiteral("群搜索方式"), QStringLiteral("选择其他用户查找群聊的方式"), {
                {QStringLiteral("通过群号及关键词搜索"), QStringLiteral("id_and_keyword")},
                {QStringLiteral("通过群号搜索"), QStringLiteral("id_only")},
                {QStringLiteral("私密"), QStringLiteral("private")}
            }, current);
            if (selected.isEmpty() || selected == current) return;
            if (serverGroup) {
                if (!m_client || !m_client->sendServerGroupSettingsUpdate(groupId, QJsonObject{{QStringLiteral("searchMode"), selected}})) {
                    ui->statusbar->showMessage(QStringLiteral("群搜索方式提交失败，请检查连接"), 2600);
                    return;
                }
            } else {
                QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("searchMode"), selected);
            }
            searchRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(searchTexts.value(selected));
        });
    }
}

void MainWindow::addGroupInfoPersonalSettings(QDialog& panel, GroupInfoPanelUi& panelUi,
                                             const QString& groupId, bool serverGroup)
{
    const QJsonObject userSettings = m_serverGroupUserSettings.value(groupId);
    QSettings settings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
    auto stableSettingKey = [groupId, serverGroup]() {
        return QStringLiteral("groupInfo/%1/%2/")
            .arg(serverGroup ? QStringLiteral("server") : QStringLiteral("local"), groupId);
    };
    auto promptText = [&panel](const QString& title, const QString& label,
                               const QString& initialValue, bool multiline = false) {
        return promptGroupText(panel, title, label, initialValue, multiline);
    };
    auto chooseSetting = [&panel](const QString& title, const QString& section,
                                  const QList<QPair<QString, QString>>& choices, const QString& currentKey) {
        return chooseGroupSetting(panel, title, section, choices, currentKey);
    };

    panelUi.addCaption(QStringLiteral("我的群资料"));
    const QString nicknameKey = stableSettingKey() + QStringLiteral("nickname");
    const QString remarkKey = stableSettingKey() + QStringLiteral("remark");
    const QString currentNickname = serverGroup ? userSettings.value(QStringLiteral("nickname")).toString(m_currentUserName)
                                                : settings.value(nicknameKey, m_currentUserName).toString();
    const QString currentRemark = serverGroup ? userSettings.value(QStringLiteral("remark")).toString()
                                              : settings.value(remarkKey).toString();
    QPushButton* nicknameRow = panelUi.addRow(QStringLiteral("我的本群昵称"), currentNickname, true);
    connect(nicknameRow, &QPushButton::clicked, this, [nicknameRow, nicknameKey, serverGroup, groupId, this, promptText]() {
        const QString current = nicknameRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->text();
        const auto result = promptText(QStringLiteral("我的本群昵称"), QStringLiteral("输入群昵称"), current);
        const QString value = result.second;
        if (!result.first || value.isEmpty()) return;
        if (serverGroup) {
            if (!m_client || !m_client->sendServerGroupUserSettingsUpdate(groupId, QJsonObject{{QStringLiteral("nickname"), value}})) {
                ui->statusbar->showMessage(QStringLiteral("群昵称保存失败，请检查连接"), 2600);
            }
            return;
        }
        QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        localSettings.setValue(nicknameKey, value);
        nicknameRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(value);
    });
    QPushButton* remarkRow = panelUi.addRow(QStringLiteral("群聊备注"), currentRemark.isEmpty() ? QStringLiteral("填写备注") : currentRemark, true);
    connect(remarkRow, &QPushButton::clicked, this, [remarkRow, remarkKey, serverGroup, groupId, this, promptText]() {
        const QString current = remarkRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->text();
        const auto result = promptText(QStringLiteral("群聊备注"), QStringLiteral("输入备注名称"),
                                       current == QStringLiteral("填写备注") ? QString() : current);
        const QString value = result.second;
        if (!result.first) return;
        if (serverGroup) {
            if (!m_client || !m_client->sendServerGroupUserSettingsUpdate(groupId, QJsonObject{{QStringLiteral("remark"), value}})) {
                ui->statusbar->showMessage(QStringLiteral("群聊备注保存失败，请检查连接"), 2600);
            }
            return;
        }
        QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        localSettings.setValue(remarkKey, value);
        remarkRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(value.isEmpty() ? QStringLiteral("填写备注") : value);
    });
    panelUi.addCaption(QStringLiteral("消息设置"));
    auto* notificationToggle = panelUi.addToggleRow(QStringLiteral("消息免打扰"));
    const QString muteNotificationKey = stableSettingKey() + QStringLiteral("muteNotifications");
    notificationToggle->setChecked(serverGroup ? userSettings.value(QStringLiteral("muteNotifications")).toBool(false)
                                               : settings.value(muteNotificationKey, false).toBool());
    connect(notificationToggle, &QCheckBox::toggled, this, [this, serverGroup, groupId, muteNotificationKey](bool enabled) {
        if (serverGroup) {
            if (!m_client || !m_client->sendServerGroupUserSettingsUpdate(groupId, QJsonObject{{QStringLiteral("muteNotifications"), enabled}})) {
                ui->statusbar->showMessage(QStringLiteral("消息设置保存失败，请检查连接"), 2600);
            }
            return;
        }
        QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        localSettings.setValue(muteNotificationKey, enabled);
    });
    const QString receiveMode = serverGroup
        ? userSettings.value(QStringLiteral("receiveMode")).toString(QStringLiteral("receive_quiet"))
        : settings.value(stableSettingKey() + QStringLiteral("receiveMode"), QStringLiteral("receive_quiet")).toString();
    const QMap<QString, QString> receiveTexts = {
        {QStringLiteral("receive_quiet"), QStringLiteral("接收消息但不提醒")},
        {QStringLiteral("assistant_quiet"), QStringLiteral("收进群助手且不提醒")},
        {QStringLiteral("block"), QStringLiteral("屏蔽群消息")}
    };
    QPushButton* receiveRow = panelUi.addRow(QStringLiteral("群消息设置"), receiveTexts.value(receiveMode), true);
    connect(receiveRow, &QPushButton::clicked, this, [this, receiveRow, stableSettingKey, serverGroup, groupId, chooseSetting, receiveTexts]() {
        const QSettings localSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat"));
        const QString current = serverGroup
            ? m_serverGroupUserSettings.value(groupId).value(QStringLiteral("receiveMode")).toString(QStringLiteral("receive_quiet"))
            : localSettings.value(stableSettingKey() + QStringLiteral("receiveMode"), QStringLiteral("receive_quiet")).toString();
        const QString selected = chooseSetting(QStringLiteral("群消息设置"), QStringLiteral("选择该群消息的接收方式"), {
            {QStringLiteral("接收消息但不提醒"), QStringLiteral("receive_quiet")},
            {QStringLiteral("收进群助手且不提醒"), QStringLiteral("assistant_quiet")},
            {QStringLiteral("屏蔽群消息"), QStringLiteral("block")}
        }, current);
        if (selected.isEmpty() || selected == current) return;
        if (serverGroup && (!m_client || !m_client->sendServerGroupUserSettingsUpdate(
                groupId, QJsonObject{{QStringLiteral("receiveMode"), selected}}))) {
            ui->statusbar->showMessage(QStringLiteral("消息设置保存失败，请检查连接"), 2600);
            return;
        }
        if (!serverGroup) QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(stableSettingKey() + QStringLiteral("receiveMode"), selected);
        receiveRow->findChild<QLabel*>(QStringLiteral("groupInfoRowValue"))->setText(receiveTexts.value(selected));
    });
    const QString pinKey = stableSettingKey() + QStringLiteral("pinned");
    auto* pinToggle = panelUi.addToggleRow(QStringLiteral("设为置顶"),
                                            QStringLiteral("将群聊固定在会话列表顶部"));
    pinToggle->setChecked(settings.value(pinKey, false).toBool());
    connect(pinToggle, &QCheckBox::toggled, this, [this, pinKey](bool enabled) {
        QSettings(QStringLiteral("QtNetworkChat"), QStringLiteral("QtNetworkChat")).setValue(pinKey, enabled);
        refreshFriendList();
        ui->statusbar->showMessage(enabled ? QStringLiteral("已置顶群聊") : QStringLiteral("已取消置顶"), 1800);
    });
}

void MainWindow::addGroupInfoExitAction(QDialog& panel, QVBoxLayout* contentLayout,
                                       const QString& groupId, const QString& groupName,
                                       bool serverGroup, bool localGroup, bool owner)
{
    const QString serverGroupId = groupId;
    auto confirmDanger = [&panel](const QString& title, const QString& detail, const QString& actionText) {
        return confirmGroupDanger(panel, title, detail, actionText);
    };

    if (serverGroup && owner && serverGroupId != QLatin1String("public")) {
        QPushButton* dissolve = new QPushButton(QStringLiteral("解散群聊"), &panel);
        dissolve->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        dissolve->setCursor(Qt::PointingHandCursor);
        dissolve->setFixedHeight(48);
        contentLayout->addWidget(dissolve);
        connect(dissolve, &QPushButton::clicked, this, [this, serverGroupId, groupName, &panel, confirmDanger]() {
            if (confirmDanger(QStringLiteral("解散群聊"),
                              QStringLiteral("确定解散“%1”吗？解散后成员将无法继续访问该群聊，此操作不可撤销。").arg(groupName),
                              QStringLiteral("确认解散"))
                && m_client && m_client->dissolveServerGroup(serverGroupId)) panel.accept();
        });
    } else if (serverGroup && !owner) {
        QPushButton* leave = new QPushButton(QStringLiteral("退出群聊"), &panel);
        leave->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        leave->setCursor(Qt::PointingHandCursor);
        leave->setFixedHeight(48);
        contentLayout->addWidget(leave);
        connect(leave, &QPushButton::clicked, this, [this, serverGroupId, groupName, &panel, confirmDanger]() {
            if (confirmDanger(QStringLiteral("退出群聊"), QStringLiteral("确定退出“%1”吗？退出后将不再接收该群消息。").arg(groupName),
                              QStringLiteral("确认退出"))
                && m_client && m_client->leaveServerGroup(serverGroupId)) panel.accept();
        });
    } else if (localGroup && owner) {
        QPushButton* dissolve = new QPushButton(QStringLiteral("解散本地群聊"), &panel);
        dissolve->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        dissolve->setCursor(Qt::PointingHandCursor);
        dissolve->setFixedHeight(48);
        contentLayout->addWidget(dissolve);
        connect(dissolve, &QPushButton::clicked, this, [this, groupId, groupName, &panel, confirmDanger]() {
            if (!confirmDanger(QStringLiteral("解散本地群聊"),
                               QStringLiteral("确定解散“%1”吗？所有本地成员和聊天记录入口将被移除，此操作不可撤销。").arg(groupName),
                               QStringLiteral("确认解散"))) return;
            m_localGroupIds.removeAll(groupId);
            m_localGroupNames.remove(groupId);
            m_localGroupMembers.remove(groupId);
            m_localGroupAnnouncements.remove(groupId);
            m_localGroupAvatarPaths.remove(groupId);
            saveLocalGroups();
            m_privateChatTarget.clear();
            refreshFriendList();
            panel.accept();
        });
    } else if (localGroup && !owner) {
        QPushButton* leave = new QPushButton(QStringLiteral("退出本地群聊"), &panel);
        leave->setObjectName(QStringLiteral("groupInfoLeaveBtn"));
        leave->setCursor(Qt::PointingHandCursor);
        leave->setFixedHeight(48);
        contentLayout->addWidget(leave);
        connect(leave, &QPushButton::clicked, this, [this, groupId, groupName, &panel, confirmDanger]() {
            if (!confirmDanger(QStringLiteral("退出群聊"), QStringLiteral("确定退出“%1”吗？退出后将不再接收该群消息。").arg(groupName),
                               QStringLiteral("确认退出"))) return;
            m_localGroupMembers[groupId].removeAll(m_currentUserId);
            saveLocalGroups();
            refreshFriendList();
            panel.accept();
        });
    }
}

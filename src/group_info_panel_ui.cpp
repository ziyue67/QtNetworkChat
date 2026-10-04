#include "group_info_panel_ui.h"

#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"

#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

GroupInfoPanelUi::GroupInfoPanelUi(QDialog& panel, QVBoxLayout& contentLayout)
    : m_panel(panel), m_contentLayout(contentLayout) {}

void GroupInfoPanelUi::addCaption(const QString& text) {
    auto* caption = new QLabel(text, &m_panel);
    caption->setObjectName(QStringLiteral("groupInfoCaption"));
    m_contentLayout.addWidget(caption);
    auto* card = new QFrame(&m_panel);
    card->setObjectName(QStringLiteral("groupInfoSettingCard"));
    m_activeSettingsLayout = new QVBoxLayout(card);
    m_activeSettingsLayout->setContentsMargins(0, 0, 0, 0);
    m_activeSettingsLayout->setSpacing(0);
    m_contentLayout.addWidget(card);
}

QPushButton* GroupInfoPanelUi::addRow(const QString& title, const QString& value, bool clickable) {
    auto* row = new QPushButton(&m_panel);
    row->setObjectName(clickable ? QStringLiteral("groupInfoActionRow") : QStringLiteral("groupInfoRow"));
    row->setFlat(true);
    row->setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    row->setFixedHeight(48);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(15, 0, 13, 0);
    auto* label = new QLabel(title, row);
    label->setObjectName(QStringLiteral("groupInfoRowTitle"));
    layout->addWidget(label);
    layout->addStretch();
    if (!value.isEmpty()) {
        auto* detail = new QLabel(value, row);
        detail->setObjectName(QStringLiteral("groupInfoRowValue"));
        layout->addWidget(detail);
    }
    if (clickable) {
        auto* arrow = new QLabel(QStringLiteral("›"), row);
        arrow->setObjectName(QStringLiteral("groupInfoArrow"));
        layout->addWidget(arrow);
    }
    if (m_activeSettingsLayout) m_activeSettingsLayout->addWidget(row);
    else m_contentLayout.addWidget(row);
    return row;
}

QCheckBox* GroupInfoPanelUi::addToggleRow(const QString& title, const QString& toolTip, int height) {
    auto* row = new QFrame(&m_panel);
    row->setObjectName(QStringLiteral("groupInfoRow"));
    row->setFixedHeight(height);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(height == 48 ? 15 : 12, 0, height == 48 ? 13 : 12, 0);
    auto* label = new QLabel(title, row);
    label->setObjectName(QStringLiteral("groupInfoRowTitle"));
    auto* toggle = new QCheckBox(row);
    toggle->setToolTip(toolTip);
    layout->addWidget(label);
    layout->addStretch();
    layout->addWidget(toggle);
    if (m_activeSettingsLayout) m_activeSettingsLayout->addWidget(row);
    else m_contentLayout.addWidget(row);
    return toggle;
}

GroupInfoOverviewWidgets GroupInfoPanelUi::addOverview(const QString& groupId,
                                                       const QString& groupName,
                                                       int memberCount, bool serverGroup,
                                                       bool manager, const QPixmap& avatarPixmap) {
    auto* overview = new QFrame(&m_panel);
    overview->setObjectName(QStringLiteral("groupInfoOverview"));
    auto* layout = new QHBoxLayout(overview);
    layout->setContentsMargins(15, 15, 12, 15);
    layout->setSpacing(12);
    auto* avatarButton = new QToolButton(overview);
    avatarButton->setObjectName(QStringLiteral("groupInfoAvatarButton"));
    avatarButton->setFixedSize(56, 56);
    avatarButton->setCursor(manager ? Qt::PointingHandCursor : Qt::ArrowCursor);
    avatarButton->setToolTip(manager ? QStringLiteral("点击从本地选择群头像")
                                     : QStringLiteral("群头像"));
    avatarButton->setEnabled(manager);
    auto* avatar = new AvatarLabel(avatarButton, 56);
    avatar->setAttribute(Qt::WA_TransparentForMouseEvents);
    if (avatarPixmap.isNull()) avatar->setTextAvatar(groupName, ThemeManager::instance()->primaryColor());
    else avatar->setPixmap(avatarPixmap);
    layout->addWidget(avatarButton);

    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(3);
    auto* name = new QLabel(groupName, overview);
    name->setObjectName(QStringLiteral("groupInfoName"));
    name->setWordWrap(true);
    const QString visibleNumber = serverGroup ? groupId
        : groupId.mid(QStringLiteral("local_group_").size());
    auto* meta = new QLabel(QStringLiteral("群号 %1  ·  %2 位成员")
                                .arg(visibleNumber).arg(memberCount), overview);
    meta->setObjectName(QStringLiteral("groupInfoMeta"));
    textLayout->addWidget(name);
    textLayout->addWidget(meta);
    layout->addLayout(textLayout, 1);

    auto* share = new QToolButton(overview);
    share->setObjectName(QStringLiteral("groupInfoShare"));
    share->setText(QStringLiteral("↗"));
    share->setToolTip(QStringLiteral("复制群号"));
    share->setAccessibleName(QStringLiteral("复制群号"));
    share->setFixedSize(30, 30);
    layout->addWidget(share);
    m_contentLayout.addWidget(overview);
    return {avatarButton, avatar, share};
}

QPushButton* GroupInfoPanelUi::addAnnouncement(const QString& announcement, bool manager) {
    auto* card = new QFrame(&m_panel);
    card->setObjectName(QStringLiteral("groupInfoAnnouncementCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);
    auto* header = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("群公告"), card);
    title->setObjectName(QStringLiteral("groupInfoAnnouncementTitle"));
    header->addWidget(title);
    header->addStretch();
    QPushButton* edit = nullptr;
    if (manager) {
        edit = new QPushButton(QStringLiteral("编辑"), card);
        edit->setObjectName(QStringLiteral("groupInfoLinkButton"));
        edit->setToolTip(QStringLiteral("编辑群公告"));
        header->addWidget(edit);
    }
    layout->addLayout(header);
    auto* body = new QLabel(announcement.isEmpty() ? QStringLiteral("暂无群公告") : announcement, card);
    body->setObjectName(QStringLiteral("groupInfoAnnouncementBody"));
    body->setWordWrap(true);
    body->setMaximumHeight(62);
    layout->addWidget(body);
    m_contentLayout.addWidget(card);
    return edit;
}

void GroupInfoPanelUi::applyStyle(QDialog& panel) {
    panel.setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#groupInfoDialog,QScrollArea#groupInfoScroll,QWidget#groupInfoContent { background:%1; }"
        "QScrollArea#groupInfoScroll { border:none; }"
        "QLabel#groupInfoPanelTitle { color:%4; font-size:15px; font-weight:600; }"
        "QLabel#groupInfoName { color:%4; font-size:17px; font-weight:600; }"
        "QLabel#groupInfoRowTitle,QLabel#groupInfoMemberTitle,QLabel#groupInfoAnnouncementTitle { color:%4; font-size:13px; font-weight:500; }"
        "QFrame#groupInfoOverview,QFrame#groupInfoAnnouncementCard,QFrame#groupInfoMemberCard,QFrame#groupInfoSettingCard { background:%2; border:1px solid %3; border-radius:8px; }"
        "QFrame#groupInfoRow,QPushButton#groupInfoRow,QPushButton#groupInfoActionRow { background:transparent; border:none; border-bottom:1px solid %3; border-radius:0; text-align:left; }"
        "QPushButton#groupInfoActionRow:hover { background:%5; }"
        "QPushButton#groupInfoActionRow:pressed { background:%8; }"
        "QPushButton#groupInfoActionRow:focus { border:1px solid %7; }"
        "QLabel#groupInfoMeta,QLabel#groupInfoRowValue,QLabel#groupInfoCaption,QLabel#groupInfoMemberName,QLabel#groupInfoAnnouncementBody { color:%6; font-size:12px; }"
        "QLabel#groupInfoAnnouncementBody { line-height:1.45; padding-top:2px; }"
        "QLabel#groupInfoRowValue { max-width:156px; color:%6; }"
        "QLabel#groupInfoCaption { color:%6; font-size:11px; font-weight:500; padding:14px 4px 4px 4px; }"
        "QLabel#groupInfoArrow { color:%6; font-size:20px; font-weight:400; padding-left:6px; }"
        "QLabel#groupInfoRoleManage { color:%7; background:%5; border-radius:3px; padding:0 6px; font-size:11px; }"
        "QLabel#groupInfoRoleMember { color:%6; background:%8; border-radius:3px; padding:0 6px; font-size:11px; }"
        "QToolButton#groupInfoAvatarButton { background:transparent; border:1px solid transparent; border-radius:28px; padding:0; }"
        "QToolButton#groupInfoAvatarButton:hover { border-color:%7; background:%5; }"
        "QToolButton#groupInfoAvatarButton:disabled { border-color:transparent; background:transparent; }"
        "QToolButton#groupInfoClose,QToolButton#groupInfoShare { color:%6; border:none; border-radius:4px; font-size:18px; }"
        "QToolButton#groupInfoClose:hover,QToolButton#groupInfoShare:hover { background:%5; color:%4; }"
        "QToolButton#groupInfoMemberAction { color:%7; background:%5; border:1px dashed %7; border-radius:17px; font-size:20px; }"
        "QToolButton#groupInfoMemberAction:hover { background:%7; color:%2; }"
        "QPushButton#groupInfoLinkButton { color:%7; background:transparent; border:none; font-size:12px; padding:3px 0; }"
        "QPushButton#groupInfoLinkButton:hover { color:%9; }"
        "QCheckBox { spacing:7px; }"
        "QCheckBox::indicator { width:30px; height:18px; border-radius:9px; background:%8; border:1px solid %3; image:none; }"
        "QCheckBox::indicator:checked { background:%7; border-color:%7; }"
        "QCheckBox::indicator:checked:disabled { background:%6; border-color:%6; }"
        "QPushButton#groupInfoLeaveBtn { background:%2; color:%10; border:1px solid %3; border-radius:8px; margin-top:18px; padding:0 18px; font-size:14px; font-weight:600; }"
        "QPushButton#groupInfoLeaveBtn:hover { background:%11; border-color:%10; }")
        .arg(ThemeManager::instance()->backgroundColor().name(), ThemeManager::instance()->backgroundSecondaryColor().name(),
             ThemeManager::instance()->borderColor().name(), ThemeManager::instance()->textColor().name(),
             ThemeManager::instance()->primarySoftColor().name(), ThemeManager::instance()->textSecondaryColor().name(),
             ThemeManager::instance()->primaryColor().name(), ThemeManager::instance()->backgroundTertiaryColor().name(),
             ThemeManager::instance()->primaryHoverColor().name(), ThemeManager::instance()->dangerColor().name(),
             ThemeManager::instance()->dangerColor().lighter(185).name()));
}

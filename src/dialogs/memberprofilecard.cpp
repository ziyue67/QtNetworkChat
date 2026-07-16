#include "dialogs/memberprofilecard.h"

#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MemberProfileCard::MemberProfileCard(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("memberProfileCard"));
    setWindowTitle(QStringLiteral("成员资料"));
    setMinimumSize(320, 280);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &MemberProfileCard::updateStyle);
}

void MemberProfileCard::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    // Avatar
    QHBoxLayout* avatarLayout = new QHBoxLayout();
    avatarLayout->addStretch();
    m_avatar = new AvatarLabel(this, 64);
    m_avatar->setTextAvatar(QStringLiteral("U"), QColor(QStringLiteral("#0099ff")));
    avatarLayout->addWidget(m_avatar);
    avatarLayout->addStretch();
    root->addLayout(avatarLayout);

    // Name
    m_nameLabel = new QLabel(QStringLiteral("用户名"), this);
    m_nameLabel->setObjectName(QStringLiteral("profileNameLabel"));
    m_nameLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_nameLabel);

    // ID
    m_idLabel = new QLabel(QStringLiteral("QQ: 000000"), this);
    m_idLabel->setObjectName(QStringLiteral("profileIdLabel"));
    m_idLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_idLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("profileStatusLabel"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_statusLabel);

    // Role
    m_roleLabel = new QLabel(QStringLiteral("身份: 成员"), this);
    m_roleLabel->setObjectName(QStringLiteral("profileRoleLabel"));
    m_roleLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_roleLabel);

    // Join date
    m_joinDateLabel = new QLabel(QStringLiteral("加入时间: --"), this);
    m_joinDateLabel->setObjectName(QStringLiteral("profileDateLabel"));
    m_joinDateLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_joinDateLabel);

    root->addSpacing(8);

    // Actions
    QHBoxLayout* actionLayout = new QHBoxLayout();
    m_messageBtn = new QPushButton(QStringLiteral("发消息"), this);
    m_messageBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_messageBtn, &QPushButton::clicked, this, [this]() {
        emit sendMessageRequested(m_currentUserId);
        accept();
    });
    actionLayout->addWidget(m_messageBtn);

    m_friendBtn = new QPushButton(QStringLiteral("加好友"), this);
    m_friendBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(m_friendBtn, &QPushButton::clicked, this, [this]() {
        emit addFriendRequested(m_currentUserId);
        accept();
    });
    actionLayout->addWidget(m_friendBtn);
    root->addLayout(actionLayout);

    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(closeBtn);
}

void MemberProfileCard::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#memberProfileCard { background-color: %1; }"
        "QLabel#profileNameLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QLabel#profileIdLabel { color: %3; font-size: 13px; }"
        "QLabel#profileStatusLabel { color: %8; font-size: 13px; font-weight: 600; }"
        "QLabel#profileRoleLabel { color: %3; font-size: 13px; }"
        "QLabel#profileDateLabel { color: %3; font-size: 13px; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->primaryHoverColor().name())
     .arg(tm->successColor().name()));
}

void MemberProfileCard::setMemberInfo(const QString& userId, const QString& userName, const QString& role, const QString& joinDate, bool online)
{
    m_currentUserId = userId;
    m_nameLabel->setText(userName);
    m_idLabel->setText(QStringLiteral("QQ: %1").arg(userId));
    setOnlineStatus(online);
    m_avatar->setTextAvatar(userName.left(1).toUpper(), QColor(QStringLiteral("#0099ff")));
    if (!role.isEmpty()) {
        m_roleLabel->setText(QStringLiteral("身份: %1").arg(role));
    }
    if (!joinDate.isEmpty()) {
        m_joinDateLabel->setText(QStringLiteral("加入时间: %1").arg(joinDate));
    }
}

void MemberProfileCard::setOnlineStatus(bool online)
{
    if (!m_statusLabel) return;
    m_statusLabel->setText(online ? QStringLiteral("● 在线") : QStringLiteral("● 离线"));
    m_statusLabel->setProperty("online", online);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;")
        .arg(online ? ThemeManager::instance()->successColor().name()
                    : ThemeManager::instance()->textSecondaryColor().name()));
}


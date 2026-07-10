#include "views/profileview.h"

#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ProfileView::ProfileView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("profileView"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ProfileView::updateStyle);
}

void ProfileView::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    // Avatar section
    QHBoxLayout* avatarLayout = new QHBoxLayout();
    avatarLayout->setSpacing(16);

    m_avatar = new AvatarLabel(this, 80);
    m_avatar->setTextAvatar(QStringLiteral("Q"), QColor(QStringLiteral("#0099ff")));
    avatarLayout->addWidget(m_avatar);

    QVBoxLayout* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);

    m_nameLabel = new QLabel(QStringLiteral("用户名"), this);
    m_nameLabel->setObjectName(QStringLiteral("profileNameLabel"));
    infoLayout->addWidget(m_nameLabel);

    m_idLabel = new QLabel(QStringLiteral("QQ: 000000"), this);
    m_idLabel->setObjectName(QStringLiteral("profileIdLabel"));
    infoLayout->addWidget(m_idLabel);

    m_signatureLabel = new QLabel(QStringLiteral("这个人很懒，什么都没有留下~"), this);
    m_signatureLabel->setObjectName(QStringLiteral("profileSignatureLabel"));
    m_signatureLabel->setWordWrap(true);
    infoLayout->addWidget(m_signatureLabel);

    avatarLayout->addLayout(infoLayout, 1);
    root->addLayout(avatarLayout);

    // Stats section
    QHBoxLayout* statsLayout = new QHBoxLayout();
    statsLayout->setSpacing(24);

    m_friendCountLabel = new QLabel(QStringLiteral("好友: 0"), this);
    m_friendCountLabel->setObjectName(QStringLiteral("profileStatLabel"));
    statsLayout->addWidget(m_friendCountLabel);

    m_groupCountLabel = new QLabel(QStringLiteral("群聊: 0"), this);
    m_groupCountLabel->setObjectName(QStringLiteral("profileStatLabel"));
    statsLayout->addWidget(m_groupCountLabel);

    m_messageCountLabel = new QLabel(QStringLiteral("消息: 0"), this);
    m_messageCountLabel->setObjectName(QStringLiteral("profileStatLabel"));
    statsLayout->addWidget(m_messageCountLabel);

    statsLayout->addStretch();
    root->addLayout(statsLayout);

    // Actions
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(8);

    m_editBtn = new QPushButton(QStringLiteral("编辑资料"), this);
    m_editBtn->setObjectName(QStringLiteral("profileActionBtn"));
    connect(m_editBtn, &QPushButton::clicked, this, &ProfileView::editProfileRequested);
    actionLayout->addWidget(m_editBtn);

    m_avatarBtn = new QPushButton(QStringLiteral("更换头像"), this);
    m_avatarBtn->setObjectName(QStringLiteral("profileActionBtn"));
    connect(m_avatarBtn, &QPushButton::clicked, this, &ProfileView::changeAvatarRequested);
    actionLayout->addWidget(m_avatarBtn);

    m_logoutBtn = new QPushButton(QStringLiteral("退出登录"), this);
    m_logoutBtn->setObjectName(QStringLiteral("profileLogoutBtn"));
    connect(m_logoutBtn, &QPushButton::clicked, this, &ProfileView::logoutRequested);
    actionLayout->addWidget(m_logoutBtn);

    actionLayout->addStretch();
    root->addLayout(actionLayout);

    root->addStretch();
}

void ProfileView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QLabel#profileNameLabel { color: %1; font-size: 20px; font-weight: 600; }"
        "QLabel#profileIdLabel { color: %2; font-size: 13px; }"
        "QLabel#profileSignatureLabel { color: %2; font-size: 13px; }"
        "QLabel#profileStatLabel { color: %3; font-size: 14px; font-weight: 500; }"
        "QPushButton#profileActionBtn { background-color: %4; color: %1; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#profileActionBtn:hover { background-color: %6; }"
        "QPushButton#profileLogoutBtn { background-color: %7; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#profileLogoutBtn:hover { background-color: %8; }"
    ).arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->textColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->backgroundTertiaryColor().name())
     .arg(tm->dangerColor().name())
     .arg(tm->dangerColor().lighter(120).name()));
}

void ProfileView::setUserInfo(const QString& userId, const QString& userName, const QString& signature)
{
    m_nameLabel->setText(userName);
    m_idLabel->setText(QStringLiteral("QQ: %1").arg(userId));
    m_avatar->setTextAvatar(userName.left(1).toUpper(), QColor(QStringLiteral("#0099ff")));
    if (!signature.isEmpty()) {
        m_signatureLabel->setText(signature);
    }
}

void ProfileView::setStats(int friendCount, int groupCount, int messageCount)
{
    m_friendCountLabel->setText(QStringLiteral("好友: %1").arg(friendCount));
    m_groupCountLabel->setText(QStringLiteral("群聊: %1").arg(groupCount));
    m_messageCountLabel->setText(QStringLiteral("消息: %1").arg(messageCount));
}


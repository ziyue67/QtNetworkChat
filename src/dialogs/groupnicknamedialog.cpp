#include "dialogs/groupnicknamedialog.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

GroupNicknameDialog::GroupNicknameDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("groupNicknameDialog"));
    setWindowTitle(QStringLiteral("设置群昵称"));
    setMinimumSize(300, 180);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &GroupNicknameDialog::updateStyle);
}

void GroupNicknameDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    QLabel* title = new QLabel(QStringLiteral("设置群昵称"), this);
    title->setObjectName(QStringLiteral("dialogTitleLabel"));
    root->addWidget(title);

    m_nicknameEdit = new QLineEdit(this);
    m_nicknameEdit->setObjectName(QStringLiteral("dialogInput"));
    m_nicknameEdit->setPlaceholderText(QStringLiteral("输入群昵称..."));
    root->addWidget(m_nicknameEdit);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_confirmBtn = new QPushButton(QStringLiteral("确认"), this);
    m_confirmBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_confirmBtn, &QPushButton::clicked, this, [this]() {
        if (!m_nicknameEdit->text().isEmpty()) {
            emit nicknameConfirmed(m_nicknameEdit->text());
            accept();
        }
    });
    btnLayout->addWidget(m_confirmBtn);

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    root->addLayout(btnLayout);
}

void GroupNicknameDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#groupNicknameDialog { background-color: %1; }"
        "QLabel#dialogTitleLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QLineEdit#dialogInput { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %6; }"
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
     .arg(tm->primaryHoverColor().name()));
}

QString GroupNicknameDialog::nickname() const
{
    return m_nicknameEdit->text();
}

void GroupNicknameDialog::setCurrentNickname(const QString& nickname)
{
    m_nicknameEdit->setText(nickname);
}


#include "dialogs/addfrienddialog.h"

#include "theme/thememanager.h"
#include "theme/dialogstyle.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

AddFriendDialog::AddFriendDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("addFriendDialog"));
    setWindowTitle(QStringLiteral("添加好友"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setFixedSize(420, 312);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &AddFriendDialog::updateStyle);
}

void AddFriendDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    DialogTitleBar* titleBar = new DialogTitleBar(this, QStringLiteral("添加好友"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(22, 18, 22, 20);
    bodyLayout->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral("搜索 QQ 号或昵称，确认资料后发送好友申请"), body);
    intro->setObjectName(QStringLiteral("addFriendIntro"));
    bodyLayout->addWidget(intro);

    // Search row: input + primary search button (matches AddFriendModal).
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->setSpacing(8);
    m_searchEdit = new QLineEdit(body);
    m_searchEdit->setObjectName(QStringLiteral("dialogInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("输入 QQ 号 / 昵称"));
    m_searchEdit->setClearButtonEnabled(true);
    searchLayout->addWidget(m_searchEdit, 1);

    m_searchBtn = new QPushButton(QStringLiteral("搜索"), body);
    m_searchBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setFixedSize(72, 36);
    searchLayout->addWidget(m_searchBtn);
    bodyLayout->addLayout(searchLayout);

    auto triggerSearch = [this]() {
        const QString text = m_searchEdit->text().trimmed();
        if (text.isEmpty()) {
            return;
        }
        setLoading(true);
        m_errorLabel->clear();
        m_errorLabel->setVisible(false);
        m_resultCard->setVisible(false);
        emit searchRequested(text);
    };
    connect(m_searchBtn, &QPushButton::clicked, this, triggerSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, triggerSearch);

    // Error / hint line.
    m_errorLabel = new QLabel(body);
    m_errorLabel->setObjectName(QStringLiteral("dialogErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    bodyLayout->addWidget(m_errorLabel);

    // Result card: avatar + name/signature + add button.
    m_resultCard = new QFrame(body);
    m_resultCard->setObjectName(QStringLiteral("addFriendResultCard"));
    m_resultCard->setVisible(false);
    QHBoxLayout* cardLayout = new QHBoxLayout(m_resultCard);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(12);

    m_resultAvatar = new AvatarLabel(m_resultCard, 44);
    cardLayout->addWidget(m_resultAvatar, 0, Qt::AlignVCenter);

    QVBoxLayout* infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(2);
    m_resultName = new QLabel(m_resultCard);
    m_resultName->setObjectName(QStringLiteral("addFriendResultName"));
    m_resultDesc = new QLabel(m_resultCard);
    m_resultDesc->setObjectName(QStringLiteral("addFriendResultDesc"));
    infoLayout->addWidget(m_resultName);
    infoLayout->addWidget(m_resultDesc);
    cardLayout->addLayout(infoLayout, 1);

    m_addBtn = new QPushButton(QStringLiteral("加好友"), m_resultCard);
    m_addBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentResultId.isEmpty() && !m_added) {
            emit addFriendRequested(m_currentResultId);
            setAdded();
        }
    });
    cardLayout->addWidget(m_addBtn, 0, Qt::AlignVCenter);
    bodyLayout->addWidget(m_resultCard);

    bodyLayout->addStretch(1);

    root->addWidget(body, 1);
}

void AddFriendDialog::setLoading(bool loading)
{
    m_searchBtn->setEnabled(!loading);
    m_searchBtn->setText(loading ? QStringLiteral("搜索中…") : QStringLiteral("搜索"));
    m_searchEdit->setEnabled(!loading);
}

void AddFriendDialog::setAdded(const QString& message)
{
    m_added = true;
    m_addBtn->setEnabled(false);
    m_addBtn->setText(QStringLiteral("已发送"));
    m_addBtn->setProperty("state", QStringLiteral("success"));
    // Re-polish so the success style applies immediately.
    m_addBtn->style()->unpolish(m_addBtn);
    m_addBtn->style()->polish(m_addBtn);
    m_errorLabel->setText(message.isEmpty() ? QStringLiteral("好友申请已发送，等待对方确认") : message);
    m_errorLabel->setProperty("state", QStringLiteral("success"));
    m_errorLabel->setVisible(true);
    m_errorLabel->style()->unpolish(m_errorLabel);
    m_errorLabel->style()->polish(m_errorLabel);
}

void AddFriendDialog::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->setProperty("state", QStringLiteral("error"));
    m_errorLabel->setVisible(!message.isEmpty());
    m_errorLabel->style()->unpolish(m_errorLabel);
    m_errorLabel->style()->polish(m_errorLabel);
}

void AddFriendDialog::setRequestOutcome(bool sent, const QString& message)
{
    if (sent) {
        setAdded(message);
    } else {
        m_addBtn->setEnabled(true);
        showError(message.isEmpty() ? QStringLiteral("好友申请发送失败，请稍后重试") : message);
    }
}

void AddFriendDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    // Shared input/button/list rules come from DialogStyle::common(); only the
    // add-friend-specific selectors (dialog background, error line, result card)
    // are defined here.
    setStyleSheet(DialogStyle::common() + QStringLiteral(
        "QDialog#addFriendDialog { background-color: %1; border: 1px solid %3; border-radius: 8px; }"
        "QLabel#addFriendIntro { color: %6; font-size: 12px; }"
        "QLabel#dialogErrorLabel { color: %5; font-size: 12px; }"
        "QLabel#dialogErrorLabel[state=\"success\"] { color: %7; }"
        "QFrame#addFriendResultCard { background-color: %2; border: 1px solid %3; border-radius: 8px; }"
        "QLabel#addFriendResultName { color: %4; font-size: 14px; font-weight: 600; }"
        "QLabel#addFriendResultDesc { color: %6; font-size: 12px; }"
    ).arg(tm->backgroundColor().name())            // %1
     .arg(tm->backgroundSecondaryColor().name())    // %2
     .arg(tm->borderColor().name())                 // %3
     .arg(tm->textColor().name())                   // %4
     .arg(tm->dangerColor().name())                 // %5
     .arg(tm->textSecondaryColor().name())          // %6
     .arg(tm->successColor().name()));               // %7
}

QString AddFriendDialog::searchText() const
{
    return m_searchEdit->text();
}

void AddFriendDialog::onSearchResult(const QString& account, const QString& userId, const QString& userName, bool found)
{
    setLoading(false);
    if (found) {
        const QString displayName = userName.isEmpty() ? account : userName;
        m_currentResultId = userId;
        m_added = false;
        m_resultName->setText(displayName);
        m_resultDesc->setText(QStringLiteral("QQ: %1").arg(account));
        m_resultAvatar->setTextAvatar(displayName.left(1).toUpper(),
                                      ThemeManager::instance()->primaryColor());
        m_addBtn->setEnabled(true);
        m_addBtn->setText(QStringLiteral("加好友"));
        m_addBtn->setProperty("state", QString());
        m_addBtn->style()->unpolish(m_addBtn);
        m_addBtn->style()->polish(m_addBtn);
        m_errorLabel->clear();
        m_errorLabel->setProperty("state", QString());
        m_errorLabel->setVisible(false);
        m_resultCard->setVisible(true);
    } else {
        m_currentResultId.clear();
        m_resultCard->setVisible(false);
        showError(QStringLiteral("未找到用户，请确认 QQ 号或昵称后重试"));
    }
}

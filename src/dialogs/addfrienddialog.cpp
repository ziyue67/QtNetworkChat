#include "dialogs/addfrienddialog.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

AddFriendDialog::AddFriendDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("addFriendDialog"));
    setWindowTitle(QStringLiteral("添加好友"));
    setMinimumSize(400, 300);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &AddFriendDialog::updateStyle);
}

void AddFriendDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    QLabel* title = new QLabel(QStringLiteral("添加好友"), this);
    title->setObjectName(QStringLiteral("dialogTitleLabel"));
    root->addWidget(title);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("dialogInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("输入QQ号或昵称搜索..."));
    searchLayout->addWidget(m_searchEdit, 1);

    m_searchBtn = new QPushButton(QStringLiteral("搜索"), this);
    m_searchBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() {
        emit searchRequested(m_searchEdit->text());
    });
    searchLayout->addWidget(m_searchBtn);
    root->addLayout(searchLayout);

    m_resultLabel = new QLabel(QStringLiteral("搜索结果将显示在这里"), this);
    m_resultLabel->setObjectName(QStringLiteral("dialogHintLabel"));
    m_resultLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_resultLabel);

    m_resultModel = new QStandardItemModel(this);
    m_resultList = new QListView(this);
    m_resultList->setObjectName(QStringLiteral("dialogListView"));
    m_resultList->setModel(m_resultModel);
    root->addWidget(m_resultList, 1);

    m_addBtn = new QPushButton(QStringLiteral("添加好友"), this);
    m_addBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    m_addBtn->setEnabled(false);
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentResultId.isEmpty()) {
            emit addFriendRequested(m_currentResultId);
            accept();
        }
    });
    root->addWidget(m_addBtn);

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(cancelBtn);
}

void AddFriendDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#addFriendDialog { background-color: %1; }"
        "QLabel#dialogTitleLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QLabel#dialogHintLabel { color: %3; font-size: 13px; }"
        "QLineEdit#dialogInput { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %6; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#dialogPrimaryBtn:disabled { background-color: %5; color: %3; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
        "QListView#dialogListView { background-color: %4; border: 1px solid %5; border-radius: 6px; color: %2; }"
        "QListView#dialogListView::item { padding: 8px 12px; }"
        "QListView#dialogListView::item:selected { background-color: %8; color: %2; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->primaryHoverColor().name())
     .arg(tm->primarySoftColor().name()));
}

QString AddFriendDialog::searchText() const
{
    return m_searchEdit->text();
}

void AddFriendDialog::onSearchResult(const QString& account, const QString& userId, const QString& userName, bool found)
{
    m_resultModel->clear();
    if (found) {
        QStandardItem* item = new QStandardItem(QStringLiteral("%1 (%2)").arg(userName).arg(account));
        item->setData(userId, Qt::UserRole);
        m_resultModel->appendRow(item);
        m_resultLabel->setText(QStringLiteral("找到用户"));
        m_currentResultId = userId;
        m_addBtn->setEnabled(true);
    } else {
        m_resultLabel->setText(QStringLiteral("未找到用户"));
        m_currentResultId.clear();
        m_addBtn->setEnabled(false);
    }
}


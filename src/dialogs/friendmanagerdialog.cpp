#include "dialogs/friendmanagerdialog.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

FriendManagerDialog::FriendManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("friendManagerDialog"));
    setWindowTitle(QStringLiteral("好友管理"));
    setMinimumSize(400, 400);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &FriendManagerDialog::updateStyle);
}

void FriendManagerDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    QLabel* title = new QLabel(QStringLiteral("好友管理"), this);
    title->setObjectName(QStringLiteral("dialogTitleLabel"));
    root->addWidget(title);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("dialogInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索好友..."));
    root->addWidget(m_searchEdit);

    m_model = new QStandardItemModel(this);
    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("dialogListView"));
    m_listView->setModel(m_model);
    root->addWidget(m_listView, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(QStringLiteral("添加好友"), this);
    m_addBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_addBtn, &QPushButton::clicked, this, &FriendManagerDialog::addFriendRequested);
    btnLayout->addWidget(m_addBtn);

    m_deleteBtn = new QPushButton(QStringLiteral("删除选中"), this);
    m_deleteBtn->setObjectName(QStringLiteral("dialogDangerBtn"));
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        QModelIndex idx = m_listView->currentIndex();
        if (idx.isValid()) {
            emit deleteFriendRequested(idx.data(Qt::UserRole).toString());
            m_model->removeRow(idx.row());
        }
    });
    btnLayout->addWidget(m_deleteBtn);

    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);
    root->addLayout(btnLayout);
}

void FriendManagerDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#friendManagerDialog { background-color: %1; }"
        "QLabel#dialogTitleLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QLineEdit#dialogInput { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %6; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
        "QPushButton#dialogDangerBtn { background-color: %8; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogDangerBtn:hover { background-color: %9; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
        "QListView#dialogListView { background-color: %4; border: 1px solid %5; border-radius: 6px; color: %2; }"
        "QListView#dialogListView::item { padding: 8px 12px; }"
        "QListView#dialogListView::item:selected { background-color: %10; color: %2; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->primaryHoverColor().name())
     .arg(tm->dangerColor().name())
     .arg(tm->dangerColor().lighter(120).name())
     .arg(tm->primarySoftColor().name()));
}

void FriendManagerDialog::setFriendList(const QStringList& friendIds, const QMap<QString, QString>& friendNames)
{
    m_model->clear();
    for (const QString& id : friendIds) {
        QStandardItem* item = new QStandardItem(friendNames.value(id, id));
        item->setData(id, Qt::UserRole);
        m_model->appendRow(item);
    }
}


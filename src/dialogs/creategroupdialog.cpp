#include "dialogs/creategroupdialog.h"

#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

CreateGroupDialog::CreateGroupDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("createGroupDialog"));
    setWindowTitle(QStringLiteral("创建群聊"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(400, 350);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &CreateGroupDialog::updateStyle);
}

void CreateGroupDialog::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    DialogTitleBar* titleBar = new DialogTitleBar(this, QStringLiteral("创建群聊"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(20, 20, 20, 20);
    bodyLayout->setSpacing(12);

    m_nameEdit = new QLineEdit(body);
    m_nameEdit->setObjectName(QStringLiteral("dialogInput"));
    m_nameEdit->setPlaceholderText(QStringLiteral("群聊名称"));
    bodyLayout->addWidget(m_nameEdit);

    m_hintLabel = new QLabel(QStringLiteral("选择群成员:"), body);
    m_hintLabel->setObjectName(QStringLiteral("dialogHintLabel"));
    bodyLayout->addWidget(m_hintLabel);

    m_memberModel = new QStandardItemModel(this);
    m_memberList = new QListView(body);
    m_memberList->setObjectName(QStringLiteral("dialogListView"));
    m_memberList->setModel(m_memberModel);
    m_memberList->setSelectionMode(QAbstractItemView::MultiSelection);
    bodyLayout->addWidget(m_memberList, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_createBtn = new QPushButton(QStringLiteral("创建"), body);
    m_createBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_createBtn, &QPushButton::clicked, this, [this]() {
        if (!m_nameEdit->text().isEmpty()) {
            emit createRequested(m_nameEdit->text(), selectedMembers());
            accept();
        }
    });
    btnLayout->addWidget(m_createBtn);

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), body);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    bodyLayout->addLayout(btnLayout);

    root->addWidget(body, 1);
}

void CreateGroupDialog::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#createGroupDialog { background-color: %1; }"
        "QLabel#dialogHintLabel { color: %3; font-size: 13px; }"
        "QLineEdit#dialogInput { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#dialogInput:focus { border: 1px solid %6; }"
        "QPushButton#dialogPrimaryBtn { background-color: %6; color: white; border: none; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %7; }"
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

QString CreateGroupDialog::groupName() const
{
    return m_nameEdit->text();
}

QStringList CreateGroupDialog::selectedMembers() const
{
    QStringList members;
    for (const QModelIndex& idx : m_memberList->selectionModel()->selectedIndexes()) {
        members.append(idx.data(Qt::UserRole).toString());
    }
    return members;
}


#include "dialogs/essencepanel.h"

#include "theme/thememanager.h"

#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

EssencePanel::EssencePanel(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("essencePanel"));
    setWindowTitle(QStringLiteral("精华消息"));
    setMinimumSize(450, 400);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &EssencePanel::updateStyle);
}

void EssencePanel::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    m_titleLabel = new QLabel(QStringLiteral("精华消息"), this);
    m_titleLabel->setObjectName(QStringLiteral("dialogTitleLabel"));
    root->addWidget(m_titleLabel);

    m_model = new QStandardItemModel(this);
    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("dialogListView"));
    m_listView->setModel(m_model);
    root->addWidget(m_listView, 1);

    connect(m_listView, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            emit messageActivated(index.data(Qt::UserRole).toString());
        }
    });

    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    root->addWidget(closeBtn);
}

void EssencePanel::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#essencePanel { background-color: %1; }"
        "QLabel#dialogTitleLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
        "QListView#dialogListView { background-color: %4; border: 1px solid %5; border-radius: 6px; color: %2; }"
        "QListView#dialogListView::item { padding: 10px 12px; }"
        "QListView#dialogListView::item:selected { background-color: %6; color: %2; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primarySoftColor().name()));
}

void EssencePanel::addEssenceMessage(const QString& messageId, const QString& sender, const QString& text, const QString& time)
{
    QStandardItem* item = new QStandardItem(QStringLiteral("[%1] %2: %3").arg(time, sender, text));
    item->setData(messageId, Qt::UserRole);
    m_model->appendRow(item);
}

void EssencePanel::clearMessages()
{
    m_model->clear();
}


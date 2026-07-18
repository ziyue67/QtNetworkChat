#include "dialogs/essencepanel.h"

#include "theme/thememanager.h"

#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

EssencePanel::EssencePanel(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("essencePanel"));
    setWindowTitle(QStringLiteral("精华消息"));
    setMinimumSize(440, 360);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &EssencePanel::updateStyle);
}

void EssencePanel::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    m_titleLabel = new QLabel(QStringLiteral("精华消息"), this);
    m_titleLabel->setObjectName(QStringLiteral("dialogTitleLabel"));
    root->addWidget(m_titleLabel);

    auto* hint = new QLabel(QStringLiteral("群聊中被标记的重要消息"), this);
    hint->setObjectName(QStringLiteral("essencePanelHint"));
    root->addWidget(hint);

    m_model = new QStandardItemModel(this);
    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("dialogListView"));
    m_listView->setModel(m_model);
    m_listView->setSpacing(4);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_listView, 1);

    connect(m_listView, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            emit messageActivated(index.data(Qt::UserRole).toString());
        }
    });
    connect(m_listView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = m_listView->indexAt(pos);
        if (!index.isValid()) return;
        const QString messageId = index.data(Qt::UserRole).toString();
        if (messageId.isEmpty()) return;
        QMenu menu(m_listView);
        menu.setObjectName(QStringLiteral("essenceContextMenu"));
        QAction* locate = menu.addAction(QStringLiteral("定位原消息"));
        QAction* remove = menu.addAction(QStringLiteral("取消精华"));
        QAction* selected = menu.exec(m_listView->viewport()->mapToGlobal(pos));
        if (selected == locate) {
            emit messageActivated(messageId);
        } else if (selected == remove) {
            emit messageRemovalRequested(messageId);
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
        "QLabel#dialogTitleLabel { color: %2; font-size: 17px; font-weight: 600; }"
        "QLabel#essencePanelHint { color: %3; font-size: 12px; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 8px 16px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %5; }"
        "QListView#dialogListView { background-color: transparent; border: none; color: %2; outline: none; }"
        "QListView#dialogListView::item { background-color: %4; border: 1px solid %5; border-radius: 6px; padding: 10px 12px; }"
        "QListView#dialogListView::item:hover { background-color: %6; }"
        "QListView#dialogListView::item:selected { background-color: %6; border-color: %7; color: %2; }"
        "QMenu#essenceContextMenu { background-color: %4; color: %2; border: 1px solid %5; padding: 5px; }"
        "QMenu#essenceContextMenu::item { padding: 7px 26px 7px 12px; border-radius: 4px; }"
        "QMenu#essenceContextMenu::item:selected { background-color: %6; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->primarySoftColor().name())
     .arg(tm->primaryColor().name()));
}

void EssencePanel::addEssenceMessage(const QString& messageId, const QString& sender, const QString& text, const QString& time)
{
    const QString preview = text.trimmed().isEmpty() ? QStringLiteral("[消息内容不可用]") : text.trimmed();
    QStandardItem* item = new QStandardItem(QStringLiteral("%1\n%2  ·  %3").arg(preview, sender, time));
    item->setData(messageId, Qt::UserRole);
    m_model->appendRow(item);
}

void EssencePanel::clearMessages()
{
    m_model->clear();
}


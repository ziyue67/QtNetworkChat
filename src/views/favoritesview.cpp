#include "views/favoritesview.h"

#include "theme/thememanager.h"

#include <QLabel>
#include <QListView>
#include <QStandardItemModel>
#include <QVBoxLayout>

FavoritesView::FavoritesView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("favoritesView"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &FavoritesView::updateStyle);
}

void FavoritesView::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QFrame* header = new QFrame(this);
    header->setObjectName(QStringLiteral("favoritesHeader"));
    header->setFixedHeight(60);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16, 8, 16, 8);
    headerLayout->setSpacing(2);

    QLabel* title = new QLabel(QStringLiteral("收藏"), header);
    title->setObjectName(QStringLiteral("favoritesTitleLabel"));
    headerLayout->addWidget(title);
    root->addWidget(header);

    m_model = new QStandardItemModel(this);
    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("favoritesListView"));
    m_listView->setModel(m_model);
    root->addWidget(m_listView, 1);

    QLabel* hint = new QLabel(QStringLiteral("收藏消息将在这里显示"), this);
    hint->setObjectName(QStringLiteral("favoritesHintLabel"));
    hint->setAlignment(Qt::AlignCenter);
    root->addWidget(hint);

    connect(m_listView, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            emit favoriteSelected(index.data(Qt::UserRole).toString(),
                                  index.data(Qt::UserRole + 1).toString());
        }
    });
}

void FavoritesView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#favoritesHeader { background-color: %1; border-bottom: 1px solid %2; }"
        "QLabel#favoritesTitleLabel { color: %3; font-size: 16px; font-weight: 600; }"
        "QListView#favoritesListView { background: transparent; border: none; outline: none; }"
        "QListView#favoritesListView::item { color: %3; padding: 10px; border-radius: 6px; }"
        "QListView#favoritesListView::item:hover { background-color: %4; }"
        "QLabel#favoritesHintLabel { color: %5; font-size: 13px; padding: 20px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->color(QStringLiteral("session-hover")).name())
     .arg(tm->textSecondaryColor().name()));
}

QListView* FavoritesView::listView() const { return m_listView; }
QStandardItemModel* FavoritesView::model() const { return m_model; }

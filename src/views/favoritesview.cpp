#include "views/favoritesview.h"

#include "theme/thememanager.h"

#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {
constexpr int FavoriteSessionRole = Qt::UserRole;
constexpr int FavoriteMessageRole = Qt::UserRole + 1;
constexpr int FavoriteSenderRole = Qt::UserRole + 2;
constexpr int FavoriteContentRole = Qt::UserRole + 3;
constexpr int FavoriteTimeRole = Qt::UserRole + 4;
constexpr int FavoriteSessionNameRole = Qt::UserRole + 5;
constexpr int FavoriteGroupHeaderRole = Qt::UserRole + 6;
constexpr int FavoritePayloadRole = Qt::UserRole + 7;
constexpr int FavoriteSearchRole = Qt::UserRole + 8;

class FavoritesItemDelegate final : public QStyledItemDelegate {
public:
    explicit FavoritesItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override {
        return QSize(0, index.data(FavoriteGroupHeaderRole).toBool() ? 30 : 76);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        ThemeManager* tm = ThemeManager::instance();
        const QRect rect = option.rect.adjusted(0, 1, 0, -1);
        if (index.data(FavoriteGroupHeaderRole).toBool()) {
            QFont font = option.font;
            font.setPixelSize(12);
            font.setWeight(QFont::Medium);
            painter->setFont(font);
            painter->setPen(tm->primaryColor());
            painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter,
                              index.data(FavoriteSessionNameRole).toString());
            painter->restore();
            return;
        }

        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? tm->primarySoftColor()
                                  : (hovered ? tm->backgroundTertiaryColor()
                                             : tm->backgroundSecondaryColor()));
        painter->drawRoundedRect(rect, 6, 6);

        const QString sender = index.data(FavoriteSenderRole).toString().trimmed();
        const QString initials = sender.isEmpty() ? QStringLiteral("?") : sender.left(1).toUpper();
        const QRect avatarRect(rect.left() + 12, rect.top() + 18, 36, 36);
        painter->setBrush(tm->primarySoftColor());
        painter->drawEllipse(avatarRect);
        QFont avatarFont = option.font;
        avatarFont.setPixelSize(14);
        avatarFont.setWeight(QFont::Medium);
        painter->setFont(avatarFont);
        painter->setPen(tm->primaryColor());
        painter->drawText(avatarRect, Qt::AlignCenter, initials);

        const int contentLeft = avatarRect.right() + 12;
        QFont titleFont = option.font;
        titleFont.setPixelSize(14);
        titleFont.setWeight(QFont::Medium);
        QFont timeFont = option.font;
        timeFont.setPixelSize(10);
        const QString time = index.data(FavoriteTimeRole).toString();
        const int timeWidth = QFontMetrics(timeFont).horizontalAdvance(time);
        const QRect timeRect(rect.right() - 12 - timeWidth, rect.top() + 10, timeWidth, 18);
        const QRect titleRect(contentLeft, rect.top() + 10,
                              qMax(0, timeRect.left() - contentLeft - 12), 18);
        const QRect contentRect(contentLeft, rect.top() + 33, rect.right() - contentLeft - 12, 28);
        painter->setFont(titleFont);
        painter->setPen(tm->textColor());
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(titleFont).elidedText(sender.isEmpty() ? QStringLiteral("未知用户") : sender,
                                                              Qt::ElideRight, titleRect.width()));
        painter->setFont(timeFont);
        painter->setPen(tm->textTertiaryColor());
        painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter,
                          time);
        QFont contentFont = option.font;
        contentFont.setPixelSize(12);
        painter->setFont(contentFont);
        painter->setPen(tm->textSecondaryColor());
        painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                          QFontMetrics(contentFont).elidedText(index.data(FavoriteContentRole).toString(), Qt::ElideRight, contentRect.width()));
        painter->restore();
    }
};
}

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
    header->setFixedHeight(58);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 10, 16, 10);
    m_searchEdit = new QLineEdit(header);
    m_searchEdit->setObjectName(QStringLiteral("favoritesSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索收藏内容"));
    m_searchEdit->setMinimumWidth(0);
    headerLayout->addWidget(m_searchEdit);
    root->addWidget(header);

    m_model = new QStandardItemModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterRole(FavoriteSearchRole);
    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("favoritesListView"));
    m_listView->setModel(m_proxyModel);
    m_listView->setItemDelegate(new FavoritesItemDelegate(m_listView));
    m_listView->setSpacing(8);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_listView, 1);

    m_emptyLabel = new QLabel(QStringLiteral("暂无收藏消息"), this);
    m_emptyLabel->setObjectName(QStringLiteral("favoritesHintLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setVisible(false);
    root->addWidget(m_emptyLabel, 1);

    connect(m_listView, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid() && !index.data(FavoriteGroupHeaderRole).toBool()) {
            emit favoriteSelected(index.data(Qt::UserRole).toString(),
                                  index.data(Qt::UserRole + 1).toString());
        }
    });
    connect(m_listView, &QListView::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = m_listView->indexAt(pos);
        if (!index.isValid() || index.data(FavoriteGroupHeaderRole).toBool()) return;
        QMenu menu(this);
        QAction* openAction = menu.addAction(QStringLiteral("打开会话"));
        QAction* removeAction = menu.addAction(QStringLiteral("取消收藏"));
        QAction* selected = menu.exec(m_listView->viewport()->mapToGlobal(pos));
        if (selected == openAction) {
            emit favoriteSelected(index.data(FavoriteSessionRole).toString(), index.data(FavoriteMessageRole).toString());
        } else if (selected == removeAction) {
            emit favoriteRemovalRequested(index.data(FavoritePayloadRole).toJsonObject());
        }
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxyModel->setFilterFixedString(text.trimmed());
    });
}

void FavoritesView::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QWidget#favoritesView { background-color: %1; }"
        "QFrame#favoritesHeader { background-color: %1; border: none; border-bottom: 1px solid %2; }"
        "QLineEdit#favoritesSearchEdit { background-color: %4; color: %3; border: 1px solid transparent; border-radius: 6px; padding: 8px 12px; }"
        "QLineEdit#favoritesSearchEdit:focus { border-color: %6; }"
        "QLineEdit#favoritesSearchEdit::placeholder { color: %5; }"
        "QListView#favoritesListView { background: %1; border: none; outline: none; padding: 12px 16px; }"
        "QListView#favoritesListView QWidget { background: %1; color: %3; }"
        "QListView#favoritesListView::item { border: none; }"
        "QLabel#favoritesHintLabel { color: %5; font-size: 14px; padding: 20px; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->color(QStringLiteral("session-hover")).name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->primaryColor().name()));

    if (m_listView && m_listView->viewport()) {
        m_listView->viewport()->setAutoFillBackground(true);
        QPalette palette = m_listView->viewport()->palette();
        palette.setColor(QPalette::Window, tm->backgroundColor());
        palette.setColor(QPalette::Base, tm->backgroundColor());
        m_listView->viewport()->setPalette(palette);
        m_listView->viewport()->update();
    }
}

QListView* FavoritesView::listView() const { return m_listView; }
QStandardItemModel* FavoritesView::model() const { return m_model; }

void FavoritesView::setEmptyStateVisible(bool visible)
{
    if (m_emptyLabel) m_emptyLabel->setVisible(visible);
    if (m_listView) m_listView->setVisible(!visible);
}

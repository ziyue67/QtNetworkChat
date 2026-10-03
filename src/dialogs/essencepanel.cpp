#include "dialogs/essencepanel.h"

#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {
constexpr int EssenceSenderRole = Qt::UserRole + 1;
constexpr int EssenceContentRole = Qt::UserRole + 2;
constexpr int EssenceTimeRole = Qt::UserRole + 3;

class EssenceMessageDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        ThemeManager* tm = ThemeManager::instance();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRect card = option.rect.adjusted(0, 2, 0, -2);
        const QColor surface = selected || hovered ? tm->primarySoftColor()
                                                    : tm->backgroundSecondaryColor();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(selected ? tm->primaryColor() : Qt::NoPen);
        painter->setBrush(surface);
        painter->drawRoundedRect(card, 6, 6);

        const QString sender = index.data(EssenceSenderRole).toString();
        const QString content = index.data(EssenceContentRole).toString();
        const QString time = index.data(EssenceTimeRole).toString();
        const QRect avatarRect(card.left() + 12, card.top() + 15, 34, 34);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tm->primarySoftColor());
        painter->drawEllipse(avatarRect);
        QFont avatarFont = option.font;
        avatarFont.setPixelSize(13);
        avatarFont.setWeight(QFont::Medium);
        painter->setFont(avatarFont);
        painter->setPen(tm->primaryColor());
        painter->drawText(avatarRect, Qt::AlignCenter, sender.isEmpty() ? QStringLiteral("?") : sender.left(1).toUpper());

        const int left = avatarRect.right() + 12;
        QFont timeFont = option.font;
        timeFont.setPixelSize(10);
        const int timeWidth = QFontMetrics(timeFont).horizontalAdvance(time);
        const QRect timeRect(card.right() - 12 - timeWidth, card.top() + 9, timeWidth, 18);
        const QRect titleRect(left, card.top() + 9, qMax(0, timeRect.left() - left - 10), 19);
        const QRect contentRect(left, card.top() + 31, card.right() - left - 12, 23);

        QFont contentFont = option.font;
        contentFont.setPixelSize(14);
        contentFont.setWeight(QFont::Medium);
        painter->setFont(contentFont);
        painter->setPen(tm->textColor());
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(sender.isEmpty() ? QStringLiteral("未知用户") : sender,
                                                            Qt::ElideRight, titleRect.width()));

        painter->setFont(timeFont);
        painter->setPen(tm->textTertiaryColor());
        painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter, time);
        QFont previewFont = option.font;
        previewFont.setPixelSize(12);
        painter->setFont(previewFont);
        painter->setPen(tm->textSecondaryColor());
        painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(content, Qt::ElideRight, contentRect.width()));
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return QSize(100, 72);
    }
};

} // namespace

EssencePanel::EssencePanel(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("essencePanel"));
    setWindowTitle(QStringLiteral("精华消息"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setFixedSize(420, 456);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &EssencePanel::updateStyle);
}

void EssencePanel::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* titleBar = new DialogTitleBar(this, QStringLiteral("精华消息"));
    connect(titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(titleBar);

    auto* body = new QWidget(this);
    body->setObjectName(QStringLiteral("essencePanelBody"));
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(18, 16, 18, 14);
    bodyLayout->setSpacing(10);

    m_titleLabel = new QLabel(QStringLiteral("群精华"), body);
    m_titleLabel->setObjectName(QStringLiteral("dialogTitleLabel"));
    bodyLayout->addWidget(m_titleLabel);

    auto* summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(8);
    auto* hint = new QLabel(QStringLiteral("群聊中被标记的重要消息"), body);
    hint->setObjectName(QStringLiteral("essencePanelHint"));
    summaryRow->addWidget(hint);
    summaryRow->addStretch();
    m_countLabel = new QLabel(QStringLiteral("0 条"), body);
    m_countLabel->setObjectName(QStringLiteral("essenceCountLabel"));
    m_countLabel->setAlignment(Qt::AlignCenter);
    summaryRow->addWidget(m_countLabel);
    bodyLayout->addLayout(summaryRow);

    auto* listFrame = new QFrame(body);
    listFrame->setObjectName(QStringLiteral("essenceListFrame"));
    auto* listLayout = new QVBoxLayout(listFrame);
    listLayout->setContentsMargins(5, 5, 5, 5);
    listLayout->setSpacing(0);
    m_model = new QStandardItemModel(this);
    m_listView = new QListView(listFrame);
    m_listView->setObjectName(QStringLiteral("essenceListView"));
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(new EssenceMessageDelegate(m_listView));
    m_listView->setSpacing(5);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    listLayout->addWidget(m_listView, 1);
    m_emptyLabel = new QLabel(QStringLiteral("暂无精华消息\n在聊天记录中右键消息即可添加"), listFrame);
    m_emptyLabel->setObjectName(QStringLiteral("essenceEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    listLayout->addWidget(m_emptyLabel, 1);
    bodyLayout->addWidget(listFrame, 1);

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
        QAction* locate = menu.addAction(QStringLiteral("打开会话"));
        QAction* remove = menu.addAction(QStringLiteral("取消精华"));
        QAction* selected = menu.exec(m_listView->viewport()->mapToGlobal(pos));
        if (selected == locate) {
            emit messageActivated(messageId);
        } else if (selected == remove) {
            emit messageRemovalRequested(messageId);
        }
    });

    auto* footer = new QHBoxLayout();
    footer->addStretch();
    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), body);
    closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFixedSize(76, 34);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(closeBtn);
    bodyLayout->addLayout(footer);
    root->addWidget(body, 1);
    refreshSummary();
}

void EssencePanel::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#essencePanel { background-color: %1; border: 1px solid %5; }"
        "QWidget#essencePanelBody { background-color: %1; }"
        "QLabel#dialogTitleLabel { color: %2; font-size: 18px; font-weight: 600; }"
        "QLabel#essencePanelHint { color: %3; font-size: 12px; }"
        "QLabel#essenceCountLabel { color: %7; background-color: %6; border-radius: 9px; padding: 2px 8px; font-size: 11px; font-weight: 600; }"
        "QFrame#essenceListFrame { background-color: %4; border: 1px solid %5; border-radius: 7px; }"
        "QPushButton#dialogSecondaryBtn { background-color: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 0; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %6; border-color: %7; }"
        "QPushButton#dialogSecondaryBtn:pressed { background-color: %5; }"
        "QListView#essenceListView { background-color: transparent; border: none; color: %2; outline: none; }"
        "QListView#essenceListView::item { border: none; margin: 0; padding: 0; }"
        "QLabel#essenceEmptyLabel { color: %3; font-size: 12px; line-height: 1.45; padding: 32px; }"
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
    item->setData(sender, EssenceSenderRole);
    item->setData(preview, EssenceContentRole);
    item->setData(time, EssenceTimeRole);
    item->setToolTip(QStringLiteral("左键定位原消息，右键可取消精华"));
    m_model->appendRow(item);
    refreshSummary();
}

void EssencePanel::clearMessages()
{
    m_model->clear();
    refreshSummary();
}

void EssencePanel::refreshSummary()
{
    const int count = m_model ? m_model->rowCount() : 0;
    if (m_countLabel) m_countLabel->setText(QStringLiteral("%1 条").arg(count));
    if (m_listView) m_listView->setVisible(count > 0);
    if (m_emptyLabel) m_emptyLabel->setVisible(count == 0);
}


#include "widgets/contactlistwidget.h"

#include "theme/thememanager.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace {
bool shouldDrawOnline(const ContactDisplayData& data)
{
    return !data.isGroup && !data.isTitle && data.isOnline;
}

QString fallbackFontFamily()
{
    const QStringList candidates = {
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Segoe UI"),
    };
    QFontDatabase db;
    for (const QString& c : candidates) {
        if (db.families().contains(c)) return c;
    }
    return QString();
}

QFont contactFont(int pixelSize, bool bold)
{
    QFont f = QApplication::font();
    const QString family = fallbackFontFamily();
    if (!family.isEmpty()) f.setFamily(family);
    f.setPixelSize(pixelSize);
    f.setBold(bold);
    return f;
}

QColor avatarColor(const QString& id)
{
    const QStringList palette = {
        QStringLiteral("#12a4ff"), QStringLiteral("#18c98b"),
        QStringLiteral("#ff9f1a"), QStringLiteral("#fb6f92"),
        QStringLiteral("#12c9bd"), QStringLiteral("#8b7cf6"),
    };
    int sum = 0;
    for (const QChar& c : id) sum += c.unicode();
    return palette.at(sum % palette.size());
}
}

ContactListItemDelegate::ContactListItemDelegate(QObject* parent)
    : QAbstractItemDelegate(parent)
{
}

void ContactListItemDelegate::paint(QPainter* painter,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QVariant vdata = index.data(Qt::UserRole);
    ContactDisplayData data = vdata.value<ContactDisplayData>();
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool selected = option.state & QStyle::State_Selected;

    ThemeManager* tm = ThemeManager::instance();
    QRect r = option.rect.adjusted(2, 0, -2, 0);

    if (data.isTitle) {
        if (selected) {
            painter->fillRect(r, tm->color(QStringLiteral("session-selected")));
        } else if (hovered) {
            painter->fillRect(r, tm->color(QStringLiteral("session-hover")));
        }
        drawTitle(painter, r, data.titleText.isEmpty() ? data.group : data.titleText);
        painter->restore();
        return;
    }

    if (selected) {
        painter->fillRect(r, tm->color(QStringLiteral("session-selected")));
    } else if (hovered) {
        painter->fillRect(r, tm->color(QStringLiteral("session-hover")));
    }

    const int avatarSize = 40;
    const int left = r.left() + 12;
    const int top = r.top() + 8;
    QRect avatarRect(left, top, avatarSize, avatarSize);
    drawAvatar(painter, avatarRect, data);

    const int textLeft = left + avatarSize + 12;
    const int textRight = r.right() - 12;
    const int textTop = top + 2;

    const QColor textColor = tm->textColor();
    const QColor secondaryColor = tm->textSecondaryColor();

    QFont nameFont = contactFont(14, true);
    painter->setFont(nameFont);
    painter->setPen(textColor);
    QString displayName = data.remark.isEmpty() ? data.nickname : data.remark;
    QFontMetrics nameFm(nameFont);
    displayName = nameFm.elidedText(displayName, Qt::ElideRight, textRight - textLeft - 40);
    painter->drawText(textLeft, textTop, displayName);

    if (!data.isGroup && (data.status == QStringLiteral("online") || data.isOnline)) {
        QFont statusFont = contactFont(11, false);
        painter->setFont(statusFont);
        painter->setPen(tm->successColor());
        painter->drawText(textRight - 28, textTop, QStringLiteral("在线"));
    } else if (!data.isGroup) {
        QFont statusFont = contactFont(11, false);
        painter->setFont(statusFont);
        painter->setPen(tm->textTertiaryColor());
        painter->drawText(textRight - 28, textTop, QStringLiteral("离线"));
    }

    const int secondLineY = textTop + 19;
    QFont sigFont = contactFont(12, false);
    painter->setFont(sigFont);
    painter->setPen(secondaryColor);
    QFontMetrics sigFm(sigFont);
    QString signature = data.signature.isEmpty() ? QStringLiteral(" ") : data.signature;
    if (data.isGroup) {
        signature = data.memberCount > 0
                        ? QStringLiteral("%1 人").arg(data.memberCount)
                        : QStringLiteral("群聊");
        if (!data.signature.isEmpty()) signature += QStringLiteral(" · ") + data.signature;
    }
    signature = sigFm.elidedText(signature, Qt::ElideRight, textRight - textLeft);
    painter->drawText(textLeft, secondLineY, signature);

    painter->restore();
}

QSize ContactListItemDelegate::sizeHint(const QStyleOptionViewItem& /*option*/,
                                        const QModelIndex& index) const
{
    QVariant vdata = index.data(Qt::UserRole);
    ContactDisplayData data = vdata.value<ContactDisplayData>();
    if (data.isTitle) {
        return QSize(200, 32);
    }
    return QSize(260, 56);
}

void ContactListItemDelegate::drawAvatar(QPainter* painter, const QRect& rect,
                                       const ContactDisplayData& data) const
{
    QPainterPath circle;
    circle.addEllipse(rect.adjusted(1, 1, -1, -1));
    painter->setClipPath(circle);

    QString text = data.nickname.isEmpty() ? QStringLiteral("Q") : data.nickname.left(1).toUpper();
    QColor bg = data.id.isEmpty() ? QColor(QStringLiteral("#8f959e")) : avatarColor(data.id);
    painter->setBrush(bg);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(rect.adjusted(1, 1, -1, -1));

    QFont f = contactFont(qMax(12, rect.width() * 2 / 5), true);
    painter->setFont(f);
    painter->setPen(Qt::white);
    painter->drawText(rect, Qt::AlignCenter, text);

    painter->setClipping(false);
    if (shouldDrawOnline(data)) {
        const int dotSize = qMax(8, rect.width() / 5);
        const int offset = qMax(2, rect.width() / 10);
        QRect dotRect(rect.right() - dotSize - offset, rect.bottom() - dotSize - offset,
                      dotSize, dotSize);
        painter->setBrush(ThemeManager::instance()->color(QStringLiteral("online-dot")));
        painter->setPen(QPen(Qt::white, 2));
        painter->drawEllipse(dotRect);
    }
}

void ContactListItemDelegate::drawTitle(QPainter* painter, const QRect& rect,
                                        const QString& title) const
{
    ThemeManager* tm = ThemeManager::instance();
    QFont f = contactFont(12, true);
    painter->setFont(f);
    painter->setPen(tm->textTertiaryColor());
    painter->drawText(rect.adjusted(12, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, title);
}

// ---------------------------------------------------------------------

ContactListWidget::ContactListWidget(QWidget* parent)
    : QListView(parent)
{
    setupModel();
}

void ContactListWidget::setupModel()
{
    m_model = new QStandardItemModel(this);
    m_delegate = new ContactListItemDelegate(this);
    setModel(m_model);
    setItemDelegate(m_delegate);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSpacing(2);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setFrameShape(QFrame::NoFrame);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(this, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        QVariant vdata = index.data(Qt::UserRole);
        ContactDisplayData data = vdata.value<ContactDisplayData>();
        if (data.isTitle || data.id.isEmpty()) return;
        if (data.isGroup) {
            emit groupSelected(data.id);
        } else {
            emit friendSelected(data.id);
        }
    });
}

void ContactListWidget::setFriends(const QList<ContactDisplayData>& contacts)
{
    m_friends = contacts;
    updateView();
}

void ContactListWidget::setGroups(const QList<ContactDisplayData>& groups)
{
    m_groups = groups;
}

void ContactListWidget::setShowGroups(bool show)
{
    m_showGroups = show;
    updateView();
}

void ContactListWidget::setOnlineUsers(const QSet<QString>& onlineIds)
{
    m_onlineIds = onlineIds;
    updateView();
}

void ContactListWidget::updateView()
{
    m_model->clear();

    auto appendItem = [this](const ContactDisplayData& data) {
        QStandardItem* item = new QStandardItem();
        item->setData(QVariant::fromValue(data), Qt::UserRole);
        item->setData(data.id, Qt::UserRole + 1);
        item->setSelectable(!data.isTitle);
        item->setEditable(false);
        m_model->appendRow(item);
    };

    auto addTitle = [appendItem](const QString& text) {
        ContactDisplayData data;
        data.isTitle = true;
        data.titleText = text;
        appendItem(data);
    };

    if (m_showGroups) {
        addTitle(QStringLiteral("群聊"));
        for (auto data : m_groups) {
            data.isGroup = true;
            appendItem(data);
        }
    } else {
        addTitle(QStringLiteral("好友"));
        for (auto data : m_friends) {
            data.isGroup = false;
            data.isOnline = m_onlineIds.contains(data.id)
                            || data.status == QStringLiteral("online");
            appendItem(data);
        }
    }

    if (m_model->rowCount() == 0) {
        ContactDisplayData data;
        data.nickname = QStringLiteral("暂无联系人");
        data.signature = QStringLiteral("点击添加好友开始聊天");
        appendItem(data);
    }
}

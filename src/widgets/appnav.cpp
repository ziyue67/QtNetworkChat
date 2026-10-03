#if __has_include(<QtCore/q20algorithm.h>)
#include <QtCore/q20algorithm.h>
#endif

#include "widgets/appnav.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace {
class BadgeLabel : public QLabel {
    Q_OBJECT
public:
    explicit BadgeLabel(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        setFixedSize(18, 18);
        setAlignment(Qt::AlignCenter);
        setObjectName(QStringLiteral("navBadge"));
        hide();
    }

    void setCount(int count)
    {
        if (count <= 0) {
            hide();
        } else {
            setText(QString::number(qMin(count, 99)));
            show();
        }
        updateStyle();
    }

    void updateStyle()
    {
        ThemeManager* tm = ThemeManager::instance();
        setStyleSheet(QStringLiteral(
            "QLabel#navBadge { background-color: %1; color: white; border-radius: 9px; font-size: 10px; font-weight: 700; }"
        ).arg(tm->color(QStringLiteral("badge")).name()));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QBrush(QColor(ThemeManager::instance()->color(QStringLiteral("badge")).name())));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect());
        painter.setPen(QPen(Qt::white));
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

class NavItem : public QFrame {
    Q_OBJECT

public:
    NavItem(const QString& icon, const QString& label, const QString& route, bool mock, QWidget* parent = nullptr)
        : QFrame(parent)
        , m_route(route)
        , m_mock(mock)
    {
        setObjectName(QStringLiteral("appNavItem"));
        setCursor(Qt::PointingHandCursor);
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 8, 0, 8);
        layout->setSpacing(2);
        layout->setAlignment(Qt::AlignCenter);

        // Icon container with badge and mock dot
        QHBoxLayout* iconLayout = new QHBoxLayout();
        iconLayout->setContentsMargins(0, 0, 0, 0);
        iconLayout->setSpacing(0);
        iconLayout->addStretch();

        m_icon = new QLabel(icon, this);
        m_icon->setObjectName(QStringLiteral("appNavIcon"));
        m_icon->setAlignment(Qt::AlignCenter);
        m_icon->setFont(QFont(QStringLiteral("Segoe UI Symbol"), 18));
        iconLayout->addWidget(m_icon);

        m_badge = new BadgeLabel(this);
        iconLayout->addWidget(m_badge);

        if (m_mock) {
            m_mockDot = new QLabel(this);
            m_mockDot->setFixedSize(6, 6);
            m_mockDot->setStyleSheet(QStringLiteral("background-color: #faad14; border-radius: 3px;"));
            iconLayout->addWidget(m_mockDot);
        }

        iconLayout->addStretch();

        layout->addLayout(iconLayout);

        m_label = new QLabel(label, this);
        m_label->setObjectName(QStringLiteral("appNavLabel"));
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setFont(QFont(ThemeManager::instance()->font().family(), 9));
        layout->addWidget(m_label);

        setFixedWidth(56);
        updateStyle();
    }

    QString route() const { return m_route; }
    bool mock() const { return m_mock; }

    void setActive(bool active)
    {
        m_active = active;
        updateStyle();
    }

    void setBadgeCount(int count)
    {
        m_badge->setCount(count);
    }

    void updateStyle()
    {
        ThemeManager* tm = ThemeManager::instance();
        const QColor activeBg = tm->primarySoftColor();
        const QColor activeFg = tm->primaryColor();
        const QColor inactiveFg = tm->color(QStringLiteral("nav-inactive"));
        const QColor mockFg = tm->textTertiaryColor();

        m_badge->updateStyle();

        if (m_active) {
            setStyleSheet(QStringLiteral(
                "QFrame#appNavItem { background-color: %1; border-radius: 6px; }"
                "QLabel#appNavIcon { background: transparent; color: %2; }"
                "QLabel#appNavLabel { background: transparent; color: %2; }"
            ).arg(activeBg.name()).arg(activeFg.name()));
        } else {
            QColor fg = m_mock ? mockFg : inactiveFg;
            setStyleSheet(QStringLiteral(
                "QFrame#appNavItem { background-color: transparent; }"
                "QFrame#appNavItem:hover { background-color: %1; border-radius: 6px; }"
                "QLabel#appNavIcon { background: transparent; color: %2; }"
                "QLabel#appNavLabel { background: transparent; color: %2; }"
            ).arg(tm->backgroundTertiaryColor().name()).arg(fg.name()));
        }
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
        QFrame::mousePressEvent(event);
    }

private:
    QLabel* m_icon = nullptr;
    QLabel* m_label = nullptr;
    QLabel* m_mockDot = nullptr;
    BadgeLabel* m_badge = nullptr;
    QString m_route;
    bool m_mock = false;
    bool m_active = false;
};

// appnav.moc must be included inside the anonymous namespace so that
// NavItem::staticMetaObject is defined in the same scope as the class.
#include "appnav.moc"
} // end anonymous namespace

AppNav::AppNav(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("appNav"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateStyle();
        for (QWidget* item : m_items) {
            if (NavItem* nav = qobject_cast<NavItem*>(item)) {
                nav->updateStyle();
            }
        }
    });
}

void AppNav::setupUi()
{
    setFixedWidth(ThemeManager::instance()->appNavWidth());
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 12, 8, 12);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignTop);

    m_layout = layout;

    addNavItem(QStringLiteral("💬"), QStringLiteral("消息"), QStringLiteral("messages"));
    addNavItem(QStringLiteral("👥"), QStringLiteral("联系人"), QStringLiteral("contacts"));
    addNavItem(QStringLiteral("🌐"), QStringLiteral("空间"), QStringLiteral("space"), true);
    addNavItem(QStringLiteral("📺"), QStringLiteral("频道"), QStringLiteral("channels"), true);
    addNavItem(QStringLiteral("✉️"), QStringLiteral("邮件"), QStringLiteral("mail"), true);
    addNavItem(QStringLiteral("📄"), QStringLiteral("文档"), QStringLiteral("docs"), true);
    addNavItem(QStringLiteral("📅"), QStringLiteral("日历"), QStringLiteral("calendar"), true);
    addNavItem(QStringLiteral("🎥"), QStringLiteral("会议"), QStringLiteral("meetings"), true);
    addNavItem(QStringLiteral("⭐"), QStringLiteral("收藏"), QStringLiteral("favorites"));
    addNavItem(QStringLiteral("👛"), QStringLiteral("钱包"), QStringLiteral("wallet"), true);
    addNavItem(QStringLiteral("⚙️"), QStringLiteral("设置"), QStringLiteral("settings"));

    layout->addStretch();
}

void AppNav::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#appNav { background-color: %1; border-right: 1px solid %2; }"
    ).arg(tm->backgroundSecondaryColor().name()).arg(tm->borderColor().name()));
}

void AppNav::addNavItem(const QString& icon, const QString& label, const QString& route, bool mock)
{
    NavItem* item = new NavItem(icon, label, route, mock, this);
    m_layout->addWidget(item);
    m_items.append(item);
    const int index = m_items.size() - 1;
    connect(item, &NavItem::clicked, this, [this, index, route]() {
        setCurrentIndex(index);
        emit routeActivated(route);
    });
}

void AppNav::setUnreadCount(int count)
{
    m_unreadCount = count;
    // Update badge on first nav item (messages)
    if (!m_items.isEmpty()) {
        if (NavItem* item = qobject_cast<NavItem*>(m_items.at(0))) {
            item->setBadgeCount(count);
        }
    }
}

int AppNav::currentIndex() const
{
    return m_currentIndex;
}

void AppNav::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    m_currentIndex = index;
    for (int i = 0; i < m_items.size(); ++i) {
        if (NavItem* item = qobject_cast<NavItem*>(m_items.at(i))) {
            item->setActive(i == index);
        }
    }
}

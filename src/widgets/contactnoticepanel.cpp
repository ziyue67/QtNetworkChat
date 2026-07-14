#include "widgets/contactnoticepanel.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QPixmap renderBellIcon(int size, const QColor& color)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    const int s = size;
    const int x = s / 2;
    const int y = s / 2;
    const int r = s * 7 / 20;

    QPainterPath bell;
    bell.moveTo(x - r, y - 4);
    bell.quadTo(x - r, y - r - 2, x, y - r - 2);
    bell.quadTo(x + r, y - r - 2, x + r, y - 4);
    bell.lineTo(x + r, y + r / 2);
    bell.quadTo(x + r, y + r, x + r - 3, y + r);
    bell.lineTo(x - r + 3, y + r);
    bell.quadTo(x - r, y + r, x - r, y + r / 2);
    bell.closeSubpath();
    painter.drawPath(bell);

    QRect clapper(x - 5, y + r - 1, 10, 6);
    painter.drawEllipse(clapper);
    return pixmap;
}
}

ContactNoticePanel::ContactNoticePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("contactNoticePanel"));
    setupUi();
    updateStyle();
    setNoticeType(FriendNotice);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ContactNoticePanel::updateStyle);
}

void ContactNoticePanel::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QWidget* header = new QWidget(this);
    header->setObjectName(QStringLiteral("noticePanelHeader"));
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 18, 24, 18);
    headerLayout->setSpacing(12);

    m_title = new QLabel(header);
    m_title->setObjectName(QStringLiteral("noticePanelTitle"));
    headerLayout->addWidget(m_title);
    headerLayout->addStretch();

    m_filterBtn = new QPushButton(QStringLiteral("筛选"), header);
    m_filterBtn->setObjectName(QStringLiteral("noticePanelActionBtn"));
    connect(m_filterBtn, &QPushButton::clicked, this, &ContactNoticePanel::filterRequested);
    headerLayout->addWidget(m_filterBtn);

    m_clearBtn = new QPushButton(QStringLiteral("清空"), header);
    m_clearBtn->setObjectName(QStringLiteral("noticePanelActionBtn"));
    connect(m_clearBtn, &QPushButton::clicked, this, &ContactNoticePanel::clearRequested);
    headerLayout->addWidget(m_clearBtn);

    root->addWidget(header);

    QWidget* divider = new QWidget(this);
    divider->setObjectName(QStringLiteral("noticePanelDivider"));
    divider->setFixedHeight(1);
    root->addWidget(divider);

    QVBoxLayout* emptyLayout = new QVBoxLayout();
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(16);
    emptyLayout->addStretch();

    m_icon = new QLabel(this);
    m_icon->setFixedSize(80, 80);
    m_icon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_icon, 0, Qt::AlignHCenter);

    m_subtitle = new QLabel(QStringLiteral("暂无通知"), this);
    m_subtitle->setObjectName(QStringLiteral("noticePanelSubtitle"));
    m_subtitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_subtitle, 0, Qt::AlignHCenter);

    emptyLayout->addStretch();
    root->addLayout(emptyLayout, 1);
}

void ContactNoticePanel::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    const QColor border = tm->borderColor();
    const QColor iconColor = tm->textTertiaryColor();
    m_icon->setPixmap(renderBellIcon(80, iconColor));

    setStyleSheet(QStringLiteral(
        "QWidget#contactNoticePanel { background-color: %1; }"
        "QWidget#noticePanelHeader { background-color: %1; }"
        "QLabel#noticePanelTitle { color: %2; font-size: 18px; font-weight: 600; }"
        "QLabel#noticePanelSubtitle { color: %4; font-size: 14px; }"
        "QPushButton#noticePanelActionBtn { background: transparent; color: %3; border: 1px solid %5; border-radius: 6px; padding: 5px 12px; }"
        "QPushButton#noticePanelActionBtn:hover { background-color: %6; }"
        "QWidget#noticePanelDivider { background-color: %5; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->textTertiaryColor().name())
     .arg(border.name())
     .arg(tm->backgroundTertiaryColor().name()));
}

void ContactNoticePanel::setNoticeType(NoticeType type)
{
    m_type = type;
    m_title->setText(type == GroupNotice ? QStringLiteral("群通知")
                                         : QStringLiteral("好友通知"));
}

void ContactNoticePanel::setUnread(bool unread)
{
    m_unread = unread;
}

#include "widgets/titlebar.h"

#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>

namespace {
QPushButton* createWindowButton(const QString& text, const QString& toolTip, const QString& objName)
{
    QPushButton* btn = new QPushButton(text);
    btn->setObjectName(objName);
    btn->setFixedSize(40, 30);
    btn->setToolTip(toolTip);
    btn->setFlat(true);
    return btn;
}
}

TitleBar::TitleBar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("titleBar"));
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &TitleBar::updateStyle);
}

void TitleBar::setupUi()
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 0, 0);
    layout->setSpacing(8);

    m_logoLabel = new QLabel(QStringLiteral("Q"), this);
    m_logoLabel->setObjectName(QStringLiteral("titleBarLogo"));
    m_logoLabel->setFixedSize(24, 24);
    m_logoLabel->setAlignment(Qt::AlignCenter);

    m_titleLabel = new QLabel(QStringLiteral("QQ NT"), this);
    m_titleLabel->setObjectName(QStringLiteral("titleBarTitle"));

    m_avatar = new AvatarLabel(this, 24);
    m_avatar->setTextAvatar(QStringLiteral("Q"), QColor(QStringLiteral("#0099ff")));

    m_userLabel = new QLabel(this);
    m_userLabel->setObjectName(QStringLiteral("titleBarUser"));

    layout->addWidget(m_logoLabel);
    layout->addWidget(m_titleLabel);
    layout->addStretch();
    layout->addWidget(m_avatar);
    layout->addWidget(m_userLabel);
    layout->addSpacing(12);

    m_minBtn = createWindowButton(QStringLiteral("−"), QStringLiteral("最小化"), QStringLiteral("windowMinBtn"));
    m_maxBtn = createWindowButton(QStringLiteral("□"), QStringLiteral("最大化/还原"), QStringLiteral("windowMaxBtn"));
    m_closeBtn = createWindowButton(QStringLiteral("×"), QStringLiteral("关闭"), QStringLiteral("windowCloseBtn"));

    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    connect(m_minBtn, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_maxBtn, &QPushButton::clicked, this, &TitleBar::maximizeRequested);
    connect(m_closeBtn, &QPushButton::clicked, this, &TitleBar::closeRequested);

    setFixedHeight(ThemeManager::instance()->titleBarHeight());
}

void TitleBar::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#titleBar { background-color: %1; border-bottom: 1px solid %2; }"
        "QLabel#titleBarLogo { color: white; background-color: #0099ff; border-radius: 12px; font-weight: 700; font-size: 13px; }"
        "QLabel#titleBarTitle { color: %3; font-size: 14px; font-weight: 600; }"
        "QLabel#titleBarUser { color: %4; font-size: 12px; }"
        "QPushButton#windowMinBtn, QPushButton#windowMaxBtn { color: %3; border: none; background: transparent; }"
        "QPushButton#windowMinBtn:hover, QPushButton#windowMaxBtn:hover { background-color: %5; }"
        "QPushButton#windowCloseBtn { color: %3; border: none; background: transparent; }"
        "QPushButton#windowCloseBtn:hover { background-color: #ff4d4f; color: white; }"
    ).arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name())
     .arg(tm->textSecondaryColor().name())
     .arg(tm->backgroundTertiaryColor().name()));
}

void TitleBar::setUserName(const QString& name)
{
    m_userLabel->setText(name);
    m_avatar->setTextAvatar(name.left(1).toUpper(), QColor(QStringLiteral("#0099ff")));
}

void TitleBar::setUserId(const QString& userId)
{
    m_userLabel->setToolTip(QStringLiteral("QQ: %1").arg(userId));
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragging = true;
    }
    QFrame::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        if (QWidget* win = window()) {
            win->move(event->globalPos() - m_dragStartPos);
        }
    }
    QFrame::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QFrame::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    emit maximizeRequested();
}

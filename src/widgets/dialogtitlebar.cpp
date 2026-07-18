#include "widgets/dialogtitlebar.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>

DialogTitleBar::DialogTitleBar(QWidget* parent, const QString& title, bool showCloseButton)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("dialogTitleBar"));
    setupUi(showCloseButton);
    setTitle(title);
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &DialogTitleBar::updateStyle);
}

void DialogTitleBar::setupUi(bool showCloseButton)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 8, 0);
    layout->setSpacing(8);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("dialogTitleBarLabel"));
    layout->addWidget(m_titleLabel, 1);

    if (showCloseButton) {
        // Use ASCII X here: the preview window can use a dark local stylesheet
        // and the multiplication glyph was rendered blank by the active font
        // fallback on some Windows installations.
        m_closeBtn = new QPushButton(QStringLiteral("X"), this);
        m_closeBtn->setObjectName(QStringLiteral("dialogTitleBarCloseBtn"));
        m_closeBtn->setFixedSize(32, 32);
        m_closeBtn->setFlat(true);
        m_closeBtn->setCursor(Qt::PointingHandCursor);
        // No inline stylesheet here: the theme-aware rule in updateStyle()
        // (QPushButton#dialogTitleBarCloseBtn { color: <textColor> }) must win so
        // the X stays visible on light backgrounds. A hardcoded near-white color
        // previously made it invisible on the light title bar.
        m_closeBtn->setToolTip(QStringLiteral("关闭"));
        layout->addWidget(m_closeBtn);
        connect(m_closeBtn, &QPushButton::clicked, this, &DialogTitleBar::closeRequested);
    }

    setFixedHeight(ThemeManager::instance()->titleBarHeight());
}

void DialogTitleBar::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QFrame#dialogTitleBar { background-color: %1; border-bottom: 1px solid %2; }"
        "QLabel#dialogTitleBarLabel { color: %3; font-size: 14px; font-weight: 600; }"
        "QLabel#dialogTitleBarLabel[centered=\"true\"] { font-size: 16px; font-weight: 700; }"
        "QPushButton#dialogTitleBarCloseBtn { color: %3; border: none; background: transparent; font-size: 14px; font-weight: 700; border-radius: 4px; padding: 0; }"
        "QPushButton#dialogTitleBarCloseBtn:hover { background-color: #ff4d4f; color: white; }"
    ).arg(tm->backgroundSecondaryColor().name())
     .arg(tm->borderColor().name())
     .arg(tm->textColor().name()));
}

void DialogTitleBar::setTitle(const QString& title)
{
    m_titleLabel->setText(title);
}

void DialogTitleBar::setCloseButtonVisible(bool visible)
{
    if (m_closeBtn) {
        m_closeBtn->setVisible(visible);
    }
}

void DialogTitleBar::setTitleCentered(bool centered)
{
    if (!m_titleLabel) return;
    if (centered && !m_leftBalance) {
        m_leftBalance = new QWidget(this);
        m_leftBalance->setFixedSize(32, 32);
        if (QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(this->layout())) {
            layout->insertWidget(0, m_leftBalance);
        }
    }
    if (m_leftBalance) m_leftBalance->setVisible(centered);
    m_titleLabel->setAlignment(centered ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setProperty("centered", centered);
    m_titleLabel->style()->unpolish(m_titleLabel);
    m_titleLabel->style()->polish(m_titleLabel);
}

void DialogTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragging = true;
    }
    QFrame::mousePressEvent(event);
}

void DialogTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        if (QWidget* win = window()) {
            win->move(event->globalPos() - m_dragStartPos);
        }
    }
    QFrame::mouseMoveEvent(event);
}

void DialogTitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
    m_dragging = false;
    QFrame::mouseReleaseEvent(event);
}

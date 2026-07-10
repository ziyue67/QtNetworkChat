#include "widgets/avatarlabel.h"

#include <QPainter>
#include <QPainterPath>

AvatarLabel::AvatarLabel(QWidget* parent, int size)
    : QLabel(parent)
    , m_size(size)
{
    setFixedSize(m_size, m_size);
    setAlignment(Qt::AlignCenter);
}

void AvatarLabel::setPixmap(const QPixmap& pixmap)
{
    m_pixmap = pixmap.scaled(m_size, m_size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_text.clear();
    update();
}

void AvatarLabel::setTextAvatar(const QString& text, const QColor& background)
{
    m_text = text.left(1).toUpper();
    m_textBackground = background;
    m_pixmap = QPixmap();
    update();
}

void AvatarLabel::setStatus(AvatarLabel::Status status)
{
    m_status = status;
    update();
}

int AvatarLabel::avatarSize() const
{
    return m_size;
}

void AvatarLabel::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect targetRect = rect();
    QPainterPath circle;
    circle.addEllipse(targetRect.adjusted(1, 1, -1, -1));
    painter.setClipPath(circle);

    if (!m_pixmap.isNull()) {
        painter.drawPixmap(targetRect, m_pixmap);
    } else {
        painter.setBrush(m_textBackground.isValid() ? m_textBackground : QColor(QStringLiteral("#0099ff")));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(targetRect.adjusted(1, 1, -1, -1));
        QFont f = font();
        f.setPixelSize(qMax(12, m_size * 2 / 5));
        f.setBold(true);
        painter.setFont(f);
        painter.setPen(Qt::white);
        painter.drawText(targetRect, Qt::AlignCenter, m_text.isEmpty() ? QStringLiteral("Q") : m_text);
    }

    painter.setClipping(false);
    if (m_status != None) {
        const int statusSize = qMax(8, m_size / 5);
        const int offset = qMax(2, m_size / 10);
        const QRect statusRect(targetRect.right() - statusSize - offset,
                               targetRect.bottom() - statusSize - offset,
                               statusSize, statusSize);
        QColor color;
        switch (m_status) {
        case Online: color = QColor(QStringLiteral("#52c41a")); break;
        case Away: color = QColor(QStringLiteral("#faad14")); break;
        default: color = QColor(QStringLiteral("#8f959e")); break;
        }
        painter.setBrush(color);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawEllipse(statusRect);
    }

    QLabel::paintEvent(event);
}

#include "windows/screenshotcapturewindow.h"

#include "screenshotgeometry.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

ScreenshotCaptureWindow::ScreenshotCaptureWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("screenshotCaptureWindow"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setModal(true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void ScreenshotCaptureWindow::setScreenshot(const QPixmap& pixmap)
{
    m_screenshot = pixmap;
    update();
}

void ScreenshotCaptureWindow::setCaptureScreen(QScreen* screen)
{
    m_screen = screen;
    if (m_screen) {
        setGeometry(m_screen->geometry());
    }
}

void ScreenshotCaptureWindow::setCaptureGeometry(const QRect& geometry)
{
    setGeometry(geometry);
}

void ScreenshotCaptureWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        cancelSelection();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (m_hasSelection && cancelButtonRect().contains(event->pos())) {
            cancelSelection();
            return;
        }
        if (m_hasSelection && confirmButtonRect().contains(event->pos())) {
            confirmSelection();
            return;
        }
        m_startPos = event->pos();
        m_endPos = m_startPos;
        m_selecting = true;
        m_hasSelection = false;
        update();
    }
}

void ScreenshotCaptureWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting) {
        m_endPos = event->pos();
        m_hasSelection = true;
        update();
    }
}

void ScreenshotCaptureWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_selecting = false;
        m_endPos = event->pos();
        m_hasSelection = hasUsableSelection();
        update();
    }
}

void ScreenshotCaptureWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawPixmap(rect(), m_screenshot);

    const QRect selection = selectionRect();
    painter.fillRect(rect(), QColor(0, 0, 0, m_hasSelection ? 108 : 48));
    if (!m_hasSelection) {
        painter.setPen(QColor(255, 255, 255, 220));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 11));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("拖动鼠标选择截图区域  ·  Esc 取消"));
        return;
    }

    painter.drawPixmap(selection, m_screenshot, selectedSourceRect());
    painter.fillRect(selection, QColor(18, 183, 255, 25));
    painter.setPen(QPen(QColor(QStringLiteral("#12b7ff")), 1));
    painter.drawRect(selection.adjusted(0, 0, -1, -1));

    const QString dimensions = QStringLiteral("%1 × %2").arg(selection.width()).arg(selection.height());
    QFont labelFont(QStringLiteral("Segoe UI"), 9);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    const QRect labelRect(selection.left(), qMax(0, selection.top() - 28), 100, 24);
    painter.fillRect(labelRect, QColor(21, 21, 21, 235));
    painter.setPen(Qt::white);
    painter.drawText(labelRect, Qt::AlignCenter, dimensions);

    const QRect tools = toolbarRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(21, 21, 21, 238));
    painter.drawRoundedRect(tools, 7, 7);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));

    const QRect cancel = cancelButtonRect();
    painter.setPen(QColor(232, 232, 232));
    painter.drawText(cancel, Qt::AlignCenter, QStringLiteral("取消"));

    const QRect confirm = confirmButtonRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#12b7ff")));
    painter.drawRoundedRect(confirm, 5, 5);
    painter.setPen(Qt::white);
    painter.drawText(confirm, Qt::AlignCenter, QStringLiteral("完成"));
}

QRect ScreenshotCaptureWindow::selectedSourceRect() const
{
    const QRect selection = selectionRect();
    if (m_screenshot.isNull() || selection.isEmpty() || width() <= 0 || height() <= 0) {
        return QRect();
    }
    return ScreenshotGeometry::selectionToSource(selection, size(), m_screenshot.size());
}

QRect ScreenshotCaptureWindow::selectionRect() const
{
    return QRect(m_startPos, m_endPos).normalized().intersected(rect());
}

QRect ScreenshotCaptureWindow::toolbarRect() const
{
    const QRect selection = selectionRect();
    if (selection.isEmpty()) return QRect();
    const QSize toolSize(132, 38);
    const int x = qBound(8, selection.right() - toolSize.width() + 1, width() - toolSize.width() - 8);
    int y = selection.bottom() + 10;
    if (y + toolSize.height() > height() - 8) {
        y = qMax(8, selection.top() - toolSize.height() - 10);
    }
    return QRect(x, y, toolSize.width(), toolSize.height());
}

QRect ScreenshotCaptureWindow::cancelButtonRect() const
{
    const QRect tools = toolbarRect();
    return QRect(tools.left() + 6, tools.top() + 5, 56, tools.height() - 10);
}

QRect ScreenshotCaptureWindow::confirmButtonRect() const
{
    const QRect tools = toolbarRect();
    return QRect(tools.right() - 62, tools.top() + 5, 56, tools.height() - 10);
}

bool ScreenshotCaptureWindow::hasUsableSelection() const
{
    const QRect selection = selectionRect();
    return selection.width() >= 4 && selection.height() >= 4;
}

void ScreenshotCaptureWindow::confirmSelection()
{
    if (!hasUsableSelection()) return;
    const QRect source = selectedSourceRect();
    if (!source.isEmpty()) {
        emit saveRequested(m_screenshot.copy(source));
    }
    accept();
}

void ScreenshotCaptureWindow::cancelSelection()
{
    emit cancelRequested();
    reject();
}

void ScreenshotCaptureWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelSelection();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && hasUsableSelection()) {
        confirmSelection();
        return;
    }
    QDialog::keyPressEvent(event);
}


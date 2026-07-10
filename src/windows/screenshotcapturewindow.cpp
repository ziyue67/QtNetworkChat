#include "windows/screenshotcapturewindow.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

ScreenshotCaptureWindow::ScreenshotCaptureWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("screenshotCaptureWindow"));
    setWindowTitle(QStringLiteral("截图"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ScreenshotCaptureWindow::updateStyle);
}

void ScreenshotCaptureWindow::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setObjectName(QStringLiteral("screenshotPreviewLabel"));
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->installEventFilter(this);
    root->addWidget(m_previewLabel, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(12, 8, 12, 8);
    btnLayout->setSpacing(8);

    QPushButton* saveBtn = new QPushButton(QStringLiteral("保存"), this);
    saveBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        if (m_hasSelection) {
            const QRect sourceRect = selectedSourceRect();
            emit saveRequested(sourceRect.isEmpty() ? m_screenshot : m_screenshot.copy(sourceRect));
        } else {
            emit saveRequested(m_screenshot);
        }
        accept();
    });
    btnLayout->addWidget(saveBtn);

    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        emit cancelRequested();
        reject();
    });
    btnLayout->addWidget(cancelBtn);

    btnLayout->addStretch();
    root->addLayout(btnLayout);
}

void ScreenshotCaptureWindow::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#screenshotCaptureWindow { background-color: %1; }"
        "QLabel#screenshotPreviewLabel { background-color: %1; }"
        "QPushButton#dialogPrimaryBtn { background-color: %4; color: white; border: none; border-radius: 6px; padding: 6px 14px; }"
        "QPushButton#dialogPrimaryBtn:hover { background-color: %5; }"
        "QPushButton#dialogSecondaryBtn { background-color: %2; color: %3; border: 1px solid %6; border-radius: 6px; padding: 6px 14px; }"
        "QPushButton#dialogSecondaryBtn:hover { background-color: %6; }"
    ).arg(tm->backgroundColor().name())
     .arg(tm->backgroundSecondaryColor().name())
     .arg(tm->textColor().name())
     .arg(tm->primaryColor().name())
     .arg(tm->primaryHoverColor().name())
     .arg(tm->borderColor().name()));
}

void ScreenshotCaptureWindow::setScreenshot(const QPixmap& pixmap)
{
    m_screenshot = pixmap;
    updatePreview();
}

void ScreenshotCaptureWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_startPos = event->pos();
        m_selecting = true;
        m_hasSelection = false;
    }
    QDialog::mousePressEvent(event);
}

void ScreenshotCaptureWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting) {
        m_endPos = event->pos();
        m_hasSelection = true;
        update();
    }
    QDialog::mouseMoveEvent(event);
}

void ScreenshotCaptureWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_selecting = false;
        m_endPos = event->pos();
    }
    QDialog::mouseReleaseEvent(event);
}

void ScreenshotCaptureWindow::paintEvent(QPaintEvent* event)
{
    QDialog::paintEvent(event);
    if (m_hasSelection) {
        QPainter painter(this);
        painter.setPen(QPen(QColor(QStringLiteral("#0099ff")), 2, Qt::DashLine));
        painter.setBrush(QColor(0, 153, 255, 30));
        QRect rect(m_previewLabel->mapTo(this, m_startPos),
                   m_previewLabel->mapTo(this, m_endPos));
        painter.drawRect(rect.normalized());
    }
}

void ScreenshotCaptureWindow::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updatePreview();
}

bool ScreenshotCaptureWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_previewLabel) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            mousePressEvent(static_cast<QMouseEvent*>(event));
            return true;
        case QEvent::MouseMove:
            mouseMoveEvent(static_cast<QMouseEvent*>(event));
            return true;
        case QEvent::MouseButtonRelease:
            mouseReleaseEvent(static_cast<QMouseEvent*>(event));
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

QRect ScreenshotCaptureWindow::selectedSourceRect() const
{
    const QPixmap preview = m_previewLabel->pixmap();
    if (m_screenshot.isNull() || preview.isNull()) {
        return QRect();
    }

    const QRect displayRect = m_previewLabel->contentsRect();
    const QSize displayedSize = preview.size();
    const QPoint displayTopLeft = displayRect.center() - QPoint(displayedSize.width() / 2, displayedSize.height() / 2);
    const QRect selected = QRect(m_startPos, m_endPos).normalized().intersected(
        QRect(displayTopLeft, displayedSize));
    if (selected.isEmpty()) return QRect();

    const qreal scaleX = static_cast<qreal>(m_screenshot.width()) / displayedSize.width();
    const qreal scaleY = static_cast<qreal>(m_screenshot.height()) / displayedSize.height();
    return QRect(qRound((selected.left() - displayTopLeft.x()) * scaleX),
                 qRound((selected.top() - displayTopLeft.y()) * scaleY),
                 qRound(selected.width() * scaleX),
                 qRound(selected.height() * scaleY)).intersected(m_screenshot.rect());
}

void ScreenshotCaptureWindow::updatePreview()
{
    if (m_screenshot.isNull() || !m_previewLabel || m_previewLabel->size().isEmpty()) return;
    m_previewLabel->setPixmap(m_screenshot.scaled(m_previewLabel->size(),
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
}


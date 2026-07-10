#include "windows/screenshotcapturewindow.h"

#include "theme/thememanager.h"

#include <QHBoxLayout>
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
    root->addWidget(m_previewLabel, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(12, 8, 12, 8);
    btnLayout->setSpacing(8);

    QPushButton* saveBtn = new QPushButton(QStringLiteral("保存"), this);
    saveBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        if (m_hasSelection) {
            QRect rect(m_startPos, m_endPos);
            rect = rect.normalized();
            emit saveRequested(m_screenshot.copy(rect));
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
    m_previewLabel->setPixmap(pixmap.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
        QRect rect(m_startPos, m_endPos);
        painter.drawRect(rect.normalized());
    }
}


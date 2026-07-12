#include "windows/imagepreviewwindow.h"

#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

ImagePreviewWindow::ImagePreviewWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("imagePreviewWindow"));
    setWindowTitle(QStringLiteral("图片预览"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(420, 320);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ImagePreviewWindow::updateStyle);
}

void ImagePreviewWindow::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = new DialogTitleBar(this, QStringLiteral("图片预览"));
    connect(m_titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(m_titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_scrollArea = new QScrollArea(body);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setObjectName(QStringLiteral("previewScrollArea"));

    m_imageLabel = new QLabel(m_scrollArea);
    m_imageLabel->setObjectName(QStringLiteral("previewImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setText(QStringLiteral("图片加载中"));
    m_scrollArea->setWidget(m_imageLabel);
    bodyLayout->addWidget(m_scrollArea, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(12, 8, 12, 8);
    btnLayout->setSpacing(8);

    btnLayout->addStretch();
    m_zoomOutBtn = new QPushButton(QStringLiteral("−"), body);
    m_zoomOutBtn->setObjectName(QStringLiteral("previewToolButton"));
    m_zoomOutBtn->setToolTip(QStringLiteral("缩小"));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, [this]() { setZoomFactor(m_zoomFactor - 0.1); });
    btnLayout->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel(QStringLiteral("100%"), body);
    m_zoomLabel->setObjectName(QStringLiteral("previewZoomLabel"));
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setFixedWidth(64);
    btnLayout->addWidget(m_zoomLabel);

    m_zoomInBtn = new QPushButton(QStringLiteral("+"), body);
    m_zoomInBtn->setObjectName(QStringLiteral("previewToolButton"));
    m_zoomInBtn->setToolTip(QStringLiteral("放大"));
    connect(m_zoomInBtn, &QPushButton::clicked, this, [this]() { setZoomFactor(m_zoomFactor + 0.1); });
    btnLayout->addWidget(m_zoomInBtn);

    m_resetBtn = new QPushButton(QStringLiteral("重置"), body);
    m_resetBtn->setObjectName(QStringLiteral("previewResetButton"));
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() { setZoomFactor(1.0); });
    btnLayout->addWidget(m_resetBtn);
    btnLayout->addStretch();

    bodyLayout->addLayout(btnLayout);
    root->addWidget(body, 1);
}

void ImagePreviewWindow::updateStyle()
{
    setStyleSheet(QStringLiteral(
        "QDialog#imagePreviewWindow { background-color: #1b1b1b; }"
        "QFrame#dialogTitleBar { background-color: #1b1b1b; border-bottom: 1px solid #353535; }"
        "QLabel#dialogTitleBarLabel { color: #f3f3f3; font-size: 14px; font-weight: 600; }"
        "QPushButton#dialogTitleBarCloseBtn { color: #f3f3f3; }"
        "QScrollArea#previewScrollArea { background-color: #1b1b1b; border: none; }"
        "QLabel#previewImageLabel { background-color: #1b1b1b; color: #a9a9ad; font-size: 14px; }"
        "QPushButton#previewToolButton { background: transparent; color: #f3f3f3; border: none; border-radius: 4px; font-size: 22px; min-width: 36px; min-height: 32px; }"
        "QPushButton#previewToolButton:hover, QPushButton#previewResetButton:hover { background-color: #2c2c2c; }"
        "QLabel#previewZoomLabel { color: #f3f3f3; font-size: 13px; font-weight: 600; }"
        "QPushButton#previewResetButton { background: transparent; color: #f3f3f3; border: none; border-radius: 4px; padding: 6px 12px; font-size: 13px; }"
    ));
    // DialogTitleBar owns a stylesheet, so parent QSS cannot reliably restyle
    // it. Apply the preview palette directly to prevent a white titlebar with
    // a white close glyph.
    if (m_titleBar) {
        m_titleBar->setStyleSheet(QStringLiteral(
            "QFrame#dialogTitleBar { background-color: #1b1b1b; border-bottom: 1px solid #353535; }"
            "QLabel#dialogTitleBarLabel { color: #f3f3f3; font-size: 14px; font-weight: 600; }"
            "QPushButton#dialogTitleBarCloseBtn { color: #f3f3f3; background: transparent; border: none; "
            "font-family: 'Segoe UI'; font-size: 14px; font-weight: 700; padding: 0; }"
            "QPushButton#dialogTitleBarCloseBtn:hover { background-color: #ff4d4f; color: white; }"));
    }
}

void ImagePreviewWindow::setImage(const QPixmap& pixmap)
{
    m_originalPixmap = pixmap;
    m_imageLabel->setText(QString());
    m_zoomFactor = 1.0;
    fitWindowToImage();
    fitImage();
}

void ImagePreviewWindow::setImagePath(const QString& path)
{
    m_currentPath = path;
    QPixmap pixmap(path);
    if (!pixmap.isNull()) {
        setImage(pixmap);
    } else {
        setErrorMessage(QStringLiteral("图片预览不可用"));
    }
    m_titleBar->setTitle(QStringLiteral("图片预览"));
}

void ImagePreviewWindow::setErrorMessage(const QString& message)
{
    m_originalPixmap = QPixmap();
    m_imageLabel->setPixmap(QPixmap());
    m_imageLabel->setText(message.trimmed().isEmpty() ? QStringLiteral("图片预览不可用") : message);
    m_imageLabel->setFixedSize(qMax(1, m_scrollArea->viewport()->width()), qMax(1, m_scrollArea->viewport()->height()));
    m_zoomLabel->setText(QStringLiteral("--"));
}

void ImagePreviewWindow::fitImage()
{
    if (m_originalPixmap.isNull()) return;
    if (!m_scrollArea) return;
    const QSize available = m_scrollArea->viewport()->size() - QSize(24, 24);
    if (available.width() <= 0 || available.height() <= 0) return;
    const QSize baseSize = m_originalPixmap.size().scaled(available, Qt::KeepAspectRatio);
    const QSize scaledSize(qMax(1, qRound(baseSize.width() * m_zoomFactor)),
                           qMax(1, qRound(baseSize.height() * m_zoomFactor)));
    QPixmap scaled = m_originalPixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
    m_imageLabel->setFixedSize(scaled.size());
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoomFactor * 100)));
}

void ImagePreviewWindow::fitWindowToImage()
{
    if (m_originalPixmap.isNull()) return;

    QScreen* screen = this->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    const QRect available = screen->availableGeometry();
    constexpr int titleBarHeight = 40;
    constexpr int toolbarHeight = 56;
    constexpr int padding = 24;
    const QSize minimum(420, 320);
    const QSize maximum(qMax(minimum.width(), qRound(available.width() * 0.92)),
                        qMax(minimum.height(), qRound(available.height() * 0.92)));
    const QSize maximumImage(maximum.width() - padding,
                             maximum.height() - titleBarHeight - toolbarHeight - padding);
    QSize imageSize = m_originalPixmap.size();
    imageSize.scale(maximumImage, Qt::KeepAspectRatio);
    const QSize target(qBound(minimum.width(), imageSize.width() + padding, maximum.width()),
                       qBound(minimum.height(), imageSize.height() + titleBarHeight + toolbarHeight + padding,
                              maximum.height()));
    m_initialWindowSize = target;
    resize(target);
    move(available.center() - rect().center());
}

void ImagePreviewWindow::setZoomFactor(qreal factor)
{
    m_zoomFactor = qBound<qreal>(0.2, factor, 6.0);
    fitImage();
}

void ImagePreviewWindow::showEvent(QShowEvent* event)
{
    if (m_initialWindowSize.isValid()) {
        resize(m_initialWindowSize);
    }
    QDialog::showEvent(event);
    if (layout()) layout()->activate();
    fitImage();

    // A top-level QScrollArea receives its final viewport geometry only after
    // the native window is mapped. Redraw on that first event-loop turn rather
    // than waiting for the user to drag or resize the window.
    QTimer::singleShot(0, this, [this]() {
        if (m_initialWindowSize.isValid()) resize(m_initialWindowSize);
        if (layout()) layout()->activate();
        fitImage();
    });
}

void ImagePreviewWindow::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    fitImage();
}

void ImagePreviewWindow::wheelEvent(QWheelEvent* event)
{
    if (!m_originalPixmap.isNull()) {
        setZoomFactor(m_zoomFactor + (event->angleDelta().y() > 0 ? 0.1 : -0.1));
        event->accept();
        return;
    }
    QDialog::wheelEvent(event);
}

#include "windows/imagepreviewwindow.h"

#include "theme/thememanager.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

ImagePreviewWindow::ImagePreviewWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("imagePreviewWindow"));
    setWindowTitle(QStringLiteral("图片预览"));
    setMinimumSize(400, 300);
    resize(800, 600);
    setupUi();
    updateStyle();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &ImagePreviewWindow::updateStyle);
}

void ImagePreviewWindow::setupUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setObjectName(QStringLiteral("previewScrollArea"));

    m_imageLabel = new QLabel(scrollArea);
    m_imageLabel->setObjectName(QStringLiteral("previewImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    scrollArea->setWidget(m_imageLabel);
    root->addWidget(scrollArea, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(12, 8, 12, 8);
    btnLayout->setSpacing(8);

    QPushButton* saveBtn = new QPushButton(QStringLiteral("保存"), this);
    saveBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存图片"), QStringLiteral("image.png"), QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty()) {
            emit saveRequested(path);
        }
    });
    btnLayout->addWidget(saveBtn);

    QPushButton* forwardBtn = new QPushButton(QStringLiteral("转发"), this);
    forwardBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(forwardBtn, &QPushButton::clicked, this, &ImagePreviewWindow::forwardRequested);
    btnLayout->addWidget(forwardBtn);

    btnLayout->addStretch();

    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeBtn);

    root->addLayout(btnLayout);
}

void ImagePreviewWindow::updateStyle()
{
    ThemeManager* tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "QDialog#imagePreviewWindow { background-color: %1; }"
        "QScrollArea#previewScrollArea { background-color: %1; border: none; }"
        "QLabel#previewImageLabel { background-color: %1; }"
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

void ImagePreviewWindow::setImage(const QPixmap& pixmap)
{
    m_originalPixmap = pixmap;
    fitImage();
}

void ImagePreviewWindow::setImagePath(const QString& path)
{
    m_currentPath = path;
    QPixmap pixmap(path);
    if (!pixmap.isNull()) {
        setImage(pixmap);
    }
}

void ImagePreviewWindow::fitImage()
{
    if (m_originalPixmap.isNull()) return;
    QSize available = size() - QSize(40, 80);
    QPixmap scaled = m_originalPixmap.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
    m_imageLabel->setFixedSize(scaled.size());
}


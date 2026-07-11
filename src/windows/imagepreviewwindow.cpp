#include "windows/imagepreviewwindow.h"

#include "theme/thememanager.h"
#include "widgets/dialogtitlebar.h"
#include "qqnt_backend_service.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

ImagePreviewWindow::ImagePreviewWindow(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("imagePreviewWindow"));
    setWindowTitle(QStringLiteral("图片预览"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
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

    m_titleBar = new DialogTitleBar(this, QStringLiteral("图片预览"));
    connect(m_titleBar, &DialogTitleBar::closeRequested, this, &QDialog::reject);
    root->addWidget(m_titleBar);

    QWidget* body = new QWidget(this);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    QScrollArea* scrollArea = new QScrollArea(body);
    scrollArea->setWidgetResizable(true);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setObjectName(QStringLiteral("previewScrollArea"));

    m_imageLabel = new QLabel(scrollArea);
    m_imageLabel->setObjectName(QStringLiteral("previewImageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    scrollArea->setWidget(m_imageLabel);
    bodyLayout->addWidget(scrollArea, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(12, 8, 12, 8);
    btnLayout->setSpacing(8);

    m_saveBtn = new QPushButton(QStringLiteral("保存"), body);
    m_saveBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存图片"), QStringLiteral("image.png"), QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty()) {
            emit saveRequested(path);
        }
    });
    btnLayout->addWidget(m_saveBtn);

    m_forwardBtn = new QPushButton(QStringLiteral("转发"), body);
    m_forwardBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(m_forwardBtn, &QPushButton::clicked, this, &ImagePreviewWindow::forwardRequested);
    btnLayout->addWidget(m_forwardBtn);

    m_openFolderBtn = new QPushButton(QStringLiteral("打开文件夹"), body);
    m_openFolderBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentPath.isEmpty()) {
            emit openFolderRequested();
        } else {
            openImageFolder();
        }
    });
    btnLayout->addWidget(m_openFolderBtn);

    m_copyBase64Btn = new QPushButton(QStringLiteral("复制 Base64"), body);
    m_copyBase64Btn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(m_copyBase64Btn, &QPushButton::clicked, this, [this]() {
        if (!m_currentPath.isEmpty()) {
            emit copyBase64Requested();
        } else {
            copyImageBase64();
        }
    });
    btnLayout->addWidget(m_copyBase64Btn);

    btnLayout->addStretch();

    m_closeBtn = new QPushButton(QStringLiteral("关闭"), body);
    m_closeBtn->setObjectName(QStringLiteral("dialogSecondaryBtn"));
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_closeBtn);

    bodyLayout->addLayout(btnLayout);
    root->addWidget(body, 1);
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
    m_titleBar->setTitle(QStringLiteral("图片预览 - %1").arg(QFileInfo(path).fileName()));
}

void ImagePreviewWindow::fitImage()
{
    if (m_originalPixmap.isNull()) return;
    QSize available = size() - QSize(40, 80);
    QPixmap scaled = m_originalPixmap.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
    m_imageLabel->setFixedSize(scaled.size());
}

void ImagePreviewWindow::copyImageBase64()
{
    QString sourcePath = m_currentPath;
    if (sourcePath.isEmpty()) {
        const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        sourcePath = QDir(tempDir).filePath(QStringLiteral("image-preview-temp.png"));
        if (!m_originalPixmap.isNull() && !m_originalPixmap.save(sourcePath, "PNG")) {
            QMessageBox::warning(this, QStringLiteral("复制失败"), QStringLiteral("无法准备图片数据。"));
            return;
        }
    }

    QJsonObject payload;
    payload[QStringLiteral("filePath")] = sourcePath;
    QJsonObject response;
    QString errorCode;
    QString errorMessage;
    if (QQNTBackendService::handle(QStringLiteral("read_image_base64"), payload, &response, &errorCode, &errorMessage)) {
        const QString b64 = response.value(QStringLiteral("base64")).toString();
        if (!b64.isEmpty()) {
            QClipboard* clipboard = QGuiApplication::clipboard();
            if (clipboard) clipboard->setText(b64);
            QMessageBox::information(this, QStringLiteral("已复制"), QStringLiteral("图片 Base64 已复制到剪贴板。"));
            return;
        }
    }
    QMessageBox::warning(this, QStringLiteral("复制失败"), QStringLiteral("无法读取图片 Base64。"));
}

void ImagePreviewWindow::openImageFolder()
{
    QString sourcePath = m_currentPath;
    if (sourcePath.isEmpty()) {
        const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        sourcePath = QDir(tempDir).filePath(QStringLiteral("image-preview-temp.png"));
        if (!m_originalPixmap.isNull() && !m_originalPixmap.save(sourcePath, "PNG")) {
            QMessageBox::warning(this, QStringLiteral("打开失败"), QStringLiteral("无法准备图片文件。"));
            return;
        }
    }

    const QFileInfo info(sourcePath);
    if (info.isFile()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    } else {
        QMessageBox::warning(this, QStringLiteral("打开失败"), QStringLiteral("图片路径无效。"));
    }
}


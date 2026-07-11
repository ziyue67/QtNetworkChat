#ifndef IMAGEPREVIEWWINDOW_H
#define IMAGEPREVIEWWINDOW_H

#include <QDialog>

class DialogTitleBar;
class QLabel;
class QPushButton;

class ImagePreviewWindow : public QDialog {
    Q_OBJECT

public:
    explicit ImagePreviewWindow(QWidget* parent = nullptr);

    void setImage(const QPixmap& pixmap);
    void setImagePath(const QString& path);

signals:
    void saveRequested(const QString& path);
    void forwardRequested();
    void openFolderRequested();
    void copyBase64Requested();

private:
    void setupUi();
    void updateStyle();
    void fitImage();
    void copyImageBase64();
    void openImageFolder();

    QLabel* m_imageLabel = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_forwardBtn = nullptr;
    QPushButton* m_openFolderBtn = nullptr;
    QPushButton* m_copyBase64Btn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    DialogTitleBar* m_titleBar = nullptr;
    QPixmap m_originalPixmap;
    QString m_currentPath;
};

#endif // IMAGEPREVIEWWINDOW_H


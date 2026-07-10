#ifndef IMAGEPREVIEWWINDOW_H
#define IMAGEPREVIEWWINDOW_H

#include <QDialog>

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

private:
    void setupUi();
    void updateStyle();
    void fitImage();

    QLabel* m_imageLabel = nullptr;
    QPixmap m_originalPixmap;
    QString m_currentPath;
};

#endif // IMAGEPREVIEWWINDOW_H


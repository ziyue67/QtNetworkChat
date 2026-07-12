#ifndef IMAGEPREVIEWWINDOW_H
#define IMAGEPREVIEWWINDOW_H

#include <QDialog>

class DialogTitleBar;
class QLabel;
class QPushButton;
class QScrollArea;
class QResizeEvent;
class QWheelEvent;
class QShowEvent;

class ImagePreviewWindow : public QDialog {
    Q_OBJECT

public:
    explicit ImagePreviewWindow(QWidget* parent = nullptr);

    void setImage(const QPixmap& pixmap);
    void setImagePath(const QString& path);
    void setErrorMessage(const QString& message);

signals:
    void saveRequested(const QString& path);
    void forwardRequested();
    void openFolderRequested();
    void copyBase64Requested();

private:
    void setupUi();
    void updateStyle();
    void fitImage();
    void fitWindowToImage();
    void setZoomFactor(qreal factor);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    QLabel* m_imageLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QPushButton* m_zoomOutBtn = nullptr;
    QPushButton* m_zoomInBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    DialogTitleBar* m_titleBar = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QPixmap m_originalPixmap;
    QString m_currentPath;
    qreal m_zoomFactor = 1.0;
    QSize m_initialWindowSize;
};

#endif // IMAGEPREVIEWWINDOW_H

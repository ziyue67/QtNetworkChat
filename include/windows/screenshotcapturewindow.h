#ifndef SCREENSHOTCAPTUREWINDOW_H
#define SCREENSHOTCAPTUREWINDOW_H

#include <QDialog>

class QScreen;

class ScreenshotCaptureWindow : public QDialog {
    Q_OBJECT

public:
    explicit ScreenshotCaptureWindow(QWidget* parent = nullptr);

    void setScreenshot(const QPixmap& pixmap);
    void setCaptureScreen(QScreen* screen);

signals:
    void saveRequested(const QPixmap& pixmap);
    void cancelRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRect selectionRect() const;
    QRect selectedSourceRect() const;
    QRect toolbarRect() const;
    QRect cancelButtonRect() const;
    QRect confirmButtonRect() const;
    bool hasUsableSelection() const;
    void confirmSelection();
    void cancelSelection();

    QPixmap m_screenshot;
    QPoint m_startPos;
    QPoint m_endPos;
    QScreen* m_screen = nullptr;
    bool m_selecting = false;
    bool m_hasSelection = false;
};

#endif // SCREENSHOTCAPTUREWINDOW_H


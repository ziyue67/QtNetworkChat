#ifndef SCREENSHOTCAPTUREWINDOW_H
#define SCREENSHOTCAPTUREWINDOW_H

#include <QDialog>

class QLabel;
class QPushButton;

class ScreenshotCaptureWindow : public QDialog {
    Q_OBJECT

public:
    explicit ScreenshotCaptureWindow(QWidget* parent = nullptr);

    void setScreenshot(const QPixmap& pixmap);

signals:
    void saveRequested(const QPixmap& pixmap);
    void cancelRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUi();
    void updateStyle();

    QLabel* m_previewLabel = nullptr;
    QPixmap m_screenshot;
    QPixmap m_captured;
    QPoint m_startPos;
    QPoint m_endPos;
    bool m_selecting = false;
    bool m_hasSelection = false;
};

#endif // SCREENSHOTCAPTUREWINDOW_H


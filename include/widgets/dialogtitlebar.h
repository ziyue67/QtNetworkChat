#ifndef DIALOGTITLEBAR_H
#define DIALOGTITLEBAR_H

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;

class DialogTitleBar : public QFrame {
    Q_OBJECT

public:
    explicit DialogTitleBar(QWidget* parent = nullptr, const QString& title = QString(), bool showCloseButton = true);

    void setTitle(const QString& title);
    void setCloseButtonVisible(bool visible);

signals:
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void setupUi(bool showCloseButton);
    void updateStyle();

    QLabel* m_titleLabel = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QPoint m_dragStartPos;
    bool m_dragging = false;
};

#endif // DIALOGTITLEBAR_H

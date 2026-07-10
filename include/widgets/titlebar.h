#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;
class AvatarLabel;
class ThemeManager;

class TitleBar : public QFrame {
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

    void setUserName(const QString& name);
    void setUserId(const QString& userId);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void setupUi();
    void updateStyle();

    QLabel* m_logoLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_userLabel = nullptr;
    AvatarLabel* m_avatar = nullptr;
    QPushButton* m_minBtn = nullptr;
    QPushButton* m_maxBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QPoint m_dragStartPos;
    bool m_dragging = false;
};

#endif // TITLEBAR_H

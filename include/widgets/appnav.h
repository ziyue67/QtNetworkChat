#ifndef APPNAV_H
#define APPNAV_H

#include <QFrame>
#include <QString>

class QVBoxLayout;

class AppNav : public QFrame {
    Q_OBJECT

public:
    explicit AppNav(QWidget* parent = nullptr);

    void setUnreadCount(int count);
    int currentIndex() const;
    void setCurrentIndex(int index);

signals:
    void routeActivated(const QString& route);

private:
    void setupUi();
    void updateStyle();
    void addNavItem(const QString& icon, const QString& label, const QString& route, bool mock = false);

    QVBoxLayout* m_layout = nullptr;
    int m_currentIndex = 0;
    int m_unreadCount = 0;
    QList<QWidget*> m_items;
};

#endif // APPNAV_H

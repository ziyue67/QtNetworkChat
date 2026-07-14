#ifndef CONTACTNOTICEPANEL_H
#define CONTACTNOTICEPANEL_H

#include <QWidget>

class QLabel;
class QPushButton;

class ContactNoticePanel : public QWidget {
    Q_OBJECT

public:
    enum NoticeType { FriendNotice, GroupNotice };

    explicit ContactNoticePanel(QWidget* parent = nullptr);

    void setNoticeType(NoticeType type);
    void setUnread(bool unread);

signals:
    void filterRequested();
    void clearRequested();

private:
    void setupUi();
    void updateStyle();

    QLabel* m_icon = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QPushButton* m_filterBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    NoticeType m_type = FriendNotice;
    bool m_unread = false;
};

#endif // CONTACTNOTICEPANEL_H

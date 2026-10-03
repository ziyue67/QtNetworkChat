#ifndef CONTACTCARD_H
#define CONTACTCARD_H

#include "widgets/contactlistwidget.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QListView;
class QStandardItemModel;

class ContactCard : public QWidget {
    Q_OBJECT

public:
    explicit ContactCard(QWidget* parent = nullptr);

    void setFriendData(const ContactDisplayData& data);
    void setGroupData(const ContactDisplayData& group,
                      const QList<ContactDisplayData>& members);
    void clear();

signals:
    void sendMessageRequested(const QString& id, bool isGroup);

private:
    void setupUi();
    void updateStyle();
    void setAvatar(const QString& nickname, const QString& id = QString());
    void refreshMembers(const QList<ContactDisplayData>& members);

    QLabel* m_avatar = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_state = nullptr;
    QLabel* m_signature = nullptr;
    QLabel* m_remark = nullptr;
    QFrame* m_announcementCard = nullptr;
    QLabel* m_announcementTitle = nullptr;
    QLabel* m_announcementBody = nullptr;
    QLabel* m_memberTitle = nullptr;
    QLabel* m_memberCount = nullptr;
    QListView* m_memberGrid = nullptr;
    QStandardItemModel* m_memberModel = nullptr;
    QPushButton* m_actionBtn = nullptr;

    QString m_currentId;
    bool m_isGroup = false;
};

#endif // CONTACTCARD_H

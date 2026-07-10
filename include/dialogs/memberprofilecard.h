#ifndef MEMBERPROFILECARD_H
#define MEMBERPROFILECARD_H

#include <QDialog>

class AvatarLabel;
class QLabel;
class QPushButton;

class MemberProfileCard : public QDialog {
    Q_OBJECT

public:
    explicit MemberProfileCard(QWidget* parent = nullptr);

    void setMemberInfo(const QString& userId, const QString& userName, const QString& role = QString(), const QString& joinDate = QString());

signals:
    void sendMessageRequested(const QString& userId);
    void addFriendRequested(const QString& userId);

private:
    void setupUi();
    void updateStyle();

    AvatarLabel* m_avatar = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_idLabel = nullptr;
    QLabel* m_roleLabel = nullptr;
    QLabel* m_joinDateLabel = nullptr;
    QPushButton* m_messageBtn = nullptr;
    QPushButton* m_friendBtn = nullptr;
    QString m_currentUserId;
};

#endif // MEMBERPROFILECARD_H


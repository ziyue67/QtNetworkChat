#ifndef PROFILEVIEW_H
#define PROFILEVIEW_H

#include <QWidget>

class AvatarLabel;
class QLabel;
class QPushButton;

class ProfileView : public QWidget {
    Q_OBJECT

public:
    explicit ProfileView(QWidget* parent = nullptr);

    void setUserInfo(const QString& userId, const QString& userName, const QString& signature = QString());
    void setStats(int friendCount, int groupCount, int messageCount);

signals:
    void editProfileRequested();
    void changeAvatarRequested();
    void logoutRequested();

private:
    void setupUi();
    void updateStyle();

    AvatarLabel* m_avatar = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_idLabel = nullptr;
    QLabel* m_signatureLabel = nullptr;
    QLabel* m_friendCountLabel = nullptr;
    QLabel* m_groupCountLabel = nullptr;
    QLabel* m_messageCountLabel = nullptr;
    QPushButton* m_editBtn = nullptr;
    QPushButton* m_avatarBtn = nullptr;
    QPushButton* m_logoutBtn = nullptr;
};

#endif // PROFILEVIEW_H


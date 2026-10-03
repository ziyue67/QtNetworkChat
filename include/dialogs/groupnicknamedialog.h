#ifndef GROUPNICKNAMEDIALOG_H
#define GROUPNICKNAMEDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;

class GroupNicknameDialog : public QDialog {
    Q_OBJECT

public:
    explicit GroupNicknameDialog(QWidget* parent = nullptr);

    QString nickname() const;
    void setCurrentNickname(const QString& nickname);

signals:
    void nicknameConfirmed(const QString& nickname);

private:
    void setupUi();
    void updateStyle();

    QLineEdit* m_nicknameEdit = nullptr;
    QPushButton* m_confirmBtn = nullptr;
};

#endif // GROUPNICKNAMEDIALOG_H


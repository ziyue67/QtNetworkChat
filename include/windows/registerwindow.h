#ifndef REGISTERWINDOW_H
#define REGISTERWINDOW_H

#include <QDialog>

class AvatarLabel;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;

class RegisterWindow : public QDialog {
    Q_OBJECT

public:
    explicit RegisterWindow(QWidget* parent = nullptr);

    QString userName() const;
    QString password() const;
    QString account() const;
    bool agreedToTerms() const;

signals:
    void registerRequested(const QString& userName, const QString& password);
    void loginLinkClicked();

private:
    void setupUi();
    void updateStyle();
    void updateFormState();

    AvatarLabel* m_avatar = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QLineEdit* m_confirmEdit = nullptr;
    QCheckBox* m_agreementCheck = nullptr;
    QPushButton* m_registerBtn = nullptr;
    QPushButton* m_loginLinkBtn = nullptr;
};

#endif // REGISTERWINDOW_H


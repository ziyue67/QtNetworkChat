#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QDialog>

#include "logincredentialstore.h"

class AvatarLabel;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;

class LoginWindow : public QDialog {
    Q_OBJECT

public:
    explicit LoginWindow(QWidget* parent = nullptr);

    QString userName() const;
    QString account() const;
    QString password() const;
    QString serverAddress() const;
    quint16 serverPort() const;
    bool rememberPassword() const;
    bool registerMode() const;
    QString loginMode() const;

    void setRegisterMode(bool registerMode);

signals:
    void loginRequested(const QString& account, const QString& password);
    void registerLinkClicked();

public slots:
    bool saveResolvedLoginToSqlite(const QString& account, const QString& userName, bool rememberPassword);

private:
    void setupUi();
    void updateStyle();
    void updateFormState();
    void loadSettings();
    bool loadLoginFromSqlite();

    LoginCredentialStore m_loginCredentialStore;

    AvatarLabel* m_avatar = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QLineEdit* m_accountEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QLineEdit* m_confirmEdit = nullptr;
    QCheckBox* m_agreementCheck = nullptr;
    QCheckBox* m_rememberCheck = nullptr;
    QPushButton* m_okBtn = nullptr;
    QPushButton* m_registerLinkBtn = nullptr;
    QPushButton* m_loginLinkBtn = nullptr;
    QPushButton* m_resetPasswordBtn = nullptr;

    QString m_account;
    QString m_userName;
    QString m_password;
    QString m_host;
    quint16 m_port = 8888;
    bool m_registerMode = false;
    bool m_resetPasswordMode = false;
};

#endif // LOGINWINDOW_H


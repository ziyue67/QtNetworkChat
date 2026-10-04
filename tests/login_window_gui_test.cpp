#include "windows/loginwindow.h"
#include "gui_test_support.h"
#include "theme/thememanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>

namespace {
using GuiTestSupport::expect;

template <typename Widget>
Widget* findByText(QWidget& parent, const QString& text) {
    for (Widget* widget : parent.findChildren<Widget*>()) {
        if (widget->text() == text) {
            return widget;
        }
    }
    return nullptr;
}

QLineEdit* findByPlaceholder(QWidget& parent, const QString& text) {
    for (QLineEdit* edit : parent.findChildren<QLineEdit*>()) {
        if (edit->placeholderText() == text) {
            return edit;
        }
    }
    return nullptr;
}

bool checkScreenshot(QWidget& window, const QString& path) {
    return GuiTestSupport::captureScreenshot(window, path);
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("QtNetworkChatTests"));
    QApplication::setApplicationName(QStringLiteral("login_window_gui_test"));
    QStandardPaths::setTestModeEnabled(true);
    if (!GuiTestSupport::prepareFont()) return 1;

    LoginWindow window;
    window.show();
    QApplication::processEvents();

    QLineEdit* account = findByPlaceholder(window, QStringLiteral("QQ 号 / 账号"));
    QLineEdit* name = findByPlaceholder(window, QStringLiteral("昵称"));
    QLineEdit* password = findByPlaceholder(window, QStringLiteral("密码"));
    QLineEdit* confirm = findByPlaceholder(window, QStringLiteral("确认密码"));
    QPushButton* submit = window.findChild<QPushButton*>(QStringLiteral("loginPrimaryBtn"));
    QPushButton* registerLink = findByText<QPushButton>(window, QStringLiteral("注册账号"));
    QPushButton* loginLink = findByText<QPushButton>(window, QStringLiteral("已有账号？去登录"));
    QPushButton* resetLink = findByText<QPushButton>(window, QStringLiteral("重置本地密码"));
    QCheckBox* agreement = findByText<QCheckBox>(window, QStringLiteral("我已阅读并同意服务协议和隐私政策"));
    bool ok = expect(account && name && password && confirm && submit && registerLink
                     && loginLink && resetLink && agreement,
                     "login controls should be present");
    if (!ok) {
        return 1;
    }

    ok = expect(account->isVisible() && !name->isVisible() && !confirm->isVisible(),
                "login mode should show account but hide registration fields") && ok;
    ok = expect(!submit->isEnabled(), "incomplete login should not submit") && ok;
    const QString screenshotDir = QCoreApplication::applicationDirPath();
    ok = checkScreenshot(window, QDir(screenshotDir).filePath(QStringLiteral("login_window_light.png"))) && ok;

    QTest::mouseClick(registerLink, Qt::LeftButton);
    ok = expect(window.registerMode() && name->isVisible() && confirm->isVisible()
                    && !account->isVisible() && !submit->isEnabled(),
                "register link should switch to registration form") && ok;
    name->setText(QStringLiteral("GuiUser"));
    password->setText(QStringLiteral("secret123"));
    confirm->setText(QStringLiteral("different"));
    ok = expect(!submit->isEnabled(), "mismatched confirmation should not submit") && ok;
    confirm->setText(QStringLiteral("secret123"));
    ok = expect(submit->isEnabled(), "complete registration should submit") && ok;
    ok = checkScreenshot(window, QDir(screenshotDir).filePath(QStringLiteral("login_window_register.png"))) && ok;

    QTest::mouseClick(loginLink, Qt::LeftButton);
    QTest::mouseClick(resetLink, Qt::LeftButton);
    ok = expect(window.loginMode() == QStringLiteral("reset_password"),
                "reset link should select local password reset") && ok;
    QTest::mouseClick(registerLink, Qt::LeftButton);
    QTest::mouseClick(loginLink, Qt::LeftButton);
    ok = expect(window.loginMode() == QStringLiteral("login") && account->isVisible(),
                "login link should restore login form") && ok;

    ThemeManager::instance()->setTheme(ThemeManager::Theme::Dark);
    ok = checkScreenshot(window, QDir(screenshotDir).filePath(QStringLiteral("login_window_dark.png"))) && ok;
    agreement->setChecked(false);
    account->setText(QStringLiteral("910100"));
    ok = expect(!submit->isEnabled(), "agreement must gate login") && ok;
    agreement->setChecked(true);
    password->setText(QStringLiteral("secret123"));
    ok = expect(submit->isEnabled(), "complete login should submit") && ok;
    if (ok) {
        QTest::mouseClick(submit, Qt::LeftButton);
        ok = expect(window.result() == QDialog::Accepted
                        && window.account() == QStringLiteral("910100")
                        && window.password() == QStringLiteral("secret123"),
                    "login submit should return entered credentials") && ok;
    }
    return ok ? 0 : 1;
}

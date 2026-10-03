#ifndef SETTINGSVIEW_H
#define SETTINGSVIEW_H

#include <QWidget>

class QTabWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QLineEdit;

class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(QWidget* parent = nullptr);

    void setAccountInfo(const QString& userName, const QString& userId);
    void setThemeMode(int mode); // 0=light, 1=dark, 2=system
    void setSyncStatus(const QString& text);

signals:
    void themeModeChanged(int mode); // 0=light, 1=dark, 2=system
    void notificationsToggled(bool enabled);
    void soundToggled(bool enabled);
    void desktopNotificationsToggled(bool enabled);
    void muteInSessionToggled(bool enabled);
    void e2eEnabledToggled(bool enabled);
    void autoAcceptFilesToggled(bool enabled);
    void openFolderAfterDownloadToggled(bool enabled);
    void hideWindowBeforeScreenshotToggled(bool enabled);
    void downloadPathChangeRequested();
    void screenshotShortcutChangeRequested();
    void logoutRequested();

private:
    void setupUi();
    void updateStyle();
    void onTabChanged(int index);

    QWidget* buildGeneralTab();
    QWidget* buildAccountTab();
    QWidget* buildNotificationTab();
    QWidget* buildShortcutTab();
    QWidget* buildFileTab();
    QWidget* buildE2ETab();
    QWidget* buildAboutTab();

    // Tab sidebar
    QTabWidget* m_tabWidget = nullptr;

    // General tab
    QPushButton* m_lightBtn = nullptr;
    QPushButton* m_darkBtn = nullptr;
    QPushButton* m_systemThemeBtn = nullptr;

    // Account tab
    QLabel* m_accountNameLabel = nullptr;
    QLabel* m_accountIdLabel = nullptr;

    // Notification tab
    QCheckBox* m_notifyCheck = nullptr;
    QCheckBox* m_soundCheck = nullptr;
    QCheckBox* m_desktopNotifyCheck = nullptr;
    QCheckBox* m_muteInSessionCheck = nullptr;

    // Shortcut tab
    QPushButton* m_screenshotShortcutBtn = nullptr;
    QCheckBox* m_hideWindowCheck = nullptr;

    // File tab
    QPushButton* m_downloadPathBtn = nullptr;
    QCheckBox* m_autoAcceptCheck = nullptr;
    QCheckBox* m_openFolderCheck = nullptr;

    // E2E tab
    QCheckBox* m_e2eCheck = nullptr;

    // Sync status
    QLabel* m_syncStatus = nullptr;
};

#endif // SETTINGSVIEW_H

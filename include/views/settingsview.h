#ifndef SETTINGSVIEW_H
#define SETTINGSVIEW_H

#include <QWidget>

class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(QWidget* parent = nullptr);

signals:
    void themeToggled();
    void notificationsToggled(bool enabled);

private:
    void setupUi();
    void updateStyle();
};

#endif // SETTINGSVIEW_H

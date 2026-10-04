#pragma once

#include <QString>

class AvatarLabel;
class QCheckBox;
class QDialog;
class QPixmap;
class QPushButton;
class QToolButton;
class QVBoxLayout;

struct GroupInfoOverviewWidgets {
    QToolButton* avatarButton = nullptr;
    AvatarLabel* avatar = nullptr;
    QToolButton* shareButton = nullptr;
};

// Builds the view; MainWindow owns permissions, persistence, and network callbacks.
class GroupInfoPanelUi {
public:
    GroupInfoPanelUi(QDialog& panel, QVBoxLayout& contentLayout);

    void addCaption(const QString& text);
    QPushButton* addRow(const QString& title, const QString& value = QString(),
                        bool clickable = false);
    QCheckBox* addToggleRow(const QString& title, const QString& toolTip = QString(),
                            int height = 42);
    GroupInfoOverviewWidgets addOverview(const QString& groupId, const QString& groupName,
                                         int memberCount, bool serverGroup, bool manager,
                                         const QPixmap& avatarPixmap);
    QPushButton* addAnnouncement(const QString& announcement, bool manager);

    static void applyStyle(QDialog& panel);

private:
    QDialog& m_panel;
    QVBoxLayout& m_contentLayout;
    QVBoxLayout* m_activeSettingsLayout = nullptr;
};

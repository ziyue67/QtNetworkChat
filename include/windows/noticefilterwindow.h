#ifndef NOTICEFILTERWINDOW_H
#define NOTICEFILTERWINDOW_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

class NoticeFilterWindow : public QDialog {
    Q_OBJECT

public:
    explicit NoticeFilterWindow(QWidget* parent = nullptr);

    bool friendNotificationsEnabled() const;
    bool groupNotificationsEnabled() const;
    bool mentionNotificationsEnabled() const;
    int priorityFilter() const;

signals:
    void filterChanged();

private:
    void setupUi();
    void updateStyle();

    QCheckBox* m_friendCheck = nullptr;
    QCheckBox* m_groupCheck = nullptr;
    QCheckBox* m_mentionCheck = nullptr;
    QComboBox* m_priorityCombo = nullptr;
};

#endif // NOTICEFILTERWINDOW_H


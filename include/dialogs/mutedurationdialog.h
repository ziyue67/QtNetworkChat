#ifndef MUTEDURATIONDIALOG_H
#define MUTEDURATIONDIALOG_H

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;

class MuteDurationDialog : public QDialog {
    Q_OBJECT

public:
    explicit MuteDurationDialog(QWidget* parent = nullptr);

    int durationMinutes() const;
    bool isPermanent() const;

signals:
    void muteConfirmed(int minutes);

private:
    void setupUi();
    void updateStyle();

    QComboBox* m_durationCombo = nullptr;
    QPushButton* m_confirmBtn = nullptr;
};

#endif // MUTEDURATIONDIALOG_H


#ifndef ESSENCEPANEL_H
#define ESSENCEPANEL_H

#include <QDialog>

class QListView;
class QStandardItemModel;
class QLabel;
class QPushButton;

class EssencePanel : public QDialog {
    Q_OBJECT

public:
    explicit EssencePanel(QWidget* parent = nullptr);

    void addEssenceMessage(const QString& messageId, const QString& sender, const QString& text, const QString& time);
    void clearMessages();

signals:
    void messageActivated(const QString& messageId);

private:
    void setupUi();
    void updateStyle();

    QListView* m_listView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QLabel* m_titleLabel = nullptr;
};

#endif // ESSENCEPANEL_H


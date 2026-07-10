#ifndef MESSAGESVIEW_H
#define MESSAGESVIEW_H

#include <QWidget>
#include <QString>
#include <QStandardItemModel>

class QListView;
class QLineEdit;
class QLabel;
class QStackedWidget;
class ComposerWidget;
class ChatBubbleDelegate;
class ThemeManager;

class MessagesView : public QWidget {
    Q_OBJECT

public:
    explicit MessagesView(QWidget* parent = nullptr);
    ~MessagesView() override;

    QStandardItemModel* sessionModel() const;
    QStandardItemModel* chatModel() const;
    void setSessionModel(QStandardItemModel* model);
    void setChatModel(QStandardItemModel* model);
    QLineEdit* searchEdit() const;
    QListView* sessionListView() const;
    QListView* chatListView() const;
    ComposerWidget* composer() const;

    void setCurrentUser(const QString& userId, const QString& userName);
    void setChatTitle(const QString& title, const QString& subtitle = QString(), const QString& hint = QString());
    QString chatTitle() const;
    QLabel* chatTitleLabel() const;
    QLabel* chatSubtitleLabel() const;
    QLabel* chatHintLabel() const;

    void appendMessage(const QStandardItem* item);
    void clearChat();
    void setEmptyStateVisible(bool visible);
    void setLoadingVisible(bool visible);

signals:
    void sessionSelected(const QModelIndex& index);
    void sendRequested();
    void fileRequested();
    void imageRequested();
    void emojiRequested();
    void mentionRequested();
    void clearHistoryRequested();

private:
    void setupUi();
    void updateStyle();

    QStandardItemModel* m_sessionModel = nullptr;
    QStandardItemModel* m_chatModel = nullptr;
    QListView* m_sessionListView = nullptr;
    QListView* m_chatListView = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_chatTitleLabel = nullptr;
    QLabel* m_chatSubtitleLabel = nullptr;
    QLabel* m_chatHintLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QLabel* m_loadingLabel = nullptr;
    ComposerWidget* m_composer = nullptr;
};

#endif // MESSAGESVIEW_H

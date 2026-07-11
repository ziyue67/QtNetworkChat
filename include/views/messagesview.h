#ifndef MESSAGESVIEW_H
#define MESSAGESVIEW_H

#include <QFrame>
#include <QMap>
#include <QModelIndex>
#include <QPair>
#include <QPoint>
#include <QStandardItemModel>
#include <QString>
#include <QWidget>

class QListView;
class QLineEdit;
class QLabel;
class QPushButton;
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
    void setMultiSelectMode(bool enabled);
    void setLocalActionState(const QString& actionId, bool visible, bool enabled);

signals:
    void sessionSelected(const QModelIndex& index);
    void sendRequested();
    void fileRequested();
    void imageRequested();
    void emojiRequested();
    void mentionRequested();
    void clearHistoryRequested();
    void filesDropped(const QStringList& paths);
    void messageActionRequested(const QModelIndex& index, const QString& action);
    void avatarActionRequested(const QModelIndex& index, const QString& action);
    void mediaActivated(const QModelIndex& index);
    void multiSelectForwardRequested();
    void multiSelectDeleteRequested();
    void multiSelectFavoriteRequested();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupUi();
    void updateStyle();
    void showDropOverlay();
    void hideDropOverlay();
    bool isInChatPanel(const QPoint& pos) const;
    void onChatContextMenu(const QPoint& pos);
    void onChatItemActivated(const QModelIndex& index);

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
    QLabel* m_dropOverlay = nullptr;
    QFrame* m_chatPanel = nullptr;
    ComposerWidget* m_composer = nullptr;

    // Multi-select bottom action bar
    QFrame* m_multiSelectBar = nullptr;
    QPushButton* m_multiSelectForwardBtn = nullptr;
    QPushButton* m_multiSelectDeleteBtn = nullptr;
    QPushButton* m_multiSelectFavoriteBtn = nullptr;
    QPushButton* m_multiSelectCancelBtn = nullptr;
    bool m_multiSelectMode = false;
    QMap<QString, QPair<bool, bool>> m_localActionStates; // id -> {visible, enabled}
};

#endif // MESSAGESVIEW_H

#ifndef COMPOSERWIDGET_H
#define COMPOSERWIDGET_H

#include <QFrame>
#include <QStringList>

class QTextEdit;
class QPushButton;
class QLabel;

class ComposerWidget : public QFrame {
    Q_OBJECT

public:
    explicit ComposerWidget(QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& text);
    void clear();
    void insertText(const QString& text);
    void setMentionCompletions(const QStringList& completions);

    void setSendEnabled(bool enabled);
    void setFileEnabled(bool enabled);
    void setImageEnabled(bool enabled);
    void setPlaceholderText(const QString& text);
    void setStateText(const QString& text);
    void setSessionName(const QString& sessionName);

    QTextEdit* inputEdit() const;
    QPushButton* sendButton() const;

signals:
    void sendRequested();
    void fileRequested();
    void imageRequested();
    void emojiRequested();
    void mentionRequested();
    void screenshotRequested();
    void clearHistoryRequested();
    void filesDropped(const QStringList& paths);
    void textChanged();

private:
    void setupUi();
    void updateStyle();
    void refreshHints();
    QString placeholderText() const;

    QTextEdit* m_input = nullptr;
    QPushButton* m_emojiBtn = nullptr;
    QPushButton* m_imageBtn = nullptr;
    QPushButton* m_fileBtn = nullptr;
    QPushButton* m_historyBtn = nullptr;
    QPushButton* m_mentionBtn = nullptr;
    QPushButton* m_screenshotBtn = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QLabel* m_stateLabel = nullptr;
    QString m_sessionName;
};

#endif // COMPOSERWIDGET_H

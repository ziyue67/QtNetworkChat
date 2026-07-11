#ifndef COMPOSERTEXTEDIT_H
#define COMPOSERTEXTEDIT_H

#include <QTextEdit>

class QCompleter;
class QStringListModel;

class ComposerTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit ComposerTextEdit(QWidget* parent = nullptr);

    void setMentionCompletions(const QStringList& completions);

signals:
    void sendRequested();
    void filesDropped(const QStringList& paths);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QString mentionPrefixUnderCursor() const;
    void updateMentionCompleter();
    void insertCompletion(const QString& completion);

    QStringListModel* m_mentionModel = nullptr;
    QCompleter* m_mentionCompleter = nullptr;
    QString m_activeMentionPrefix;
};

#endif // COMPOSERTEXTEDIT_H

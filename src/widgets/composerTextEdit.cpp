#include "widgets/composerTextEdit.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QUrl>

ComposerTextEdit::ComposerTextEdit(QWidget* parent)
    : QTextEdit(parent)
    , m_mentionModel(new QStringListModel(this))
    , m_mentionCompleter(new QCompleter(m_mentionModel, this))
{
    setAcceptDrops(true);
    m_mentionCompleter->setWidget(this);
    m_mentionCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_mentionCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_mentionCompleter->setFilterMode(Qt::MatchStartsWith);
    connect(m_mentionCompleter,
            QOverload<const QString&>::of(&QCompleter::activated),
            this,
            &ComposerTextEdit::insertCompletion);
}

void ComposerTextEdit::setMentionCompletions(const QStringList& completions)
{
    QStringList unique;
    for (const QString& completion : completions) {
        const QString normalized = completion.trimmed();
        if (!normalized.isEmpty() && !unique.contains(normalized, Qt::CaseInsensitive)) {
            unique.append(normalized);
        }
    }
    m_mentionModel->setStringList(unique);
    updateMentionCompleter();
}

void ComposerTextEdit::keyPressEvent(QKeyEvent* event)
{
    if (m_mentionCompleter->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
            QTextEdit::keyPressEvent(event);
            return;
        }
        emit sendRequested();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_mentionCompleter->popup()->isVisible()) {
            m_mentionCompleter->popup()->hide();
        } else {
            clear();
        }
        return;
    }

    QTextEdit::keyPressEvent(event);
    updateMentionCompleter();
}

void ComposerTextEdit::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QTextEdit::dragEnterEvent(event);
}

void ComposerTextEdit::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QTextEdit::dragMoveEvent(event);
}

void ComposerTextEdit::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        QTextEdit::dropEvent(event);
        return;
    }

    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty() && !paths.contains(path)) {
                paths.append(path);
            }
        }
    }
    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

QString ComposerTextEdit::mentionPrefixUnderCursor() const
{
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::BlockUnderCursor);
    const QString block = cursor.selectedText().left(textCursor().positionInBlock());
    const QRegularExpression matchExpression(QStringLiteral("(?:^|\\s)(@[^\\s@]*)$"));
    const QRegularExpressionMatch match = matchExpression.match(block);
    return match.hasMatch() ? match.captured(1) : QString();
}

void ComposerTextEdit::updateMentionCompleter()
{
    const QString prefix = mentionPrefixUnderCursor();
    if (prefix.isEmpty() || m_mentionModel->rowCount() == 0) {
        m_mentionCompleter->popup()->hide();
        return;
    }

    m_activeMentionPrefix = prefix;
    m_mentionCompleter->setCompletionPrefix(prefix);
    if (m_mentionCompleter->completionCount() == 0) {
        m_mentionCompleter->popup()->hide();
        return;
    }

    QRect popupRect = cursorRect();
    popupRect.setWidth(qMax(240, m_mentionCompleter->popup()->sizeHintForColumn(0)
                                  + m_mentionCompleter->popup()->verticalScrollBar()->sizeHint().width()));
    m_mentionCompleter->complete(popupRect);
}

void ComposerTextEdit::insertCompletion(const QString& completion)
{
    QTextCursor cursor = textCursor();
    const int prefixLength = m_activeMentionPrefix.size();
    if (prefixLength > 0) {
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, prefixLength);
    }
    cursor.insertText(completion + QStringLiteral(" "));
    setTextCursor(cursor);
    m_mentionCompleter->popup()->hide();
}

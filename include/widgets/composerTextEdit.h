#ifndef COMPOSERTEXTEDIT_H
#define COMPOSERTEXTEDIT_H

#include <QTextEdit>
#include <QKeyEvent>

class ComposerTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit ComposerTextEdit(QWidget* parent = nullptr) : QTextEdit(parent) {}

signals:
    void sendRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
                insertPlainText(QStringLiteral("\n"));
            } else {
                emit sendRequested();
                return;
            }
        } else if (event->key() == Qt::Key_Escape) {
            clear();
            return;
        }
        QTextEdit::keyPressEvent(event);
    }
};

#endif // COMPOSERTEXTEDIT_H

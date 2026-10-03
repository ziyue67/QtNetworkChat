#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QString>
#include <QColor>
#include <QIcon>
#include <QFont>
#include <QPainter>
#include <QPixmap>

class IconHelper
{
public:
    /// Returns the Unicode icon character for a named button.
    static QString iconFor(const QString &name);

    /// Returns a QIcon painted in the given color (defaults to current text color).
    static QIcon icon(const QString &name, const QColor &color = QColor());

    /// Returns the icon character + a space + label text (for buttons that keep textual labels).
    static QString withLabel(const QString &name, const QString &label);

    /// Returns a QFont suitable for rendering icon characters (priority: Segoe UI Symbol,
    /// Segoe MDL2 Assets, Segoe UI Emoji).  The font size defaults to the system default;
    /// callers may setPointSize/setPixelSize on the result.
    static QFont iconFont();

private:
    IconHelper() = default;
};

#endif // ICONHELPER_H

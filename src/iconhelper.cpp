#include "iconhelper.h"

// ---------------------------------------------------------------------------
// Unicode icon map — all characters render well on Windows with
// Segoe UI Symbol / Segoe MDL2 Assets / Segoe UI Emoji.
// ---------------------------------------------------------------------------
QString IconHelper::iconFor(const QString &name)
{
    // Normalise key
    const QString key = name.toLower().trimmed();

    if (key == "emoji")       return QStringLiteral("\U0001F60A"); // smiling face
    if (key == "image")       return QStringLiteral("\U0001F5BC"); // framed picture
    if (key == "file")        return QStringLiteral("\U0001F4CE"); // paperclip
    if (key == "clear")       return QStringLiteral("\U0001F5D1"); // wastebasket
    if (key == "send")        return QStringLiteral("➤");     // ➤  rightwards arrow
    if (key == "search")      return QStringLiteral("\U0001F50D"); // magnifying glass
    if (key == "menu")        return QStringLiteral("☰");    // ☰  trigram for heaven
    if (key == "notices")     return QStringLiteral("\U0001F514"); // bell
    if (key == "groupnotices") return QStringLiteral("\U0001F465"); // busts in silhouette
    if (key == "friends")     return QStringLiteral("\U0001F464"); // bust in silhouette
    if (key == "avatar")      return QStringLiteral("\U0001F4F7"); // camera
    if (key == "copyid")      return QStringLiteral("\U0001F4CB"); // clipboard
    if (key == "addfriend")   return QStringLiteral("➕");    // ➕  heavy plus
    if (key == "newgroup")    return QStringLiteral("✚");    // ✚  heavy greek cross
    if (key == "groupinfo")   return QStringLiteral("ℹ");    // ℹ  information
    if (key == "mention")     return QStringLiteral("@");

    // fallback — return the key wrapped in brackets so it is still visible
    return QStringLiteral("[%1]").arg(name);
}

// ---------------------------------------------------------------------------
// QIcon factory — paints the glyph onto a standard 20×20 pixmap.
// ---------------------------------------------------------------------------
QIcon IconHelper::icon(const QString &name, const QColor &color)
{
    const QString glyph = iconFor(name);
    if (glyph.isEmpty())
        return QIcon();

    QFont f = iconFont();
    f.setPixelSize(18);

    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setFont(f);
    p.setPen(color.isValid() ? color : QColor(0x51, 0x62, 0x74)); // default text-secondary
    p.drawText(pm.rect(), Qt::AlignCenter, glyph);
    p.end();

    return QIcon(pm);
}

// ---------------------------------------------------------------------------
// Combine the icon character with a visible label.
// ---------------------------------------------------------------------------
QString IconHelper::withLabel(const QString &name, const QString &label)
{
    const QString glyph = iconFor(name);
    return QStringLiteral("%1  %2").arg(glyph, label);
}

// ---------------------------------------------------------------------------
// Font selection — try the Windows icon fonts first, then fall back to the
// system default.
// ---------------------------------------------------------------------------
QFont IconHelper::iconFont()
{
#ifdef Q_OS_WIN
    // Segoe UI Symbol has excellent Unicode coverage on Windows
    return QFont(QStringLiteral("Segoe UI Symbol"), -1, -1, false);
#else
    // On Linux/macOS rely on the default emoji / symbol font
    return QFont();
#endif
}

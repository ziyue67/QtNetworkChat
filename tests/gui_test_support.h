#pragma once

#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QPixmap>
#include <QRawFont>
#include <QSet>
#include <QWidget>
#include <cstdio>

namespace GuiTestSupport {
inline bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "%s\n", message);
    return condition;
}

inline bool prepareFont() {
    const QString path = QString::fromUtf8(qgetenv("QTNETWORKCHAT_TEST_FONT"));
    if (path.isEmpty()) return true;
    const int fontId = QFontDatabase::addApplicationFont(path);
    if (!expect(fontId >= 0, "GUI test font could not be loaded")) return false;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (!expect(!families.isEmpty(), "GUI test font has no family")) return false;
    QApplication::setFont(QFont(families.first(), 9));
    const QRawFont font = QRawFont::fromFont(QApplication::font());
    return expect(font.supportsCharacter(QChar(0x7fa4)), "GUI test font must contain Chinese glyphs");
}

inline bool captureScreenshot(QWidget& window, const QString& path) {
    QApplication::processEvents();
    const QPixmap pixmap = window.grab();
    const QImage image = pixmap.toImage();
    const QSize expectedSize = window.size() * pixmap.devicePixelRatio();
    if (!expect(!image.isNull() && image.size() == expectedSize,
                "GUI screenshot dimensions do not match the window and device scale")) return false;

    // Sparse grids can miss text and rounded borders on different platform fonts.
    QSet<QRgb> colors;
    for (int y = 0; y < image.height() && colors.size() <= 12; ++y) {
        for (int x = 0; x < image.width() && colors.size() <= 12; ++x) {
            colors.insert(image.pixel(x, y));
        }
    }
    return expect(colors.size() > 12, "GUI screenshot should not be blank")
        && expect(image.save(path), "GUI screenshot could not be saved");
}
} // namespace GuiTestSupport

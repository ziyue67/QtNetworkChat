#ifndef SCREENSHOTGEOMETRY_H
#define SCREENSHOTGEOMETRY_H

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QList>
#include <QtMath>

// Pure geometry helpers for the screenshot capture flow. Kept free of any Qt
// widget/GUI dependency so they can be unit tested without a display.
namespace ScreenshotGeometry {

// The composite canvas scale is the maximum device pixel ratio across all
// screens, so every screen is blitted at full resolution into one canvas.
inline qreal compositeScale(const QList<qreal>& devicePixelRatios) {
    qreal scale = 1.0;
    for (qreal dpr : devicePixelRatios) {
        if (dpr > scale) {
            scale = dpr;
        }
    }
    return scale;
}

// Target rectangle (in composite-canvas pixels) that a screen with the given
// virtual-desktop geometry maps to. Handles negative-coordinate origins by
// translating relative to the virtual desktop's top-left corner.
inline QRectF screenTargetRect(const QRect& screenGeometry,
                               const QRect& virtualRect,
                               qreal scale) {
    return QRectF(
        (screenGeometry.left() - virtualRect.left()) * scale,
        (screenGeometry.top() - virtualRect.top()) * scale,
        screenGeometry.width() * scale,
        screenGeometry.height() * scale);
}

// Map a selection made in overlay-widget coordinates to the source rectangle
// in the composite screenshot pixmap. Uniform scaling in each axis based on the
// screenshot-to-overlay size ratio.
inline QRect selectionToSource(const QRect& selection,
                               const QSize& overlaySize,
                               const QSize& screenshotSize) {
    if (selection.isEmpty() || overlaySize.width() <= 0 || overlaySize.height() <= 0
        || screenshotSize.isEmpty()) {
        return QRect();
    }
    const qreal scaleX = static_cast<qreal>(screenshotSize.width()) / overlaySize.width();
    const qreal scaleY = static_cast<qreal>(screenshotSize.height()) / overlaySize.height();
    return QRect(qRound(selection.x() * scaleX),
                 qRound(selection.y() * scaleY),
                 qRound(selection.width() * scaleX),
                 qRound(selection.height() * scaleY))
        .intersected(QRect(QPoint(0, 0), screenshotSize));
}

} // namespace ScreenshotGeometry

#endif // SCREENSHOTGEOMETRY_H

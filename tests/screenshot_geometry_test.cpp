#include "screenshotgeometry.h"

#include <QCoreApplication>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}

bool nearlyEqual(qreal a, qreal b) {
    return qAbs(a - b) < 1e-6;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;

    // compositeScale should pick the maximum device pixel ratio, never below 1.0.
    ok = expect(nearlyEqual(ScreenshotGeometry::compositeScale({}), 1.0),
                "empty dpr list should fall back to scale 1.0") && ok;
    ok = expect(nearlyEqual(ScreenshotGeometry::compositeScale({1.0, 1.0}), 1.0),
                "uniform 1.0 dprs should keep scale 1.0") && ok;
    ok = expect(nearlyEqual(ScreenshotGeometry::compositeScale({1.0, 2.0, 1.5}), 2.0),
                "mixed dprs should select the maximum") && ok;
    ok = expect(nearlyEqual(ScreenshotGeometry::compositeScale({0.75}), 1.0),
                "sub-1.0 dpr should still clamp to 1.0") && ok;

    // Primary screen at origin, single-scale canvas.
    {
        const QRect virtualRect(0, 0, 3840, 1080);
        const QRectF primaryTarget = ScreenshotGeometry::screenTargetRect(
            QRect(0, 0, 1920, 1080), virtualRect, 1.0);
        ok = expect(primaryTarget == QRectF(0, 0, 1920, 1080),
                    "primary screen at origin maps to itself at scale 1.0") && ok;

        const QRectF secondaryTarget = ScreenshotGeometry::screenTargetRect(
            QRect(1920, 0, 1920, 1080), virtualRect, 1.0);
        ok = expect(secondaryTarget == QRectF(1920, 0, 1920, 1080),
                    "secondary screen to the right maps beside the primary") && ok;
    }

    // Negative-origin secondary screen: virtual desktop top-left becomes the origin.
    {
        const QRect virtualRect(-1920, -120, 3840, 1200);
        const QRectF leftScreen = ScreenshotGeometry::screenTargetRect(
            QRect(-1920, 0, 1920, 1080), virtualRect, 1.0);
        // (-1920 - (-1920)) = 0 x; (0 - (-120)) = 120 y.
        ok = expect(leftScreen == QRectF(0, 120, 1920, 1080),
                    "negative-origin left screen should translate relative to the virtual origin") && ok;

        const QRectF rightScreen = ScreenshotGeometry::screenTargetRect(
            QRect(0, -120, 1920, 1200), virtualRect, 1.0);
        ok = expect(rightScreen == QRectF(1920, 0, 1920, 1200),
                    "negative-origin right screen should map past the left screen at the virtual top") && ok;
    }

    // DPR-aware canvas: a 2x screen is blitted at double resolution.
    {
        const QRect virtualRect(0, 0, 1920, 1080);
        const QRectF hiDpiTarget = ScreenshotGeometry::screenTargetRect(
            QRect(0, 0, 1920, 1080), virtualRect, 2.0);
        ok = expect(hiDpiTarget == QRectF(0, 0, 3840, 2160),
                    "2x scale should expand the target rect to physical pixels") && ok;
    }

    // selectionToSource: uniform scaling from overlay to composite screenshot.
    {
        // Overlay 960x540 shown for a 1920x1080 screenshot -> 2x scale.
        const QRect source = ScreenshotGeometry::selectionToSource(
            QRect(100, 50, 200, 100), QSize(960, 540), QSize(1920, 1080));
        ok = expect(source == QRect(200, 100, 400, 200),
                    "selection should scale up by the screenshot/overlay ratio") && ok;
    }

    // selectionToSource clamps to the screenshot bounds.
    {
        const QRect clamped = ScreenshotGeometry::selectionToSource(
            QRect(900, 500, 200, 100), QSize(1000, 600), QSize(1000, 600));
        ok = expect(clamped == QRect(900, 500, 100, 100),
                    "selection running past the edge should be clipped to the screenshot") && ok;
    }

    // selectionToSource degrades gracefully on invalid input.
    {
        ok = expect(ScreenshotGeometry::selectionToSource(QRect(), QSize(100, 100), QSize(100, 100)).isNull(),
                    "empty selection should produce a null source rect") && ok;
        ok = expect(ScreenshotGeometry::selectionToSource(QRect(0, 0, 10, 10), QSize(0, 0), QSize(100, 100)).isNull(),
                    "zero overlay size should produce a null source rect") && ok;
        ok = expect(ScreenshotGeometry::selectionToSource(QRect(0, 0, 10, 10), QSize(100, 100), QSize()).isNull(),
                    "empty screenshot size should produce a null source rect") && ok;
    }

    return ok ? 0 : 1;
}

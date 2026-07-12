#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

// Verifies two things for the QQNT theme:
//   1. Token alignment: the light/dark QSS stylesheets embed the same hex color
//      values that the ThemeManager design-token contract hands out for the core
//      tokens, so the global QSS and the C++-driven inline styles (dialog title
//      bar, views) cannot silently drift apart. The token contract below is a
//      literal mirror of the TOKENS table in src/theme/thememanager.cpp; if that
//      table changes, this test must be updated in lock-step, which is exactly
//      the drift guard we want.
//   2. Selector <-> objectName coverage: the critical structural objectNames that
//      the QSS files target with an id selector (#name) must also appear in the
//      source tree via setObjectName, so a stylesheet rule can never point at a
//      widget that no longer exists.
//
// Test args: <light.qss> <dark.qss> <source-scan-manifest>
// The manifest is a newline-separated list of repo-relative source files to scan
// for setObjectName("...") occurrences; it lives at tests/fixtures/<manifest>.

namespace {
bool expect(bool condition, const QString& message) {
    if (!condition) {
        qWarning("%s", qUtf8Printable(message));
        return false;
    }
    return true;
}

QString readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("cannot open %s", qUtf8Printable(path));
        return QString();
    }
    QTextStream stream(&file);
    return stream.readAll();
}

// Extract the set of id selectors (#objectName) used in a stylesheet. Hex colors
// like #ffffff are excluded because the pattern requires a leading letter/_.
QSet<QString> objectNameSelectors(const QString& qss) {
    QSet<QString> names;
    static const QRegularExpression re(QStringLiteral("#([A-Za-z_][A-Za-z0-9_]*)"));
    QRegularExpressionMatchIterator it = re.globalMatch(qss);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        names.insert(m.captured(1));
    }
    return names;
}

QSet<QString> objectNamesDefinedIn(const QStringList& sourceFiles) {
    QSet<QString> names;
    static const QRegularExpression re(
        QStringLiteral("setObjectName\\(\\s*(?:QStringLiteral\\(\\s*)?\"([^\"]+)\""));
    for (const QString& path : sourceFiles) {
        const QString content = readFile(path);
        QRegularExpressionMatchIterator it = re.globalMatch(content);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            names.insert(m.captured(1));
        }
    }
    return names;
}

struct TokenContract {
    const char* key;
    const char* light;
    const char* dark;
};
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        qWarning("usage: theme_qss_consistency_test <light.qss> <dark.qss> <source-manifest>");
        return 2;
    }
    const QString lightPath = args.at(1);
    const QString darkPath = args.at(2);
    const QString manifestPath = args.at(3);

    const QString lightQss = readFile(lightPath);
    const QString darkQss = readFile(darkPath);
    if (lightQss.isEmpty() || darkQss.isEmpty()) {
        return 2;
    }

    bool ok = true;

    // Core design tokens whose hex value must appear literally in each stylesheet.
    // Mirror of ThemeManager's TOKENS table (src/theme/thememanager.cpp).
    static const TokenContract kTokens[] = {
        { "bg",           "#ffffff", "#1e1e1e" },
        { "bg-secondary", "#f5f6f7", "#252525" },
        { "border",       "#e1e3e6", "#3a3a3a" },
        { "text",         "#1f2329", "#e8e8e8" },
        { "primary",      "#0099ff", "#3da7ff" },
    };

    for (const TokenContract& token : kTokens) {
        const QString light = QString::fromLatin1(token.light);
        const QString dark = QString::fromLatin1(token.dark);
        ok = expect(lightQss.contains(light, Qt::CaseInsensitive),
                    QStringLiteral("light QSS must embed token %1 hex %2")
                        .arg(QString::fromLatin1(token.key), light)) && ok;
        ok = expect(darkQss.contains(dark, Qt::CaseInsensitive),
                    QStringLiteral("dark QSS must embed token %1 hex %2")
                        .arg(QString::fromLatin1(token.key), dark)) && ok;
    }

    // Selector <-> objectName coverage. The manifest lists repo-relative source
    // paths, resolved against the repo root (manifest lives at
    // tests/fixtures/<manifest>, so root is two directories up).
    const QString manifest = readFile(manifestPath);
    QDir repoRoot = QFileInfo(manifestPath).absoluteDir(); // tests/fixtures
    repoRoot.cdUp();                                        // tests
    repoRoot.cdUp();                                        // repo root
    QStringList sourceFiles;
    for (const QString& rel : manifest.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = rel.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) continue;
        sourceFiles.append(QDir(repoRoot).filePath(trimmed));
    }
    ok = expect(!sourceFiles.isEmpty(), QStringLiteral("source manifest must list files")) && ok;

    const QSet<QString> definedNames = objectNamesDefinedIn(sourceFiles);
    ok = expect(!definedNames.isEmpty(),
                QStringLiteral("expected to discover setObjectName usages in sources")) && ok;

    // A curated set of QQNT structural object names that MUST be styled and MUST
    // exist in code. This guards the critical selectors called out in the plan
    // (dialog title bar unification for the login/register/preview windows).
    const QStringList criticalNames = {
        QStringLiteral("dialogTitleBar"),
        QStringLiteral("dialogTitleBarLabel"),
        QStringLiteral("dialogTitleBarCloseBtn"),
    };
    const QSet<QString> lightSelectors = objectNameSelectors(lightQss);
    const QSet<QString> darkSelectors = objectNameSelectors(darkQss);
    for (const QString& name : criticalNames) {
        ok = expect(definedNames.contains(name),
                    QStringLiteral("critical objectName %1 must be set in code").arg(name)) && ok;
        ok = expect(lightSelectors.contains(name),
                    QStringLiteral("critical objectName %1 must be styled in light QSS").arg(name)) && ok;
        ok = expect(darkSelectors.contains(name),
                    QStringLiteral("critical objectName %1 must be styled in dark QSS").arg(name)) && ok;
    }

    return ok ? 0 : 1;
}

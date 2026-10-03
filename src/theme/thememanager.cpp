#include "theme/thememanager.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>

namespace {
struct ColorToken {
    QString key;
    QColor light;
    QColor dark;
};

constexpr int TOKEN_COUNT = 24;

const ColorToken TOKENS[TOKEN_COUNT] = {
    { QStringLiteral("bg"),                QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#1e1e1e")) },
    { QStringLiteral("bg-secondary"),      QColor(QStringLiteral("#f5f6f7")), QColor(QStringLiteral("#252525")) },
    { QStringLiteral("bg-tertiary"),       QColor(QStringLiteral("#ebedf0")), QColor(QStringLiteral("#2d2d2d")) },
    { QStringLiteral("border"),            QColor(QStringLiteral("#e1e3e6")), QColor(QStringLiteral("#3a3a3a")) },
    { QStringLiteral("text"),              QColor(QStringLiteral("#1f2329")), QColor(QStringLiteral("#e8e8e8")) },
    { QStringLiteral("text-secondary"),    QColor(QStringLiteral("#5f6672")), QColor(QStringLiteral("#a8a8a8")) },
    { QStringLiteral("text-tertiary"),     QColor(QStringLiteral("#8f959e")), QColor(QStringLiteral("#787878")) },
    { QStringLiteral("primary"),            QColor(QStringLiteral("#0099ff")), QColor(QStringLiteral("#3da7ff")) },
    { QStringLiteral("primary-hover"),     QColor(QStringLiteral("#007acc")), QColor(QStringLiteral("#66c1ff")) },
    { QStringLiteral("primary-soft"),      QColor(QStringLiteral("#e6f4ff")), QColor(QStringLiteral("#1a3a52")) },
    { QStringLiteral("danger"),             QColor(QStringLiteral("#ff4d4f")), QColor(QStringLiteral("#ff7875")) },
    { QStringLiteral("success"),            QColor(QStringLiteral("#52c41a")), QColor(QStringLiteral("#73d13d")) },
    { QStringLiteral("warning"),            QColor(QStringLiteral("#faad14")), QColor(QStringLiteral("#ffc53d")) },
    { QStringLiteral("bubble-outgoing"),   QColor(QStringLiteral("#0099ff")), QColor(QStringLiteral("#0099ff")) },
    { QStringLiteral("bubble-outgoing-text"), QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#ffffff")) },
    { QStringLiteral("bubble-incoming"),   QColor(QStringLiteral("#f5f6f7")), QColor(QStringLiteral("#2d2d2d")) },
    { QStringLiteral("bubble-incoming-text"), QColor(QStringLiteral("#1f2329")), QColor(QStringLiteral("#e8e8e8")) },
    { QStringLiteral("nav-active"),        QColor(QStringLiteral("#0099ff")), QColor(QStringLiteral("#3da7ff")) },
    { QStringLiteral("nav-inactive"),      QColor(QStringLiteral("#8f959e")), QColor(QStringLiteral("#787878")) },
    { QStringLiteral("badge"),             QColor(QStringLiteral("#ff4d4f")), QColor(QStringLiteral("#ff7875")) },
    { QStringLiteral("shadow"),            QColor(QStringLiteral("#0c000000")), QColor(QStringLiteral("#0c000000")) },
    { QStringLiteral("header-bg"),         QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#252525")) },
    { QStringLiteral("session-hover"),     QColor(QStringLiteral("#f5f6f7")), QColor(QStringLiteral("#2d2d2d")) },
    { QStringLiteral("session-selected"),  QColor(QStringLiteral("#e6f4ff")), QColor(QStringLiteral("#1a3a52")) },
};

QStringList preferredFonts() {
    QFontDatabase fontDatabase;
    QStringList families = fontDatabase.families();
    const QStringList candidates = {
        QStringLiteral("PingFang SC"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Segoe UI"),
        QStringLiteral("SF Pro Display"),
    };
    for (const QString& candidate : candidates) {
        if (families.contains(candidate)) {
            return QStringList{candidate};
        }
    }
    return QStringList{};
}
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , m_theme(Theme::Light)
{
}

ThemeManager::~ThemeManager() = default;

ThemeManager* ThemeManager::instance()
{
    static ThemeManager* s_instance = new ThemeManager(qApp);
    return s_instance;
}

ThemeManager::Theme ThemeManager::currentTheme() const
{
    return m_theme;
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_theme == theme) {
        return;
    }
    m_theme = theme;
    applyPalette();
    emit themeChanged();
}

void ThemeManager::toggleTheme()
{
    setTheme(m_theme == Theme::Light ? Theme::Dark : Theme::Light);
}

bool ThemeManager::isDark() const
{
    return m_theme == Theme::Dark;
}

QColor ThemeManager::color(const QString& key) const
{
    for (int i = 0; i < TOKEN_COUNT; ++i) {
        if (TOKENS[i].key == key) {
            return m_theme == Theme::Dark ? TOKENS[i].dark : TOKENS[i].light;
        }
    }
    return Qt::transparent;
}

QColor ThemeManager::primaryColor() const
{
    return color(QStringLiteral("primary"));
}

QColor ThemeManager::primaryHoverColor() const
{
    return color(QStringLiteral("primary-hover"));
}

QColor ThemeManager::backgroundColor() const
{
    return color(QStringLiteral("bg"));
}

QColor ThemeManager::backgroundSecondaryColor() const
{
    return color(QStringLiteral("bg-secondary"));
}

QColor ThemeManager::backgroundTertiaryColor() const
{
    return color(QStringLiteral("bg-tertiary"));
}

QColor ThemeManager::borderColor() const
{
    return color(QStringLiteral("border"));
}

QColor ThemeManager::textColor() const
{
    return color(QStringLiteral("text"));
}

QColor ThemeManager::textSecondaryColor() const
{
    return color(QStringLiteral("text-secondary"));
}

QColor ThemeManager::textTertiaryColor() const
{
    return color(QStringLiteral("text-tertiary"));
}

QColor ThemeManager::dangerColor() const
{
    return color(QStringLiteral("danger"));
}

QColor ThemeManager::primarySoftColor() const
{
    return color(QStringLiteral("primary-soft"));
}

QColor ThemeManager::successColor() const
{
    return color(QStringLiteral("success"));
}

QFont ThemeManager::font() const
{
    const QStringList families = preferredFonts();
    QFont font;
    if (!families.isEmpty()) {
        font.setFamily(families.first());
    }
    font.setPointSize(9);
    return font;
}

int ThemeManager::titleBarHeight() const
{
    return 40;
}

int ThemeManager::appNavWidth() const
{
    return 64;
}

int ThemeManager::sessionListWidth() const
{
    return 260;
}

int ThemeManager::cornerRadius() const
{
    return 6;
}

void ThemeManager::applyPalette() const
{
    QPalette palette;
    const QColor bg = backgroundColor();
    const QColor bgSecondary = backgroundSecondaryColor();
    const QColor text = textColor();
    const QColor textSecondary = textSecondaryColor();
    const QColor primary = primaryColor();
    const QColor border = borderColor();

    palette.setColor(QPalette::Window, bg);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, bgSecondary);
    palette.setColor(QPalette::AlternateBase, bgSecondary);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, bgSecondary);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, primary);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Light, border);
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Dark, border);
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0, 40));
    palette.setColor(QPalette::PlaceholderText, textSecondary);

    if (qApp) {
        qApp->setPalette(palette);
    }
}


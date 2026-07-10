#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

class ThemeManager : public QObject {
    Q_OBJECT

public:
    enum class Theme { Light, Dark };
    Q_ENUM(Theme)

    static ThemeManager* instance();

    Theme currentTheme() const;
    void setTheme(Theme theme);
    void toggleTheme();
    bool isDark() const;

    QColor color(const QString& key) const;
    QColor primaryColor() const;
    QColor primaryHoverColor() const;
    QColor backgroundColor() const;
    QColor backgroundSecondaryColor() const;
    QColor backgroundTertiaryColor() const;
    QColor borderColor() const;
    QColor textColor() const;
    QColor textSecondaryColor() const;
    QColor textTertiaryColor() const;
    QColor dangerColor() const;
    QColor successColor() const;
    QColor primarySoftColor() const;

    QFont font() const;
    int titleBarHeight() const;
    int appNavWidth() const;
    int sessionListWidth() const;
    int cornerRadius() const;

signals:
    void themeChanged();

private:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager();
    Q_DISABLE_COPY_MOVE(ThemeManager)

    Theme m_theme = Theme::Light;
};

#endif // THEMEMANAGER_H


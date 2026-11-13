#pragma once

#include <QColor>
#include <QObject>

/**
 * ThemeManager — the C++ side of the theme changer.
 *
 * The recipe that makes QML react to theme changes:
 *   1. Q_PROPERTY with READ + WRITE + NOTIFY (missing NOTIFY = UI never updates)
 *   2. Registered as a context property BEFORE engine.load()
 *   3. Bound in QML through the Theme.qml singleton (bindings, never assignments)
 */
class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(int effectiveTheme READ effectiveTheme NOTIFY effectiveThemeChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)

public:
    enum Theme { Dark = 0, Light = 1, System = 2 };
    Q_ENUM(Theme)

    explicit ThemeManager(QObject *parent = nullptr);

    int theme() const { return m_theme; }
    void setTheme(int theme);

    /** Dark or Light actually in effect (resolves System against the OS setting). */
    int effectiveTheme() const;

    QColor accentColor() const { return m_accent; }
    void setAccentColor(const QColor &color);

signals:
    void themeChanged(int theme);
    void effectiveThemeChanged();
    void accentColorChanged(const QColor &color);

private:
    int m_theme = Dark;
    QColor m_accent;
};

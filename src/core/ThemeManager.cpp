#include "ThemeManager.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    const QSettings s;
    m_theme = s.value(QStringLiteral("appearance/theme"), int(Dark)).toInt();
    if (m_theme < Dark || m_theme > System)
        m_theme = Dark;

    m_accent = QColor(s.value(QStringLiteral("appearance/accent"),
                              QStringLiteral("#6ce6d0")).toString());
    if (!m_accent.isValid())
        m_accent = QColor(QStringLiteral("#6ce6d0"));

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // In "System" mode, follow live OS dark/light switches.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &ThemeManager::effectiveThemeChanged);
#endif
}

void ThemeManager::setTheme(int theme)
{
    if (theme < Dark || theme > System || theme == m_theme)
        return;

    m_theme = theme;
    QSettings().setValue(QStringLiteral("appearance/theme"), m_theme);

    // NOTIFY is what wakes up every QML binding that reads themes.theme.
    emit themeChanged(m_theme);
    emit effectiveThemeChanged();
}

int ThemeManager::effectiveTheme() const
{
    if (m_theme != System)
        return m_theme;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
               ? int(Dark)
               : int(Light);
#else
    return int(Dark);
#endif
}

void ThemeManager::setAccentColor(const QColor &color)
{
    if (!color.isValid() || color == m_accent)
        return;

    m_accent = color;
    QSettings().setValue(QStringLiteral("appearance/accent"), m_accent.name());
    emit accentColorChanged(m_accent);
}

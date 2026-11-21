#pragma once
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// Rebuilt PLAYER About links are independent of the Customizer's own About
// page. Stable ids keep uploaded icons attached when rows are added/removed.
// No markup, executable URL schemes, credentials, or user-supplied QML.
namespace SettingsPanelLinks {
constexpr int maximum = 24;
inline bool validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9_-]{1,48}$"));
    return pattern.match(id).hasMatch();
}
inline bool clean(const QVariantList &input, QVariantList *output, QString *error)
{
    const auto fail = [&](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    if (input.size() > maximum) return fail("Use at most 24 About links.");
    QVariantList links;
    QSet<QString> ids;
    static const QRegularExpression controls(QStringLiteral("[\\x00-\\x1f\\x7f]"));
    for (int index = 0; index < input.size(); ++index) {
        const QVariantMap row = input.at(index).toMap();
        if (row.isEmpty()) return fail("An About link row is invalid.");
        for (auto it = row.cbegin(); it != row.cend(); ++it) {
            if (it.key() != QLatin1String("id") && it.key() != QLatin1String("kind")
                && it.key() != QLatin1String("title") && it.key() != QLatin1String("url"))
                return fail("An About link contains an unsupported field.");
            if (it.value().userType() != QMetaType::QString)
                return fail("About link fields must be text, not objects, numbers or flags.");
        }
        const QString title = row.value(QStringLiteral("title")).toString().trimmed();
        const QString urlText = row.value(QStringLiteral("url")).toString().trimmed();
        // Legacy four-slot profiles used empty pairs to hide a row.
        if (title.isEmpty() && urlText.isEmpty() && !row.contains(QStringLiteral("id"))) continue;
        const QString id = row.value(QStringLiteral("id"), QStringLiteral("link-%1").arg(index + 1)).toString();
        const QString kind = row.value(QStringLiteral("kind"), QStringLiteral("link")).toString();
        if (!validId(id) || ids.contains(id)) return fail("About link ids must be valid and unique.");
        if (kind != QLatin1String("link") && kind != QLatin1String("github")
            && kind != QLatin1String("discord") && kind != QLatin1String("youtube"))
            return fail("Choose a website, GitHub, Discord or YouTube link.");
        if (title.isEmpty() || title.size() > 80 || urlText.isEmpty() || urlText.size() > 512
            || controls.match(title).hasMatch() || controls.match(urlText).hasMatch())
            return fail("Each About link needs a label (up to 80 characters) and HTTP(S) URL (up to 512), without control characters. Fill or remove incomplete rows.");
        const QUrl url(urlText, QUrl::StrictMode);
        const QString scheme = url.scheme().toLower();
        if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
            || (scheme != QLatin1String("https") && scheme != QLatin1String("http")))
            return fail("About links must be valid HTTP(S) URLs without embedded credentials.");
        ids.insert(id);
        links << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind},
                             {QStringLiteral("title"), title}, {QStringLiteral("url"), urlText}};
    }
    if (output) *output = links;
    return true;
}
} // namespace SettingsPanelLinks

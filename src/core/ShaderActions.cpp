#include "ShaderActions.h"
#include "ShaderInstall.h"
#include <QRegularExpression>
#include <QVersionNumber>

namespace ShaderActions {
namespace {
bool hash(const QString &text)
{
    static const QRegularExpression rx(QStringLiteral("^[0-9a-f]{64}$"));
    return rx.match(text).hasMatch();
}
QVariantMap failure(const QString &message) { return {{"ok", false}, {"msg", message}}; }
QVariantMap response(const ShaderInstall::Result &r, const QString &verb)
{
    QString message;
    if (r.ok) message = r.changed ? verb : QStringLiteral("No shader file changes were needed.");
    else message = r.error;
    if (r.rolledBack) message += QStringLiteral(" The previous EXE/module pair was restored and verified.");
    if (r.recoveryRequired) message += QStringLiteral(" Do not launch the player. Keep the journal and recovery directory, close all instances and use Recover transaction.");
    if (r.cleanupPending) message += QStringLiteral(" Verified recovery/staging files remain; keep them until reviewed. ") + r.error;
    return {{"ok", r.ok}, {"changed", r.changed}, {"msg", message}, {"recoveryRequired", r.recoveryRequired},
            {"rolledBack", r.rolledBack}, {"cleanupPending", r.cleanupPending}, {"recoveryDirectory", r.recoveryDirectory}};
}
}
QVariantMap bundleInfo()
{
    const auto bundle = ShaderBundle::load();
    QVariantList entries;
    for (const auto &entry : bundle.entries) {
        const auto description = ShaderPackage::describe(entry.payload);
        entries << QVariantMap{{"id", entry.id}, {"qtVersion", entry.qtVersion}, {"publicQtApi", description.publicQtApi},
            {"packageSha256", entry.packageSha256}, {"dllSha256", ShaderPackage::sha256(entry.payload.dll)}};
    }
    return {{"ok", bundle.ok}, {"msg", bundle.error}, {"entries", entries}};
}
QVariantMap inspect(const QString &exePath, const ShaderBundle::Catalog &bundle)
{
    auto r = ShaderInstall::inspect(exePath);
    QString id, bundleError = bundle.error;
    QStringList versions;
    const ShaderBundle::Entry *selected = nullptr;
    if (bundle.ok && r.ok && !r.qtVersion.isEmpty()) for (const auto &entry : bundle.entries) {
        const auto description = ShaderPackage::describe(entry.payload);
        if (ShaderPackage::canTarget(description, r.qtVersion)
            && (!selected || QVersionNumber::compare(QVersionNumber::fromString(entry.qtVersion),
                                                     QVersionNumber::fromString(selected->qtVersion)) > 0)) selected = &entry;
    }
    // Deterministic newest eligible baseline, then FULL byte/dependency checks.
    // Do not silently try another candidate to hide a corrupt/missing provider.
    if (selected) { id = selected->id; r = ShaderInstall::inspect(exePath, &selected->payload); }
    // List the whole bundle, even when a compatible baseline was selected.
    versions.clear(); for (const auto &entry : bundle.entries) versions << entry.qtVersion;
    if (!r.recoveryRequired && id.isEmpty() && r.applyError.isEmpty()) {
        if (!bundle.ok) r.applyError = bundleError;
        else if (!r.runtimeError.isEmpty()) r.applyError = r.runtimeError;
        else r.applyError = QStringLiteral("No bundled module matches this player's Qt compatibility contract (%1). Module build baselines: %2. Legacy modules remain exact-version only; newer public-API modules still require the same Qt major and full dependency checks. The player runtime is never replaced or downgraded.")
                                .arg(r.qtVersion.isEmpty() ? QStringLiteral("unknown") : r.qtVersion, versions.join(QStringLiteral(", ")));
    }
    QStringList report;
    report << QStringLiteral("Experimental native Settings → Shaders. Preflight is not game/GPU verification.");
    if (r.recoveryRequired) report << r.error << r.recoveryError;
    else if (!r.ok) report << r.error;
    else {
        report << (r.installed ? QStringLiteral("Owned native shader integration is installed.")
                              : r.sourcePresent ? QStringLiteral("Native sources are present but the module is missing. Remove this inactive integration before reinstalling.")
                              : r.modulePresent ? QStringLiteral("An owned shader module exists without its matching native source integration; automatic overwrite/removal is refused.")
                                                : QStringLiteral("Shaders are not installed in this player."));
        if (!r.qtVersion.isEmpty()) report << QStringLiteral("Player release Qt: %1 (x64). Module build baselines: %2.").arg(r.qtVersion, versions.join(QStringLiteral(", ")));
        if (!r.moduleQtVersion.isEmpty()) report << QStringLiteral("Selected module built with Qt %1; contract: %2. This preflight is not installed-player or GPU certification.")
            .arg(r.moduleQtVersion, r.publicQtApi ? QStringLiteral("audited public API, same major, newer/equal runtime") : QStringLiteral("legacy exact version"));
        if (r.canApply) report << (r.updateAvailable ? QStringLiteral("A different bundled module is ready to install as an update.")
                                                   : r.installed ? QStringLiteral("This bundled package is already installed; reapply is idempotent.")
                                                                 : QStringLiteral("Native source capacity and the matching module/dependency contract passed preflight."));
        else if (!r.applyError.isEmpty()) report << QStringLiteral("Install unavailable: ") + r.applyError;
        if (!r.canRemove && r.sourcePresent && !r.removeError.isEmpty()) report << QStringLiteral("Removal unavailable: ") + r.removeError;
    }
    report.removeAll(QString());
    QVariantMap runtime; for (auto i = r.runtimeSha256.cbegin(); i != r.runtimeSha256.cend(); ++i) runtime.insert(i.key(), i.value());
    return {{"ok", r.ok}, {"msg", report.join(QLatin1Char('\n'))}, {"installed", r.installed},
            {"sourcePresent", r.sourcePresent}, {"modulePresent", r.modulePresent}, {"playerClosed", r.playerClosed},
            {"canApply", r.canApply}, {"canRemove", r.canRemove}, {"canRecover", r.canRecover},
            {"updateAvailable", r.updateAvailable}, {"recoveryRequired", r.recoveryRequired},
            {"qtMajor", r.qtMajor}, {"qtVersion", r.qtVersion}, {"moduleQtVersion", r.moduleQtVersion},
            {"publicQtApi", r.publicQtApi}, {"exeSha256", r.exeSha256},
            {"packageSha256", r.packageSha256}, {"installedPackageSha256", r.installedPackageSha256},
            {"stateSha256", r.stateSha256}, {"applyReviewSha256", r.applyReviewSha256}, {"journalSha256", r.journalSha256},
            {"recoveryDirectory", r.recoveryDirectory}, {"bundleId", id}, {"bundledQtVersions", versions},
            {"bundleAvailable", bundle.ok}, {"runtimeSha256", runtime}, {"applyError", r.applyError},
            {"runtimeError", r.runtimeError}, {"removeError", r.removeError}, {"recoveryError", r.recoveryError}};
}
QVariantMap apply(const QString &exePath, const QString &bundleId, const QString &exeSha256,
                  const QString &packageSha256, const QString &reviewSha256, bool acknowledged)
{
    if (!acknowledged || !hash(exeSha256) || !hash(packageSha256) || !hash(reviewSha256))
        return failure(QStringLiteral("Inspect and acknowledge the exact bundled module/EXE/runtime set first."));
    const auto bundle = ShaderBundle::load();
    if (!bundle.ok) return failure(bundle.error);
    for (const auto &entry : bundle.entries) if (entry.id == bundleId) {
        if (entry.packageSha256 != packageSha256) return failure(QStringLiteral("The selected bundled package changed; inspect again."));
        return response(ShaderInstall::apply(exePath, entry.payload, exeSha256, packageSha256, true, reviewSha256),
            QStringLiteral("Installed the regular BSIShaders.dll module and native Settings → Shaders controls. No system proxy or runtime replacement was used. Launch the player, enable a preset and verify the actual game image/API before treating it as working."));
    }
    return failure(QStringLiteral("Only a reviewed, compiled-in shader package can be installed."));
}
QVariantMap remove(const QString &exePath, const QString &exeSha256, const QString &stateSha256, bool acknowledged)
{
    if (!acknowledged || !hash(exeSha256) || !hash(stateSha256)) return failure(QStringLiteral("Inspect and acknowledge this EXE/module pair before removal."));
    return response(ShaderInstall::remove(exePath, exeSha256, true, stateSha256),
        QStringLiteral("Removed the owned native shader integration and module. The Settings redesign and unrelated edits/runtime DLLs were kept. Saved shader preferences were kept but cannot process frames without the module."));
}
QVariantMap recover(const QString &exePath, const QString &journalSha256, bool acknowledged)
{
    if (!acknowledged || !hash(journalSha256)) return failure(QStringLiteral("Inspect and acknowledge the pending recovery transaction first."));
    return response(ShaderInstall::recover(exePath, journalSha256, true),
        QStringLiteral("Shader transaction recovery completed and the resulting EXE/module pair was verified."));
}
} // namespace ShaderActions

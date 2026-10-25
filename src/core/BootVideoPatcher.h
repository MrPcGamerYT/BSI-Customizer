#pragma once

#include <QString>

// Boot video patcher — points the player's boot screen at a video of the user's
// own, `Assets\Promo.mp4` next to HD-Player.exe.
//
// The video is not played from inside the player's own QML. The boot screen is
// given a Loader whose `source` is a small QML file the tool writes next to the
// EXE (`Assets\BootVideo.qml`), and that file tries Qt6 Multimedia, Qt5
// Multimedia and finally a WebEngine <video> at run time, each attempt inside
// its own `try`. A missing module therefore costs the video, never the boot
// screen: nothing is imported into the player's own QML, so a type that this
// particular build does not have cannot take the boot window down with it.
//
// The two QML files that already exist in the EXE (the boot screen and the
// player window that hosts it) are edited in place inside the record they
// already live in, so the EXE never grows, and their resource timestamp is
// refreshed so Qt recompiles them instead of reusing a cached compilation.
namespace BootVideoPatcher {

struct Options
{
    // The player destroys the boot screen as soon as boot reports complete;
    // without a hold the video would be visible for a fraction of a second.
    // The host is taught to keep the boot screen (and so the video) on screen
    // this long after boot completes.
    int holdMs = 3000;
    // Refresh the resource timestamps so the player cannot reuse the QML it
    // compiled before the edit.
    bool cacheBust = true;
};

struct Result
{
    bool ok = false;
    QString msg;
};

Result patch(const QString &exePath, const Options &options = Options());

} // namespace BootVideoPatcher

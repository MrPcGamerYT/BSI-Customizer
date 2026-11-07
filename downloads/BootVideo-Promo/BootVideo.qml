import QtQuick 2.15

// Save beside Promo.mp4 in the emulator's Assets folder.
// Video-only wrapper: no BootScreen.html or QtWebEngine dependency.
Item {
    id: root
    objectName: "bsiBootHtmlWrapper"
    anchors.fill: parent
    clip: true

    // Retain the earlier wrapper's public fields for loader compatibility.
    // This video-only version does not render its HTML/logo/ring animation.
    property string logoUrl: ""
    property string bootTheme: "orbit"
    property bool htmlLoaded: false
    property bool withVideo: true
    property bool videoPlaying: false
    property string diagnostic: "Loading Promo.mp4..."
    property url videoSource: Qt.resolvedUrl("Promo.mp4")
    property var playerItem: null

    function reportError(message) {
        videoPlaying = false;
        diagnostic = "Promo.mp4 could not play.\n" + message;
        console.warn("BSI boot video: " + message);
    }

    Rectangle {
        anchors.fill: parent
        color: "#050506"
    }

    // Fallback only. The emulator's original boot progress/ready gate
    // remains owned by its native boot controller, not this file.
    Text {
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 48)
        visible: !root.videoPlaying
        text: root.diagnostic
        color: "#d5d5de"
        font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    function startVideo() {
        if (playerItem !== null) {
            playerItem.destroy();
            playerItem = null;
        }
        videoPlaying = false;
        if (!withVideo) {
            diagnostic = "Boot video disabled.";
            return;
        }
        diagnostic = "Loading Promo.mp4...";
        var sourceLiteral = JSON.stringify(videoSource.toString());

        // Dynamic imports let the wrapper load even if multimedia is absent.
        var qt6 = [
            'import QtQuick 2.15',
            'import QtMultimedia',
            'Item {',
            '  objectName: "bsiPromoQt6"',
            '  anchors.fill: parent',
            '  z: 10',
            '  visible: root.videoPlaying',
            '  Rectangle { anchors.fill: parent; color: "black" }',
            '  VideoOutput {',
            '    id: output',
            '    anchors.fill: parent',
            '    fillMode: VideoOutput.PreserveAspectCrop',
            '  }',
            '  MediaPlayer {',
            '    id: player',
            '    source: ' + sourceLiteral,
            '    loops: MediaPlayer.Infinite',
            '    videoOutput: output',
            '    audioOutput: AudioOutput { muted: true }',
            '    onPlaybackStateChanged: {',
            '      root.videoPlaying = playbackState === MediaPlayer.PlayingState',
            '    }',
            '    onErrorOccurred: function(error, errorString) {',
            '      root.reportError(errorString)',
            '    }',
            '    Component.onCompleted: play()',
            '  }',
            '}'
        ].join("\n");

        try {
            playerItem = Qt.createQmlObject(qt6, root, "bsiPromoQt6");
            return;
        } catch (qt6Error) {
            console.warn("BSI Qt6 player unavailable: " + qt6Error);
        }

        // Qt5 fallback is used only if the Qt6 component cannot be created.
        // A runtime decoding error is reported, not treated as successful video.
        var qt5 = [
            'import QtQuick 2.15',
            'import QtMultimedia 5.15',
            'Video {',
            '  objectName: "bsiPromoQt5"',
            '  anchors.fill: parent',
            '  z: 10',
            '  visible: root.videoPlaying',
            '  source: ' + sourceLiteral,
            '  fillMode: VideoOutput.PreserveAspectCrop',
            '  loops: MediaPlayer.Infinite',
            '  muted: true',
            '  onPlaybackStateChanged: {',
            '    root.videoPlaying = playbackState === MediaPlayer.PlayingState',
            '  }',
            '  onErrorChanged: {',
            '    if (error !== MediaPlayer.NoError) root.reportError(errorString)',
            '  }',
            '  Component.onCompleted: play()',
            '}'
        ].join("\n");

        try {
            playerItem = Qt.createQmlObject(qt5, root, "bsiPromoQt5");
        } catch (qt5Error) {
            reportError("No compatible QtMultimedia player could be created.\n"
                        + "Check the emulator's multimedia modules. See the QML log for details.");
            console.warn("BSI Qt5 player unavailable: " + qt5Error);
        }
    }

    Component.onCompleted: startVideo()
}

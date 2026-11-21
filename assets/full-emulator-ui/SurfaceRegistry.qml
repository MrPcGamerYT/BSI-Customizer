pragma Singleton
import QtQuick 2.15

QtObject {
    // Only known native player/Settings/editor hosts opt in. Boot/auth windows do not.
    property var hosts: []
    function attach(owner, window) {
        var next = []
        for (var i=0;i<hosts.length;++i) if (hosts[i].owner !== owner) next.push(hosts[i])
        if (owner && window) next.push({owner:owner, window:window})
        hosts = next
    }
    function detach(owner) { attach(owner, null) }
    function owns(window) {
        var current=window
        for (var depth=0;current && depth<12;++depth) {
            for (var i=0;i<hosts.length;++i) {
                if (hosts[i].window === current && current.pIsBootComplete !== false) return true
            }
            current = current.transientParent
        }
        return false
    }
}

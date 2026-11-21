.pragma library
// Shared only inside this player's QML engine; no settings/registry writes.
var target = null;
var pending = false;
function request() {
    pending = true;
    if (target) { pending = false; target.fVoidSelectOptimizer(); }
}
function attach(window) {
    target = window;
    if (pending) {
        pending = false;
        Qt.callLater(function() { if (target === window) window.fVoidSelectOptimizer(); });
    }
}
function detach(window) { if (target === window) target = null; }

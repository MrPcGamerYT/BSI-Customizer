/* Transport contract v1. Future hosts can supply this contract without moving
   authorization, update verification or patching into the renderer. */
(() => {
  'use strict';
  class DesktopTransport {
    constructor(remote) {
      this.remote = remote;
      this.fileRequest = null;
      remote.fileSelected.connect((role, path) => {
        if (this.fileRequest && this.fileRequest.role === role) {
          const pending = this.fileRequest;
          this.fileRequest = null;
          clearTimeout(pending.timeout);
          pending.resolve(path);
        }
      });
    }
    windowAction(action) { if (['min', 'max', 'close', 'move'].includes(action)) this.remote.windowAction?.(action); }
    snapshot() { return new Promise(resolve => this.remote.snapshot(resolve)); }
    async invoke(method, args = {}) {
      const reply = await new Promise(resolve => this.remote.invoke(method, args, resolve));
      if (!reply || !reply.ok) throw new Error(reply?.error || 'The native request failed.');
      if (reply.value && reply.value.ok === false) {
        throw new Error(reply.value.msg || reply.value.error || reply.value.message || 'The engine refused this operation.');
      }
      return reply.value;
    }
    chooseFile(role) {
      if (this.fileRequest) return Promise.reject(new Error('A file dialog is already open.'));
      return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
          this.fileRequest = null;
          reject(new Error('File selection timed out. Please try again.'));
        }, 300000);
        this.fileRequest = { role, resolve, timeout };
        this.remote.requestFile(role);
      });
    }
    onVideo(fn) { this.remote.videoStatus?.connect(fn); }
    onState(fn) { this.remote.stateChanged.connect(fn); }
    onLog(fn) { this.remote.logMessage.connect(fn); }
  }
  window.BsiTransport = {
    connect() {
      return new Promise((resolve, reject) => {
        if (!window.qt?.webChannelTransport || typeof QWebChannel !== 'function') {
          reject(new Error('Open this workspace using BSI-Customizer.exe. The browser alone cannot access C++ engines.'));
          return;
        }
        const timeout = setTimeout(() => reject(new Error('The native bridge did not respond. Restart with --native-ui.')), 10000);
        new QWebChannel(qt.webChannelTransport, channel => {
          clearTimeout(timeout);
          if (!channel.objects.bsi) return reject(new Error('The BSI native bridge is unavailable.'));
          resolve(new DesktopTransport(channel.objects.bsi));
        });
      });
    }
  };
})();

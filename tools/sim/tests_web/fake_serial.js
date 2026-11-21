// Put in the page before its scripts run: navigator.serial with one port, a
// Monitor Buddy whose other end is the firmware simulator. Bytes the page
// writes go to mbsimWrite(hex); every 40 ms mbsimTick() lets the simulated
// Buddy live a little and hands back what it printed. Both are Python
// functions (tests_web/conftest.py).
(() => {
  const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
  const unhex = (text) => Uint8Array.from(text.match(/../g) || [], (pair) => parseInt(pair, 16));
  const sim = (window.__mbsim = { opens: 0, signals: [], filters: null, cancel: false, busy: false, written: "" });

  class FakePort extends EventTarget {
    constructor() { super(); this.readable = null; this.writable = null; this.isOpen = false; }
    getInfo() { return { usbVendorId: 0x303a, usbProductId: 0x1001 }; }
    async open(options) {
      if (this.isOpen) throw new DOMException("The port is already open.", "InvalidStateError");
      if (sim.busy) throw new DOMException("Failed to open serial port.", "NetworkError");
      sim.opens++;
      sim.baud = options && options.baudRate;
      this.isOpen = true;
      let feed = null;
      this.feed = (text) => { if (text && feed) feed.enqueue(unhex(text)); };
      this.readable = new ReadableStream({ start: (c) => { feed = c; }, cancel: () => { feed = null; } });
      this.writable = new WritableStream({
        write: async (chunk) => {
          sim.written += new TextDecoder().decode(chunk);
          this.feed(await window.mbsimWrite(hex(chunk)));
        },
      });
      let ticking = false;
      this.timer = setInterval(async () => {
        if (ticking) return;
        ticking = true;
        try { this.feed(await window.mbsimTick()); } finally { ticking = false; }
      }, 40);
    }
    async setSignals(signals) { sim.signals.push(signals); }
    async getSignals() { return {}; }
    async close() {
      clearInterval(this.timer);
      this.isOpen = false;
      this.readable = this.writable = null;
    }
    async forget() {}
  }

  const port = new FakePort();
  const serial = new EventTarget();
  serial.requestPort = async (options) => {
    sim.filters = options;
    if (sim.cancel) throw new DOMException("No port selected by the user.", "NotFoundError");
    return port;
  };
  serial.getPorts = async () => [port];
  Object.defineProperty(Navigator.prototype, "serial", { get: () => serial, configurable: true });
})();

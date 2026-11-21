// Monitor Buddy, the live preview: drives the firmware built for the browser
// (mbsim.js + mbsim.wasm, made by tools/sim/build_wasm.sh from the same
// src/main.cpp the board runs). What tools/sim/mbsim.py is for the tests,
// this file is for a page: boot a Buddy with some settings, let its time pass,
// read the 320x172 screen, touch it, change a setting the way the app does
// over USB ("@MB SET"), feed it what the laptop would send.
//
//   const sim = new BuddySim(MBSim, wasmBytes);
//   await sim.boot({config: {...}, whenMs: Date.now()});
//   await sim.run(40);  const {w, h, rgba} = sim.frame();
//
// Works in a browser (global BuddySim) and in Node (module.exports), where the tests use it.
(function (root) {
  'use strict';

  // The key of the simulated Buddy's Wi-Fi link (tools/sim/keys/link-key.txt): it protects nothing.
  const LINK_KEY = 'abe3f2ce3e5d9b091b8685fd30e671c9b491f11c889b3109a5ad2aaa8d6bb196';
  const WIFI = ['HomeWiFi', 'password123'];
  const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
  const encoder = new TextEncoder(), decoder = new TextDecoder();
  const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
  const unhex = (text) => Uint8Array.from(text.match(/../g) || [], (pair) => parseInt(pair, 16));
  const hexText = (text) => hex(encoder.encode(text));

  // ── The internet as the simulated Buddy sees it (tools/sim/mock_internet.py) ──
  class MockInternet {
    constructor(today) {
      this.today = today || new Date(Date.UTC(2026, 0, 5));
      this.weather = {temperature_2m: 27.4, relative_humidity_2m: 48, weather_code: 1, wind_speed_10m: 13.2, is_day: 1};
      this.github = {followers: 128, public_repos: 31, stars: [12, 40, 3, 0, 7], streak: 9, today: 4};
      this.stock = {c: 231.42, d: 1.15, dp: 0.4994, h: 233.1, l: 229.8, o: 230.2, pc: 230.27, t: 1767600000};
      this.coin = {open: '91250.10', high: '93890.00', low: '90811.55', last: '93120.42', volume: '8123.4'};
      this.anySymbol = false;   // the preview: every symbol and pair has (the same sample) numbers
      this.stocks = {AAPL: this.stock};
      this.coins = {'BTC-USD': this.coin};
      this.requests = [];
    }

    contributions() {
      const cells = [];
      const g = this.github;
      for (let back = 370; back >= 0; back--) {
        const day = new Date(this.today.getTime() - back * 86400000);
        let count;
        if (back === 0) count = g.today;
        else if (back < g.streak + (g.today ? 0 : 1)) count = 1 + (back * 7) % 5;
        else if (back === g.streak + (g.today ? 0 : 1)) count = 0;
        else count = back % 3 ? (back * 5) % 4 : 0;
        const weekday = (day.getUTCDay() + 6) % 7;
        const ident = 'contribution-day-component-' + weekday + '-' + back;
        const words = !count ? 'No contributions' : count + ' contribution' + (count === 1 ? '' : 's');
        cells.push('<td tabindex="0" data-ix="' + back + '" style="width: 11px" data-date="' + day.toISOString().slice(0, 10) + '" id="' + ident
          + '" data-level="' + Math.min(count, 4) + '" role="gridcell" class="ContributionCalendar-day"></td>\n'
          + '<tool-tip id="tooltip-' + back + '" for="' + ident + '" popover="manual" data-type="label" class="sr-only position-absolute">'
          + words + ' on ' + MONTHS[day.getUTCMonth()] + ' ' + day.getUTCDate() + '.</tool-tip>\n');
      }
      return '<table class="ContributionCalendar-grid js-calendar-graph-table">\n<tbody>\n<tr style="height: 10px">\n' + cells.join('') + '</tr>\n</tbody>\n</table>\n';
    }

    answer(host, path, query) {
      if (host === 'api.open-meteo.com') {
        return [200, JSON.stringify({latitude: 25.2, longitude: 55.27, current: Object.assign({}, this.weather, {time: '2026-01-05T12:00'})})];
      }
      if (host === 'api.github.com' && path.endsWith('/repos')) {
        return [200, JSON.stringify(this.github.stars.map((stars, i) => ({name: 'repo' + i, stargazers_count: stars, fork: false})))];
      }
      if (host === 'api.github.com') {
        return [200, JSON.stringify({login: path.split('/').pop(), followers: this.github.followers, public_repos: this.github.public_repos})];
      }
      if (host === 'github.com' && path.endsWith('/contributions')) return [200, this.contributions()];
      if (host === 'finnhub.io') {
        const symbol = (query.get('symbol') || '').replace('COINBASE:', '');
        const quote = this.stocks[symbol] || (this.anySymbol && symbol ? this.stock : null);
        return [200, JSON.stringify(quote || {c: 0, d: null, dp: null, h: 0, l: 0, o: 0, pc: 0, t: 0})];
      }
      if (host === 'api.exchange.coinbase.com') {
        const parts = path.replace(/^\/+|\/+$/g, '').split('/');
        const coin = this.coins[parts[1] || ''] || (this.anySymbol && parts[1] ? this.coin : null);
        if (!coin) return [404, JSON.stringify({message: 'NotFound'})];
        if (parts[parts.length - 1] === 'candles') {
          const last = parseFloat(coin.last);
          const rows = [];
          for (let i = 0; i < 24; i++) rows.push([1767600000 - 3600 * i, last * 0.99, last * 1.01, last, last * (1 - 0.002 * i), 5.0]);
          return [200, JSON.stringify(rows)];
        }
        return [200, JSON.stringify(coin)];
      }
      return [404, 'the mock internet has no ' + host];
    }

    // One request as the simulator sends it ("GET /<host><path> HTTP/1.0 ..."): the whole answer as bytes.
    serve(request) {
      const target = request.split(' ')[1] || '/';
      const url = new URL('http://mock' + target);
      const at = url.pathname.replace(/^\/+/, '');
      const slash = at.indexOf('/');
      const host = slash < 0 ? at : at.slice(0, slash), path = slash < 0 ? '/' : at.slice(slash);
      this.requests.push([host, path + url.search]);
      const [status, body] = this.answer(host, path, url.searchParams);
      const data = encoder.encode(body);
      const head = encoder.encode('HTTP/1.0 ' + status + ' OK\r\nContent-Length: ' + data.length + '\r\n\r\n');
      const out = new Uint8Array(head.length + data.length);
      out.set(head);
      out.set(data, head.length);
      return out;
    }
  }

  class BuddySim {
    // factory: the MBSim function of mbsim.js; wasm: the bytes of mbsim.wasm.
    constructor(factory, wasm) {
      this.factory = factory;
      this.wasm = wasm;
      this.module = null;
      this.internet = new MockInternet();
      this.files = {};            // the board's flash and card: path under /data -> bytes; survives restarts
      this.boots = 0;
      this.restarts = 0;          // restarts the firmware asked for (a saved setting restarts it)
      this.log = '';
      this.trueMs = 0;
      this.rtcMs = 0;
      this.tMs = 0;
      this.nonceMs = 0;
      this.options = {};
      this.key = null;
      this.clock = null;          // () => ms: the time out in the world at each start; without it a restart goes on where the clock was
    }

    // Power on. config: /config.json (the settings); whenMs: the time out in the world;
    // wifi: [ssid, password] or null for a Buddy that was never on a network.
    async boot(options) {
      options = Object.assign({config: null, whenMs: Date.UTC(2026, 0, 5, 4, 0, 0), wifi: WIFI, keys: true, card: 'none',
                               seed: 1, accel: [0, -1, 0], internet: true, files: {}}, options || {});
      this.options = options;
      this.files = {};
      this.boots = 0;
      this.log = '';
      this.trueMs = options.whenMs;
      this.rtcMs = 0;
      this.resetReason = 1;
      if (options.wifi) this.writeFile('/wifi.json', JSON.stringify({ssid: options.wifi[0], password: options.wifi[1]}));
      if (options.keys) this.writeFile('/keys.dat', JSON.stringify({link_key: LINK_KEY, update_key: ''}));
      if (options.config) this.writeFile('/config.json', typeof options.config === 'string' ? options.config : JSON.stringify(options.config));
      for (const path of Object.keys(options.files)) this.writeFile(path, options.files[path]);
      await this._start();
    }

    writeFile(path, content, fs) {
      const bytes = typeof content === 'string' ? encoder.encode(content) : content;
      const at = '/data/' + (fs || 'littlefs') + '/' + path.replace(/^\/+/, '');
      this.files[at] = bytes;
      if (this.module) this._put(this.module, at, bytes);
    }

    readFile(path, fs) {
      const at = '/data/' + (fs || 'littlefs') + '/' + path.replace(/^\/+/, '');
      try { return this.module.FS.readFile(at); } catch (error) { return null; }
    }

    _put(module, at, bytes) {
      const parts = at.split('/').slice(1, -1);
      let dir = '';
      for (const part of parts) {
        dir += '/' + part;
        try { module.FS.mkdir(dir); } catch (error) { /* there already */ }
      }
      module.FS.writeFile(at, bytes);
    }

    _walk(module, dir, into) {
      for (const name of module.FS.readdir(dir)) {
        if (name === '.' || name === '..') continue;
        const at = dir + '/' + name;
        if (module.FS.isDir(module.FS.stat(at).mode)) this._walk(module, at, into);
        else into[at] = module.FS.readFile(at);
      }
    }

    async _start() {
      const o = this.options;
      const sim = this;
      if (this.clock) this.trueMs = this.clock();
      const module = await this.factory({
        wasmBinary: this.wasm,
        noInitialRun: true,
        print: () => {},
        printErr: () => {},
        mbsimHttp: (request) => (sim.options.internet ? sim.internet.serve(request) : null),
      });
      for (const dir of ['/data', '/data/littlefs']) { try { module.FS.mkdir(dir); } catch (error) { /* there */ } }
      for (const at of Object.keys(this.files)) this._put(module, at, this.files[at]);
      const args = ['--data', '/data', '--epoch', String(Math.floor(this.trueMs / 1000)), '--rtc', String(Math.floor(this.rtcMs / 1000)),
                    '--reset-reason', String(this.resetReason), '--seed', String(o.seed + this.boots), '--card', o.card,
                    '--accel', o.accel.join(',')];
      if (o.wifi) args.push('--ap', o.wifi[0] + ':' + o.wifi[1] + ':-55');
      if (o.internet) args.push('--http-mock', 'page:0'); else args.push('--no-internet');
      module.callMain(args);
      this.line = module._malloc(16384);
      this.size = module._malloc(8);
      this.module = module;
      this.boots++;
      this.tMs = 0;
    }

    // One command of the simulator's control channel (tools/sim/host/sim_core.cpp); its answer.
    // A firmware restart is noted, and carried out by the next run().
    cmd(line) {
      const m = this.module;
      const bytes = encoder.encode(line);
      if (bytes.length > 16380) throw new Error('command too long');
      m.HEAPU8.set(bytes, this.line);
      m.HEAPU8[this.line + bytes.length] = 0;
      m._mbsim_cmd(this.line);
      const answer = JSON.parse(m.UTF8ToString(m._mbsim_reply()));
      this.tMs = answer.t_ms;
      this.trueMs = answer.true_ms;
      this.rtcMs = answer.rtc_ms;
      if (answer.restart) { this.pending = true; this.resetReason = answer.reason; }
      return answer;
    }

    async _restartIfAsked() {
      if (!this.pending) return false;
      this.pending = false;
      const kept = {};
      this._walk(this.module, '/data', kept);
      this.files = kept;
      this.module = null;
      this.restarts++;
      await this._start();
      return true;
    }

    // Let `ms` of the Buddy's time pass. True when the firmware restarted on the way (the rest of the time is then dropped).
    async run(ms, stop) {
      let line = 'run ' + Math.max(0, Math.round(ms));
      if (stop && stop.udp) line += ' udp';
      if (stop && stop.serial) line += ' serial ' + hexText(stop.serial);
      this.cmd(line);
      this.log += decoder.decode(unhex(this.cmd('serial_out').hex));
      if (this.log.length > 400000) this.log = this.log.slice(-200000);
      return this._restartIfAsked();
    }

    // The glass: {w, h, rgba} (a Uint8ClampedArray, a copy).
    frame() {
      const m = this.module;
      const at = m._mbsim_frame(this.size, this.size + 4);
      const w = m.HEAP32[this.size >> 2], h = m.HEAP32[(this.size + 4) >> 2];
      return {w, h, rgba: new Uint8ClampedArray(m.HEAPU8.buffer, at, w * h * 4).slice()};
    }

    board() { return this.cmd('state'); }

    statusLine() {
      const found = this.log.replace(/\r/g, '').match(/^app=.*$/gm);
      const out = {};
      if (found) for (const pair of found[found.length - 1].split(' ')) { const [k, v] = pair.split('='); out[k] = v; }
      return out;
    }

    // ── USB: "@MB <COMMAND> <json>", as the app and the installer send it ──
    async mb(command, body, waitMs) {
      const mark = this.log.length;
      this.cmd('serial_in ' + hexText('@MB ' + command + (body === undefined ? '' : ' ' + JSON.stringify(body)) + '\n'));
      const word = '"cmd":"' + command.slice(0, 16) + '"';
      let restarted = await this.run(waitMs || 3000, {serial: word});
      if (!restarted) restarted = await this.run(40);
      let answer = null;
      for (const line of this.log.slice(mark).replace(/\r/g, '').split('\n')) {
        if (!line.startsWith('@MB {')) continue;
        try { const got = JSON.parse(line.slice(4)); if (got.cmd === command.slice(0, 16)) answer = got; } catch (error) { /* a torn line */ }
      }
      return answer;
    }

    // "@MB SET": the answer, after the restart a saved change brings.
    async set(config, wifi) {
      const body = {config};
      if (wifi) body.wifi = wifi;
      const before = this.restarts;
      const answer = await this.mb('SET', body);
      if (answer && answer.ok && this.restarts === before) await this.run(2000);
      return answer;
    }

    // ── fingers ──
    touch(x, y) { this.cmd('touch ' + Math.round(x) + ' ' + Math.round(y)); }
    release() { this.cmd('release'); }
    async tap(x, y) { this.touch(x, y); await this.run(90); this.release(); await this.run(450); }
    async swipe(x0, y0, x1, y1) {
      for (let i = 0; i <= 8; i++) { this.touch(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8); await this.run(30); }
      this.release();
      await this.run(450);
    }
    async swipeLeft() { await this.swipe(250, 86, 70, 86); }
    async swipeRight() { await this.swipe(70, 86, 250, 86); }
    async doubleKnock() {
      const [x, y, z] = this.options.accel;
      await this.run(400);
      for (const gap of [260, 500]) {
        this.cmd('accel ' + (x + 0.9) + ' ' + y + ' ' + z);
        await this.run(6);
        this.cmd('accel ' + x + ' ' + y + ' ' + z);
        await this.run(gap);
      }
    }

    // ── the Wi-Fi link, as tools/buddy_link.py speaks it: AES-256-GCM, the nonce in front ──
    async link(message, waitMs) {
      const subtle = (root.crypto || require('crypto').webcrypto).subtle;
      if (!this.key) this.key = await subtle.importKey('raw', unhex(LINK_KEY), 'AES-GCM', false, ['encrypt', 'decrypt']);
      const stamp = Math.max(this.trueMs, this.nonceMs + 1);
      this.nonceMs = stamp;
      const nonce = new Uint8Array(12);
      const view = new DataView(nonce.buffer);
      view.setUint32(0, Math.floor(stamp / 4294967296));
      view.setUint32(4, stamp % 4294967296);
      view.setUint32(8, (this.boots * 65536 + (stamp % 65536)) >>> 0);
      const plain = typeof message === 'string' ? encoder.encode(message) : message;
      const sealed = new Uint8Array(await subtle.encrypt({name: 'AES-GCM', iv: nonce}, this.key, plain));
      this.cmd('udp_out');
      this.cmd('udp ' + hex(nonce) + hex(sealed));
      if (await this.run(waitMs || 400, {udp: true})) return null;
      const back = nonce.slice();
      back[0] |= 0x80;
      for (const packet of this.cmd('udp_out').packets) {
        try {
          const text = new Uint8Array(await subtle.decrypt({name: 'AES-GCM', iv: back}, this.key, unhex(packet)));
          if (text[0] === 111 && text[1] === 107) return text.slice(2);   // "ok"
        } catch (error) { /* not ours */ }
      }
      return null;
    }

    // What the laptop tells the Buddy every minute (docs/HANDOVER.md §6.3). True when the Buddy answered.
    async state(fields) { return (await this.link(JSON.stringify(fields))) !== null; }
    async odoo(fields) { return (await this.link(new Uint8Array([79, ...encoder.encode(JSON.stringify(fields))]))) !== null; }
    async say(text) { return (await this.link(new Uint8Array([83, ...encoder.encode(text)]))) !== null; }
    async showPage(page) { return (await this.link(new Uint8Array([71, page]))) !== null; }
  }

  BuddySim.MockInternet = MockInternet;
  BuddySim.LINK_KEY = LINK_KEY;
  if (typeof module !== 'undefined' && module.exports) module.exports = BuddySim;
  else root.BuddySim = BuddySim;
})(typeof globalThis !== 'undefined' ? globalThis : this);

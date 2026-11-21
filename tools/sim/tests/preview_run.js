// Runs a script of steps on the browser build of the simulator (tools/app/sim),
// under Node: what tests/test_preview.py uses to compare its pixels with the
// native simulator's golden pictures.
//
//   node preview_run.js APP_SIM_DIR OUT_DIR < steps.json
//
// steps.json: {"boot": {...options of BuddySim.boot, "sd": DIR copied to the card...}, "steps": [[verb, ...], ...]}
// Verbs: run MS · state {} · odoo {} · page N · set {} · shot NAME (OUT_DIR/NAME.rgba) · swipe left|right · tap X Y.
// {"$now": N} anywhere in a step is the simulated clock in seconds, plus N;
// {"$mark": N} is the clock as it was at the last "mark" step, plus N.
const fs = require('fs'), path = require('path');
const [dir, out] = process.argv.slice(2);
const MBSim = require(path.join(dir, 'mbsim.js')), BuddySim = require(path.join(dir, 'buddy-sim.js'));

(async () => {
  const script = JSON.parse(fs.readFileSync(0, 'utf8'));
  const sim = new BuddySim(MBSim, fs.readFileSync(path.join(dir, 'mbsim.wasm')));
  const boot = script.boot || {};
  boot.files = boot.files || {};
  await sim.boot(Object.assign({}, boot, {sd: undefined}));
  if (boot.sd) {   // the card's files; a restart keeps them
    for (const name of fs.readdirSync(boot.sd).sort()) sim.writeFile('/gifs/' + name, fs.readFileSync(path.join(boot.sd, name)), 'sd');
    sim.pending = true;
    sim.resetReason = 1;
    sim.boots = 0;
    await sim._restartIfAsked();
    sim.restarts = 0;
  }
  const now = (value) => {
    if (Array.isArray(value)) return value.map(now);
    if (value && typeof value === 'object') {
      if ('$now' in value) return Math.floor(sim.trueMs / 1000) + value.$now;
      if ('$mark' in value) return mark + value.$mark;
      const copy = {};
      for (const key of Object.keys(value)) copy[key] = now(value[key]);
      return copy;
    }
    return value;
  };
  const report = [];
  let mark = 0;
  for (const [verb, ...args] of script.steps) {
    if (verb === 'run') await sim.run(args[0]);
    else if (verb === 'mark') mark = Math.floor(sim.trueMs / 1000);
    else if (verb === 'state') report.push(['state', await sim.state(now(args[0]))]);
    else if (verb === 'odoo') report.push(['odoo', await sim.odoo(now(args[0]))]);
    else if (verb === 'page') report.push(['page', await sim.showPage(args[0])]);
    else if (verb === 'set') report.push(['set', await sim.set(args[0], args[1])]);
    else if (verb === 'get') report.push(['get', await sim.mb('GET')]);
    else if (verb === 'swipe') await (args[0] === 'left' ? sim.swipeLeft() : sim.swipeRight());
    else if (verb === 'tap') await sim.tap(args[0], args[1]);
    else if (verb === 'status') report.push(['status', sim.statusLine()]);
    else if (verb === 'shot') { const f = sim.frame(); fs.writeFileSync(path.join(out, args[0] + '.rgba'), f.rgba); report.push(['shot', args[0], f.w, f.h]); }
    else throw new Error('unknown step ' + verb);
  }
  process.stdout.write(JSON.stringify({report, restarts: sim.restarts, requests: sim.internet.requests}));
})().catch((error) => { console.error(error); process.exit(1); });

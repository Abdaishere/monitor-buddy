// Every minute: my Odoo figures from www.odoo.com, read with the session
// Chrome is signed in with, handed to tools/buddy_odoo_host.py. The shape is
// the one odooFromPc() in src/main.cpp reads; buddy_link.py keeps the
// meetings' links, their fourth item, to itself.
const ODOO = 'https://www.odoo.com';
const HOST = 'com.monitor_buddy.odoo';
const OPEN = [['stage_id.name', 'not ilike', 'done'], ['stage_id.name', 'not ilike', 'cancel'],
              ['stage_id.name', 'not ilike', 'parking'], ['stage_id.name', 'not ilike', 'template']];

async function rpc(route, params) {
  const reply = await fetch(ODOO + route, {
    method: 'POST', credentials: 'include', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({jsonrpc: '2.0', method: 'call', id: 1, params}),
  });
  const data = await reply.json();
  if (data.error) throw new Error(data.error.data?.message || data.error.message);
  return data.result;
}

const call = (model, method, args, kwargs = {}) => rpc(`/web/dataset/call_kw/${model}/${method}`, {model, method, args, kwargs});
const day = date => date.toLocaleDateString('en-CA');   // YYYY-MM-DD in this PC's time zone
const utc = date => date.toISOString().slice(0, 19).replace('T', ' ');
const seconds = text => Date.parse(text.replace(' ', 'T') + 'Z') / 1000;

async function hours(uid, date) {
  const lines = await call('account.analytic.line', 'search_read', [[['user_id', '=', uid], ['date', '=', day(date)]]], {fields: ['unit_amount']});
  return lines.reduce((sum, line) => sum + line.unit_amount, 0);
}

// My meetings of the next 24 hours, with their video call link where this Odoo has the field.
function meetings(partner, now) {
  const domain = [['partner_ids', 'in', [partner]], ['stop', '>=', utc(now)],
                  ['start', '<=', utc(new Date(+now + 86400000))], ['allday', '=', false]];
  const read = fields => call('calendar.event', 'search_read', [domain], {fields, order: 'start asc', limit: 6});
  const fields = ['name', 'start', 'stop', 'current_status'];
  return read([...fields, 'videocall_location']).catch(() => read(fields));
}

async function summary() {
  const session = await rpc('/web/session/get_session_info', {});
  if (!session.uid) return {error: 'signed out of www.odoo.com'};
  const uid = session.uid;
  const partner = session.partner_id || (await call('res.users', 'read', [[uid], ['partner_id']]))[0].partner_id[0];
  const mine = [['user_ids', 'in', [uid]], ...OPEN];
  const now = new Date();
  const before = new Date(now - (now.getDay() === 1 ? 3 : now.getDay() === 0 ? 2 : 1) * 86400000);   // the working day before
  const beforeStart = new Date(before.getFullYear(), before.getMonth(), before.getDate());   // its local midnight
  const beforeEnd = new Date(before.getFullYear(), before.getMonth(), before.getDate() + 1);
  const [h, hb, y, o, r, d, a, tasks, events] = await Promise.all([
    hours(uid, now),
    hours(uid, before),
    call('project.task', 'search_read', [[['user_ids', 'in', [uid]], ['write_date', '>=', utc(beforeStart)],
         ['write_date', '<', utc(beforeEnd)]]], {fields: ['name'], order: 'write_date desc', limit: 3}),
    call('project.task', 'search_count', [mine]),
    call('project.task', 'search_count', [[...mine, ['stage_id.name', 'ilike', 'code review']]]),
    call('project.task', 'search_count', [[...mine, ['stage_id.name', 'ilike', 'development']]]),
    call('mail.activity', 'search_count', [[['user_id', '=', uid], ['date_deadline', '<=', day(now)]]]),
    call('project.task', 'search_read', [mine], {fields: ['name', 'stage_id'], order: 'write_date desc', limit: 6}),
    meetings(partner, now),
  ]);
  return {
    h, hb, bw: before.getDay(), o, r, d, a,
    y: y.map(task => task.name.slice(0, 40)),
    t: tasks.map(task => [task.id, task.name.slice(0, 64), task.stage_id ? task.stage_id[1].slice(0, 40) : '']),
    m: events.filter(event => event.current_status !== 'declined').slice(0, 3)
             .map(event => [event.name.slice(0, 40), seconds(event.start), seconds(event.stop), event.videocall_location || '']),
  };
}

async function tick() {
  let data;
  try {
    data = await summary();
  } catch (error) {
    data = {error: String(error.message || error)};
  }
  await chrome.runtime.sendNativeMessage(HOST, data).catch(error => console.warn('Monitor Buddy helper:', error.message));
}

chrome.alarms.get('odoo').then(alarm => alarm?.periodInMinutes === 1 || chrome.alarms.create('odoo', {periodInMinutes: 1}));
chrome.alarms.onAlarm.addListener(tick);
chrome.runtime.onStartup.addListener(tick);
chrome.runtime.onInstalled.addListener(tick);

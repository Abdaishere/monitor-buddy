// The toolbar popup: a line for the Buddy, through the same helper as buddy-say.
const HOST = 'com.monitor_buddy.odoo';
const QUICK = ['Be right back', 'In a call', 'Lunch?', 'Coffee time', 'Do not disturb'];
const result = document.getElementById('result');
const input = document.getElementById('text');

async function say(text) {
  result.className = '';
  result.textContent = 'Sending…';
  let answer;
  try {
    answer = await chrome.runtime.sendNativeMessage(HOST, {say: text});
  } catch (error) {   // the helper is not registered: setup has not run with the Odoo box ticked
    answer = {ok: false, error: 'The Buddy helper is not connected to Chrome. Open Monitor Buddy on this computer, tick "Chrome extension" and click Set up.'};
  }
  result.className = answer.ok ? '' : 'bad';
  result.textContent = answer.ok ? 'On the Buddy now.' : answer.error;
  if (answer.ok) input.value = '';
}

document.getElementById('form').addEventListener('submit', event => {
  event.preventDefault();
  say(input.value);
});
for (const line of QUICK) {
  const button = document.createElement('button');
  button.type = 'button';
  button.textContent = line;
  button.addEventListener('click', () => say(line));
  document.getElementById('quick').append(button);
}

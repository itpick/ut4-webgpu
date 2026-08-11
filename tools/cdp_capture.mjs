// Minimal CDP driver (no puppeteer/playwright dependency): launches headless
// Chrome with --remote-debugging-port, opens a target page via the HTTP
// /json/new endpoint, connects to its devtools websocket, enables
// Page+Runtime domains, navigates, and collects Runtime.consoleAPICalled
// messages for N seconds, then prints them and exits. Node 22+ has a
// built-in global WebSocket + fetch, so no npm deps needed.
import { spawn } from 'node:child_process';

const CHROME = process.argv[2];
const URL = process.argv[3];
const WAIT_MS = parseInt(process.argv[4] || '6000', 10);
const CDP_PORT = 9333;

const args = [
  `--headless=new`,
  `--remote-debugging-port=${CDP_PORT}`,
  `--enable-unsafe-webgpu`,
  `--enable-features=Vulkan`,
  `--ignore-gpu-blocklist`,
  `--no-sandbox`,
  `--disable-gpu-sandbox`,
  `--use-gl=angle`,
  `--use-angle=vulkan`,
  `--user-data-dir=/tmp/chrome-cdp-profile-${Date.now()}`,
  `about:blank`,
];

// NOTE: BUILD_RECIPE.md found --use-angle=vulkan broke the compositor
// surface for canvas screenshotting; we don't need a canvas surface here
// (offscreen readback only), so try WITHOUT --use-angle=vulkan first --
// override via env if needed.
const finalArgs = process.env.NO_ANGLE_VULKAN
  ? args.filter(a => a !== '--use-angle=vulkan' && a !== '--use-gl=angle')
  : args;

console.error('Launching: ' + CHROME + ' ' + finalArgs.join(' '));
const chrome = spawn(CHROME, finalArgs, { stdio: ['ignore', 'pipe', 'pipe'] });
chrome.stdout.on('data', d => process.stderr.write('[chrome stdout] ' + d));
chrome.stderr.on('data', d => process.stderr.write('[chrome stderr] ' + d));

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function waitForCdp() {
  for (let i = 0; i < 60; i++) {
    try {
      const res = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`);
      if (res.ok) return await res.json();
    } catch (e) { /* not up yet */ }
    await sleep(500);
  }
  throw new Error('CDP never came up');
}

async function main() {
  await waitForCdp();
  console.error('CDP is up.');

  const newTarget = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?about:blank`, { method: 'PUT' });
  const target = await newTarget.json();
  const wsUrl = target.webSocketDebuggerUrl;
  console.error('Target ws: ' + wsUrl);

  const ws = new WebSocket(wsUrl);
  const messages = [];
  let msgId = 1;
  const pending = new Map();

  function send(method, params = {}) {
    const id = msgId++;
    return new Promise(resolve => {
      pending.set(id, resolve);
      ws.send(JSON.stringify({ id, method, params }));
    });
  }

  await new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve);
    ws.addEventListener('error', reject);
  });

  ws.addEventListener('message', ev => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      pending.get(msg.id)(msg.result);
      pending.delete(msg.id);
      return;
    }
    if (msg.method === 'Runtime.consoleAPICalled') {
      const text = (msg.params.args || []).map(a => a.value !== undefined ? a.value : (a.description || '')).join(' ');
      messages.push(`[console.${msg.params.type}] ${text}`);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      messages.push(`[exception] ${JSON.stringify(msg.params.exceptionDetails)}`);
    } else if (msg.method === 'Log.entryAdded') {
      messages.push(`[log.${msg.params.entry.level}] ${msg.params.entry.text}`);
    }
  });

  await send('Runtime.enable');
  await send('Log.enable');
  await send('Page.enable');
  await send('Page.navigate', { url: URL });

  await sleep(WAIT_MS);

  console.log('===== CAPTURED CONSOLE OUTPUT =====');
  for (const m of messages) console.log(m);
  console.log('===== END (' + messages.length + ' messages) =====');

  ws.close();
  chrome.kill('SIGKILL');
  process.exit(0);
}

main().catch(e => {
  console.error('FATAL: ' + e.stack);
  chrome.kill('SIGKILL');
  process.exit(1);
});

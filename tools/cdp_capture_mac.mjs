// Minimal CDP driver (no puppeteer/playwright dependency): launches headless
// Chrome with --remote-debugging-port, opens a target page via the HTTP
// /json/new endpoint, connects to its devtools websocket, enables
// Page+Runtime+Network+Log domains, navigates, and STREAMS
// Runtime.consoleAPICalled / Log.entryAdded / network-failure messages to
// stdout AS THEY ARRIVE (so a redirected log file can be polled live instead
// of only appearing after the full wait elapses), then also prints the full
// buffered transcript at the end for convenience. Node 22+ has a built-in
// global WebSocket + fetch, so no npm deps needed.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
const SHOT_DIR = process.argv[5] || '/tmp/cdp-shots';
try { fs.mkdirSync(SHOT_DIR, { recursive: true }); } catch {}

const CHROME = process.argv[2];
const URL = process.argv[3];
const WAIT_MS = parseInt(process.argv[4] || '6000', 10);
const CDP_PORT = 9333;

// macOS-tuned: HEADED (real Metal GPU + user sees the window), WebGPU via Metal.
const HEADLESS = !!process.env.HEADLESS;
const args = [
  ...(HEADLESS ? [`--headless=new`] : []),
  `--remote-debugging-port=${CDP_PORT}`,
  `--enable-unsafe-webgpu`,
  `--ignore-gpu-blocklist`,
  `--use-angle=metal`,
  `--user-data-dir=/tmp/chrome-cdp-profile-${Date.now()}`,
  `about:blank`,
];
const finalArgs = args;

function ts() {
  return new Date().toISOString().split('T')[1].replace('Z', '');
}

function emit(line) {
  // Timestamped + flushed immediately so a tailing/polling reader sees
  // progress in real time, not just at process exit.
  process.stdout.write(`[${ts()}] ${line}\n`);
}

console.error('Launching: ' + CHROME + ' ' + finalArgs.join(' '));
const chrome = spawn(CHROME, finalArgs, { stdio: ['ignore', 'pipe', 'pipe'], env: process.env });
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
  let msgId = 1;
  const pending = new Map();
  const requestUrls = new Map(); // requestId -> url, for correlating failures

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
    switch (msg.method) {
      case 'Runtime.consoleAPICalled': {
        const text = (msg.params.args || []).map(a => a.value !== undefined ? a.value : (a.description || '')).join(' ');
        emit(`[console.${msg.params.type}] ${text}`);
        break;
      }
      case 'Runtime.exceptionThrown': {
        emit(`[exception] ${JSON.stringify(msg.params.exceptionDetails)}`);
        break;
      }
      case 'Log.entryAdded': {
        const e = msg.params.entry;
        emit(`[log.${e.level}] ${e.text}${e.url ? ' url=' + e.url : ''}`);
        break;
      }
      case 'Network.requestWillBeSent': {
        requestUrls.set(msg.params.requestId, msg.params.request.url); if(process.env.REQEMIT) emit('[req] '+msg.params.request.url);
        break;
      }
      case 'Network.responseReceived': {
        const st = msg.params.response.status;
        if (st >= 400) {
          emit(`[network.FAIL ${st}] ${msg.params.response.url}`);
        }
        break;
      }
      case 'Network.loadingFailed': {
        const url = requestUrls.get(msg.params.requestId) || '(unknown url)';
        emit(`[network.loadingFailed] ${url} :: ${msg.params.errorText}${msg.params.canceled ? ' (canceled)' : ''}`);
        break;
      }
    }
  });

  await send('Runtime.enable');
  await send('Log.enable');
  await send('Network.enable');
  await send('Page.enable');
  emit(`--- navigating to ${URL} ---`);
  await send('Page.navigate', { url: URL });

  // Periodic screenshots so the last pre-crash frame (the DOM log overlay,
  // which captures worker-thread engine logs the CDP console misses) is saved.
  const shotEvery = 2500;
  let shots = 0;
  for (let elapsed = 0; elapsed < WAIT_MS; elapsed += shotEvery) {
    await sleep(shotEvery);
    try {
      const shot = await send('Page.captureScreenshot', { format: 'png' });
      if (shot && shot.data) {
        const p = `${SHOT_DIR}/shot_${String(++shots).padStart(2, '0')}.png`;
        fs.writeFileSync(p, Buffer.from(shot.data, 'base64'));
        emit(`[screenshot] ${p}`);
      }
    } catch (e) { emit(`[screenshot FAILED — page may have crashed] ${e.message}`); }
  }

  emit('--- wait elapsed, exiting ---');

  ws.close();
  chrome.kill('SIGKILL');
  process.exit(0);
}

main().catch(e => {
  console.error('FATAL: ' + e.stack);
  chrome.kill('SIGKILL');
  process.exit(1);
});

import http from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { WebSocketServer } from 'ws';
import { SerialPort } from 'serialport';
import { ReadlineParser } from '@serialport/parser-readline';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = process.env.PORT || 8080;

const SAMPLES_DIR = process.env.SAMPLES_DIR
  || path.join(__dirname, '..', 'firmware', 'mushtalk_multi_v1', 'data');
const WEB_PAGE = path.join(__dirname, '..', 'web', 'index.html');

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');

  if (url.pathname === '/samples') {
    const files = fs.existsSync(SAMPLES_DIR)
      ? fs.readdirSync(SAMPLES_DIR).filter(f => f.endsWith('.wav'))
      : [];
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(files));
    return;
  }

  if (url.pathname.startsWith('/samples/')) {
    const name = path.basename(decodeURIComponent(url.pathname));
    const full = path.join(SAMPLES_DIR, name);
    if (!full.startsWith(SAMPLES_DIR) || !fs.existsSync(full)) { res.writeHead(404); res.end('no'); return; }
    res.writeHead(200, { 'Content-Type': 'audio/wav' });
    fs.createReadStream(full).pipe(res);
    return;
  }

  fs.readFile(WEB_PAGE, (err, data) => {
    if (err) { res.writeHead(404); res.end('web/index.html not found'); return; }
    res.writeHead(200, { 'Content-Type': 'text/html' });
    res.end(data);
  });
});

const wss = new WebSocketServer({ server });
let nextId = 1;

const serialState = { connected: false, path: null, error: 'looking for ESP32…' };
let serialPort = null;
let serialOpening = false;

function broadcast(fromWs, obj) {
  const msg = JSON.stringify(obj);
  for (const ws of wss.clients) if (ws !== fromWs && ws.readyState === ws.OPEN) ws.send(msg);
}

function broadcastAll(obj) {
  const msg = JSON.stringify(obj);
  for (const ws of wss.clients) if (ws.readyState === ws.OPEN) ws.send(msg);
}

function serialInfo() {
  return { type: 'serial', ...serialState };
}

function setSerialState(next) {
  const same = serialState.connected === next.connected
    && serialState.path === (next.path ?? serialState.path)
    && serialState.error === (next.error ?? serialState.error);
  Object.assign(serialState, next);
  if (!same) broadcastAll(serialInfo());
}

function sendEsp(line) {
  if (!serialPort?.isOpen) {
    console.log(`[esp] not connected — would send: ${line}`);
    return false;
  }
  serialPort.write(line.replace(/\s+$/, '') + '\n');
  return true;
}

function playToEsp(m) {
  const raw = (m.file || '').toString();
  const cmd = raw.replace(/^.*\//, '').replace(/\.wav$/i, '') || String(m.slot ?? '');
  if (!cmd) return;
  const ok = sendEsp(cmd);
  console.log(`[play] ${cmd}${ok ? '' : ' (no board)'}`);
}

function soundToEsp(on) {
  const ok = sendEsp(on ? 'on' : 'off');
  console.log(`[sound] ${on ? 'ON' : 'OFF'}${ok ? '' : ' (no board)'}`);
}

function toCalloutPath(p) {
  if (process.platform === 'darwin' && p.includes('/tty.')) return p.replace('/tty.', '/cu.');
  return p;
}

async function findEspPath() {
  if (process.env.SERIAL_PATH) return toCalloutPath(process.env.SERIAL_PATH);
  const ports = await SerialPort.list();
  const seen = new Set();
  for (const p of ports) {
    const espPath = toCalloutPath(p.path);
    if (seen.has(espPath)) continue;
    const blob = `${espPath} ${p.manufacturer || ''} ${p.friendlyName || ''} ${p.serialNumber || ''}`;
    const match = /cu\.usbmodem|cu\.usbserial|cu\.wchusb|cu\.SLAB_USBtoUART|cu\.wch/i.test(espPath)
      || /espressif|silicon labs|wch|cp210|ch340|usb jtag|usb serial/i.test(blob);
    if (!match) continue;
    seen.add(espPath);
    return espPath;
  }
  return null;
}

function openSerial(espPath) {
  if (serialPort?.isOpen || serialOpening) return;
  serialOpening = true;
  console.log(`[esp] opening ${espPath}`);
  const port = new SerialPort({ path: espPath, baudRate: 115200 });
  serialPort = port;
  const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
  parser.on('data', (line) => {
    const t = line.trim();
    if (!t) return;
    console.log(`[esp] ${t}`);
    broadcastAll({ type: 'esp', line: t });
    // Old sketches don't know `stop`; `c` toggles auto-cycle off if it was on.
    if (/unknown: stop/i.test(t)) sendEsp('c');
  });
  port.on('open', () => {
    serialOpening = false;
    setSerialState({ connected: true, path: espPath, error: null });
    console.log(`[esp] connected ${espPath}`);
    setTimeout(() => sendEsp('stop'), 1800);
  });
  port.on('close', () => {
    serialOpening = false;
    if (serialPort === port) serialPort = null;
    setSerialState({ connected: false, path: null, error: 'disconnected' });
    console.log('[esp] disconnected');
  });
  port.on('error', (err) => {
    serialOpening = false;
    const busy = /busy|in use|access denied|cannot open/i.test(err.message);
    const error = busy
      ? 'USB port busy — close Arduino Serial Monitor'
      : err.message;
    if (serialPort === port) serialPort = null;
    setSerialState({ connected: false, path: espPath, error });
    console.log(`[esp] ${error}`);
  });
}

async function pollSerial() {
  try {
    if (!serialPort?.isOpen && !serialOpening) {
      const espPath = await findEspPath();
      if (espPath) openSerial(espPath);
      else setSerialState({ connected: false, path: null, error: 'plug in the ESP32' });
    }
  } catch (err) {
    setSerialState({ connected: false, error: err.message });
  }
  setTimeout(pollSerial, serialPort?.isOpen ? 4000 : 3000);
}

wss.on('connection', (ws) => {
  const name = `ctrl-${nextId++}`;
  ws.send(JSON.stringify({ type: 'welcome', name, tServer: Date.now(), serial: serialState }));
  broadcast(ws, { type: 'peer', event: 'join', name });
  console.log(`[+] ${name} connected (${wss.clients.size} total)`);

  ws.on('message', (raw) => {
    let m; try { m = JSON.parse(raw.toString()); } catch { return; }
    if (m.type === 'ping') { ws.send(JSON.stringify({ type: 'pong', t0: m.t0, tServer: Date.now() })); return; }
    m.from = name;
    if (m.type === 'play') playToEsp(m);
    if (m.type === 'sound') soundToEsp(!!m.on);
    broadcast(ws, m);
  });

  ws.on('close', () => { broadcast(ws, { type: 'peer', event: 'leave', name }); console.log(`[-] ${name} left`); });
});

server.listen(PORT, () => {
  console.log(`conductor on http://localhost:${PORT}  (samples: ${SAMPLES_DIR})`);
  console.log('web page is a remote — audio plays on the ESP32 speaker via USB serial');
  pollSerial();
});

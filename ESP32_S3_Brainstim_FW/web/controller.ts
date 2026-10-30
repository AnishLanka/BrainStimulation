import { type BleAdapter, NusConnection, WebBluetoothAdapter } from './ble_transport';
import { MockBleAdapter } from './mock_adapter';
import { type DdsStatus, parseStatusLine, stateRank, channelLabel } from './dds_status';

declare global { interface Window { ddsNativeAdapter?: () => BleAdapter; } }

const FREQ_MIN = 500;
const FREQ_MAX = 30000;
const AMP_MAX  = 5.00;

// Safety slew limits: output never jumps to a new setpoint, it walks there at these rates.
const AMP_SLEW_MA_PER_S   = 0.5;
const FREQ_SLEW_OCT_PER_S = 1.0;
const SLEW_TICK_MS        = 100;

const LOG_MIN  = Math.log(FREQ_MIN);
const LOG_SPAN = Math.log(FREQ_MAX) - LOG_MIN;
const sliderToFreq = (s: number) => Math.round(Math.exp(LOG_MIN + LOG_SPAN * s / 1000));
const freqToSlider = (f: number) => Math.round(1000 * (Math.log(f) - LOG_MIN) / LOG_SPAN);
const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));
const r2    = (v: number) => Math.round(v * 100) / 100;

// ── Demo / adapter ─────────────────────────────────────────────────────────
// Native app injects its BLE adapter; browsers fall back to Web Bluetooth.
const hasBluetooth = 'bluetooth' in (navigator as object);
const realAdapter  = window.ddsNativeAdapter ?? (hasBluetooth ? () => new WebBluetoothAdapter() : null);
const forceDemo    = new URLSearchParams(location.search).has('demo') || !realAdapter;
const readPref     = (k: string) => { try { return localStorage.getItem(k); } catch { return null; } };
const writePref    = (k: string, v: string) => { try { localStorage.setItem(k, v); } catch { /* storage unavailable */ } };
const simPref = readPref('research-lab-sim');
let useDemo = forceDemo || (simPref === null ? !window.ddsNativeAdapter : simPref === '1');

const conn = new NusConnection(() => useDemo || !realAdapter ? new MockBleAdapter() : realAdapter());

// ── Mode toggle ────────────────────────────────────────────────────────────
const modeOpts = document.querySelectorAll<HTMLButtonElement>('.mode-opt');
function setMode(mode: string): void {
  const view = mode === 'dev' ? 'dev' : 'student';
  document.body.dataset.mode = view;
  modeOpts.forEach(b => { const active = b.dataset.mode === view; b.classList.toggle('active', active); b.setAttribute('aria-pressed', String(active)); });
  writePref('r2d2-mode', view);
}
setMode(readPref('r2d2-mode') ?? 'student');
modeOpts.forEach(btn => btn.addEventListener('click', () => { setMode(btn.dataset.mode ?? 'student'); renderStudentOverview(); }));

// ── Simulated-device switch (dev mode) ─────────────────────────────────────
const demoChip  = document.getElementById('demo-chip')!;
const simToggle = document.getElementById('sim-toggle') as HTMLInputElement;
function renderDemo(): void {
  demoChip.style.display = useDemo ? 'inline-flex' : 'none';
  simToggle.checked = useDemo;
}
simToggle.addEventListener('change', () => {
  useDemo = simToggle.checked;
  writePref('research-lab-sim', useDemo ? '1' : '0');
  renderDemo();
  setStatus('disconnected', useDemo ? 'Demo disconnected' : 'Disconnected');
  setConnected(false);
  log('INFO', useDemo ? 'Simulated device selected.' : 'Real device selected.');
});
renderDemo();

// ── DOM refs ───────────────────────────────────────────────────────────────
const dot               = document.getElementById('status-dot')!;
const statusText        = document.getElementById('status-text')!;
const btnConnect        = document.getElementById('btn-connect')      as HTMLButtonElement;
const btnDisconn        = document.getElementById('btn-disconnect')   as HTMLButtonElement;
const btnClear          = document.getElementById('btn-clear')        as HTMLButtonElement;
const btnSend           = document.getElementById('btn-send')         as HTMLButtonElement;
const msgInput          = document.getElementById('msg-input')        as HTMLInputElement;
const logEl             = document.getElementById('log')!;
const deviceDetails     = document.getElementById('device-details');
const infoName          = document.getElementById('info-name')!;
const infoId            = document.getElementById('info-id')!;
const infoMtu           = document.getElementById('info-mtu')!;
const channelTabs       = document.querySelectorAll<HTMLButtonElement>('.ch-tab');
const freqSlider        = document.getElementById('freq-slider')      as HTMLInputElement;
const freqNumber        = document.getElementById('freq-number')      as HTMLInputElement;
const freqOut           = document.getElementById('freq-out')!;
const ampSlider         = document.getElementById('amp-slider')       as HTMLInputElement;
const ampNumber         = document.getElementById('amp-number')       as HTMLInputElement;
const ampOut            = document.getElementById('amp-out')!;
const waveBtns          = document.querySelectorAll<HTMLButtonElement>('.wave-btn');
const freqPresets       = document.querySelectorAll<HTMLButtonElement>('.freq-preset');
const ampPresets        = document.querySelectorAll<HTMLButtonElement>('.amp-preset');
const btnDacOn          = document.getElementById('btn-dac-on')       as HTMLButtonElement;
const btnDacOff         = document.getElementById('btn-dac-off')      as HTMLButtonElement;
const btnDacStatus      = document.getElementById('btn-dac-status')   as HTMLButtonElement;
const dacStatusLine     = document.getElementById('dac-status-line')!;
const waveCanvas        = document.getElementById('wave-canvas')      as HTMLCanvasElement;
const waveInfo          = document.getElementById('wave-info')!;
const channelStatusList = document.getElementById('channel-status-list')!;
const studentChannel = document.getElementById('student-channel')!;
const studentState = document.getElementById('student-state')!;
const studentFrequency = document.getElementById('student-frequency')!;
const studentRequested = document.getElementById('student-requested')!;
const studentApplied = document.getElementById('student-applied')!;
const studentNotice = document.getElementById('student-notice')!;
const devLink = document.getElementById('dev-link')!;
const devStatusAge = document.getElementById('dev-status-age')!;
const devWriteMs = document.getElementById('dev-write-ms')!;
const devRxFrames = document.getElementById('dev-rx-frames')!;
const ctx               = waveCanvas.getContext('2d')!;

// ── Control state ──────────────────────────────────────────────────────────
let currentChannel = 'ALL';
let currentWave    = 'SINE';
let setFreq = 1000, setAmp = 0;   // setpoints: where the user put the controls
let outFreq = 1000, outAmp = 0;   // last values actually sent to the device

const chanData       = new Map<string, DdsStatus>();
const lastStatusLine = new Map<string, string>();
const statusSeen = new Map<string, number>();
let lastWriteMs: number | null = null;
let rxFrames = 0;
let pendingSync = false;           // adopt device values on next status for the selection

function selectedChans(): string[] {
  return currentChannel === 'ALL' ? ['0', '1', '2', '3'] : [currentChannel];
}

function selectedOutput(): { state: string; current: number } {
  let best = { state: 'OFF', current: 0 };
  for (const c of selectedChans()) {
    const d = freshChannel(c);
    if (d && stateRank(d.state) > stateRank(best.state)) best = { state: d.state, current: d.current };
  }
  return best;
}

// ── Scope ──────────────────────────────────────────────────────────────────
function freshChannel(ch: string): DdsStatus | undefined {
  const seen = statusSeen.get(ch);
  return seen !== undefined && performance.now() - seen < 3500 ? chanData.get(ch) : undefined;
}

function miniWavePath(wave: string, amp: number): string {
  const half = 19 * clamp(amp / AMP_MAX, 0, 1);
  const points: string[] = [];
  for (let i = 0; i <= 64; i++) {
    const phase = (i / 64 * 2) % 1;
    const v = wave === 'SQUARE' ? (phase < 0.5 ? 1 : -1)
      : wave === 'TRIANGLE' ? (phase < 0.5 ? 4 * phase - 1 : 3 - 4 * phase)
      : wave === 'SAWTOOTH' ? 1 - 2 * phase
      : Math.sin(phase * 2 * Math.PI);
    points.push(`${i === 0 ? 'M' : 'L'}${(i * 2.5).toFixed(1)} ${(24 - half * v).toFixed(1)}`);
  }
  return points.join(' ');
}

function renderChannelStrips(): void {
  for (let i = 0; i < 4; i++) {
    const ch = String(i);
    const tab = document.querySelector<HTMLButtonElement>(`.ch-strip[data-ch="${ch}"]`)!;
    const d = freshChannel(ch);
    const state = !conn.connected ? 'OFFLINE' : d?.fault ? 'FAULT' : d?.state
      ?? (chanData.has(ch) ? 'STALE' : 'WAITING');
    const selected = currentChannel === 'ALL' || currentChannel === ch;
    const freq = selected ? setFreq : d?.freq;
    const amp = selected ? setAmp : d?.amp;
    tab.querySelector('.ch-name')!.textContent = document.body.dataset.mode === 'dev' ? `CH ${i}` : `CH ${String(i + 1).padStart(2, '0')}`;
    tab.dataset.state = state;
    document.getElementById(`strip-state-${ch}`)!.textContent = state;
    document.getElementById(`strip-freq-${ch}`)!.textContent = freq === undefined ? '--' : `${freq} Hz`;
    document.getElementById(`strip-set-${ch}`)!.textContent = amp === undefined ? '--' : `${amp.toFixed(2)} mA`;
    document.getElementById(`strip-ramp-${ch}`)!.textContent = d ? `${d.current.toFixed(3)} mA` : '--';
    document.getElementById(`strip-wave-${ch}`)!.setAttribute('d', miniWavePath(selected ? currentWave : d?.wave ?? 'SINE', amp ?? 0));
    tab.setAttribute('aria-label', `Channel ${i + 1}: ${state}, setpoint ${amp?.toFixed(2) ?? '--'} milliamps`);
  }
}

function renderTelemetry(): void {
  devLink.textContent = `${useDemo ? 'SIM' : 'BLE'} / ${conn.connected ? 'ONLINE' : 'OFFLINE'}`;
  const ages = [...statusSeen.values()].map(t => performance.now() - t);
  const age = ages.length ? Math.min(...ages) : null;
  devStatusAge.textContent = !conn.connected ? '--' : age === null ? 'WAITING' : `${(age / 1000).toFixed(1)} s`;
  devStatusAge.classList.toggle('stale', conn.connected && age !== null && age >= 3500);
  devWriteMs.textContent = lastWriteMs === null ? '--' : `${lastWriteMs.toFixed(1)} ms`;
  devRxFrames.textContent = String(rxFrames).padStart(4, '0');
}

async function writeCommand(text: string): Promise<void> {
  const start = performance.now();
  await conn.send(text);
  lastWriteMs = performance.now() - start;
  renderTelemetry();
}
function announceStudent(message: string, error = false): void {
  studentNotice.textContent = message;
  studentNotice.classList.toggle('error', error);
}

function renderStudentOverview(): void {
  studentChannel.textContent = currentChannel === 'ALL' ? 'All channels' : `Channel ${Number(currentChannel) + 1}`;
  studentFrequency.textContent = `${setFreq.toLocaleString()} Hz`;
  studentRequested.textContent = `${setAmp.toFixed(2)} mA`;
  const selected = selectedChans().map(freshChannel).filter((d): d is DdsStatus => Boolean(d));
  const valid = selected.filter(d => d.state !== 'N/A' && d.state !== 'ERR');
  const fault = selected.some(d => d.fault);
  const ranked = valid.reduce<DdsStatus | undefined>((best, d) =>
    !best || stateRank(d.state) > stateRank(best.state) ? d : best, undefined);
  const state = fault ? 'Fault' : !conn.connected ? (useDemo ? 'Demo disconnected' : 'Disconnected')
    : !ranked ? 'Waiting for status'
    : ({
      OFF: 'Off', PRECHARGE: 'Preparing', CONNECT: 'Connecting output',
      RAMP_UP: 'Ramping up', ACTIVE: 'Active', RAMP_DOWN: 'Ramping down',
      DISCONNECT: 'Disconnecting'
    } as Record<string, string>)[ranked.state] ?? 'Status unknown';
  studentState.textContent = state;
  studentState.className = 'state-pill' + (fault ? ' fault'
    : ranked && stateRank(ranked.state) === 3 ? ' active'
    : ranked && stateRank(ranked.state) === 2 ? ' ramping' : '');
  studentApplied.textContent = valid.length ? `${Math.max(...valid.map(d => d.current)).toFixed(3)} mA` : '?';
  btnDacOn.classList.toggle('engaged', ranked?.state === 'ACTIVE' || ranked?.state === 'RAMP_UP');
  renderChannelStrips();
  renderTelemetry();
}

let cssW = 0, cssH = 0;
let phase = 0;
let scopeAmp = 0;
let lastFrame = 0;

function resizeCanvas(): void {
  const dpr = window.devicePixelRatio || 1;
  cssW = waveCanvas.offsetWidth;
  cssH = waveCanvas.offsetHeight;
  waveCanvas.width  = cssW * dpr;
  waveCanvas.height = cssH * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}
window.addEventListener('resize', resizeCanvas);

function drawFrame(dt: number): void {
  if (cssW === 0) return;
  phase = (phase + dt * 1.8) % 1;

  const out  = selectedOutput();
  const live = stateRank(out.state) > 0;
  // ACTIVE follows the slewed amplitude; ramps follow the device's reported current; OFF shows a preview.
  const target = out.state === 'ACTIVE' ? outAmp : live ? out.current : outAmp;
  scopeAmp += (target - scopeAmp) * (1 - Math.exp(-dt * 6));

  const w = cssW, h = cssH;
  ctx.fillStyle = '#0b1216';
  ctx.fillRect(0, 0, w, h);

  const cols = 8, rows = 4;
  ctx.setLineDash([]);
  ctx.lineWidth = 1;
  ctx.strokeStyle = 'rgba(102,217,228,0.10)';
  for (let c = 1; c < cols; c++) { ctx.beginPath(); ctx.moveTo(w * c / cols, 0); ctx.lineTo(w * c / cols, h); ctx.stroke(); }
  for (let r = 1; r < rows; r++) { ctx.beginPath(); ctx.moveTo(0, h * r / rows); ctx.lineTo(w, h * r / rows); ctx.stroke(); }
  ctx.strokeStyle = 'rgba(102,217,228,0.20)';
  ctx.beginPath(); ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2); ctx.stroke();
  ctx.strokeStyle = 'rgba(102,217,228,0.15)';
  const ticks = cols * 5;
  for (let i = 0; i <= ticks; i++) {
    const x = w * i / ticks, tH = i % 5 === 0 ? 7 : 3;
    ctx.beginPath(); ctx.moveTo(x, h / 2 - tH); ctx.lineTo(x, h / 2 + tH); ctx.stroke();
  }

  const halfAmp = (h / 2 - 10) * (scopeAmp / AMP_MAX);
  const cycles  = 1.5 + 8.5 * (Math.log(outFreq) - LOG_MIN) / LOG_SPAN;

  if (live) {
    ctx.beginPath();
    ctx.strokeStyle = 'rgba(102,217,228,0.16)';
    ctx.lineWidth = 6; ctx.lineJoin = 'round';
    traceWave(cycles, halfAmp, h / 2);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.strokeStyle = live ? '#66d9e4' : 'rgba(102,217,228,0.35)';
  ctx.lineWidth = live ? 1.8 : 1.4; ctx.lineJoin = 'round';
  if (!live) ctx.setLineDash([5, 4]);
  traceWave(cycles, halfAmp, h / 2);
  ctx.stroke();
  ctx.setLineDash([]);

  const chLabel = currentChannel === 'ALL' ? 'ALL' : `CH${currentChannel}`;
  waveInfo.textContent =
    `${chLabel}  ${currentWave}  ${outFreq} Hz  ${scopeAmp.toFixed(2)} mA  ${live ? out.state : 'OFF · PREVIEW'}`;
}

function traceWave(cycles: number, halfAmp: number, cy: number): void {
  for (let i = 0; i <= cssW; i++) {
    const p = ((i / cssW) * cycles + phase) % 1;
    let y: number;
    switch (currentWave) {
      case 'SINE':
        y = cy - halfAmp * Math.sin(p * 2 * Math.PI);
        break;
      case 'SQUARE': {
        const e = 0.01;
        if      (p < 0.5 - e) y = cy - halfAmp;
        else if (p < 0.5 + e) y = cy - halfAmp * (1 - (p - (0.5 - e)) / e);
        else if (p < 1 - e)   y = cy + halfAmp;
        else                  y = cy + halfAmp * (1 - (p - (1 - e)) / e);
        break;
      }
      case 'TRIANGLE':
        y = cy - halfAmp * (p < 0.5 ? 4 * p - 1 : 3 - 4 * p);
        break;
      case 'SAWTOOTH':
        y = p < 0.98
          ? cy - halfAmp * (1 - 2 * p / 0.98)
          : cy - halfAmp * (-1 + (p - 0.98) / 0.02 * 2);
        break;
      default:
        y = cy;
    }
    i === 0 ? ctx.moveTo(i, y) : ctx.lineTo(i, y);
  }
}

function loop(ts: number): void {
  const dt = lastFrame === 0 ? 0 : Math.min((ts - lastFrame) / 1000, 0.05);
  lastFrame = ts;
  drawFrame(dt);
  requestAnimationFrame(loop);
}
resizeCanvas();
requestAnimationFrame(loop);

// ── Logging ────────────────────────────────────────────────────────────────
function escHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function log(tag: string, msg: string, bytes?: Uint8Array): void {
  const ts = new Date().toLocaleTimeString('en', { hour12: false });
  const entry = document.createElement('div');
  const hex = bytes ? Array.from(bytes.slice(0, 12), b => b.toString(16).padStart(2, '0')).join(' ').toUpperCase()
    + (bytes.length > 12 ? ' ...' : '') : '';
  entry.className = 'log-entry';
  entry.innerHTML =
    `<span class="log-ts">${ts}</span>` +
    `<span class="log-tag ${tag.toLowerCase()}">[${tag}]</span>` +
    `<span class="log-msg">${escHtml(msg)}</span>` +
    `<span class="log-hex">${hex}</span>`;
  logEl.appendChild(entry);
  if (logEl.children.length > 500) logEl.firstElementChild?.remove();
  logEl.scrollTop = logEl.scrollHeight;
}

const errMsg = (err: unknown) => err instanceof Error ? err.message : String(err);

// ── Connection ─────────────────────────────────────────────────────────────
function setStatus(state: string, text: string): void {
  dot.className          = state;
  statusText.textContent = text;
}

function setConnected(isConn: boolean): void {
  announceStudent(isConn
    ? (useDemo ? 'Connected to the demo. Choose a channel and review its status.' : 'Connected. Choose a channel and review its status.')
    : (useDemo ? 'Demo disconnected. Output status is unavailable.' : 'Disconnected. Output status is unavailable.'));
  btnConnect.textContent = useDemo ? 'Connect to demo' : 'Connect to device';
  btnConnect.disabled = isConn;
  btnDisconn.disabled = !isConn;
  btnSend.disabled    = !isConn;
  msgInput.disabled   = !isConn;
  simToggle.disabled  = isConn || forceDemo; // adapter is chosen at connect time
  if (deviceDetails) deviceDetails.style.display = isConn ? '' : 'none';
  if (!isConn) { infoName.textContent = useDemo ? 'Simulated device - no hardware' : 'Bluetooth device'; infoId.textContent = '—'; infoMtu.textContent = '—'; }
  setDacPanelEnabled(isConn);
}

btnConnect.addEventListener('click', async () => {
  try {
    setStatus('connecting', useDemo ? 'Connecting to demo…' : 'Scanning for device…');
    log('INFO', useDemo ? 'Demo — simulating device…' : 'Scanning for device…');
    const dev = await conn.connect(onValue, onDisconnected);
    setStatus('connected', useDemo ? 'Demo connected' : 'Connected');
    setConnected(true);
    infoName.textContent = dev.name;
    infoId.textContent   = dev.id;
    infoMtu.textContent  = dev.mtu;
    log('INFO', `Connected to ${dev.name}  ·  MTU ${dev.mtu}`);
  } catch (err: unknown) {
    setStatus('disconnected', 'Error');
    log('ERR', errMsg(err));
    announceStudent('Connection failed: ' + errMsg(err), true);
  }
});

btnDisconn.addEventListener('click', async () => {
  log('INFO', 'Disconnecting…');
  await conn.disconnect();
});

function onDisconnected(): void {
  setStatus('disconnected', useDemo ? 'Demo disconnected' : 'Disconnected');
  log('INFO', 'Device disconnected.');
  setConnected(false);
}

// ── Receive ────────────────────────────────────────────────────────────────
function onValue(dv: DataView): void {
  const value = new TextDecoder().decode(dv);
  const bytes = new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength);
  rxFrames++;
  renderTelemetry();
  const sm = value.match(/^DDS S(\d) /);
  if (sm) {
    // Status is polled constantly; only log a channel's line when it changes.
    if (lastStatusLine.get(sm[1]) !== value) { lastStatusLine.set(sm[1], value); log('DDS', value, bytes); }
    parseDdsStatus(value);
    return;
  }
  if (value.startsWith('DDS ERR')) announceStudent('Device rejected command: ' + value.slice(8), true);
  else if (value.startsWith('DDS OK')) announceStudent('Command accepted. Waiting for output status.');
  log(value.startsWith('HB#') ? 'HB' : value.startsWith('DDS ERR') ? 'ERR' : value.startsWith('DDS OK') ? 'ACK' : value.startsWith('DDS ') ? 'DDS' : 'RX', value, bytes);
}

// ── Send ───────────────────────────────────────────────────────────────────
async function sendCmd(text: string): Promise<void> {
  try { await writeCommand(text); log('TX', text, new TextEncoder().encode(text)); }
  catch (err: unknown) { log('ERR', errMsg(err)); announceStudent('Could not send command: ' + errMsg(err), true); }
}

async function sendMessage(): Promise<void> {
  const text = msgInput.value.trim();
  if (!text) return;
  await sendCmd(text);
  msgInput.value = '';
}
btnSend.addEventListener('click', () => void sendMessage());
msgInput.addEventListener('keydown', e => { if (e.key === 'Enter') void sendMessage(); });
btnClear.addEventListener('click', () => { logEl.innerHTML = ''; });

function ddsPrefix(): string { return `DDS ${currentChannel} `; }

// ── Controls ↔ state ───────────────────────────────────────────────────────
function syncControls(): void {
  freqSlider.value = String(freqToSlider(setFreq));
  freqNumber.value = String(setFreq);
  ampSlider.value  = String(setAmp);
  ampNumber.value  = setAmp.toFixed(2);
  waveBtns.forEach(b => b.classList.toggle('active', b.dataset.wave === currentWave));
}

function renderOut(): void {
  freqOut.textContent = `OUT ${outFreq} Hz`;
  ampOut.textContent  = `OUT ${outAmp.toFixed(2)} mA`;
  freqOut.classList.toggle('slewing', outFreq !== setFreq);
  ampOut.classList.toggle('slewing',  outAmp  !== setAmp);
  renderStudentOverview();
}

function adoptChannel(ch: string): void {
  if (ch === 'ALL') {
    const known = [...chanData.values()].filter(d => d.state !== 'N/A' && d.state !== 'ERR');
    if (known.length) {
      // Take the lowest amplitude so the first ALL step can only lower a channel, never jump one up.
      outAmp  = setAmp  = r2(Math.min(...known.map(d => d.amp)));
      outFreq = setFreq = chanData.get('0')?.freq ?? outFreq;
    }
  } else {
    const d = chanData.get(ch);
    if (!d || d.state === 'N/A' || d.state === 'ERR') { pendingSync = true; return; }
    outFreq = setFreq = d.freq;
    outAmp  = setAmp  = r2(d.amp);
    currentWave = d.wave;
  }
  syncControls();
  renderOut();
}

// ── Slew limiter ───────────────────────────────────────────────────────────
let slewTimer: ReturnType<typeof setInterval> | null = null;
let slewBusy  = false;
let lastSlew  = 0;

const isSlewing = () => outFreq !== setFreq || outAmp !== setAmp;

function startSlew(): void {
  renderOut();
  if (slewTimer || !conn.connected || !isSlewing()) return;
  lastSlew  = performance.now();
  slewTimer = setInterval(() => void slewTick(), SLEW_TICK_MS);
  schedulePoll(250);
}

function stopSlew(reason?: string): void {
  if (slewTimer) { clearInterval(slewTimer); slewTimer = null; }
  if (reason && isSlewing()) log('INFO', `Ramp halted at ${outFreq} Hz / ${outAmp.toFixed(2)} mA (${reason})`);
  setFreq = outFreq;
  setAmp  = outAmp;
  syncControls();
  renderOut();
}

async function slewTick(): Promise<void> {
  if (slewBusy) return;
  const now = performance.now();
  const dt  = Math.min((now - lastSlew) / 1000, 0.3);
  lastSlew  = now;

  let nf = outFreq;
  if (nf !== setFreq) {
    const ratio = Math.pow(2, FREQ_SLEW_OCT_PER_S * dt);
    nf = setFreq > nf ? Math.min(setFreq, Math.round(nf * ratio)) : Math.max(setFreq, Math.round(nf / ratio));
    if (nf === outFreq) nf += setFreq > outFreq ? 1 : -1;
  }

  let na = outAmp;
  if (na !== setAmp) {
    const step = AMP_SLEW_MA_PER_S * dt;
    na = r2(setAmp > na ? Math.min(setAmp, na + step) : Math.max(setAmp, na - step));
    if (na === outAmp) na = r2(na + (setAmp > outAmp ? 0.01 : -0.01));
  }

  slewBusy = true;
  const prefix = ddsPrefix();
  try {
    if (nf !== outFreq) { await writeCommand(prefix + 'FREQ ' + nf); outFreq = nf; }
    if (na !== outAmp)  { await writeCommand(prefix + 'AMP ' + na.toFixed(2)); outAmp = na; }
  } catch (err: unknown) {
    log('ERR', errMsg(err));
    stopSlew('send failed');
  } finally {
    slewBusy = false;
  }
  renderOut();

  if (slewTimer && !isSlewing()) {
    clearInterval(slewTimer); slewTimer = null;
    log('INFO', `Reached ${outFreq} Hz / ${outAmp.toFixed(2)} mA on ${currentChannel}`);
  }
}

function applyFreq(f: number): void {
  setFreq = clamp(Math.round(f) || FREQ_MIN, FREQ_MIN, FREQ_MAX);
  freqNumber.value = String(setFreq);
  startSlew();
}

function applyAmp(a: number): void {
  setAmp = r2(clamp(a || 0, 0, AMP_MAX));
  ampNumber.value = setAmp.toFixed(2);
  startSlew();
}

// Slider drags update the setpoint live; the number boxes only commit on Enter/blur.
freqSlider.addEventListener('input', () => applyFreq(sliderToFreq(Number(freqSlider.value))));
freqNumber.addEventListener('change', () => {
  applyFreq(parseInt(freqNumber.value, 10));
  freqSlider.value = String(freqToSlider(setFreq));
});
freqPresets.forEach(b => b.addEventListener('click', () => {
  applyFreq(Number(b.dataset.freq));
  freqSlider.value = String(freqToSlider(setFreq));
}));

ampSlider.addEventListener('input', () => applyAmp(parseFloat(ampSlider.value)));
ampNumber.addEventListener('change', () => {
  applyAmp(parseFloat(ampNumber.value));
  ampSlider.value = String(setAmp);
});
ampPresets.forEach(b => b.addEventListener('click', () => {
  applyAmp(parseFloat(b.dataset.amp!));
  ampSlider.value = String(setAmp);
}));

for (const input of [freqNumber, ampNumber]) {
  input.addEventListener('keydown', e => { if (e.key === 'Enter') input.blur(); });
}

// ── Channel tabs ───────────────────────────────────────────────────────────
channelTabs.forEach(tab => {
  tab.addEventListener('click', () => {
    if (tab.dataset.ch === currentChannel) return;
    stopSlew('channel switched');
    channelTabs.forEach(t => t.classList.toggle('active', t === tab));
    currentChannel = tab.dataset.ch!;
    log('INFO', `Channel → ${currentChannel}`);
    adoptChannel(currentChannel);
    renderStudentOverview();
  });
});

// ── Wave / output ──────────────────────────────────────────────────────────
waveBtns.forEach(btn => {
  btn.addEventListener('click', () => {
    currentWave = btn.dataset.wave!;
    waveBtns.forEach(b => b.classList.toggle('active', b === btn));
    renderStudentOverview();
    void sendCmd(ddsPrefix() + 'WAVE ' + currentWave);
  });
});

btnDacOn.addEventListener('click', () => {
  announceStudent('Start requested. Waiting for device confirmation.');
  void sendCmd(ddsPrefix() + 'ON');
  schedulePoll(100);
});
btnDacOff.addEventListener('click', () => {
  announceStudent('Stop requested. Waiting for device confirmation.');
  void sendCmd(ddsPrefix() + 'OFF');
  schedulePoll(100);
});
btnDacStatus.addEventListener('click', () => void sendCmd(ddsPrefix() + 'STATUS'));

// ── Status polling ─────────────────────────────────────────────────────────
let pollTimer: ReturnType<typeof setTimeout> | null = null;

function schedulePoll(ms?: number): void {
  if (pollTimer) clearTimeout(pollTimer);
  const ramping = [...chanData.values()].some(d => stateRank(d.state) === 2);
  const delay   = ms ?? (ramping || slewTimer ? 250 : 1000);
  pollTimer = setTimeout(async () => {
    pollTimer = null;
    if (!conn.connected) return;
    try { await writeCommand('DDS ALL STATUS'); } catch { /* retried next poll */ }
    renderChannelStatuses();
    renderStudentOverview();
    schedulePoll();
  }, delay);
}

function stopPoll(): void {
  if (pollTimer) { clearTimeout(pollTimer); pollTimer = null; }
}

// ── Status parsing ─────────────────────────────────────────────────────────
function renderChannelStatuses(): void {
  channelStatusList.replaceChildren();
  for (let i = 0; i < 4; i++) {
    const d    = freshChannel(String(i));
    const stale = chanData.has(String(i)) && !d;
    const rank = d ? stateRank(d.state) : 0;
    const cell = document.createElement('div');
    cell.className = `ch-cell${stale ? ' stale' : d?.fault ? ' fault' : rank === 3 ? ' active' : rank === 2 ? ' ramping' : ''}`;
    cell.innerHTML =
      `<div class="ch-cell-id">CH ${i}</div>` +
      `<div class="ch-cell-dot"></div>` +
      `<div class="ch-cell-st">${stale ? 'STALE' : channelLabel(d)}</div>` +
      `<div class="ch-cell-hz">${d && d.state !== 'N/A' && d.state !== 'ERR' ? `${d.freq} Hz` : ''}</div>` +
      `<div class="ch-cell-current">${d && d.state !== 'N/A' && d.state !== 'ERR' ? `${d.current.toFixed(3)} mA` : ''}</div>`;
    channelStatusList.appendChild(cell);
  }
}

function finishSync(ch: string): void {
  if (!pendingSync) return;
  if (currentChannel === 'ALL' ? ch === '3' : ch === currentChannel) {
    pendingSync = false;
    adoptChannel(currentChannel);
  }
}

function parseDdsStatus(msg: string): void {
  const d = parseStatusLine(msg);
  if (!d) return;
  chanData.set(d.ch, d);
  statusSeen.set(d.ch, performance.now());
  renderChannelStatuses();
  renderStudentOverview();
  if (d.state !== 'N/A' && d.state !== 'ERR') {
    dacStatusLine.textContent =
      `CH${d.ch}  ${d.state}  ${d.current.toFixed(3)} mA  ${d.freq} Hz${d.fault ? '  FAULT' : ''}`;
  }
  // Sliders are setpoints: device status never moves them, except when adopting a channel's state.
  finishSync(d.ch);
}

function setDacPanelEnabled(on: boolean): void {
  if (on) {
    pendingSync = true;
    schedulePoll(0);
  } else {
    stopPoll();
    stopSlew();
    chanData.clear();
    statusSeen.clear();
    lastStatusLine.clear();
    renderChannelStatuses();
    dacStatusLine.textContent = '—';
  }
  channelTabs.forEach(t => { t.disabled = !on; });
  freqSlider.disabled = freqNumber.disabled = ampSlider.disabled = ampNumber.disabled = !on;
  waveBtns.forEach(b    => { b.disabled = !on; });
  freqPresets.forEach(b => { b.disabled = !on; });
  ampPresets.forEach(b  => { b.disabled = !on; });
  btnDacOn.disabled = btnDacOff.disabled = btnDacStatus.disabled = !on;
  renderStudentOverview();
}

// ── Init ───────────────────────────────────────────────────────────────────
setStatus('', useDemo ? 'Demo disconnected' : 'Disconnected');
setConnected(false);
log('INFO', !realAdapter
  ? 'No Bluetooth here (use Chrome/Edge or the app) — running the simulated device.'
  : useDemo ? 'Simulated device — click Connect to start.' : 'Ready. Click Connect to scan.');

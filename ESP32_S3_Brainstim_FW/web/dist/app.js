(() => {
  // ../web/ble_transport.ts
  var NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
  var NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
  var NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
  var NusConnection = class {
    constructor(adapterFactory, pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms))) {
      this.adapterFactory = adapterFactory;
      this.pause = pause;
    }
    adapterFactory;
    pause;
    session = null;
    busy = false;
    get connected() {
      var _a;
      return Boolean((_a = this.session) == null ? void 0 : _a.ready);
    }
    async connect(onValue2, onDisconnect) {
      if (this.busy || this.session) throw new Error("A connection is already active.");
      this.busy = true;
      const session = {
        adapter: this.adapterFactory(),
        ready: false,
        queue: Promise.resolve(),
        pending: 0,
        lost: () => {
        }
      };
      this.session = session;
      session.lost = () => {
        if (this.session !== session) return;
        this.session = null;
        const wasReady = session.ready;
        session.ready = false;
        if (wasReady) onDisconnect();
      };
      try {
        const device = await session.adapter.connect(
          (value) => {
            if (this.session === session) onValue2(value);
          },
          session.lost
        );
        if (this.session !== session) throw new Error("Disconnected during setup.");
        session.ready = true;
        return device;
      } catch (error) {
        session.lost();
        try {
          await session.adapter.disconnect();
        } catch {
        }
        throw error;
      } finally {
        this.busy = false;
      }
    }
    async disconnect() {
      const session = this.session;
      if (!session) return;
      this.busy = true;
      session.lost();
      try {
        await session.adapter.disconnect();
      } finally {
        this.busy = false;
      }
    }
    async send(text) {
      const session = this.session;
      if (!(session == null ? void 0 : session.ready)) throw new Error("Connect to a device first.");
      const bytes = new TextEncoder().encode(text);
      const max = Math.min(252, session.adapter.maxWriteBytes ?? 252);
      if (!bytes.length || bytes.length > max)
        throw new Error(`Command must fit in one BLE write (${max} bytes). Try an individual channel for long ALL commands.`);
      if (session.pending >= 16) throw new Error("Command queue full. Wait before sending more.");
      session.pending++;
      const job = session.queue.then(async () => {
        if (this.session !== session || !session.ready)
          throw new Error("Connection changed; command cancelled.");
        await session.adapter.write(new DataView(bytes.buffer));
        await this.pause(30);
      });
      session.queue = job.catch(() => {
      });
      try {
        await job;
      } finally {
        session.pending--;
      }
    }
  };
  var WebBluetoothAdapter = class {
    device = null;
    rx = null;
    tx = null;
    valueHook = null;
    disconnHook = null;
    async connect(onValue2, onDisconnect) {
      var _a;
      if (!((_a = globalThis.navigator) == null ? void 0 : _a.bluetooth))
        throw new Error("Use the installed iOS/Android app, or Chrome/Edge with Web Bluetooth.");
      this.device = await navigator.bluetooth.requestDevice({ filters: [{ services: [NUS_SERVICE] }] });
      this.disconnHook = onDisconnect;
      this.device.addEventListener("gattserverdisconnected", onDisconnect);
      const server = await this.device.gatt.connect();
      const service = await server.getPrimaryService(NUS_SERVICE);
      this.tx = await service.getCharacteristic(NUS_TX);
      this.rx = await service.getCharacteristic(NUS_RX);
      this.valueHook = (e) => {
        const ev = e;
        onValue2(ev.target.value);
      };
      this.tx.addEventListener("characteristicvaluechanged", this.valueHook);
      await this.tx.startNotifications();
      return { id: this.device.id, name: this.device.name ?? "(unknown)", mtu: "negotiated by browser" };
    }
    write(value) {
      return this.rx.writeValueWithResponse(value);
    }
    async disconnect() {
      var _a, _b;
      if (this.tx && this.valueHook) this.tx.removeEventListener("characteristicvaluechanged", this.valueHook);
      if (this.device && this.disconnHook)
        this.device.removeEventListener("gattserverdisconnected", this.disconnHook);
      if ((_b = (_a = this.device) == null ? void 0 : _a.gatt) == null ? void 0 : _b.connected) this.device.gatt.disconnect();
    }
  };

  // ../web/mock_adapter.ts
  var delay = (ms) => new Promise((r) => setTimeout(r, ms));
  var clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  var easeInOut = (t) => t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
  function makeChan() {
    return { freq: 1e3, wave: "SINE", amp: 0, state: "OFF", rampStart: 0, rampDuration: 0, rampFrom: 0 };
  }
  var MockBleAdapter = class {
    maxWriteBytes = 244;
    notify;
    hbTimer;
    hbSeq = 0;
    chans = Array.from({ length: 4 }, makeChan);
    async connect(onValue2, _onDisconnect) {
      await delay(850);
      this.notify = onValue2;
      this.hbTimer = setInterval(() => this.emit(`HB#${this.hbSeq++} VBAT=3.85V CHG=0`), 2e3);
      return { id: "research-lab-demo", name: "Research Lab demo", mtu: "247" };
    }
    async write(value) {
      await delay(10 + Math.random() * 15);
      this.handleCmd(new TextDecoder().decode(value));
    }
    async disconnect() {
      clearInterval(this.hbTimer);
      this.hbTimer = void 0;
    }
    emit(text) {
      var _a;
      const b = new TextEncoder().encode(text);
      (_a = this.notify) == null ? void 0 : _a.call(this, new DataView(b.buffer));
    }
    progress(c) {
      return c.rampDuration > 0 ? clamp((Date.now() - c.rampStart) / c.rampDuration, 0, 1) : 1;
    }
    resolveState(c) {
      if ((c.state === "RAMP_UP" || c.state === "RAMP_DOWN") && this.progress(c) >= 1) {
        c.state = c.state === "RAMP_UP" ? "ACTIVE" : "OFF";
      }
      return c.state;
    }
    current(c) {
      switch (this.resolveState(c)) {
        case "ACTIVE":
          return c.amp;
        case "RAMP_UP":
          return c.rampFrom + (c.amp - c.rampFrom) * easeInOut(this.progress(c));
        case "RAMP_DOWN":
          return c.rampFrom * (1 - easeInOut(this.progress(c)));
        default:
          return 0;
      }
    }
    startRamp(c, state, ms) {
      c.rampFrom = this.current(c);
      c.rampStart = Date.now();
      c.rampDuration = ms;
      c.state = state;
    }
    handleCmd(text) {
      const m = text.match(/^DDS (ALL|\d) (\w+)(?: (.+))?$/);
      if (!m) return;
      const [, ch, cmd, arg = ""] = m;
      const idx = ch === "ALL" ? [0, 1, 2, 3] : [Number(ch)];
      for (const i of idx) {
        const s = this.chans[i];
        if (!s) continue;
        const st = this.resolveState(s);
        switch (cmd) {
          case "FREQ":
            s.freq = clamp(parseInt(arg) || 1e3, 500, 3e4);
            break;
          case "WAVE":
            if (["SINE", "SQUARE", "TRIANGLE", "SAWTOOTH"].includes(arg)) s.wave = arg;
            break;
          case "AMP":
            s.amp = clamp(parseFloat(arg) || 0, 0, 5);
            break;
          case "ON":
            if (st === "OFF" || st === "RAMP_DOWN") this.startRamp(s, "RAMP_UP", 1400);
            break;
          case "OFF":
            if (st === "ACTIVE" || st === "RAMP_UP") this.startRamp(s, "RAMP_DOWN", 600);
            break;
          case "STATUS":
            setTimeout(() => {
              const sc = this.chans[i];
              const cur = this.current(sc);
              const out = sc.state === "OFF" ? 0 : 1;
              this.emit(
                `DDS S${i} FREQ=${sc.freq} WAVE=${sc.wave} AMP=${sc.amp.toFixed(2)} STATE=${sc.state} CURRENT=${cur.toFixed(3)} OUT=${out} DUMMY=0`
              );
            }, 15 + Math.random() * 20);
            break;
        }
      }
    }
  };

  // ../web/dds_status.ts
  var STATUS_RE = /^DDS S([0-3]) FREQ=([\d.]+) WAVE=(\w+) AMP=([\d.]+) STATE=(\w+) CURRENT=([\d.]+) OUT=([01]) DUMMY=([01])( FAULT)?$/;
  function parseStatusLine(line) {
    const miss = line.match(/^DDS S([0-3]) (NOT_PRESENT|ERR.*)$/);
    if (miss) {
      const absent = miss[2] === "NOT_PRESENT";
      return {
        ch: miss[1],
        freq: 0,
        amp: 0,
        wave: "SINE",
        state: absent ? "N/A" : "ERR",
        current: 0,
        out: false,
        fault: !absent
      };
    }
    const m = line.match(STATUS_RE);
    if (!m) return null;
    return {
      ch: m[1],
      freq: Number(m[2]),
      wave: m[3],
      amp: Number(m[4]),
      state: m[5],
      current: Number(m[6]),
      out: m[7] === "1",
      fault: Boolean(m[9])
    };
  }
  var TRANSITION_STATES = ["PRECHARGE", "CONNECT", "RAMP_UP", "RAMP_DOWN", "DISCONNECT"];
  function stateRank(state) {
    if (state === "ACTIVE") return 3;
    return TRANSITION_STATES.includes(state) ? 2 : 0;
  }
  function channelLabel(d) {
    if (!d) return "\u2014";
    if (d.fault) return "FAULT";
    if (d.state === "N/A") return "N/A";
    const rank = stateRank(d.state);
    return rank === 3 ? "ON" : rank === 2 ? "RAMP" : "OFF";
  }

  // ../web/controller.ts
  var FREQ_MIN = 500;
  var FREQ_MAX = 3e4;
  var AMP_MAX = 5;
  var AMP_SLEW_MA_PER_S = 0.5;
  var FREQ_SLEW_OCT_PER_S = 1;
  var SLEW_TICK_MS = 100;
  var LOG_MIN = Math.log(FREQ_MIN);
  var LOG_SPAN = Math.log(FREQ_MAX) - LOG_MIN;
  var sliderToFreq = (s) => Math.round(Math.exp(LOG_MIN + LOG_SPAN * s / 1e3));
  var freqToSlider = (f) => Math.round(1e3 * (Math.log(f) - LOG_MIN) / LOG_SPAN);
  var clamp2 = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  var r2 = (v) => Math.round(v * 100) / 100;
  var hasBluetooth = "bluetooth" in navigator;
  var realAdapter = window.ddsNativeAdapter ?? (hasBluetooth ? () => new WebBluetoothAdapter() : null);
  var forceDemo = new URLSearchParams(location.search).has("demo") || !realAdapter;
  var readPref = (k) => {
    try {
      return localStorage.getItem(k);
    } catch {
      return null;
    }
  };
  var writePref = (k, v) => {
    try {
      localStorage.setItem(k, v);
    } catch {
    }
  };
  var simPref = readPref("research-lab-sim");
  var useDemo = forceDemo || (simPref === null ? !window.ddsNativeAdapter : simPref === "1");
  var conn = new NusConnection(() => useDemo || !realAdapter ? new MockBleAdapter() : realAdapter());
  var modeOpts = document.querySelectorAll(".mode-opt");
  function setMode(mode) {
    const view = mode === "dev" ? "dev" : "student";
    document.body.dataset.mode = view;
    modeOpts.forEach((b) => {
      const active = b.dataset.mode === view;
      b.classList.toggle("active", active);
      b.setAttribute("aria-pressed", String(active));
    });
    writePref("r2d2-mode", view);
  }
  setMode(readPref("r2d2-mode") ?? "student");
  modeOpts.forEach((btn) => btn.addEventListener("click", () => {
    setMode(btn.dataset.mode ?? "student");
    renderStudentOverview();
  }));
  var demoChip = document.getElementById("demo-chip");
  var simToggle = document.getElementById("sim-toggle");
  function renderDemo() {
    demoChip.style.display = useDemo ? "inline-flex" : "none";
    simToggle.checked = useDemo;
  }
  simToggle.addEventListener("change", () => {
    useDemo = simToggle.checked;
    writePref("research-lab-sim", useDemo ? "1" : "0");
    renderDemo();
    setStatus("disconnected", useDemo ? "Demo disconnected" : "Disconnected");
    setConnected(false);
    log("INFO", useDemo ? "Simulated device selected." : "Real device selected.");
  });
  renderDemo();
  var dot = document.getElementById("status-dot");
  var statusText = document.getElementById("status-text");
  var btnConnect = document.getElementById("btn-connect");
  var btnDisconn = document.getElementById("btn-disconnect");
  var btnClear = document.getElementById("btn-clear");
  var btnSend = document.getElementById("btn-send");
  var msgInput = document.getElementById("msg-input");
  var logEl = document.getElementById("log");
  var deviceDetails = document.getElementById("device-details");
  var infoName = document.getElementById("info-name");
  var infoId = document.getElementById("info-id");
  var infoMtu = document.getElementById("info-mtu");
  var channelTabs = document.querySelectorAll(".ch-tab");
  var freqSlider = document.getElementById("freq-slider");
  var freqNumber = document.getElementById("freq-number");
  var freqOut = document.getElementById("freq-out");
  var ampSlider = document.getElementById("amp-slider");
  var ampNumber = document.getElementById("amp-number");
  var ampOut = document.getElementById("amp-out");
  var waveBtns = document.querySelectorAll(".wave-btn");
  var freqPresets = document.querySelectorAll(".freq-preset");
  var ampPresets = document.querySelectorAll(".amp-preset");
  var btnDacOn = document.getElementById("btn-dac-on");
  var btnDacOff = document.getElementById("btn-dac-off");
  var btnDacStatus = document.getElementById("btn-dac-status");
  var dacStatusLine = document.getElementById("dac-status-line");
  var waveCanvas = document.getElementById("wave-canvas");
  var waveInfo = document.getElementById("wave-info");
  var channelStatusList = document.getElementById("channel-status-list");
  var studentChannel = document.getElementById("student-channel");
  var studentState = document.getElementById("student-state");
  var studentFrequency = document.getElementById("student-frequency");
  var studentRequested = document.getElementById("student-requested");
  var studentApplied = document.getElementById("student-applied");
  var studentNotice = document.getElementById("student-notice");
  var devLink = document.getElementById("dev-link");
  var devStatusAge = document.getElementById("dev-status-age");
  var devWriteMs = document.getElementById("dev-write-ms");
  var devRxFrames = document.getElementById("dev-rx-frames");
  var ctx = waveCanvas.getContext("2d");
  var currentChannel = "ALL";
  var currentWave = "SINE";
  var setFreq = 1e3;
  var setAmp = 0;
  var outFreq = 1e3;
  var outAmp = 0;
  var chanData = /* @__PURE__ */ new Map();
  var lastStatusLine = /* @__PURE__ */ new Map();
  var statusSeen = /* @__PURE__ */ new Map();
  var lastWriteMs = null;
  var rxFrames = 0;
  var pendingSync = false;
  function selectedChans() {
    return currentChannel === "ALL" ? ["0", "1", "2", "3"] : [currentChannel];
  }
  function selectedOutput() {
    let best = { state: "OFF", current: 0 };
    for (const c of selectedChans()) {
      const d = freshChannel(c);
      if (d && stateRank(d.state) > stateRank(best.state)) best = { state: d.state, current: d.current };
    }
    return best;
  }
  function freshChannel(ch) {
    const seen = statusSeen.get(ch);
    return seen !== void 0 && performance.now() - seen < 3500 ? chanData.get(ch) : void 0;
  }
  function miniWavePath(wave, amp) {
    const half = 19 * clamp2(amp / AMP_MAX, 0, 1);
    const points = [];
    for (let i = 0; i <= 64; i++) {
      const phase2 = i / 64 * 2 % 1;
      const v = wave === "SQUARE" ? phase2 < 0.5 ? 1 : -1 : wave === "TRIANGLE" ? phase2 < 0.5 ? 4 * phase2 - 1 : 3 - 4 * phase2 : wave === "SAWTOOTH" ? 1 - 2 * phase2 : Math.sin(phase2 * 2 * Math.PI);
      points.push(`${i === 0 ? "M" : "L"}${(i * 2.5).toFixed(1)} ${(24 - half * v).toFixed(1)}`);
    }
    return points.join(" ");
  }
  function renderChannelStrips() {
    for (let i = 0; i < 4; i++) {
      const ch = String(i);
      const tab = document.querySelector(`.ch-strip[data-ch="${ch}"]`);
      const d = freshChannel(ch);
      const state = !conn.connected ? "OFFLINE" : (d == null ? void 0 : d.fault) ? "FAULT" : (d == null ? void 0 : d.state) ?? (chanData.has(ch) ? "STALE" : "WAITING");
      const selected = currentChannel === "ALL" || currentChannel === ch;
      const freq = selected ? setFreq : d == null ? void 0 : d.freq;
      const amp = selected ? setAmp : d == null ? void 0 : d.amp;
      tab.querySelector(".ch-name").textContent = document.body.dataset.mode === "dev" ? `CH ${i}` : `CH ${String(i + 1).padStart(2, "0")}`;
      tab.dataset.state = state;
      document.getElementById(`strip-state-${ch}`).textContent = state;
      document.getElementById(`strip-freq-${ch}`).textContent = freq === void 0 ? "--" : `${freq} Hz`;
      document.getElementById(`strip-set-${ch}`).textContent = amp === void 0 ? "--" : `${amp.toFixed(2)} mA`;
      document.getElementById(`strip-ramp-${ch}`).textContent = d ? `${d.current.toFixed(3)} mA` : "--";
      document.getElementById(`strip-wave-${ch}`).setAttribute("d", miniWavePath(selected ? currentWave : (d == null ? void 0 : d.wave) ?? "SINE", amp ?? 0));
      tab.setAttribute("aria-label", `Channel ${i + 1}: ${state}, setpoint ${(amp == null ? void 0 : amp.toFixed(2)) ?? "--"} milliamps`);
    }
  }
  function renderTelemetry() {
    devLink.textContent = `${useDemo ? "SIM" : "BLE"} / ${conn.connected ? "ONLINE" : "OFFLINE"}`;
    const ages = [...statusSeen.values()].map((t) => performance.now() - t);
    const age = ages.length ? Math.min(...ages) : null;
    devStatusAge.textContent = !conn.connected ? "--" : age === null ? "WAITING" : `${(age / 1e3).toFixed(1)} s`;
    devStatusAge.classList.toggle("stale", conn.connected && age !== null && age >= 3500);
    devWriteMs.textContent = lastWriteMs === null ? "--" : `${lastWriteMs.toFixed(1)} ms`;
    devRxFrames.textContent = String(rxFrames).padStart(4, "0");
  }
  async function writeCommand(text) {
    const start = performance.now();
    await conn.send(text);
    lastWriteMs = performance.now() - start;
    renderTelemetry();
  }
  function announceStudent(message, error = false) {
    studentNotice.textContent = message;
    studentNotice.classList.toggle("error", error);
  }
  function renderStudentOverview() {
    studentChannel.textContent = currentChannel === "ALL" ? "All channels" : `Channel ${Number(currentChannel) + 1}`;
    studentFrequency.textContent = `${setFreq.toLocaleString()} Hz`;
    studentRequested.textContent = `${setAmp.toFixed(2)} mA`;
    const selected = selectedChans().map(freshChannel).filter((d) => Boolean(d));
    const valid = selected.filter((d) => d.state !== "N/A" && d.state !== "ERR");
    const fault = selected.some((d) => d.fault);
    const ranked = valid.reduce((best, d) => !best || stateRank(d.state) > stateRank(best.state) ? d : best, void 0);
    const state = fault ? "Fault" : !conn.connected ? useDemo ? "Demo disconnected" : "Disconnected" : !ranked ? "Waiting for status" : {
      OFF: "Off",
      PRECHARGE: "Preparing",
      CONNECT: "Connecting output",
      RAMP_UP: "Ramping up",
      ACTIVE: "Active",
      RAMP_DOWN: "Ramping down",
      DISCONNECT: "Disconnecting"
    }[ranked.state] ?? "Status unknown";
    studentState.textContent = state;
    studentState.className = "state-pill" + (fault ? " fault" : ranked && stateRank(ranked.state) === 3 ? " active" : ranked && stateRank(ranked.state) === 2 ? " ramping" : "");
    studentApplied.textContent = valid.length ? `${Math.max(...valid.map((d) => d.current)).toFixed(3)} mA` : "?";
    btnDacOn.classList.toggle("engaged", (ranked == null ? void 0 : ranked.state) === "ACTIVE" || (ranked == null ? void 0 : ranked.state) === "RAMP_UP");
    renderChannelStrips();
    renderTelemetry();
  }
  var cssW = 0;
  var cssH = 0;
  var phase = 0;
  var scopeAmp = 0;
  var lastFrame = 0;
  function resizeCanvas() {
    const dpr = window.devicePixelRatio || 1;
    cssW = waveCanvas.offsetWidth;
    cssH = waveCanvas.offsetHeight;
    waveCanvas.width = cssW * dpr;
    waveCanvas.height = cssH * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }
  window.addEventListener("resize", resizeCanvas);
  function drawFrame(dt) {
    if (cssW === 0) return;
    phase = (phase + dt * 1.8) % 1;
    const out = selectedOutput();
    const live = stateRank(out.state) > 0;
    const target = out.state === "ACTIVE" ? outAmp : live ? out.current : outAmp;
    scopeAmp += (target - scopeAmp) * (1 - Math.exp(-dt * 6));
    const w = cssW, h = cssH;
    ctx.fillStyle = "#0b1216";
    ctx.fillRect(0, 0, w, h);
    const cols = 8, rows = 4;
    ctx.setLineDash([]);
    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(102,217,228,0.10)";
    for (let c = 1; c < cols; c++) {
      ctx.beginPath();
      ctx.moveTo(w * c / cols, 0);
      ctx.lineTo(w * c / cols, h);
      ctx.stroke();
    }
    for (let r = 1; r < rows; r++) {
      ctx.beginPath();
      ctx.moveTo(0, h * r / rows);
      ctx.lineTo(w, h * r / rows);
      ctx.stroke();
    }
    ctx.strokeStyle = "rgba(102,217,228,0.20)";
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(w, h / 2);
    ctx.stroke();
    ctx.strokeStyle = "rgba(102,217,228,0.15)";
    const ticks = cols * 5;
    for (let i = 0; i <= ticks; i++) {
      const x = w * i / ticks, tH = i % 5 === 0 ? 7 : 3;
      ctx.beginPath();
      ctx.moveTo(x, h / 2 - tH);
      ctx.lineTo(x, h / 2 + tH);
      ctx.stroke();
    }
    const halfAmp = (h / 2 - 10) * (scopeAmp / AMP_MAX);
    const cycles = 1.5 + 8.5 * (Math.log(outFreq) - LOG_MIN) / LOG_SPAN;
    if (live) {
      ctx.beginPath();
      ctx.strokeStyle = "rgba(102,217,228,0.16)";
      ctx.lineWidth = 6;
      ctx.lineJoin = "round";
      traceWave(cycles, halfAmp, h / 2);
      ctx.stroke();
    }
    ctx.beginPath();
    ctx.strokeStyle = live ? "#66d9e4" : "rgba(102,217,228,0.35)";
    ctx.lineWidth = live ? 1.8 : 1.4;
    ctx.lineJoin = "round";
    if (!live) ctx.setLineDash([5, 4]);
    traceWave(cycles, halfAmp, h / 2);
    ctx.stroke();
    ctx.setLineDash([]);
    const chLabel = currentChannel === "ALL" ? "ALL" : `CH${currentChannel}`;
    waveInfo.textContent = `${chLabel}  ${currentWave}  ${outFreq} Hz  ${scopeAmp.toFixed(2)} mA  ${live ? out.state : "OFF \xB7 PREVIEW"}`;
  }
  function traceWave(cycles, halfAmp, cy) {
    for (let i = 0; i <= cssW; i++) {
      const p = (i / cssW * cycles + phase) % 1;
      let y;
      switch (currentWave) {
        case "SINE":
          y = cy - halfAmp * Math.sin(p * 2 * Math.PI);
          break;
        case "SQUARE": {
          const e = 0.01;
          if (p < 0.5 - e) y = cy - halfAmp;
          else if (p < 0.5 + e) y = cy - halfAmp * (1 - (p - (0.5 - e)) / e);
          else if (p < 1 - e) y = cy + halfAmp;
          else y = cy + halfAmp * (1 - (p - (1 - e)) / e);
          break;
        }
        case "TRIANGLE":
          y = cy - halfAmp * (p < 0.5 ? 4 * p - 1 : 3 - 4 * p);
          break;
        case "SAWTOOTH":
          y = p < 0.98 ? cy - halfAmp * (1 - 2 * p / 0.98) : cy - halfAmp * (-1 + (p - 0.98) / 0.02 * 2);
          break;
        default:
          y = cy;
      }
      i === 0 ? ctx.moveTo(i, y) : ctx.lineTo(i, y);
    }
  }
  function loop(ts) {
    const dt = lastFrame === 0 ? 0 : Math.min((ts - lastFrame) / 1e3, 0.05);
    lastFrame = ts;
    drawFrame(dt);
    requestAnimationFrame(loop);
  }
  resizeCanvas();
  requestAnimationFrame(loop);
  function escHtml(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }
  function log(tag, msg, bytes) {
    var _a;
    const ts = (/* @__PURE__ */ new Date()).toLocaleTimeString("en", { hour12: false });
    const entry = document.createElement("div");
    const hex = bytes ? Array.from(bytes.slice(0, 12), (b) => b.toString(16).padStart(2, "0")).join(" ").toUpperCase() + (bytes.length > 12 ? " ..." : "") : "";
    entry.className = "log-entry";
    entry.innerHTML = `<span class="log-ts">${ts}</span><span class="log-tag ${tag.toLowerCase()}">[${tag}]</span><span class="log-msg">${escHtml(msg)}</span><span class="log-hex">${hex}</span>`;
    logEl.appendChild(entry);
    if (logEl.children.length > 500) (_a = logEl.firstElementChild) == null ? void 0 : _a.remove();
    logEl.scrollTop = logEl.scrollHeight;
  }
  var errMsg = (err) => err instanceof Error ? err.message : String(err);
  function setStatus(state, text) {
    dot.className = state;
    statusText.textContent = text;
  }
  function setConnected(isConn) {
    announceStudent(isConn ? useDemo ? "Connected to the demo. Choose a channel and review its status." : "Connected. Choose a channel and review its status." : useDemo ? "Demo disconnected. Output status is unavailable." : "Disconnected. Output status is unavailable.");
    btnConnect.textContent = useDemo ? "Connect to demo" : "Connect to device";
    btnConnect.disabled = isConn;
    btnDisconn.disabled = !isConn;
    btnSend.disabled = !isConn;
    msgInput.disabled = !isConn;
    simToggle.disabled = isConn || forceDemo;
    if (deviceDetails) deviceDetails.style.display = isConn ? "" : "none";
    if (!isConn) {
      infoName.textContent = useDemo ? "Simulated device - no hardware" : "Bluetooth device";
      infoId.textContent = "\u2014";
      infoMtu.textContent = "\u2014";
    }
    setDacPanelEnabled(isConn);
  }
  btnConnect.addEventListener("click", async () => {
    try {
      setStatus("connecting", useDemo ? "Connecting to demo\u2026" : "Scanning for device\u2026");
      log("INFO", useDemo ? "Demo \u2014 simulating device\u2026" : "Scanning for device\u2026");
      const dev = await conn.connect(onValue, onDisconnected);
      setStatus("connected", useDemo ? "Demo connected" : "Connected");
      setConnected(true);
      infoName.textContent = dev.name;
      infoId.textContent = dev.id;
      infoMtu.textContent = dev.mtu;
      log("INFO", `Connected to ${dev.name}  \xB7  MTU ${dev.mtu}`);
    } catch (err) {
      setStatus("disconnected", "Error");
      log("ERR", errMsg(err));
      announceStudent("Connection failed: " + errMsg(err), true);
    }
  });
  btnDisconn.addEventListener("click", async () => {
    log("INFO", "Disconnecting\u2026");
    await conn.disconnect();
  });
  function onDisconnected() {
    setStatus("disconnected", useDemo ? "Demo disconnected" : "Disconnected");
    log("INFO", "Device disconnected.");
    setConnected(false);
  }
  function onValue(dv) {
    const value = new TextDecoder().decode(dv);
    const bytes = new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength);
    rxFrames++;
    renderTelemetry();
    const sm = value.match(/^DDS S(\d) /);
    if (sm) {
      if (lastStatusLine.get(sm[1]) !== value) {
        lastStatusLine.set(sm[1], value);
        log("DDS", value, bytes);
      }
      parseDdsStatus(value);
      return;
    }
    if (value.startsWith("DDS ERR")) announceStudent("Device rejected command: " + value.slice(8), true);
    else if (value.startsWith("DDS OK")) announceStudent("Command accepted. Waiting for output status.");
    log(value.startsWith("HB#") ? "HB" : value.startsWith("DDS ERR") ? "ERR" : value.startsWith("DDS OK") ? "ACK" : value.startsWith("DDS ") ? "DDS" : "RX", value, bytes);
  }
  async function sendCmd(text) {
    try {
      await writeCommand(text);
      log("TX", text, new TextEncoder().encode(text));
    } catch (err) {
      log("ERR", errMsg(err));
      announceStudent("Could not send command: " + errMsg(err), true);
    }
  }
  async function sendMessage() {
    const text = msgInput.value.trim();
    if (!text) return;
    await sendCmd(text);
    msgInput.value = "";
  }
  btnSend.addEventListener("click", () => void sendMessage());
  msgInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") void sendMessage();
  });
  btnClear.addEventListener("click", () => {
    logEl.innerHTML = "";
  });
  function ddsPrefix() {
    return `DDS ${currentChannel} `;
  }
  function syncControls() {
    freqSlider.value = String(freqToSlider(setFreq));
    freqNumber.value = String(setFreq);
    ampSlider.value = String(setAmp);
    ampNumber.value = setAmp.toFixed(2);
    waveBtns.forEach((b) => b.classList.toggle("active", b.dataset.wave === currentWave));
  }
  function renderOut() {
    freqOut.textContent = `OUT ${outFreq} Hz`;
    ampOut.textContent = `OUT ${outAmp.toFixed(2)} mA`;
    freqOut.classList.toggle("slewing", outFreq !== setFreq);
    ampOut.classList.toggle("slewing", outAmp !== setAmp);
    renderStudentOverview();
  }
  function adoptChannel(ch) {
    var _a;
    if (ch === "ALL") {
      const known = [...chanData.values()].filter((d) => d.state !== "N/A" && d.state !== "ERR");
      if (known.length) {
        outAmp = setAmp = r2(Math.min(...known.map((d) => d.amp)));
        outFreq = setFreq = ((_a = chanData.get("0")) == null ? void 0 : _a.freq) ?? outFreq;
      }
    } else {
      const d = chanData.get(ch);
      if (!d || d.state === "N/A" || d.state === "ERR") {
        pendingSync = true;
        return;
      }
      outFreq = setFreq = d.freq;
      outAmp = setAmp = r2(d.amp);
      currentWave = d.wave;
    }
    syncControls();
    renderOut();
  }
  var slewTimer = null;
  var slewBusy = false;
  var lastSlew = 0;
  var isSlewing = () => outFreq !== setFreq || outAmp !== setAmp;
  function startSlew() {
    renderOut();
    if (slewTimer || !conn.connected || !isSlewing()) return;
    lastSlew = performance.now();
    slewTimer = setInterval(() => void slewTick(), SLEW_TICK_MS);
    schedulePoll(250);
  }
  function stopSlew(reason) {
    if (slewTimer) {
      clearInterval(slewTimer);
      slewTimer = null;
    }
    if (reason && isSlewing()) log("INFO", `Ramp halted at ${outFreq} Hz / ${outAmp.toFixed(2)} mA (${reason})`);
    setFreq = outFreq;
    setAmp = outAmp;
    syncControls();
    renderOut();
  }
  async function slewTick() {
    if (slewBusy) return;
    const now = performance.now();
    const dt = Math.min((now - lastSlew) / 1e3, 0.3);
    lastSlew = now;
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
      if (nf !== outFreq) {
        await writeCommand(prefix + "FREQ " + nf);
        outFreq = nf;
      }
      if (na !== outAmp) {
        await writeCommand(prefix + "AMP " + na.toFixed(2));
        outAmp = na;
      }
    } catch (err) {
      log("ERR", errMsg(err));
      stopSlew("send failed");
    } finally {
      slewBusy = false;
    }
    renderOut();
    if (slewTimer && !isSlewing()) {
      clearInterval(slewTimer);
      slewTimer = null;
      log("INFO", `Reached ${outFreq} Hz / ${outAmp.toFixed(2)} mA on ${currentChannel}`);
    }
  }
  function applyFreq(f) {
    setFreq = clamp2(Math.round(f) || FREQ_MIN, FREQ_MIN, FREQ_MAX);
    freqNumber.value = String(setFreq);
    startSlew();
  }
  function applyAmp(a) {
    setAmp = r2(clamp2(a || 0, 0, AMP_MAX));
    ampNumber.value = setAmp.toFixed(2);
    startSlew();
  }
  freqSlider.addEventListener("input", () => applyFreq(sliderToFreq(Number(freqSlider.value))));
  freqNumber.addEventListener("change", () => {
    applyFreq(parseInt(freqNumber.value, 10));
    freqSlider.value = String(freqToSlider(setFreq));
  });
  freqPresets.forEach((b) => b.addEventListener("click", () => {
    applyFreq(Number(b.dataset.freq));
    freqSlider.value = String(freqToSlider(setFreq));
  }));
  ampSlider.addEventListener("input", () => applyAmp(parseFloat(ampSlider.value)));
  ampNumber.addEventListener("change", () => {
    applyAmp(parseFloat(ampNumber.value));
    ampSlider.value = String(setAmp);
  });
  ampPresets.forEach((b) => b.addEventListener("click", () => {
    applyAmp(parseFloat(b.dataset.amp));
    ampSlider.value = String(setAmp);
  }));
  for (const input of [freqNumber, ampNumber]) {
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") input.blur();
    });
  }
  channelTabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      if (tab.dataset.ch === currentChannel) return;
      stopSlew("channel switched");
      channelTabs.forEach((t) => t.classList.toggle("active", t === tab));
      currentChannel = tab.dataset.ch;
      log("INFO", `Channel \u2192 ${currentChannel}`);
      adoptChannel(currentChannel);
      renderStudentOverview();
    });
  });
  waveBtns.forEach((btn) => {
    btn.addEventListener("click", () => {
      currentWave = btn.dataset.wave;
      waveBtns.forEach((b) => b.classList.toggle("active", b === btn));
      renderStudentOverview();
      void sendCmd(ddsPrefix() + "WAVE " + currentWave);
    });
  });
  btnDacOn.addEventListener("click", () => {
    announceStudent("Start requested. Waiting for device confirmation.");
    void sendCmd(ddsPrefix() + "ON");
    schedulePoll(100);
  });
  btnDacOff.addEventListener("click", () => {
    announceStudent("Stop requested. Waiting for device confirmation.");
    void sendCmd(ddsPrefix() + "OFF");
    schedulePoll(100);
  });
  btnDacStatus.addEventListener("click", () => void sendCmd(ddsPrefix() + "STATUS"));
  var pollTimer = null;
  function schedulePoll(ms) {
    if (pollTimer) clearTimeout(pollTimer);
    const ramping = [...chanData.values()].some((d) => stateRank(d.state) === 2);
    const delay2 = ms ?? (ramping || slewTimer ? 250 : 1e3);
    pollTimer = setTimeout(async () => {
      pollTimer = null;
      if (!conn.connected) return;
      try {
        await writeCommand("DDS ALL STATUS");
      } catch {
      }
      renderChannelStatuses();
      renderStudentOverview();
      schedulePoll();
    }, delay2);
  }
  function stopPoll() {
    if (pollTimer) {
      clearTimeout(pollTimer);
      pollTimer = null;
    }
  }
  function renderChannelStatuses() {
    channelStatusList.replaceChildren();
    for (let i = 0; i < 4; i++) {
      const d = freshChannel(String(i));
      const stale = chanData.has(String(i)) && !d;
      const rank = d ? stateRank(d.state) : 0;
      const cell = document.createElement("div");
      cell.className = `ch-cell${stale ? " stale" : (d == null ? void 0 : d.fault) ? " fault" : rank === 3 ? " active" : rank === 2 ? " ramping" : ""}`;
      cell.innerHTML = `<div class="ch-cell-id">CH ${i}</div><div class="ch-cell-dot"></div><div class="ch-cell-st">${stale ? "STALE" : channelLabel(d)}</div><div class="ch-cell-hz">${d && d.state !== "N/A" && d.state !== "ERR" ? `${d.freq} Hz` : ""}</div><div class="ch-cell-current">${d && d.state !== "N/A" && d.state !== "ERR" ? `${d.current.toFixed(3)} mA` : ""}</div>`;
      channelStatusList.appendChild(cell);
    }
  }
  function finishSync(ch) {
    if (!pendingSync) return;
    if (currentChannel === "ALL" ? ch === "3" : ch === currentChannel) {
      pendingSync = false;
      adoptChannel(currentChannel);
    }
  }
  function parseDdsStatus(msg) {
    const d = parseStatusLine(msg);
    if (!d) return;
    chanData.set(d.ch, d);
    statusSeen.set(d.ch, performance.now());
    renderChannelStatuses();
    renderStudentOverview();
    if (d.state !== "N/A" && d.state !== "ERR") {
      dacStatusLine.textContent = `CH${d.ch}  ${d.state}  ${d.current.toFixed(3)} mA  ${d.freq} Hz${d.fault ? "  FAULT" : ""}`;
    }
    finishSync(d.ch);
  }
  function setDacPanelEnabled(on) {
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
      dacStatusLine.textContent = "\u2014";
    }
    channelTabs.forEach((t) => {
      t.disabled = !on;
    });
    freqSlider.disabled = freqNumber.disabled = ampSlider.disabled = ampNumber.disabled = !on;
    waveBtns.forEach((b) => {
      b.disabled = !on;
    });
    freqPresets.forEach((b) => {
      b.disabled = !on;
    });
    ampPresets.forEach((b) => {
      b.disabled = !on;
    });
    btnDacOn.disabled = btnDacOff.disabled = btnDacStatus.disabled = !on;
    renderStudentOverview();
  }
  setStatus("", useDemo ? "Demo disconnected" : "Disconnected");
  setConnected(false);
  log("INFO", !realAdapter ? "No Bluetooth here (use Chrome/Edge or the app) \u2014 running the simulated device." : useDemo ? "Simulated device \u2014 click Connect to start." : "Ready. Click Connect to scan.");
})();
//# sourceMappingURL=app.js.map

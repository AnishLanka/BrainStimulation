import type { BleAdapter, DeviceInfo, ValueHandler, DisconnectHandler } from './ble_transport';

type ChanState = 'OFF' | 'RAMP_UP' | 'ACTIVE' | 'RAMP_DOWN';

interface Chan {
  freq: number; wave: string; amp: number;
  state: ChanState;
  rampStart: number; rampDuration: number; rampFrom: number;
}

const delay = (ms: number) => new Promise<void>(r => setTimeout(r, ms));
const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));
const easeInOut = (t: number) => t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;

function makeChan(): Chan {
  return { freq: 1000, wave: 'SINE', amp: 0, state: 'OFF', rampStart: 0, rampDuration: 0, rampFrom: 0 };
}

export class MockBleAdapter implements BleAdapter {
  maxWriteBytes = 244;
  private notify?: ValueHandler;
  private hbTimer?: ReturnType<typeof setInterval>;
  private hbSeq = 0;
  private chans: Chan[] = Array.from({ length: 4 }, makeChan);

  async connect(onValue: ValueHandler, _onDisconnect: DisconnectHandler): Promise<DeviceInfo> {
    await delay(850);
    this.notify = onValue;
    this.hbTimer = setInterval(() => this.emit(`HB#${this.hbSeq++} VBAT=3.85V CHG=0`), 2000);
    return { id: 'research-lab-demo', name: 'Research Lab demo', mtu: '247' };
  }

  async write(value: DataView): Promise<void> {
    await delay(10 + Math.random() * 15);
    this.handleCmd(new TextDecoder().decode(value));
  }

  async disconnect(): Promise<void> {
    clearInterval(this.hbTimer);
    this.hbTimer = undefined;
  }

  private emit(text: string): void {
    const b = new TextEncoder().encode(text);
    this.notify?.(new DataView(b.buffer));
  }

  private progress(c: Chan): number {
    return c.rampDuration > 0 ? clamp((Date.now() - c.rampStart) / c.rampDuration, 0, 1) : 1;
  }

  private resolveState(c: Chan): ChanState {
    if ((c.state === 'RAMP_UP' || c.state === 'RAMP_DOWN') && this.progress(c) >= 1) {
      c.state = c.state === 'RAMP_UP' ? 'ACTIVE' : 'OFF';
    }
    return c.state;
  }

  private current(c: Chan): number {
    switch (this.resolveState(c)) {
      case 'ACTIVE':    return c.amp;
      case 'RAMP_UP':   return c.rampFrom + (c.amp - c.rampFrom) * easeInOut(this.progress(c));
      case 'RAMP_DOWN': return c.rampFrom * (1 - easeInOut(this.progress(c)));
      default:          return 0;
    }
  }

  private startRamp(c: Chan, state: ChanState, ms: number): void {
    c.rampFrom = this.current(c);
    c.rampStart = Date.now();
    c.rampDuration = ms;
    c.state = state;
  }

  private handleCmd(text: string): void {
    const m = text.match(/^DDS (ALL|\d) (\w+)(?: (.+))?$/);
    if (!m) return;
    const [, ch, cmd, arg = ''] = m;
    const idx = ch === 'ALL' ? [0, 1, 2, 3] : [Number(ch)];
    for (const i of idx) {
      const s = this.chans[i]; if (!s) continue;
      const st = this.resolveState(s);
      switch (cmd) {
        case 'FREQ': s.freq = clamp(parseInt(arg) || 1000, 500, 30000); break;
        case 'WAVE': if (['SINE', 'SQUARE', 'TRIANGLE', 'SAWTOOTH'].includes(arg)) s.wave = arg; break;
        case 'AMP':  s.amp  = clamp(parseFloat(arg) || 0, 0, 5); break;
        case 'ON':   if (st === 'OFF' || st === 'RAMP_DOWN') this.startRamp(s, 'RAMP_UP', 1400); break;
        case 'OFF':  if (st === 'ACTIVE' || st === 'RAMP_UP') this.startRamp(s, 'RAMP_DOWN', 600); break;
        case 'STATUS':
          setTimeout(() => {
            const sc  = this.chans[i]!;
            const cur = this.current(sc);
            const out = sc.state === 'OFF' ? 0 : 1;
            this.emit(
              `DDS S${i} FREQ=${sc.freq} WAVE=${sc.wave} AMP=${sc.amp.toFixed(2)}` +
              ` STATE=${sc.state} CURRENT=${cur.toFixed(3)} OUT=${out} DUMMY=0`
            );
          }, 15 + Math.random() * 20);
          break;
      }
    }
  }
}

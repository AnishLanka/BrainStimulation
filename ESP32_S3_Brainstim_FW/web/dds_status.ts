export interface DdsStatus {
  ch: string; freq: number; amp: number; wave: string;
  state: string; current: number; out: boolean; fault: boolean;
}

const STATUS_RE =
  /^DDS S([0-3]) FREQ=([\d.]+) WAVE=(\w+) AMP=([\d.]+) STATE=(\w+) CURRENT=([\d.]+) OUT=([01]) DUMMY=([01])( FAULT)?$/;

export function parseStatusLine(line: string): DdsStatus | null {
  const miss = line.match(/^DDS S([0-3]) (NOT_PRESENT|ERR.*)$/);
  if (miss) {
    const absent = miss[2] === 'NOT_PRESENT';
    return { ch: miss[1], freq: 0, amp: 0, wave: 'SINE', state: absent ? 'N/A' : 'ERR',
             current: 0, out: false, fault: !absent };
  }
  const m = line.match(STATUS_RE);
  if (!m) return null;
  return { ch: m[1], freq: Number(m[2]), wave: m[3], amp: Number(m[4]), state: m[5],
           current: Number(m[6]), out: m[7] === '1', fault: Boolean(m[9]) };
}

const TRANSITION_STATES = ['PRECHARGE', 'CONNECT', 'RAMP_UP', 'RAMP_DOWN', 'DISCONNECT'];

/** 3 = delivering set current, 2 = switching or ramping, 0 = no output / unknown. */
export function stateRank(state: string): number {
  if (state === 'ACTIVE') return 3;
  return TRANSITION_STATES.includes(state) ? 2 : 0;
}

export function channelLabel(d: DdsStatus | undefined): string {
  if (!d) return '—';
  if (d.fault) return 'FAULT';
  if (d.state === 'N/A') return 'N/A';
  const rank = stateRank(d.state);
  return rank === 3 ? 'ON' : rank === 2 ? 'RAMP' : 'OFF';
}

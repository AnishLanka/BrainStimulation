export const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_RX      = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_TX      = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

export interface DeviceInfo { id: string; name: string; mtu: string; }
export type ValueHandler      = (value: DataView) => void;
export type DisconnectHandler = () => void;

export interface BleAdapter {
  maxWriteBytes?: number;
  connect(onValue: ValueHandler, onDisconnect: DisconnectHandler): Promise<DeviceInfo>;
  write(value: DataView): Promise<void>;
  disconnect(): Promise<void>;
}

interface Session {
  adapter:   BleAdapter;
  ready:     boolean;
  queue:     Promise<void>;
  pending:   number;
  lost:      () => void;
}

export class NusConnection {
  private session: Session | null = null;
  private busy = false;

  constructor(
    private adapterFactory: () => BleAdapter,
    private pause: (ms: number) => Promise<void> = ms =>
      new Promise(resolve => setTimeout(resolve, ms))
  ) {}

  get connected(): boolean { return Boolean(this.session?.ready); }

  async connect(onValue: ValueHandler, onDisconnect: DisconnectHandler): Promise<DeviceInfo> {
    if (this.busy || this.session) throw new Error('A connection is already active.');
    this.busy = true;
    const session: Session = {
      adapter: this.adapterFactory(), ready: false,
      queue: Promise.resolve(), pending: 0, lost: () => {},
    };
    this.session = session;
    session.lost = () => {
      if (this.session !== session) return;
      this.session = null;
      const wasReady = session.ready;
      session.ready = false;
      if (wasReady) onDisconnect(); // only fire if we were actually connected
    };
    try {
      const device = await session.adapter.connect(
        value => { if (this.session === session) onValue(value); },
        session.lost,
      );
      if (this.session !== session) throw new Error('Disconnected during setup.');
      session.ready = true;
      return device;
    } catch (error) {
      session.lost();
      try { await session.adapter.disconnect(); } catch { /* preserve original error */ }
      throw error;
    } finally { this.busy = false; }
  }

  async disconnect(): Promise<void> {
    const session = this.session;
    if (!session) return;
    this.busy = true;
    session.lost();
    try { await session.adapter.disconnect(); }
    finally { this.busy = false; }
  }

  async send(text: string): Promise<void> {
    const session = this.session;
    if (!session?.ready) throw new Error('Connect to a device first.');
    const bytes = new TextEncoder().encode(text);
    const max = Math.min(252, session.adapter.maxWriteBytes ?? 252);
    if (!bytes.length || bytes.length > max)
      throw new Error(`Command must fit in one BLE write (${max} bytes). Try an individual channel for long ALL commands.`);
    if (session.pending >= 16) throw new Error('Command queue full. Wait before sending more.');
    session.pending++;
    const job = session.queue.then(async () => {
      if (this.session !== session || !session.ready)
        throw new Error('Connection changed; command cancelled.');
      await session.adapter.write(new DataView(bytes.buffer));
      await this.pause(30);
    });
    session.queue = job.catch(() => {});
    try { await job; } finally { session.pending--; }
  }
}

export class WebBluetoothAdapter implements BleAdapter {
  private device:      BluetoothDevice | null = null;
  private rx:          BluetoothRemoteGATTCharacteristic | null = null;
  private tx:          BluetoothRemoteGATTCharacteristic | null = null;
  private valueHook:   ((e: Event) => void) | null = null;
  private disconnHook: DisconnectHandler | null = null;

  async connect(onValue: ValueHandler, onDisconnect: DisconnectHandler): Promise<DeviceInfo> {
    if (!globalThis.navigator?.bluetooth)
      throw new Error('Use the installed iOS/Android app, or Chrome/Edge with Web Bluetooth.');
    this.device = await navigator.bluetooth.requestDevice({ filters: [{ services: [NUS_SERVICE] }] });
    this.disconnHook = onDisconnect;
    this.device.addEventListener('gattserverdisconnected', onDisconnect as EventListener);
    const server  = await this.device.gatt!.connect();
    const service = await server.getPrimaryService(NUS_SERVICE);
    this.tx = await service.getCharacteristic(NUS_TX);
    this.rx = await service.getCharacteristic(NUS_RX);
    this.valueHook = (e: Event) => {
      const ev = e as Event & { target: { value: DataView } };
      onValue(ev.target.value);
    };
    this.tx.addEventListener('characteristicvaluechanged', this.valueHook);
    await this.tx.startNotifications();
    return { id: this.device.id, name: this.device.name ?? '(unknown)', mtu: 'negotiated by browser' };
  }

  write(value: DataView): Promise<void> { return this.rx!.writeValueWithResponse(value); }

  async disconnect(): Promise<void> {
    if (this.tx && this.valueHook)  this.tx.removeEventListener('characteristicvaluechanged', this.valueHook);
    if (this.device && this.disconnHook)
      this.device.removeEventListener('gattserverdisconnected', this.disconnHook as EventListener);
    if (this.device?.gatt?.connected) this.device.gatt.disconnect();
  }
}

// BleClient is injected for testability — the same interface as @capacitor-community/bluetooth-le.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export class NativeBleAdapter implements BleAdapter {
  maxWriteBytes?: number;
  private id?: string;

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  constructor(private client: any) {}

  async connect(onValue: ValueHandler, onDisconnect: DisconnectHandler): Promise<DeviceInfo> {
    await this.client.initialize();
    if (!await this.client.isEnabled())
      throw new Error('Turn on Bluetooth in phone settings, then reconnect.');
    const device = await this.client.requestDevice({ services: [NUS_SERVICE] });
    this.id = device.deviceId as string;
    await this.client.connect(this.id, onDisconnect);
    const mtu = await this.client.getMtu(this.id) as number;
    if (!Number.isFinite(mtu) || mtu < 23) throw new Error('Invalid BLE MTU reported by phone.');
    this.maxWriteBytes = mtu - 3;
    await this.client.startNotifications(this.id, NUS_SERVICE, NUS_TX,
      (dv: { value: DataView }) => onValue(dv.value));
    return { id: this.id!, name: device.name as string, mtu: String(mtu) };
  }

  write(value: DataView): Promise<void> {
    return this.client.write(this.id, NUS_SERVICE, NUS_RX, value);
  }

  async disconnect(): Promise<void> {
    if (!this.id) return;
    try { await this.client.stopNotifications(this.id, NUS_SERVICE, NUS_TX); }
    finally { await this.client.disconnect(this.id); }
  }
}

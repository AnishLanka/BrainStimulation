// Both app transports use the ESP32's existing Nordic UART Service.
export const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

export class NusConnection {
  constructor(adapterFactory, pause = ms => new Promise(resolve => setTimeout(resolve, ms))) {
    this.adapterFactory = adapterFactory;
    this.pause = pause;
    this.session = null;
    this.busy = false;
  }

  get connected() { return Boolean(this.session?.ready); }

  async connect(onValue, onDisconnect) {
    if (this.busy || this.session) throw new Error('A connection is already active.');
    this.busy = true;
    const session = { adapter: this.adapterFactory(), ready: false, queue: Promise.resolve(), pending: 0 };
    this.session = session;
    session.lost = () => {
      if (this.session !== session) return;
      this.session = null;
      session.ready = false;
      onDisconnect();
    };
    try {
      const device = await session.adapter.connect(
        value => { if (this.session === session) onValue(value); }, session.lost);
      if (this.session !== session) throw new Error('Disconnected during setup.');
      session.ready = true;
      return device;
    } catch (error) {
      session.lost();
      try { await session.adapter.disconnect(); } catch { /* Preserve setup error. */ }
      throw error;
    } finally {
      this.busy = false;
    }
  }

  async disconnect() {
    const session = this.session;
    if (!session) return;
    this.busy = true;
    session.lost(); // Invalidate queued writes before awaiting the native stack.
    try { await session.adapter.disconnect(); }
    finally { this.busy = false; }
  }

  async send(text) {
    const session = this.session;
    if (!session?.ready) throw new Error('Connect to a device first.');
    const bytes = new TextEncoder().encode(text);
    const max = Math.min(252, session.adapter.maxWriteBytes ?? 252);
    if (!bytes.length || bytes.length > max) {
      throw new Error(`Command must fit in one BLE write (${max} bytes). Try an individual channel for long ALL commands.`);
    }
    if (session.pending >= 16) throw new Error('Command queue full. Wait before sending more.');
    session.pending++;
    const job = session.queue.then(async () => {
      if (this.session !== session || !session.ready) throw new Error('Connection changed; command cancelled.');
      await session.adapter.write(new DataView(bytes.buffer));
      // The firmware command task consumes at most one command every 15 ms.
      await this.pause(30);
    });
    session.queue = job.catch(() => {});
    try { await job; } finally { session.pending--; }
  }
}

export class WebBluetoothAdapter {
  async connect(onValue, onDisconnect) {
    if (!globalThis.navigator?.bluetooth) {
      throw new Error('Use the installed iOS/Android app, or Chrome/Edge with Web Bluetooth.');
    }
    this.device = await navigator.bluetooth.requestDevice({ filters: [{ services: [NUS_SERVICE] }] });
    this.onDisconnect = onDisconnect;
    this.device.addEventListener('gattserverdisconnected', onDisconnect);
    const server = await this.device.gatt.connect();
    const service = await server.getPrimaryService(NUS_SERVICE);
    this.tx = await service.getCharacteristic(NUS_TX);
    this.rx = await service.getCharacteristic(NUS_RX);
    this.onValue = event => onValue(event.target.value);
    this.tx.addEventListener('characteristicvaluechanged', this.onValue);
    await this.tx.startNotifications();
    return { id: this.device.id, name: this.device.name, mtu: 'negotiated by browser' };
  }

  write(value) { return this.rx.writeValueWithResponse(value); }

  async disconnect() {
    this.tx?.removeEventListener('characteristicvaluechanged', this.onValue);
    this.device?.removeEventListener('gattserverdisconnected', this.onDisconnect);
    if (this.device?.gatt.connected) this.device.gatt.disconnect();
  }
}

// BleClient is injected so the native API can be tested without a phone.
export class NativeBleAdapter {
  constructor(client) { this.client = client; }

  async connect(onValue, onDisconnect) {
    await this.client.initialize();
    if (!await this.client.isEnabled()) throw new Error('Turn on Bluetooth in phone settings, then reconnect.');
    const device = await this.client.requestDevice({ services: [NUS_SERVICE] });
    this.id = device.deviceId;
    await this.client.connect(this.id, onDisconnect);
    const mtu = await this.client.getMtu(this.id);
    if (!Number.isFinite(mtu) || mtu < 23) throw new Error('Invalid BLE MTU reported by phone.');
    this.maxWriteBytes = mtu - 3;
    await this.client.startNotifications(this.id, NUS_SERVICE, NUS_TX, onValue);
    return { id: this.id, name: device.name, mtu: String(mtu) };
  }

  write(value) {
    // A GATT acknowledgement reports transport errors; firmware DDS OK/ERR
    // notifications still determine whether a command was accepted.
    return this.client.write(this.id, NUS_SERVICE, NUS_RX, value);
  }

  async disconnect() {
    if (!this.id) return;
    try { await this.client.stopNotifications(this.id, NUS_SERVICE, NUS_TX); }
    finally { await this.client.disconnect(this.id); }
  }
}

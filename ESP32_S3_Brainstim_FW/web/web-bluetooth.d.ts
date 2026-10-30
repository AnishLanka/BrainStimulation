// Minimal Web Bluetooth API declarations (included in Chrome/Edge but absent from TypeScript's default DOM lib).
interface Navigator {
  readonly bluetooth: Bluetooth;
}

interface Bluetooth extends EventTarget {
  requestDevice(options?: RequestDeviceOptions): Promise<BluetoothDevice>;
  getAvailability(): Promise<boolean>;
}

interface RequestDeviceOptions {
  filters?: BluetoothLEScanFilterInit[];
  optionalServices?: BluetoothServiceUUID[];
  acceptAllDevices?: boolean;
}

interface BluetoothLEScanFilterInit {
  services?: BluetoothServiceUUID[];
  name?: string;
  namePrefix?: string;
}

type BluetoothServiceUUID = string | number;

interface BluetoothDevice extends EventTarget {
  readonly id: string;
  readonly name?: string;
  readonly gatt?: BluetoothRemoteGATTServer;
  addEventListener(type: 'gattserverdisconnected', listener: EventListenerOrEventListenerObject, opts?: boolean | AddEventListenerOptions): void;
  removeEventListener(type: 'gattserverdisconnected', listener: EventListenerOrEventListenerObject, opts?: boolean | EventListenerOptions): void;
}

interface BluetoothRemoteGATTServer {
  readonly device: BluetoothDevice;
  readonly connected: boolean;
  connect(): Promise<BluetoothRemoteGATTServer>;
  disconnect(): void;
  getPrimaryService(service: BluetoothServiceUUID): Promise<BluetoothRemoteGATTService>;
}

interface BluetoothRemoteGATTService {
  getCharacteristic(characteristic: BluetoothServiceUUID): Promise<BluetoothRemoteGATTCharacteristic>;
}

interface BluetoothRemoteGATTCharacteristic extends EventTarget {
  readonly value?: DataView;
  writeValueWithResponse(value: BufferSource): Promise<void>;
  writeValueWithoutResponse(value: BufferSource): Promise<void>;
  startNotifications(): Promise<BluetoothRemoteGATTCharacteristic>;
  stopNotifications(): Promise<BluetoothRemoteGATTCharacteristic>;
  addEventListener(type: 'characteristicvaluechanged', listener: EventListenerOrEventListenerObject, opts?: boolean | AddEventListenerOptions): void;
  removeEventListener(type: 'characteristicvaluechanged', listener: EventListenerOrEventListenerObject, opts?: boolean | EventListenerOptions): void;
}

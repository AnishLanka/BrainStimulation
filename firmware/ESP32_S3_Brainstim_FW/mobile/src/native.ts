import { Capacitor } from '@capacitor/core';
import { BleClient } from '@capacitor-community/bluetooth-le';
import { type BleAdapter, NativeBleAdapter } from '../../web/ble_transport';

declare global { interface Window { ddsNativeAdapter?: () => BleAdapter; } }

if (Capacitor.isNativePlatform()) {
  window.ddsNativeAdapter = () => new NativeBleAdapter(BleClient);
}

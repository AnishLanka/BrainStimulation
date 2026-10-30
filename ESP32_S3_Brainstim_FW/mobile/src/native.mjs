import { Capacitor } from '@capacitor/core';
import { BleClient } from '@capacitor-community/bluetooth-le';
import { NusConnection, NativeBleAdapter } from '../../web/ble_transport.mjs';

if (Capacitor.isNativePlatform()) {
  window.ddsBle = new NusConnection(() => new NativeBleAdapter(BleClient));
}

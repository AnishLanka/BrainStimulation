import { existsSync } from 'node:fs';
import { readFile, writeFile } from 'node:fs/promises';

// Android permissions come from the BLE plugin's merged manifest.
// Keep the iOS purpose string in the generated native project, including on sync.
const plist = new URL('../ios/App/App/Info.plist', import.meta.url);
if (existsSync(plist)) {
  let text = await readFile(plist, 'utf8');
  if (!text.includes('<key>NSBluetoothAlwaysUsageDescription</key>')) {
    const end = text.lastIndexOf('</dict>');
    if (end < 0) throw new Error('Invalid iOS Info.plist.');
    text = text.slice(0, end) + '\t<key>NSBluetoothAlwaysUsageDescription</key>\n' +
      '\t<string>Connect to your R2D2 controller to send waveform settings and read device status.</string>\n' + text.slice(end);
    await writeFile(plist, text);
  }
  console.log('iOS Bluetooth permission description configured.');
}

import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';

const root      = fileURLToPath(new URL('../../', import.meta.url));
const distWeb   = new URL('../../web/dist/', import.meta.url);
const distMob   = new URL('../dist/', import.meta.url);
const webOnly   = process.argv.includes('--web');

await mkdir(distWeb, { recursive: true });
if (!webOnly) await mkdir(distMob, { recursive: true });

const html     = await readFile(new URL('../../web/ble_test.html', import.meta.url), 'utf8');
const htmlOut  = html.replace(/<script\b[^>]*>[\s\S]*?<\/script>/, '<script src="./app.js" defer></script>');

const common = {
  bundle:    true,
  format:    /** @type {const} */ ('iife'),
  target:    ['safari15', 'chrome89'],
  sourcemap: true,
};

// Web bundle — uses Web Bluetooth; no native adapter.
await build({
  ...common,
  entryPoints: [`${root}web/controller.ts`],
  outfile:     fileURLToPath(new URL('app.js', distWeb)),
});
await writeFile(new URL('index.html', distWeb), htmlOut);
console.log('Built web/dist/');

if (!webOnly) {
  // Mobile bundle — injects native BLE adapter before running the controller.
  await build({
    ...common,
    stdin: {
      contents:    `import './mobile/src/native.ts';\nimport './web/controller.ts';`,
      resolveDir:  root,
      sourcefile:  'mobile-entry.ts',
      loader:      'ts',
    },
    outfile: fileURLToPath(new URL('app.js', distMob)),
  });
  await writeFile(new URL('index.html', distMob), htmlOut);
  console.log('Built mobile/dist/');
}

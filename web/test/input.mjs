// Checks input reaches the app in a browser: loads the page (the orbiting first view), clicks the
// scene, presses keys, and reads the app's state through emerald_state(). Fails if the orbit does
// not stop, the camera does not move or turn, pointer lock is not requested from inside the
// click's event handler (browsers refuse it elsewhere), or the panel shortcuts do not toggle the
// panel. Headless Chromium cannot lock the pointer, so the request is recorded, not granted.
//
// Usage: node test/input.mjs [build dir]   (default ../build-web)

import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, resolve } from 'node:path';

const root = resolve(process.argv[2] ?? '../build-web');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const server = createServer(async (request, response) => {
  let path = join(root, decodeURIComponent(new URL(request.url, 'http://x').pathname));
  if (path.endsWith('/')) path = join(path, 'index.html');
  try {
    const body = await readFile(path);
    response.writeHead(200, { 'Content-Type': types[extname(path)] ?? 'application/octet-stream' });
    response.end(body);
  } catch {
    response.writeHead(404);
    response.end();
  }
}).listen(0);

const failures = [];
const check = (ok, what) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
  if (!ok) failures.push(what);
};

const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
// Small, at low quality: CI runners render WebGL on the CPU.
const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
await page.addInitScript(() => {
  window.pointerLockRequests = [];
  Element.prototype.requestPointerLock = function () {
    pointerLockRequests.push(window.event ? window.event.type : 'outside an event handler');
    return Promise.resolve();
  };
});
page.on('console', (m) => { if (m.type() === 'error') console.log(`[console error] ${m.text()}`); });
page.on('pageerror', (e) => failures.push(`page error: ${e.message}`));
await page.goto(`http://127.0.0.1:${server.address().port}/?view=glenbeigh&time=21:30&orbit&paused&quality=low`);
await page.waitForFunction(() => document.getElementById('loading')?.classList.contains('done'), null,
                           { timeout: 120_000 });

const state = () => page.evaluate(() => JSON.parse(Module.ccall('emerald_state', 'string', [], [])));
// Software rendering is slow: wait for the app to have drawn a few more frames.
const settle = async (frames = 4) => {
  const start = (await state()).frames;
  await page.waitForFunction((f) => JSON.parse(Module.ccall('emerald_state', 'string', [], [])).frames >= f,
                             start + frames, { timeout: 180_000 });
};
const hold = async (key, frames = 6) => {
  await page.keyboard.down(key);
  await settle(frames);
  await page.keyboard.up(key);
  await settle(2);
};

let s = await state();
check(s.orbiting, 'starts orbiting');
await page.mouse.click(400, 250);
await settle();
s = await state();
check(!s.orbiting, 'a click stops the orbit');
const requests = await page.evaluate(() => pointerLockRequests);
check(requests.length > 0 && requests.every((type) => /^(mouse|pointer)down$/.test(type)),
      `a click requests pointer lock from inside its event handler (${requests.join(', ') || 'no request'})`);

const before = await state();
await hold('KeyW');
const after = await state();
const moved = Math.hypot(after.x - before.x, after.y - before.y, after.z - before.z);
// Only meaningful once the orbit has stopped (it moves the camera by itself).
check(!before.orbiting && moved > 1, `W moves the camera (${moved.toFixed(1)} m)`);

await hold('ArrowLeft');
check(!after.orbiting && Math.abs((await state()).yaw - after.yaw) > 1e-3, 'arrow keys turn the camera');

const beforeScroll = await state();
await page.mouse.wheel(0, 200);
await settle(6);
const afterScroll = await state();
check(Math.abs(afterScroll.yaw - beforeScroll.yaw) + Math.abs(afterScroll.pitch - beforeScroll.pitch) > 1e-3,
      'scrolling (wheel or trackpad) turns the camera');

for (const key of ['KeyH', 'Backquote', 'F1']) {
  const was = (await state()).panel;
  await page.keyboard.press(key);
  await settle();
  check((await state()).panel !== was, `${key} toggles the panel`);
}

// The panel is open now (H, then ` and F1, toggled it three times): clicks on it are for the panel.
check((await state()).panel, 'the panel is open after three toggles');
const requestsBefore = (await page.evaluate(() => pointerLockRequests)).length;
await page.mouse.click(60, 30);
await settle();
check((await page.evaluate(() => pointerLockRequests)).length === requestsBefore,
      'a click on the panel does not capture the pointer');

await browser.close();
server.close();
if (failures.length > 0) {
  console.error(`\nInput test FAILED: ${failures.length} check(s)`);
  process.exit(1);
}
console.log('\nInput test passed');

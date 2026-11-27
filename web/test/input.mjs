// Checks input reaches the app in a browser: loads the page (the orbiting first view), clicks the
// scene, presses keys, and reads the app's state through emerald_state(). Fails if the orbit does
// not stop, the camera does not move or turn, pointer lock is not requested from inside the
// click's event handler (browsers refuse it elsewhere), or the panel shortcuts do not toggle the
// panel. Headless Chromium cannot lock the pointer, so the request is recorded, not granted; a
// lock is then faked to check that locked mouse movement turns the camera by the full amount.
// Then, as an iPhone (touch, coarse pointer): touch mode at Low quality, the first gesture stops
// the orbit, a drag rotates, a pinch and a two-finger drag move the camera, each overlay button
// changes the state, and the page never scrolls.
//
// Usage: node test/input.mjs [build dir]   (default ../build-web)

import { chromium, devices } from 'playwright';
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
  let locked = null;
  Object.defineProperty(Document.prototype, 'pointerLockElement', { get() { return locked; } });
  window.fakePointerLock = (on) => {
    locked = on ? document.getElementById('canvas') : null;
    document.dispatchEvent(new Event('pointerlockchange'));
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

// Locked movement arrives as movementX/Y only: 40 moves of 5 px, a few per frame, must turn the
// camera by 200 px at 0.0025 rad per px.
await page.evaluate(() => fakePointerLock(true));
await settle(2);
const beforeLook = await state();
await page.evaluate(async () => {
  for (let i = 0; i < 40; ++i) {
    document.dispatchEvent(new MouseEvent('mousemove', { movementX: 5, bubbles: true }));
    if (i % 3 === 2) await new Promise((r) => requestAnimationFrame(r));
  }
});
await settle(3);
const lookPx = ((await state()).yaw - beforeLook.yaw) / 0.0025;
check(beforeLook.captured && Math.abs(lookPx - 200) < 1, `locked mouse movement turns the camera fully (${lookPx.toFixed(1)} of 200 px)`);
await page.evaluate(() => fakePointerLock(false));
await settle(2);

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

await page.close();

// ---- Touch screen ----
console.log('\nAs an iPhone:');
const phone = await browser.newContext({ ...devices['iPhone 13'] });
const touchPage = await phone.newPage();
await touchPage.addInitScript(() => {
  window.pointerLockRequests = [];
  Element.prototype.requestPointerLock = function () {
    pointerLockRequests.push(window.event ? window.event.type : 'outside an event handler');
    return Promise.resolve();
  };
});
touchPage.on('pageerror', (e) => failures.push(`page error: ${e.message}`));
await touchPage.goto(`http://127.0.0.1:${server.address().port}/`);
await touchPage.waitForFunction(() => document.getElementById('loading')?.classList.contains('done'), null,
                                { timeout: 120_000 });
const touchState = () => touchPage.evaluate(() => JSON.parse(Module.ccall('emerald_state', 'string', [], [])));
const touchSettle = async (frames = 4) => {
  const start = (await touchState()).frames;
  await touchPage.waitForFunction((f) => JSON.parse(Module.ccall('emerald_state', 'string', [], [])).frames >= f,
                                  start + frames, { timeout: 180_000 });
};
// Multi-touch goes through the DevTools protocol: Playwright's touchscreen only taps.
const cdp = await phone.newCDPSession(touchPage);
const touchEvent = (type, points) => cdp.send('Input.dispatchTouchEvent', {
  type, touchPoints: points.map(([x, y], id) => ({ x, y, id })) });
const gesture = async (from, to, steps = 8) => {
  await touchEvent('touchStart', from);
  for (let i = 1; i <= steps; ++i) {
    await touchEvent('touchMove', from.map(([x, y], k) => [x + (to[k][0] - x) * i / steps, y + (to[k][1] - y) * i / steps]));
  }
  await touchEvent('touchEnd', []);
  await touchSettle(3);
};
const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);

let t = await touchState();
check(t.touch && t.quality === 'Low', `touch mode at Low quality (${t.quality})`);
check(t.orbiting, 'starts orbiting');
check(await touchPage.isVisible('#touchbar') && !(await touchPage.isVisible('#panel-button')),
      'the overlay replaces the panel button');
await touchPage.evaluate(() => document.getElementById('intro').classList.remove('hidden'));
check(await touchPage.isVisible('#intro .touch-only') && !(await touchPage.isVisible('#intro .desktop-only')),
      'the hint is the touch one');

await gesture([[195, 300]], [[215, 300]]);
const stopped = await touchState();
await touchSettle(6);
check(distance(await touchState(), stopped) < 1e-3, 'the first gesture stops the automatic orbit');

const beforeDrag = await touchState();
await gesture([[120, 320]], [[270, 340]]);
const afterDrag = await touchState();
check(afterDrag.orbiting && Math.abs(afterDrag.yaw - beforeDrag.yaw) > 1e-3, 'a one-finger drag rotates around the view');

await gesture([[170, 320], [220, 320]], [[110, 320], [280, 320]]);
const afterPinch = await touchState();
check(distance(afterPinch, afterDrag) > 1, `a pinch moves the camera (${distance(afterPinch, afterDrag).toFixed(0)} m)`);

await gesture([[160, 300], [230, 300]], [[160, 380], [230, 380]]);
const afterPan = await touchState();
check(distance(afterPan, afterPinch) > 1, `a two-finger drag pans (${distance(afterPan, afterPinch).toFixed(0)} m)`);

for (const [id, field] of [['#view-button', 'view'], ['#time-button', 'minutes'], ['#rain-button', 'rain']]) {
  const was = (await touchState())[field];
  const label = await touchPage.textContent(`${id} span`);
  await touchPage.tap(id);
  await touchSettle(2);
  const now = (await touchState())[field];
  const newLabel = await touchPage.textContent(`${id} span`);
  check(now !== was && newLabel !== label, `${id.slice(1)} changes ${field} (${was} to ${now}; "${label}" to "${newLabel}")`);
}
check(await touchPage.evaluate(() => (document.scrollingElement.scrollTop | window.scrollY) === 0), 'the page never scrolls');
check((await touchPage.evaluate(() => pointerLockRequests)).length === 0, 'no pointer lock on a touch screen');
await phone.close();

await browser.close();
server.close();
if (failures.length > 0) {
  console.error(`\nInput test FAILED: ${failures.length} check(s)`);
  process.exit(1);
}
console.log('\nInput test passed');

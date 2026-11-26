// Loads the web build in headless Chromium (WebGL 2 through SwiftShader, no GPU needed), waits
// until the app is rendering, saves a screenshot, and fails on any console error, page error or
// failed request.
//
// Usage: node test/smoke.mjs [build dir] [seconds to run]   (defaults: ../build-web, 15)

import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { extname, join, resolve } from 'node:path';

const root = resolve(process.argv[2] ?? '../build-web');
const seconds = Number(process.argv[3] ?? 15);
// The first view, at low quality to keep software rendering quick (options replace the defaults).
const query = process.argv[4] ?? '?view=glenbeigh&time=21:30&orbit&paused&quality=low';
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
                '.data': 'application/octet-stream' };

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
const url = `http://127.0.0.1:${server.address().port}/`;

const problems = [];
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
// Small: CI runners render WebGL on the CPU, a few seconds a frame at full size.
const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
let warmedUp = false;
page.on('console', (message) => {
  const text = message.text();
  console.log(`[${message.type()}] ${text}`);
  if (message.type() === 'error') problems.push(`console error: ${text}`);
  // WebGL reports misuse (GL_INVALID_OPERATION and the like) as warnings.
  if (/GL_INVALID|GL ERROR/.test(text)) problems.push(`WebGL error: ${text}`);
  if (text.startsWith('Warm-up:')) warmedUp = true;
});
page.on('pageerror', (error) => problems.push(`page error: ${error.message}`));
page.on('requestfailed', (request) => problems.push(`request failed: ${request.url()}`));
page.on('response', (response) => {
  if (response.status() >= 400) problems.push(`HTTP ${response.status()}: ${response.url()}`);
});

await page.goto(url + query);
const deadline = Date.now() + 120_000;
while (!warmedUp && problems.length === 0 && Date.now() < deadline) {
  await page.waitForTimeout(250);
}
if (!warmedUp && problems.length === 0) problems.push('the app did not start within 120 s');
if (problems.length === 0) {
  await page.waitForTimeout(seconds * 1000);
}
// The screenshot waits for the page to paint, which software rendering can make slow; it is an
// aid, not part of the test.
await mkdir('test-results', { recursive: true });
try {
  await page.screenshot({ path: 'test-results/smoke.png', timeout: 120_000 });
} catch (error) {
  console.warn(`No screenshot: ${error.message.split('\n')[0]}`);
}
await browser.close();
server.close();

if (problems.length > 0) {
  console.error(`\nSmoke test FAILED:\n  ${problems.join('\n  ')}`);
  process.exit(1);
}
console.log('\nSmoke test passed; screenshot in test-results/smoke.png');

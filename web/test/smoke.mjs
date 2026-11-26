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
const query = process.argv[4] ?? ''; // e.g. "?view=shore&rain=1"
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
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
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
await mkdir('test-results', { recursive: true });
await page.screenshot({ path: 'test-results/smoke.png' });
await browser.close();
server.close();

if (problems.length > 0) {
  console.error(`\nSmoke test FAILED:\n  ${problems.join('\n  ')}`);
  process.exit(1);
}
console.log('\nSmoke test passed; screenshot in test-results/smoke.png');

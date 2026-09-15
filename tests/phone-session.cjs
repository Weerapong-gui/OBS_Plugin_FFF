const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const puppeteer = require('puppeteer-core');
const streams = new Set();
const votes = new Map();
let connections = 0;
const screenshots = process.env.FFF_SCREENSHOT_DIR || '/private/tmp/fff-phone-review';
let delayVote = false, releaseVote, voteReceived;
const state = (name, vote = 'none') => ({ you: { name, school: 'สำนักทดสอบ', vote }, round: 3, phase: 'collecting', total: 2, voted: vote === 'none' ? 0 : 1 });
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (url.pathname === '/api/events') {
    connections++;
    res.writeHead(200, { 'Content-Type': 'text/event-stream' });
    res.write('data: ' + JSON.stringify(state(url.searchParams.get('token'), votes.get(url.searchParams.get('token')))) + '\n\n');
    streams.add(res); req.on('close', () => streams.delete(res)); return;
  }
  if (req.method === 'POST') {
    let body = ''; for await (const chunk of req) body += chunk;
    const data = JSON.parse(body);
    if (req.url === '/api/auth') {
      if (!['111111','222222'].includes(data.pin)) { res.writeHead(401); res.end(); return; }
      res.end(JSON.stringify({ token: data.pin, state: state(data.pin, votes.get(data.pin)) })); return;
    }
    if (delayVote) { voteReceived?.(); await new Promise(resolve => { releaseVote = resolve; }); }
    votes.set(data.token, data.color);
    res.end(JSON.stringify(state(data.token, data.color))); return;
  }
  const file = url.pathname === '/app.css' ? 'app.css' : 'phone.html';
  res.setHeader('Content-Type', file.endsWith('css') ? 'text/css' : 'text/html');
  res.end(fs.readFileSync(path.resolve(__dirname, '../data/web', file)));
});
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const browser = await puppeteer.launch({ executablePath: process.env.FFF_BROWSER || '/Applications/Brave Browser.app/Contents/MacOS/Brave Browser', headless: true });
  try {
    const page = await browser.newPage();
    fs.mkdirSync(screenshots, { recursive: true });
    // Deliberately let the old vote resolve even after abort to exercise the
    // generation guard independently of the browser's cancellation behavior.
    await page.evaluateOnNewDocument(() => {
      const originalFetch = window.fetch;
      window.completedVoteBodies = 0;
      window.fetch = async (url, options) => {
        const response = await originalFetch(url, url === '/api/vote' ? { ...options, signal: undefined } : options);
        if (url === '/api/vote') {
          const originalJson = response.json.bind(response);
          response.json = async () => {
            const body = await originalJson();
            window.completedVoteBodies++;
            return body;
          };
        }
        return response;
      };
    });
    const errors = []; page.on('pageerror', error => errors.push(error.message));
    await page.setViewport({ width: 320, height: 568 });
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    await page.type('#pin', '999999'); await page.click('#enter');
    await page.waitForSelector('#loginError:not(.hidden)');
    await page.screenshot({ path: path.join(screenshots, 'phone-wrong-pin.png'), fullPage: true });
    await page.$eval('#pin', el => { el.value = '111111'; }); await page.click('#enter');
    await page.waitForSelector('#vote:not(.hidden)');
    assert.equal(await page.$('#logout') !== null, true, 'mobile needs an explicit logout control');
    await page.click('#logout'); await page.waitForSelector('#logoutDialog[open]');
    await page.screenshot({ path: path.join(screenshots, 'phone-logout-dialog.png'), fullPage: true });
    await page.click('#cancelLogout');
    assert.equal(await page.$eval('#logoutDialog', el => el.open), false);
    assert.equal(await page.$eval('#vote', el => el.classList.contains('hidden')), false);
    delayVote = true;
    const sent = new Promise(resolve => { voteReceived = resolve; });
    await page.click('#red'); await sent;
    assert.match(await page.$eval('#status', el => el.textContent), /กำลังส่ง/);
    await page.click('#logout'); await page.click('#confirmLogout');
    await page.waitForSelector('#login:not(.hidden)');
    assert.equal(await page.$eval('#pin', el => el.value), '');
    assert.equal(await page.evaluate(() => localStorage.getItem('fff-token')), null);
    assert.equal(await page.evaluate(() => localStorage.getItem('fff-pin')), null);
    await page.type('#pin', '222222'); await page.click('#enter');
    await page.waitForFunction(() => document.querySelector('#name').textContent === '222222');
    const oldResponse = page.waitForResponse(response => response.url().endsWith('/api/vote'));
    releaseVote(); delayVote = false;
    await (await oldResponse).text();
    await page.waitForFunction(() => window.completedVoteBodies === 1);
    assert.equal(await page.$eval('#name', el => el.textContent), '222222', 'old response must not paint the new account');
    assert.equal(await page.$eval('#red', el => el.getAttribute('aria-pressed')), 'false');
    await page.click('#green');
    await page.waitForFunction(() => !pendingVote && confirmed === 'green');
    assert.match(await page.$eval('#round', el => el.textContent), /3/);
    delayVote = true;
    const oppositeSent = new Promise(resolve => { voteReceived = resolve; });
    await page.click('#red'); await oppositeSent;
    await page.click('#green');
    for (const stream of streams) stream.write('data: ' + JSON.stringify({ ...state('222222', 'green'), total: 3 }) + '\n\n');
    await page.waitForFunction(() => lastState.total === 3);
    assert.equal(await page.evaluate(() => pendingVote), true, 'old-green SSE cannot acknowledge a choice while red is in flight');
    const oppositeResponse = page.waitForResponse(response => response.url().endsWith('/api/vote'));
    delayVote = false; releaseVote();
    await (await oppositeResponse).text();
    await page.waitForFunction(() => window.completedVoteBodies >= 3);
    await page.waitForFunction(() => !sending && !pendingVote);
    assert.equal(await page.$eval('#green', el => el.getAttribute('aria-pressed')), 'true', 'latest green intent survives an older red response');
    assert.equal(votes.get('222222'), 'green', 'latest intent must reach the server');
    const previousConnections = connections;
    for (const stream of streams) stream.end();
    await page.waitForFunction(() => document.querySelector('#connection').textContent.includes('ขาดการเชื่อมต่อ'));
    await page.waitForFunction(() => document.querySelector('#connection').textContent === 'เชื่อมต่อแล้ว');
    assert.ok(connections > previousConnections, 'SSE must reconnect');
    assert.equal(await page.$eval('#name', el => el.textContent), '222222');
    assert.equal(await page.$eval('#green', el => el.getAttribute('aria-pressed')), 'true');
    for (const size of [{width:320,height:568},{width:667,height:375}]) {
      await page.setViewport(size);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
      await page.screenshot({ path: path.join(screenshots, `phone-${size.width}x${size.height}.png`), fullPage: true });
    }
    assert.deepEqual(errors, []);
    console.log('Phone session browser checks passed');
  } finally { releaseVote?.(); for (const stream of streams) stream.end(); await browser.close(); server.closeAllConnections(); server.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

// Focused real-browser checks for cover geometry and interruptible transitions.
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const puppeteer = require("puppeteer-core");
const web = path.resolve(__dirname, "../data/web");
const PNG = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScL3TAAAAABJRU5ErkJggg==";
const state = { phase: "revealed", round: 1, displayMode: "bottomBar", voted: 1, total: 2,
  layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {},
  bottomBar: { coverUrl: PNG, pieces: {}, layers: {}, logoPresidentId: "a" },
  presidents: [{ id: "a", name: "A", school: "School", vote: "green", bottomBarUrl: PNG, logoUrl: PNG },
    { id: "b", name: "B", school: "School", vote: "none", bottomBarUrl: "" }] };
const server = http.createServer((req, res) => {
  const file = path.join(web, req.url === "/monitor" ? "monitor.html" : req.url === "/overlay" ? "overlay.html" : req.url.slice(1));
  if (!file.startsWith(web + path.sep) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.setHeader("Content-Type", file.endsWith(".js") ? "text/javascript" : file.endsWith(".css") ? "text/css" : "text/html");
  res.end(fs.readFileSync(file));
});
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
(async () => {
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const browser = await puppeteer.launch({ executablePath: process.env.FFF_BROWSER || "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser", headless: true, args: ["--no-sandbox"] });
  const errors = [];
  try {
    const overlay = await browser.newPage(), monitor = await browser.newPage();
    for (const [page, route] of [[overlay, "overlay"], [monitor, "monitor"]]) {
      page.on("pageerror", error => errors.push(error.message));
      await page.evaluateOnNewDocument(() => { window.EventSource = class {}; });
      await page.setViewport({ width: 1920, height: 1080 });
      await page.goto(`http://127.0.0.1:${server.address().port}/${route}`);
    }
    const push = async () => {
      await overlay.evaluate(s => render(s), state);
      await monitor.evaluate(s => { editMode = "bottomBar"; render(s); }, state);
    };
    await push();
    assert.equal(await overlay.evaluate(() => board.getAnimations({ subtree: true }).length), 4);
    const continuous = await overlay.evaluate(s => {
      const before = board.getAnimations({ subtree: true });
      for (const animation of before) { animation.pause(); animation.currentTime = 100; }
      s.presidents[1].vote = "red";
      s.bottomBar.coverUrl = "";
      render(s);
      const after = board.getAnimations({ subtree: true });
      const result = before.length === after.length && before.every(a => after.includes(a) && a.currentTime === 100);
      for (const animation of before) animation.play();
      return result;
    }, state);
    assert.equal(continuous, true, "vote during entrance preserves animation identity and progress");
    await overlay.waitForFunction(() => board.getAnimations({ subtree: true }).length === 0, { polling: 20 });
    const coverInfo = page => page.evaluate(() => {
      const p = document.querySelector('[data-target="cover"]');
      return { width: p.offsetWidth, height: p.offsetHeight, transform: p.style.transform,
        opacity: p.querySelector("img").style.opacity, fit: getComputedStyle(p.querySelector("img")).objectFit,
        z: p.style.zIndex, pointer: getComputedStyle(p).pointerEvents };
    });
    assert.deepEqual(await overlay.evaluate(() => ["logo", "card:a", "card:b", "cover"].map(target =>
      document.querySelector(`[data-target="${target}"]`).style.zIndex)), ["0", "1", "2", "3"], "default stacking agrees with native layer actions");
    const cover = await coverInfo(overlay);
    assert.equal(cover.width, 1920); assert.equal(cover.height, 1080); assert.equal(cover.opacity, "1");
    assert.equal(cover.fit, "contain"); assert.equal(cover.z, "3");
    assert.equal((await coverInfo(monitor)).transform, cover.transform);
    assert.equal((await coverInfo(monitor)).pointer, "none");
    await monitor.select("#selection", "cover");
    assert.equal((await coverInfo(monitor)).pointer, "auto");
    assert.equal(await monitor.$eval("#resultOpacity", el => el.disabled), true);
    assert.equal(await monitor.evaluate(() => board.getAnimations({ subtree: true }).length), 0);
    for (const opacity of [0, 0.5, 1]) {
      state.bottomBar.pieces.cover = { x: 0.5, y: 0.5, scaleX: 1, scaleY: 1, imageOpacity: opacity };
      state.bottomBar.cardTemplate = { image: { x: 0, y: 0, width: 850, height: 250, opacity: 0.2 }, result: { x: 0, y: 0, width: 850, height: 250 }, order: ["result", "image"] };
      await push();
      assert.equal((await coverInfo(overlay)).opacity, String(opacity));
      assert.equal((await coverInfo(monitor)).opacity, String(opacity));
      assert.equal(await overlay.evaluate(() => board.getAnimations({ subtree: true }).length), 0, "edits do not replay entrance");
    }
    delete state.bottomBar.pieces.cover;
    await push(); assert.equal((await coverInfo(overlay)).opacity, "1", "reset ignores template opacity");
    // Synthetic transparent, non-16:9 PNG tests alpha and aspect handling.
    state.bottomBar.coverUrl = await overlay.evaluate(() => {
      const canvas = document.createElement("canvas"); canvas.width = 4; canvas.height = 2;
      canvas.getContext("2d").fillRect(1, 0, 2, 2);
      return canvas.toDataURL("image/png");
    });
    await push();
    for (const page of [overlay, monitor]) {
      await page.waitForFunction(() => document.querySelector('[data-target="cover"] img').naturalWidth === 4, { polling: 20 });
      assert.deepEqual(await page.$eval('[data-target="cover"] img', img => {
        const c = document.createElement("canvas"); c.width = 4; c.height = 2;
        const ctx = c.getContext("2d"); ctx.drawImage(img, 0, 0);
        return [img.naturalWidth / img.naturalHeight, ctx.getImageData(0, 0, 1, 1).data[3], getComputedStyle(img).objectFit];
      }), [2, 0, "contain"]);
    }
    state.bottomBar.coverUrl = "";
    await push(); assert.equal(await overlay.$eval('[data-target="cover"] img', el => el.hidden && !el.hasAttribute("src")), true);
    state.bottomBar.coverUrl = PNG; state.bottomBar.layers.cover = -1;
    await push(); assert.equal((await coverInfo(overlay)).z, "-1");
    state.phase = "collecting"; state.round++; state.presidents[0].vote = "none";
    await push();
    assert.equal(await overlay.$eval('[data-target="card:a"] .slot', el => el.classList.contains("green")), true, "clear freezes old flag");
    assert.equal(await overlay.$eval("#wrap", el => el.hidden), false);
    state.presidents[0].vote = "red";
    await push();
    assert.equal(await overlay.$eval('[data-target="card:a"] .slot', el => el.classList.contains("green")), true, "votes cannot alter exit snapshot");
    await overlay.waitForFunction(() => wrap.hidden, { polling: 20 });
    assert.equal(await overlay.$eval("#wrap", el => el.hidden), true);
    state.phase = "revealed"; await push();
    await pause(35);
    state.displayMode = "scoreboard"; await push();
    await pause(35);
    state.displayMode = "bottomBar"; await push();
    await overlay.waitForFunction(() => !transition, { polling: 20 });
    assert.equal(await overlay.$eval("#board", el => el.classList.contains("bottom-bar")), true);
    assert.equal(await overlay.$eval("#wrap", el => el.hidden), false);
    assert.equal(await overlay.evaluate(() => board.getAnimations({ subtree: true }).length), 0);
    state.displayMode = "scoreboard"; await push();
    await overlay.waitForFunction(() => !board.classList.contains("bottom-bar"), { polling: 20 });
    assert.equal(await overlay.$eval("#board", el => el.classList.contains("bottom-bar")), false);
    assert.equal(await overlay.$('[data-target="cover"]'), null);
    // Old sessions have no cover and still render their bottom bar.
    state.displayMode = "bottomBar"; delete state.bottomBar.coverUrl;
    await push(); await overlay.waitForFunction(() => !transition, { polling: 20 });
    assert.equal(await overlay.$eval('[data-target="cover"] img', el => el.hidden), true);
    // Clear and immediate reveal must finish the outgoing snapshot first.
    await overlay.evaluate(s => {
      window.savedTimeouts = [];
      const original = window.setTimeout;
      window.setTimeout = (callback, delay, ...args) => {
        if (delay === 200 || delay === 300) savedTimeouts.push({ callback, delay });
        return original(callback, delay, ...args);
      };
      render({ ...s, phase: "collecting", round: s.round + 1 });
      render({ ...s, phase: "revealed", round: s.round + 1 });
    }, state);
    assert.equal(await overlay.evaluate(() => transition.entering), false);
    await overlay.waitForFunction(() => transition?.entering, { polling: 10 });
    const staleIgnored = await overlay.evaluate(() => {
      const active = transition;
      savedTimeouts.find(t => t.delay === 200).callback();
      return transition === active && !wrap.hidden;
    });
    assert.equal(staleIgnored, true, "old exit callback cannot hide next entrance");
    await overlay.waitForFunction(() => !transition, { polling: 20 });
    state.displayMode = "scoreboard"; state.phase = "collecting"; state.round += 2;
    await push();
    await overlay.waitForFunction(() => wrap.hidden, { polling: 20 });
    state.phase = "revealed";
    const scoreboardContinuous = await overlay.evaluate(s => {
      render(s);
      const before = board.getAnimations({ subtree: true });
      for (const animation of before) { animation.pause(); animation.currentTime = 100; }
      s.presidents[0].vote = "green"; render(s);
      const after = board.getAnimations({ subtree: true });
      return before.length > 0 && before.every(a => after.includes(a) && a.currentTime === 100);
    }, state);
    assert.equal(scoreboardContinuous, true, "scoreboard vote preserves reveal animation");
    assert.deepEqual(errors, []);
    console.log("bottom-bar cover and transition browser checks passed");
  } finally { await browser.close(); server.close(); }
})().catch(error => { console.error(error); server.close(); process.exitCode = 1; });

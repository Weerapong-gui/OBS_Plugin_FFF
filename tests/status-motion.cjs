// Show Status motion: cards slide in from the left one after another and leave
// to the right in the same order, moving only compositor properties.
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const puppeteer = require("puppeteer-core");

const web = path.resolve(__dirname, "../data/web");
const PNG = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScL3TAAAAABJRU5ErkJggg==";
const server = http.createServer((req, res) => {
  const name = req.url === "/overlay" ? "overlay.html" : req.url.slice(1);
  const file = path.join(web, name);
  if (!file.startsWith(web + path.sep) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.setHeader("Content-Type", file.endsWith(".js") ? "text/javascript" : file.endsWith(".css") ? "text/css" : "text/html");
  res.end(fs.readFileSync(file));
});
const roster = count => ({
  phase: "collecting", round: 1, displayMode: "scoreboard", voted: 0, total: count,
  layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {},
  bottomBar: { pieces: {}, layers: {} },
  presidents: Array.from({ length: count }, (_, i) => ({ id: String(i), name: "P" + i, school: "S" + i,
    vote: "none", status: "waiting", cardUrl: PNG, statusUrl: PNG, bottomBarUrl: "", logoUrl: "" }))
});
const installHelper = () => {
  // Script animations on each Show Status card, in roster order; CSS pops are
  // the status-change replay and are read separately.
  window.slotMotions = () => [...board.querySelectorAll(":scope > .piece")].map(piece => {
    const animation = piece.firstElementChild.getAnimations().find(a => !(a instanceof CSSAnimation || a instanceof CSSTransition));
    if (!animation) return null;
    const frames = animation.effect.getKeyframes(), timing = animation.effect.getTiming();
    const ignored = new Set(["offset", "easing", "composite", "computedOffset"]);
    return { delay: timing.delay, duration: timing.duration, easing: timing.easing,
      first: frames[0].transform, last: frames.at(-1).transform,
      properties: [...new Set(frames.flatMap(frame => Object.keys(frame).filter(key => !ignored.has(key))))].sort() };
  });
};

(async () => {
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const browser = await puppeteer.launch({
    executablePath: process.env.FFF_BROWSER || "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
    headless: true, args: ["--no-sandbox"] });
  const errors = [];
  try {
    const page = await browser.newPage();
    page.on("pageerror", error => errors.push(error.message));
    await page.evaluateOnNewDocument(() => { window.EventSource = class {}; });
    await page.evaluateOnNewDocument(installHelper);
    await page.setViewport({ width: 1920, height: 1080 });
    await page.goto(`http://127.0.0.1:${server.address().port}/overlay`);

    const five = roster(5);
    await page.evaluate(s => render(s), five);
    assert.equal(await page.evaluate(() => getComputedStyle(board.querySelector(":scope > .piece > .slot")).willChange),
      "transform, opacity", "cards on deck are promoted before the entrance");

    const entrance = await page.evaluate(s => {
      render({ ...s, phase: "revealed" });
      return { hidden: wrap.hidden, motions: slotMotions() };
    }, five);
    assert.equal(entrance.hidden, false, "the press shows the board in the same task");
    entrance.motions.forEach((motion, i) => {
      assert.ok(motion, `card ${i} animates`);
      assert.equal(motion.delay, i * 120, `card ${i} enters after the one before it`);
      assert.equal(motion.duration, 600);
      assert.equal(motion.easing, "cubic-bezier(0.22, 1, 0.36, 1)");
      assert.equal(motion.first, "translateX(-48px)", "cards come in from the left");
      assert.deepEqual(motion.properties, ["opacity", "transform"], "only compositor properties move");
    });
    await page.waitForFunction(() => !transition, { polling: 20 });
    assert.deepEqual(await page.evaluate(() => slotMotions()), [null, null, null, null, null], "cards rest without animations");

    const changed = { ...five, phase: "revealed", presidents: five.presidents.map((p, i) => i === 2 ? { ...p, status: "qualified" } : p) };
    const replay = await page.evaluate(s => {
      render(s);
      return [...board.querySelectorAll(":scope > .piece > .slot")].map(slot => slot.classList.contains("reveal-in"));
    }, changed);
    assert.deepEqual(replay, [false, false, true, false, false], "a status change on air replays only that card");

    const exit = await page.evaluate(s => {
      render({ ...s, phase: "collecting" });
      return { entering: transition && transition.entering, hidden: wrap.hidden, motions: slotMotions() };
    }, changed);
    assert.equal(exit.entering, false, "hiding plays the exit");
    assert.equal(exit.hidden, false, "cards leave before the board hides");
    exit.motions.forEach((motion, i) => {
      assert.equal(motion.delay, i * 60, `card ${i} leaves after the one before it`);
      assert.equal(motion.duration, 320);
      assert.equal(motion.last, "translateX(48px)", "cards leave to the right");
      assert.deepEqual(motion.properties, ["opacity", "transform"]);
    });
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    const fourteen = roster(14);
    await page.evaluate(s => render(s), fourteen);
    const longEntrance = await page.evaluate(s => { render({ ...s, phase: "revealed" }); return slotMotions().map(m => m.delay); }, fourteen);
    assert.ok(Math.abs(longEntrance[13] - 1200) < 0.001 && longEntrance.every((d, i) => i === 0 || d > longEntrance[i - 1]),
      "a long roster still starts every card within 1.2 s, in order");
    await page.waitForFunction(() => !transition, { polling: 20 });
    const longExit = await page.evaluate(s => { render(s); return slotMotions().map(m => m.delay); }, fourteen);
    assert.ok(Math.abs(longExit[13] - 600) < 0.001, "and leaves within 0.6 s");
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    await page.evaluate(s => render({ ...s, phase: "revealed" }), five);
    await page.waitForFunction(() => !transition, { polling: 20 });
    const switching = await page.evaluate(s => {
      render({ ...s, phase: "revealed", displayMode: "bottomBar" });
      return { entering: transition.entering, scoreboardShown: !views.scoreboard.wrap.hidden };
    }, five);
    assert.deepEqual(switching, { entering: false, scoreboardShown: true }, "switching to BOTTOM BAR lets the cards leave first");
    await page.waitForFunction(() => views.scoreboard.wrap.hidden && !views.bottomBar.wrap.hidden, { polling: 20 });

    assert.deepEqual(errors, []);
    console.log("PASS: Show Status slides in and out card by card on compositor properties");
  } finally {
    await browser.close();
    server.close();
  }
})().catch(error => { console.error(error); server.close(); process.exitCode = 1; });

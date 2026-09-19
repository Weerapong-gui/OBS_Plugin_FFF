// Monitor page furniture against a fixture server: pinned tools, side tabs and
// card controls that stay put. No OBS config is touched.
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const puppeteer = require("puppeteer-core");

const web = path.resolve(__dirname, "../data/web");
const state = {
  phase: "collecting", round: 2, total: 3, voted: 1, displayMode: "scoreboard",
  layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {},
  bottomBar: { layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {} },
  presidents: [0, 1, 2].map((i) => ({
    id: String(i), name: "นายก " + i, school: "สำนัก " + i, cardUrl: "",
    vote: i === 0 ? "green" : "none", status: "waiting", statusUrl: ""
  }))
};
let accessStatus = 204;
let layoutRequests = [];
let timingRequests = [];
let timingStatusCode = 200;
let capabilityOnAir = true;
let airRequests = [];
const streams = new Set();
const push = () => { for (const res of streams) res.write(`data: ${JSON.stringify(state)}\n\n`); };

const server = http.createServer((req, res) => {
  if (req.url === "/api/events/overlay") {
    if (accessStatus === 403) { res.writeHead(403); res.end(); return; }
    res.writeHead(200, { "Content-Type": "text/event-stream", "Cache-Control": "no-store" });
    res.write("retry: 200\n\n");
    res.write(`data: ${JSON.stringify(state)}\n\n`);
    streams.add(res);
    req.on("close", () => streams.delete(res));
    return;
  }
  if (req.url === "/api/monitor/access") { res.writeHead(accessStatus); res.end(); return; }
  if (req.method === "POST" && req.url === "/api/timing") {
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      timingRequests.push(JSON.parse(body));
      res.writeHead(timingStatusCode, { "Content-Type": "application/json" });
      res.end(timingStatusCode === 200 ? '{"ok":true}' : '{"error":"save failed"}');
    });
    return;
  }
  if (req.method === "POST" && req.url === "/api/layout") {
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      layoutRequests.push(JSON.parse(body));
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end('{"ok":true}');
    });
    return;
  }
  if (req.url === "/api/monitor/capabilities") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ onAir: capabilityOnAir }));
    return;
  }
  if (req.method === "POST" && (req.url === "/api/display" || req.url === "/api/round")) {
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      airRequests.push({ url: req.url, body: JSON.parse(body) });
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end('{"ok":true}');
    });
    return;
  }
  const file = { "/monitor": "monitor.html", "/app.css": "app.css", "/board.js": "board.js", "/timing.js": "timing.js",
    "/template-editor.js": "template-editor.js", "/view-zoom.js": "view-zoom.js",
    "/monitor-ui.js": "monitor-ui.js" }[req.url];
  if (!file) { res.writeHead(404); res.end(); return; }
  res.setHeader("Content-Type", file.endsWith(".js") ? "application/javascript" :
    file.endsWith(".css") ? "text/css" : "text/html");
  res.end(fs.readFileSync(path.join(web, file)));
});

async function setEditMode(page, mode) {
  await page.evaluate((value) => {
    const radio = document.querySelector(`input[name="editMode"][value="${value}"]`);
    radio.checked = true;
    radio.dispatchEvent(new Event("change", { bubbles: true }));
  }, mode);
}

async function main() {
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  const browser = await puppeteer.launch({
    executablePath: process.env.FFF_BROWSER || "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
    headless: true, args: ["--no-first-run", "--disable-background-networking"]
  });
  const errors = [];
  try {
    const page = await browser.newPage();
    page.on("pageerror", (error) => errors.push(error.message));
    await page.setViewport({ width: 1440, height: 900 });
    await page.goto(base + "/monitor");
    const loaded = () => page.waitForFunction(() =>
      document.querySelectorAll('#board .piece[data-target^="card:"]').length === 3);
    await loaded();

    // Card controls stay in place and say why they are unavailable.
    const frameTop = () => page.$eval("#frame", (el) => el.getBoundingClientRect().top);
    assert.equal(await page.$eval('[data-vote="green"]', (el) => el.disabled), false,
      "the first card is selected, so flags are available");
    const topWithCard = await frameTop();
    await setEditMode(page, "bottomBar");
    await page.waitForFunction(() => [...document.getElementById("selection").options].some((o) => o.value === "logo"));
    await page.select("#selection", "logo");
    assert.equal(await page.$eval('[data-vote="green"]', (el) => el.disabled), true, "a logo has no flag");
    assert.equal(await page.$eval('[data-status="qualified"]', (el) => el.title), "เลือกการ์ดก่อน");
    assert.equal(await page.$eval('[data-asset="qualified"]', (el) => el.disabled), true, "no status artwork for a logo");
    assert.equal(await frameTop(), topWithCard, "disabling card controls does not move the canvas");

    // Grid arrangement belongs to Show Status.
    assert.equal(await page.$eval("#tab-grid", (el) => el.disabled), true, "grid tab rests in BOTTOM BAR");
    await setEditMode(page, "scoreboard");
    await page.click("#tab-grid");
    assert.equal(await page.$eval("#panel-grid", (el) => el.hidden), false, "grid tab opens in Show Status");
    await setEditMode(page, "bottomBar");
    assert.equal(await page.$eval("#panel-position", (el) => el.hidden), false,
      "switching to BOTTOM BAR leaves the grid tab");
    await setEditMode(page, "scoreboard");

    // The template stage is fitted once its tab is visible.
    await page.click("#tab-template");
    await page.waitForFunction(() => parseInt(document.getElementById("templateZoomValue").textContent, 10) > 10);

    // The last tab survives a reload.
    await page.click("#tab-status-png");
    await page.reload();
    await loaded();
    assert.equal(await page.$eval("#panel-status-png", (el) => el.hidden), false, "last tab is remembered");
    assert.equal(await page.$eval("#tab-status-png", (el) => el.getAttribute("aria-selected")), "true");

    // The top tools stay pinned while the page scrolls.
    await page.click("#tab-template");
    await page.setViewport({ width: 1440, height: 600 });
    await page.evaluate(() => window.scrollTo(0, document.body.scrollHeight));
    assert.ok(await page.evaluate(() => window.scrollY > 0), "the page scrolls at this height");
    const pinned = await page.$eval(".monitor-top", (el) => el.getBoundingClientRect().top);
    assert.ok(Math.abs(pinned) < 1, `top tools stay pinned (${pinned})`);

    // No sideways scrolling on a narrow window.
    await page.setViewport({ width: 900, height: 900 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth),
      "no horizontal scroll at 900px");
    await page.setViewport({ width: 1440, height: 900 });

    // On air: status in words and colour, warning only while editing the aired mode.
    state.phase = "revealed"; state.displayMode = "bottomBar"; push();
    await page.waitForFunction(() => document.getElementById("status").textContent.includes("ออกอากาศ"));
    assert.equal(await page.$eval("#status", (el) => el.textContent), "● ออกอากาศ · BOTTOM BAR");
    assert.equal(await page.$eval("#status", (el) => el.dataset.air), "on");
    assert.equal(await page.$eval("#roundStatus", (el) => el.textContent), "รอบ 2 · โหวตแล้ว 1/3");
    assert.equal(await page.$eval("#airWarning", (el) => el.hidden), true,
      "editing Show Status while BOTTOM BAR is on air is safe");
    await setEditMode(page, "bottomBar");
    assert.equal(await page.$eval("#airWarning", (el) => el.hidden), false, "editing the aired mode warns");
    state.phase = "collecting"; push();
    await page.waitForFunction(() => document.getElementById("airWarning").hidden);
    assert.equal(await page.$eval("#status", (el) => el.textContent), "○ จอว่าง");
    await setEditMode(page, "scoreboard");

    // Reset all asks twice and forgets a lone press.
    await page.click("#tab-position");
    layoutRequests = [];
    await page.click("#resetAll");
    await new Promise((resolve) => setTimeout(resolve, 300));
    assert.equal(layoutRequests.length, 0, "the first press sends nothing");
    assert.equal(await page.$eval("#resetAll", (el) => el.textContent), "กดอีกครั้งเพื่อรีเซ็ตทั้งหมด");
    await page.click("#resetAll");
    await page.waitForFunction(() => pending.size === 0 && !saving);
    assert.deepEqual(layoutRequests.map((request) => request.target), ["all"], "the second press resets");
    await page.click("#resetAll");
    await new Promise((resolve) => setTimeout(resolve, 3300));
    assert.equal(await page.$eval("#resetAll", (el) => el.textContent), "รีเซ็ตทั้งหมด", "arming expires");
    await page.click("#resetAll");
    await page.click("#tab-grid");
    await page.click("#tab-position");
    assert.equal(await page.$eval("#resetAll", (el) => el.textContent), "รีเซ็ตทั้งหมด", "changing tab disarms");

    // A row's place on the stack is fixed, so artwork taller than the pitch
    // overlaps the row below it. The preview says so before the show does.
    assert.equal(await page.$eval("#stackWarning", el => el.hidden), true,
      "nothing to warn about while the roster has no artwork");
    const tallPng = await page.evaluate(() => {
      const canvasElement = document.createElement("canvas");
      canvasElement.width = 346; canvasElement.height = 120;
      canvasElement.getContext("2d").fillRect(0, 0, 346, 120);
      return canvasElement.toDataURL("image/png");
    });
    state.presidents[1].cardUrl = tallPng;
    state.presidents[1].statusUrl = tallPng;
    push();
    await page.waitForFunction(() => !document.getElementById("stackWarning").hidden);
    const warned = await page.$eval("#stackWarning", el => el.textContent);
    assert.ok(warned.includes("120px") && warned.includes(String(57)),
      `the warning names the artwork and the pitch (${warned})`);
    assert.ok(warned.includes("นายก 1"), "and which card it is");
    await setEditMode(page, "bottomBar");
    assert.equal(await page.$eval("#stackWarning", el => el.hidden), true,
      "BOTTOM BAR places its own rows, so the warning is not its business");
    await setEditMode(page, "scoreboard");
    state.presidents[1].cardUrl = ""; state.presidents[1].statusUrl = ""; push();
    await page.waitForFunction(() => document.getElementById("stackWarning").hidden);

    // The timing panel: a field per length, drafted and applied like a template.
    await page.click("#tab-timing");
    assert.equal(await page.$eval("#panel-timing", (el) => el.hidden), false, "the timing tab opens");
    const offered = await page.evaluate(() => Object.entries(FFF_TIMING_DEFAULTS).flatMap(([group, values]) =>
      Object.keys(values).map(key => "timing-" + group + "-" + key)));
    assert.deepEqual(await page.evaluate(ids => ids.filter(id => !document.getElementById(id)), offered), [],
      "every length the page knows about has a field");
    assert.equal(await page.$eval("#timing-scoreboard-card", (el) => el.value),
      String(await page.evaluate(() => FFF_TIMING_DEFAULTS.scoreboard.card)),
      "the fields open on the lengths in use");
    assert.equal(await page.$eval("#timingApply", (el) => el.disabled), true, "nothing to apply before an edit");
    timingRequests = [];
    await page.$eval("#timing-scoreboard-card", (el) => { el.value = 900; el.dispatchEvent(new Event("change")); });
    assert.equal(await page.$eval("#timingApply", (el) => el.disabled), false, "an edit arms the apply button");
    assert.equal(await page.evaluate(() => fffTiming.scoreboard.card),
      await page.evaluate(() => FFF_TIMING_DEFAULTS.scoreboard.card),
      "a draft does not reach the board before it is applied");
    await page.click("#timingApply");
    await page.waitForFunction(() => !document.getElementById("timingStatus").textContent.includes("ยัง") &&
      document.getElementById("timingStatus").textContent !== "");
    assert.equal(timingRequests.length, 1, "applying sends one request");
    assert.equal(timingRequests[0].scoreboard.card, 900, "the edited length is sent");
    assert.equal(timingRequests[0].board.countRoll,
      await page.evaluate(() => FFF_TIMING_DEFAULTS.board.countRoll), "and every other length rides with it");
    // Out-of-range values are clamped to what the plugin will accept.
    await page.$eval("#timing-board-countTick", (el) => { el.value = 1; el.dispatchEvent(new Event("change")); });
    assert.equal(await page.$eval("#timing-board-countTick", (el) => el.value), "10", "a too-fast tick is clamped");
    await page.click("#timingCancel");
    assert.equal(await page.$eval("#timing-board-countTick", (el) => el.value),
      String(await page.evaluate(() => FFF_TIMING_DEFAULTS.board.countTick)), "cancel restores the session's lengths");
    // A refused save puts the panel back rather than leaving a phantom draft.
    timingStatusCode = 500;
    await page.$eval("#timing-scoreboard-exit", (el) => { el.value = 1000; el.dispatchEvent(new Event("change")); });
    await page.click("#timingApply");
    await page.waitForFunction(() => document.getElementById("timingStatus").textContent.includes("ไม่สำเร็จ"));
    assert.equal(await page.$eval("#timing-scoreboard-exit", (el) => el.value),
      String(await page.evaluate(() => FFF_TIMING_DEFAULTS.scoreboard.exit)), "a failed save rolls the panel back");
    timingStatusCode = 200;
    // The session's own lengths arrive over SSE and the panel follows them.
    state.timing = { scoreboard: { card: 750 } }; push();
    await page.waitForFunction(() => fffTiming.scoreboard.card === 750);
    assert.equal(await page.$eval("#timing-scoreboard-card", (el) => el.value), "750",
      "the panel follows what the session says");
    await page.click("#timingDefaults");
    assert.equal(await page.$eval("#timing-scoreboard-card", (el) => el.value),
      String(await page.evaluate(() => FFF_TIMING_DEFAULTS.scoreboard.card)), "the defaults button restores them all");
    await page.click("#timingCancel");
    delete state.timing; push();
    await page.click("#tab-position");

    // The on-air buttons: the dock's toggles, in the strip that never scrolls.
    airRequests = [];
    await page.reload({ waitUntil: "load" });
    await page.waitForFunction(() => !document.getElementById("airScoreboard").disabled);
    assert.equal(await page.$eval("#airBottomBar", (el) => el.getAttribute("aria-pressed")), "false");

    // Awaiting the real response keeps the handshake out of the page: no
    // test-only global lives in code that ships.
    const sent = (suffix) => page.waitForResponse((res) => res.url().endsWith(suffix));

    let settled = sent("/api/display");
    await page.click("#airBottomBar");
    await settled;
    assert.deepEqual(airRequests.at(-1), { url: "/api/display", body: { mode: "bottomBar" } });

    // With that mode on air, the same button asks for the board to come down.
    state.phase = "revealed"; state.displayMode = "bottomBar"; push();
    await page.waitForFunction(() =>
      document.getElementById("airBottomBar").getAttribute("aria-pressed") === "true");
    settled = sent("/api/display");
    await page.click("#airBottomBar");
    await settled;
    assert.deepEqual(airRequests.at(-1), { url: "/api/display", body: { hide: true } });

    settled = sent("/api/display");
    await page.click("#airHide");
    await settled;
    assert.deepEqual(airRequests.at(-1), { url: "/api/display", body: { hide: true } });

    // Ending a round asks twice, the way resetting every placement does.
    const before = airRequests.length;
    await page.click("#airNewRound");
    assert.equal(airRequests.length, before, "one press only arms the button");
    settled = sent("/api/round");
    await page.click("#airNewRound");
    await settled;
    assert.deepEqual(airRequests.at(-1), { url: "/api/round", body: { action: "clear" } });

    // A LAN operator sees the buttons and is told why they do nothing.
    capabilityOnAir = false;
    await page.reload({ waitUntil: "load" });
    await page.waitForFunction(() => document.getElementById("airScoreboard").disabled);
    for (const id of ["airScoreboard", "airBottomBar", "airHide", "airNewRound"]) {
      assert.equal(await page.$eval("#" + id, (el) => el.disabled), true, id + " is disabled");
      assert.match(await page.$eval("#" + id, (el) => el.title), /เครื่องที่รัน OBS/);
    }
    capabilityOnAir = true;
    state.phase = "collecting"; state.displayMode = "scoreboard"; push();
    await page.reload({ waitUntil: "load" });
    console.log("PASS: the on-air buttons toggle, confirm twice and grey for a LAN operator");

    // A revoked key is told apart from a dropped connection.
    accessStatus = 403;
    for (const res of streams) res.destroy();
    await page.waitForFunction(() => !document.getElementById("accessDenied").classList.contains("hidden"));
    assert.equal(await page.$eval("#warn", (el) => el.classList.contains("hidden")), true,
      "a revoked key replaces the connection warning");

    assert.deepEqual(errors, []);
    console.log("PASS: pinned tools, side tabs, card controls, on-air warning, two-step reset, revoked key");
    console.log("PASS: the timing panel drafts, applies, clamps, rolls back and follows the session");
    console.log("PASS: artwork that would overlap the row below it is called out in the preview");
  } finally {
    await browser.close();
    server.close();
  }
}

main().catch((error) => { console.error(error); process.exit(1); });

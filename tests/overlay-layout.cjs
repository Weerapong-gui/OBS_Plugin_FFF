// Real browser checks against a small HTTP/SSE fixture; no OBS config is touched.
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const puppeteer = require("puppeteer-core");

const web = path.resolve(__dirname, "../data/web");
const fixture = (count) => ({
  phase: "collecting", round: 1, total: count, voted: 0,
  layout: { x: 0.43, y: 0.57, scale: 1.1 }, pieces: {},
  presidents: Array.from({ length: count }, (_, i) => ({
    id: String(i), name: "นายกคนที่ " + (i + 1), school: "สำนักวิชาทดสอบ",
    photoUrl: "", vote: "none"
  }))
});
let state = fixture(6);
let failSaves = false;
const streams = new Set();
const errors = [];
const push = () => {
  for (const stream of streams) stream.write("data: " + JSON.stringify(state) + "\n\n");
};

const server = http.createServer(async (req, res) => {
  if (req.url === "/api/events/overlay") {
    res.writeHead(200, { "Content-Type": "text/event-stream" });
    streams.add(res);
    res.write("data: " + JSON.stringify(state) + "\n\n");
    req.on("close", () => streams.delete(res));
    return;
  }
  if (req.url === "/api/layout") {
    let body = "";
    for await (const chunk of req) body += chunk;
    if (failSaves) { res.writeHead(500); res.end(); return; }
    const request = JSON.parse(body);
    if (request.target === "all") {
      state.layout = { x: 0.5, y: 0.5, scale: 1 };
      state.pieces = {};
    } else if (request.reset) delete state.pieces[request.target];
    else state.pieces[request.target] = request.layout;
    push();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end('{"ok":true}');
    return;
  }
  const file = { "/overlay": "overlay.html", "/monitor": "monitor.html",
    "/board.js": "board.js", "/app.css": "app.css" }[req.url];
  if (!file) { res.writeHead(404); res.end(); return; }
  res.setHeader("Content-Type", file.endsWith(".js") ? "application/javascript" :
    file.endsWith(".css") ? "text/css" : "text/html");
  res.end(fs.readFileSync(path.join(web, file)));
});

async function geometry(page, target) {
  return page.evaluate((key) => {
    const piece = document.querySelector(`[data-target="${key}"]`);
    const rect = piece.getBoundingClientRect();
    const canvas = document.getElementById("canvas");
    const bounds = canvas ? canvas.getBoundingClientRect() : { x: 0, y: 0, width: 1920 };
    const factor = bounds.width / 1920;
    return { x: (rect.x + rect.width / 2 - bounds.x) / factor,
      y: (rect.y + rect.height / 2 - bounds.y) / factor,
      width: rect.width / factor, height: rect.height / factor };
  }, target);
}

function close(a, b, message) {
  for (const key of Object.keys(a))
    assert.ok(Math.abs(a[key] - b[key]) < 1.5, `${message}: ${key}: ${a[key]} vs ${b[key]}`);
}

async function main() {
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  const browser = await puppeteer.launch({
    executablePath: process.env.FFF_BROWSER || "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
    headless: true, args: ["--no-first-run", "--disable-background-networking"]
  });
  try {
    const monitor = await browser.newPage();
    const overlay = await browser.newPage();
    for (const page of [monitor, overlay]) {
      await page.setViewport({ width: 1920, height: 1080 });
      page.on("pageerror", (error) => errors.push(error.message));
    }
    await monitor.goto(base + "/monitor");
    await overlay.goto(base + "/overlay");
    const settled = () => monitor.waitForFunction(() => !saving && pending.size === 0);
    const ready = async (count) => {
      await monitor.waitForFunction((n) => document.querySelectorAll("#board .piece").length === n, {}, count);
    };
    await ready(6);
    assert.equal(await overlay.$eval("#wrap", (el) => el.hidden), true);
    for (const count of [1, 5, 6]) {
      state = fixture(count);
      state.phase = "revealed";
      push();
      await ready(count);
      await overlay.waitForFunction((n) => !document.getElementById("wrap").hidden &&
        document.querySelectorAll("#board .piece").length === n, {}, count);
      for (const target of ["heading", "card:0"])
        close(await geometry(monitor, target), await geometry(overlay, target), "legacy geometry matches");
    }
    console.log("PASS: 1/5/6 cards, legacy layout, reveal, monitor/overlay geometry");

    await monitor.bringToFront();
    const beforeOther = await geometry(monitor, "card:1");
    const beforeHeading = await geometry(monitor, "heading");
    failSaves = true;
    const box = await monitor.$eval('[data-target="card:0"]', (el) => {
      const r = el.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
    });
    const destination = await monitor.$eval("#canvas", (el) => {
      const r = el.getBoundingClientRect(); return { x: r.x + r.width * 0.75, y: r.y + r.height * 0.8 };
    });
    await monitor.mouse.move(box.x, box.y);
    await monitor.mouse.down();
    assert.equal(await monitor.evaluate(() => dragging), true, "pointer selects a card");
    await monitor.mouse.move(destination.x, destination.y, { steps: 5 });
    const duringDrag = await geometry(monitor, "card:0");
    state.presidents[1].vote = "green";
    state.voted = 1;
    push();
    await monitor.waitForFunction(() => lastState.voted === 1);
    close(await geometry(monitor, "card:0"), duringDrag, "vote does not interrupt drag");
    await monitor.mouse.up();
    await monitor.waitForFunction(() => document.getElementById("saveStatus").textContent.includes("ยังบันทึกไม่ได้"));
    assert.equal(state.pieces["card:0"], undefined);
    close(await geometry(monitor, "card:1"), beforeOther, "other card unchanged");
    close(await geometry(monitor, "heading"), beforeHeading, "heading unchanged");
    failSaves = false;
    await monitor.evaluate(() => flush());
    await settled();
    assert.ok(Math.abs(state.pieces["card:0"].x - 0.75) < 0.002);
    close(await geometry(monitor, "card:0"), await geometry(overlay, "card:0"), "retry reaches overlay");
    console.log("PASS: real pointer drag, vote during drag, independent placement, failed save and retry");

    await monitor.select("#selection", "heading");
    await monitor.$eval("#scale", (el) => {
      el.value = "150"; el.dispatchEvent(new Event("input"));
    });
    state.voted = 2;
    push();
    await monitor.waitForFunction(() => lastState.voted === 2);
    assert.equal(await monitor.$eval("#scale", (el) => el.value), "150");
    await monitor.$eval("#scale", (el) => el.dispatchEvent(new Event("change")));
    await settled();
    assert.equal(state.pieces.heading.scale, 1.5);
    await monitor.click("#centerX");
    await settled();
    assert.equal(state.pieces.heading.x, 0.5);
    close(await geometry(monitor, "heading"), await geometry(overlay, "heading"), "heading scale matches");
    const pinned = await geometry(monitor, "card:0");
    await monitor.select("#selection", "card:0");
    await monitor.focus("#selection");
    await monitor.keyboard.press("ArrowRight");
    close(await geometry(monitor, "card:0"), pinned, "selection input does not nudge card");
    await monitor.evaluate(() => document.activeElement.blur());
    await monitor.keyboard.press("ArrowRight");
    await settled();
    assert.ok(Math.abs((await geometry(monitor, "card:0")).x - pinned.x - 1) < 0.1);
    await monitor.keyboard.down("Shift");
    await monitor.keyboard.press("ArrowDown");
    await monitor.keyboard.up("Shift");
    await settled();
    assert.ok(Math.abs((await geometry(monitor, "card:0")).y - pinned.y - 10) < 0.1);
    console.log("PASS: independent heading scale, scale protected from SSE, centering and keyboard controls");

    const saved = structuredClone(state.pieces);
    const placed = await geometry(monitor, "card:0");
    state.presidents.splice(1, 1);
    state.total--;
    push();
    await ready(5);
    close(await geometry(monitor, "card:0"), placed, "roster removal preserves saved position");
    state.presidents.push({ ...state.presidents[0], id: "new", name: "คนใหม่" });
    state.total++;
    push();
    await ready(6);
    close(await geometry(monitor, "card:0"), placed, "roster addition preserves saved position");
    state.phase = "collecting";
    state.round++;
    for (const president of state.presidents) president.vote = "none";
    push();
    await overlay.waitForFunction(() => document.getElementById("wrap").hidden);
    assert.deepEqual(state.pieces, saved);
    await monitor.reload();
    await ready(6);
    close(await geometry(monitor, "card:0"), placed, "reload restores placement");
    await monitor.select("#selection", "card:0");
    await monitor.click("#reset");
    await settled();
    assert.equal(state.pieces["card:0"], undefined);
    assert.deepEqual(state.pieces.heading, saved.heading);
    await monitor.click("#resetAll");
    await settled();
    assert.deepEqual(state.pieces, {});
    assert.deepEqual(state.layout, { x: 0.5, y: 0.5, scale: 1 });
    state.presidents = [];
    state.total = 0;
    push();
    await ready(0);
    assert.equal(await monitor.$eval("#selection", (el) => el.value), "heading");
    assert.deepEqual(errors, []);
    console.log("PASS: roster changes, clear, reload, selected/all resets, empty roster, no JS errors");
  } finally {
    await browser.close();
    for (const stream of streams) stream.end();
    server.close();
  }
}

main().catch((error) => { console.error(error); process.exit(1); });

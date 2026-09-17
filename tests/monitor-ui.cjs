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
  const file = { "/monitor": "monitor.html", "/app.css": "app.css", "/board.js": "board.js",
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
    const loaded = () => page.waitForFunction(() => document.querySelectorAll("#board .piece").length === 3);
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

    assert.deepEqual(errors, []);
    console.log("PASS: pinned tools, side tabs, remembered tab, card controls stay put");
  } finally {
    await browser.close();
    server.close();
  }
}

main().catch((error) => { console.error(error); process.exit(1); });

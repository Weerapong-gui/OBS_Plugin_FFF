// Real browser checks for /score: the flag tally counted live on a page of its
// own. Served by a small HTTP/SSE fixture; no OBS config is touched.
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const puppeteer = require("puppeteer-core");

const web = path.resolve(__dirname, "../data/web");
const ROLL_SETTLE_MS = 2000;

const fixture = (count) => ({
  phase: "collecting", round: 1, total: count, voted: 0,
  displayMode: "scoreboard", layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {},
  bottomBar: { layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {} },
  presidents: Array.from({ length: count }, (_, i) => ({
    id: String(i), name: "นายกคนที่ " + (i + 1), school: "สำนักวิชาทดสอบ",
    cardUrl: "", statusUrl: "", status: "waiting", vote: "none"
  }))
});
let state = fixture(6);
let failSaves = false;
const templatePosts = [];
const overlayStreams = new Set();
const errors = [];
const push = () => {
  for (const stream of overlayStreams) stream.write("data: " + JSON.stringify(state) + "\n\n");
};
const setVotes = (red, green) => {
  state.presidents.forEach((president, index) => {
    president.vote = index < red ? "red" : index < red + green ? "green" : "none";
  });
  state.voted = state.presidents.filter((president) => president.vote !== "none").length;
  push();
};

const server = http.createServer(async (req, res) => {
  if (req.url === "/api/events/overlay") {
    res.writeHead(200, { "Content-Type": "text/event-stream" });
    overlayStreams.add(res);
    res.write("data: " + JSON.stringify(state) + "\n\n");
    req.on("close", () => overlayStreams.delete(res));
    return;
  }
  if (req.url === "/api/template") {
    let body = ""; for await (const chunk of req) body += chunk;
    if (failSaves) { res.writeHead(500); res.end(); return; }
    const { mode, piece, ...template } = JSON.parse(body);
    templatePosts.push({ mode, piece, template });
    // The plugin keeps the /score pieces at the root, beside the Show Status
    // title, because /score is a page rather than a board.
    const key = mode === "score"
      ? (piece === "heading" ? "scoreHeadingTemplate" : "scoreCountTemplate")
      : piece === "logo" ? "logoTemplate" : piece === "count" ? "countTemplate"
        : piece === "heading" ? "headingTemplate" : "cardTemplate";
    (mode === "bottomBar" ? state.bottomBar : state)[key] = template;
    push();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end('{"ok":true}');
    return;
  }
  for (const endpoint of ["/api/layout", "/api/layer", "/api/timing", "/api/status", "/api/operator/vote"]) {
    if (req.url === endpoint) {
      for await (const chunk of req) void chunk;
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end('{"ok":true}');
      return;
    }
  }
  const file = { "/score": "score.html", "/monitor": "monitor.html", "/overlay": "overlay.html",
    "/board.js": "board.js", "/timing.js": "timing.js", "/template-editor.js": "template-editor.js",
    "/view-zoom.js": "view-zoom.js", "/monitor-ui.js": "monitor-ui.js", "/app.css": "app.css" }[req.url];
  if (!file) { res.writeHead(404); res.end(); return; }
  res.setHeader("Content-Type", file.endsWith(".js") ? "application/javascript" :
    file.endsWith(".css") ? "text/css" : "text/html");
  res.end(fs.readFileSync(path.join(web, file)));
});

const digits = (page) => page.evaluate(() => ({
  red: document.querySelector(".count-red .count-value").textContent,
  green: document.querySelector(".count-green .count-value").textContent
}));

// A roll runs for countRoll ms through random digits before it lands, so a
// check reads the settled number rather than whatever frame it caught.
async function settled(page, red, green) {
  await page.waitForFunction((wantRed, wantGreen) =>
    document.querySelector(".count-red .count-value").textContent === wantRed &&
    document.querySelector(".count-green .count-value").textContent === wantGreen,
    { timeout: ROLL_SETTLE_MS, polling: 50 }, String(red), String(green));
}

const headingStyle = (page) => page.evaluate(() => {
  const element = document.getElementById("heading");
  const style = getComputedStyle(element);
  return { text: element.textContent, color: style.color, fontSize: style.fontSize,
    fontWeight: style.fontWeight, textAlign: style.textAlign,
    left: element.style.left, top: element.style.top,
    width: element.style.width, height: element.style.height };
});

async function openScoreTab(page) {
  await page.evaluate(() => document.getElementById("tab-score").click());
  await page.waitForFunction(() => !document.getElementById("panel-score").hidden);
}

async function main() {
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  const browser = await puppeteer.launch({
    executablePath: process.env.FFF_BROWSER || "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
    headless: true, args: ["--no-first-run", "--disable-background-networking"]
  });
  try {
    const score = await browser.newPage();
    const monitor = await browser.newPage();
    for (const page of [score, monitor]) {
      await page.setViewport({ width: 1920, height: 1080 });
      page.on("pageerror", (error) => errors.push(error.message));
    }
    await score.goto(base + "/score");

    // A session that has never been edited draws the defaults board.js holds.
    await settled(score, 0, 0);
    const opening = await headingStyle(score);
    assert.equal(opening.text, "พี่เนย");
    assert.equal(opening.fontSize, "84px");
    assert.equal(opening.fontWeight, "800");
    assert.equal(opening.textAlign, "center");
    assert.deepEqual([opening.left, opening.top, opening.width, opening.height],
      ["660px", "300px", "600px", "120px"]);
    console.log("PASS: the score page opens on its defaults");

    // The whole point of this page: the tally is counted while the board is
    // still blank, so nothing here waits for a reveal.
    assert.equal(state.phase, "collecting");
    setVotes(3, 2);
    await settled(score, 3, 2);
    assert.equal(await score.evaluate(() => document.querySelector("#board").children.length), 3);
    console.log("PASS: flags are counted while the stream is still collecting");

    // Over live video the page cannot borrow a background, so every piece has
    // to bring its own plate.
    const plates = await score.evaluate(() => {
      const of = (selector) => {
        const style = getComputedStyle(document.querySelector(selector));
        return { background: style.backgroundColor, shadow: style.boxShadow };
      };
      // The title's plate hangs off a pseudo-element so it can clear a Thai
      // tone mark; every other piece paints its own box.
      const ofBefore = (selector) => {
        const style = getComputedStyle(document.querySelector(selector), "::before");
        return { background: style.backgroundColor, shadow: style.boxShadow };
      };
      return { heading: ofBefore("#heading"), red: of(".count-red"), green: of(".count-green"),
        label: of(".count-red .count-label") };
    });
    for (const [name, plate] of Object.entries(plates)) {
      assert.notEqual(plate.background, "rgba(0, 0, 0, 0)", `${name} has a plate behind it`);
      assert.notEqual(plate.shadow, "none", `${name} is lifted off the video`);
    }
    console.log("PASS: every piece carries its own plate");

    // Which number is which flag, without having to read the colour.
    const labels = await score.evaluate(() => {
      const of = (selector) => {
        const element = document.querySelector(selector);
        return { text: element.textContent, color: getComputedStyle(element).color };
      };
      return { red: of(".count-red .count-label"), green: of(".count-green .count-label") };
    });
    assert.deepEqual(labels.red, { text: "แดง", color: "rgb(226, 60, 60)" });
    assert.deepEqual(labels.green, { text: "เขียว", color: "rgb(33, 176, 74)" });
    console.log("PASS: each counter says which flag it counts");

    // The colours are the counter template's, and the two counters sit apart
    // on the canvas rather than on top of each other.
    const boxes = await score.evaluate(() => {
      const box = (selector) => {
        const rect = document.querySelector(selector).getBoundingClientRect();
        return { x: Math.round(rect.x), y: Math.round(rect.y) };
      };
      return { red: box(".count-red .count-value"), green: box(".count-green .count-value"),
        redColor: getComputedStyle(document.querySelector(".count-red .count-value")).color,
        greenColor: getComputedStyle(document.querySelector(".count-green .count-value")).color };
    });
    // Green reads first, red second.
    assert.ok(boxes.green.x < boxes.red.x, "green sits left of red");
    assert.equal(boxes.redColor, "rgb(226, 60, 60)");
    assert.equal(boxes.greenColor, "rgb(33, 176, 74)");
    console.log("PASS: the two counters keep their own place and colour");

    // A flag arriving while the digits are still rolling has to move the
    // landing value, never put the older number back on air.
    setVotes(5, 1);
    setVotes(6, 0);
    await settled(score, 6, 0);
    await new Promise((resolve) => setTimeout(resolve, 400));
    assert.deepEqual(await digits(score), { red: "6", green: "0" });
    console.log("PASS: a flag mid-roll lands on the newest tally");

    // Starting a fresh round empties both counters.
    setVotes(0, 0);
    await settled(score, 0, 0);
    console.log("PASS: a cleared round counts back to zero");

    // A roster that has not been entered yet is a page with two zeroes on it,
    // not a page that throws.
    state = fixture(0);
    push();
    await settled(score, 0, 0);
    state = fixture(6);
    push();
    await settled(score, 0, 0);
    console.log("PASS: an empty roster is still a readable page");

    // The operator edits the title from the monitor, and nothing reaches the
    // page until apply.
    await monitor.goto(base + "/monitor");
    await openScoreTab(monitor);
    assert.equal(await monitor.$eval("#scoreText", (element) => element.value), "พี่เนย");
    assert.equal(await monitor.$eval("#scoreApply", (element) => element.disabled), true);
    await monitor.evaluate(() => {
      const text = document.getElementById("scoreText");
      text.value = "พี่เนย 2";
      text.dispatchEvent(new Event("input", { bubbles: true }));
      const color = document.getElementById("scoreHeadingColor");
      color.value = "#ff8800";
      color.dispatchEvent(new Event("input", { bubbles: true }));
      const size = document.getElementById("scoreHeadingFontSize");
      size.value = "120";
      size.dispatchEvent(new Event("change", { bubbles: true }));
    });
    assert.equal(await monitor.$eval("#scoreApply", (element) => element.disabled), false);
    assert.equal((await headingStyle(score)).text, "พี่เนย", "a draft never reaches the page");
    await monitor.click("#scoreApply");
    // The draft message also ends in "ใช้", and the button is disabled while the
    // save is still running, so only the finished message says it landed.
    await monitor.waitForFunction(() =>
      document.getElementById("scoreStatus").textContent === "ใช้กับหน้า Score แล้ว");
    await score.waitForFunction(() => document.getElementById("heading").textContent === "พี่เนย 2",
      { polling: 50 });
    const edited = await headingStyle(score);
    assert.equal(edited.color, "rgb(255, 136, 0)");
    assert.equal(edited.fontSize, "120px");
    const posted = templatePosts.filter((entry) => entry.mode === "score");
    assert.deepEqual(posted.map((entry) => entry.piece), ["heading", "count"]);
    assert.ok(!("mode" in posted[0].template) && !("piece" in posted[0].template),
      "the routing fields do not travel inside the template");
    console.log("PASS: the monitor's Score tab edits the page, on apply only");

    // The plate is drawn on the very element the template positions, so moving
    // the title moves what is behind it too.
    await monitor.evaluate(() => {
      const x = document.getElementById("scoreHeadingX");
      x.value = "300";
      x.dispatchEvent(new Event("change", { bubbles: true }));
    });
    await monitor.click("#scoreApply");
    await monitor.waitForFunction(() =>
      document.getElementById("scoreStatus").textContent === "ใช้กับหน้า Score แล้ว");
    await score.waitForFunction(() => document.getElementById("heading").style.left === "300px",
      { polling: 50 });
    const moved = await score.evaluate(() => {
      const rect = document.getElementById("heading").getBoundingClientRect();
      return { x: Math.round(rect.x), width: Math.round(rect.width) };
    });
    assert.deepEqual(moved, { x: 300, width: 600 }, "the plate is the box, not a fixed frame");
    console.log("PASS: the title plate is the operator's box, not a fixed frame");

    // A refused save puts the panel back on what the session still holds.
    failSaves = true;
    await monitor.evaluate(() => {
      const text = document.getElementById("scoreText");
      text.value = "ไม่ควรถึงจอ";
      text.dispatchEvent(new Event("input", { bubbles: true }));
    });
    await monitor.click("#scoreApply");
    await monitor.waitForFunction(() =>
      document.getElementById("scoreStatus").textContent.includes("บันทึกไม่สำเร็จ"));
    assert.equal(await monitor.$eval("#scoreText", (element) => element.value), "พี่เนย 2");
    assert.equal((await headingStyle(score)).text, "พี่เนย 2");
    failSaves = false;
    console.log("PASS: a refused save rolls the Score panel back");

    assert.deepEqual(errors, []);
    console.log("PASS: no JS errors on /score or /monitor");
  } finally {
    await browser.close();
    for (const stream of overlayStreams) stream.end();
    server.close();
  }
}

main().catch((error) => { console.error(error); process.exit(1); });

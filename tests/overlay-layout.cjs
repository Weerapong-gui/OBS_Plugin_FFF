// Real browser checks against a small HTTP/SSE fixture; no OBS config is touched.
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const puppeteer = require("puppeteer-core");

const web = path.resolve(__dirname, "../data/web");
const CARD_PNG = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScL3TAAAAABJRU5ErkJggg==";
const fixture = (count) => ({
  phase: "collecting", round: 1, total: count, voted: 0,
  layout: { x: 0.43, y: 0.57, scale: 1.1 }, pieces: {}, layers: {},
  presidents: Array.from({ length: count }, (_, i) => ({
    id: String(i), name: "นายกคนที่ " + (i + 1), school: "สำนักวิชาทดสอบ",
    cardUrl: i === 0 ? CARD_PNG : "", vote: "none"
  }))
});
let state = fixture(6);
let failSaves = false;
let layoutDelay = 0;
let templateDelay = 0;
const overlayStreams = new Set();
const phoneStreams = new Map();
const phoneState = (id) => {
  const president = state.presidents.find((item) => item.id === id);
  return { phase: state.phase, round: state.round, total: state.total, voted: state.voted,
    you: president && { id: president.id, name: president.name, school: president.school, vote: president.vote } };
};
const errors = [];
const push = () => {
  for (const stream of overlayStreams) stream.write("data: " + JSON.stringify(state) + "\n\n");
  for (const [stream, id] of phoneStreams)
    stream.write("data: " + JSON.stringify(phoneState(id)) + "\n\n");
};

const server = http.createServer(async (req, res) => {
  if (req.url === "/api/template") {
    if (templateDelay) await new Promise(resolve => setTimeout(resolve, templateDelay));
    let body = ""; for await (const chunk of req) body += chunk;
    if (failSaves) { res.writeHead(500); res.end(); return; }
    const request = JSON.parse(body);
    (request.mode === "bottomBar" ? state.bottomBar : state).cardTemplate = request; push();
    res.writeHead(200); res.end('{"ok":true}'); return;
  }
  const url = new URL(req.url, "http://127.0.0.1");
  if (req.url === "/api/events/overlay") {
    res.writeHead(200, { "Content-Type": "text/event-stream" });
    overlayStreams.add(res);
    res.write("data: " + JSON.stringify(state) + "\n\n");
    req.on("close", () => overlayStreams.delete(res));
    return;
  }
  if (url.pathname === "/api/events" && url.searchParams.get("token") === "phone-0") {
    res.writeHead(200, { "Content-Type": "text/event-stream" });
    phoneStreams.set(res, "0");
    res.write("data: " + JSON.stringify(phoneState("0")) + "\n\n");
    req.on("close", () => phoneStreams.delete(res));
    return;
  }
  if (req.url === "/api/auth") {
    let body = "";
    for await (const chunk of req) body += chunk;
    if (JSON.parse(body).pin !== "000001") { res.writeHead(401); res.end(); return; }
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ token: "phone-0", state: phoneState("0") }));
    return;
  }
  if (req.url === "/api/vote") {
    let body = "";
    for await (const chunk of req) body += chunk;
    const request = JSON.parse(body);
    if (request.token !== "phone-0") { res.writeHead(401); res.end(); return; }
    const president = state.presidents.find((item) => item.id === "0");
    president.vote = request.color;
    state.voted = state.presidents.filter((item) => item.vote !== "none").length;
    push();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify(phoneState("0")));
    return;
  }
  if (req.url === "/api/layout") {
    if (layoutDelay) await new Promise(resolve => setTimeout(resolve, layoutDelay));
    let body = "";
    for await (const chunk of req) body += chunk;
    if (failSaves) { res.writeHead(500); res.end(); return; }
    const request = JSON.parse(body);
    const targetState = request.mode === "bottomBar" ? state.bottomBar : state;
    if (request.target === "all") {
      targetState.layout = { x: 0.5, y: 0.5, scale: 1 };
      targetState.pieces = {};
      targetState.layers = {};
    } else if (request.reset) delete targetState.pieces[request.target];
    else targetState.pieces[request.target] = request.layout;
    push();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end('{"ok":true}');
    return;
  }
  if (req.url === "/api/layer") {
    let body = "";
    for await (const chunk of req) body += chunk;
    const request = JSON.parse(body);
    const targetState = request.mode === "bottomBar" ? state.bottomBar : state;
    const targets = [...(request.mode === "bottomBar" ? ["logo"] : ["heading"]), ...state.presidents.map((item) => "card:" + item.id), ...(request.mode === "bottomBar" ? ["cover"] : [])];
    if (!targets.includes(request.target)) { res.writeHead(404); res.end(); return; }
    const actions = ["front", "forward", "backward", "back"];
    if (!actions.includes(request.action)) { res.writeHead(400); res.end(); return; }
    const ordered = targets.map((target, index) => ({ target, index, layer: targetState.layers[target] ?? index }))
      .sort((a, b) => a.layer - b.layer || a.index - b.index);
    const current = ordered.findIndex((item) => item.target === request.target);
    if (request.action === "front") ordered.push(ordered.splice(current, 1)[0]);
    if (request.action === "back") ordered.unshift(ordered.splice(current, 1)[0]);
    if (request.action === "forward" && current + 1 < ordered.length)
      [ordered[current], ordered[current + 1]] = [ordered[current + 1], ordered[current]];
    if (request.action === "backward" && current > 0)
      [ordered[current], ordered[current - 1]] = [ordered[current - 1], ordered[current]];
    targetState.layers = Object.fromEntries(ordered.map((item, index) => [item.target, index]));
    push();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end('{"ok":true}');
    return;
  }
  if (req.url === "/api/operator/vote") {
    let body = "";
    for await (const chunk of req) body += chunk;
    const request = JSON.parse(body);
    const president = state.presidents.find((item) => item.id === request.presidentId);
    if (!president) { res.writeHead(404); res.end(); return; }
    president.vote = request.color;
    state.voted = state.presidents.filter((item) => item.vote !== "none").length;
    push();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end('{"ok":true}');
    return;
  }
  const file = { "/": "phone.html", "/overlay": "overlay.html", "/monitor": "monitor.html",
    "/board.js": "board.js", "/template-editor.js": "template-editor.js", "/app.css": "app.css" }[req.url];
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
      if (count === 1 || count === 5 || count === 6) {
        await monitor.waitForFunction(() => document.querySelector('[data-target="card:0"] .card-image').naturalWidth > 0);
        await overlay.waitForFunction(() => document.querySelector('[data-target="card:0"] .card-image').naturalWidth > 0);
      }
      for (const target of ["card:0"])
        close(await geometry(monitor, target), await geometry(overlay, target), "legacy geometry matches");
    }
    assert.equal(await monitor.$eval('[data-target="card:0"] .card-image', (el) => el.hidden), false);
    assert.equal(await monitor.$eval('[data-target="card:1"] .card-image', (el) => el.hidden), true);
    assert.equal(await monitor.$eval('[data-target="card:0"] .slot', (el) => Math.round(el.getBoundingClientRect().height / el.getBoundingClientRect().width)), 1,
      "PNG aspect ratio controls card height");
    assert.notEqual(
      await monitor.$eval('[data-target="card:0"] .slot', (el) => getComputedStyle(el).backgroundColor),
      await overlay.$eval('[data-target="card:0"] .slot', (el) => getComputedStyle(el).backgroundColor),
      "Monitor keeps a placement background while the overlay is transparent"
    );
    console.log("PASS: 1/5/6 cards, PNG and empty-card fallback, monitor/overlay geometry");

    await monitor.bringToFront();
    const beforeOther = await geometry(monitor, "card:1");
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
    await monitor.waitForFunction(() => document.getElementById("saveStatus").textContent.includes("บันทึกไม่สำเร็จ"));
    assert.equal(state.pieces["card:0"], undefined);
    close(await geometry(monitor, "card:1"), beforeOther, "other card unchanged");
    failSaves = false;
    await monitor.evaluate(() => { startDraft(); draft.layout.x = 0.75; draft.layout.y = 0.8; commitDraft(); });
    await settled();
    assert.ok(Math.abs(state.pieces["card:0"].x - 0.75) < 0.002);
    close(await geometry(monitor, "card:0"), await geometry(overlay, "card:0"), "retry reaches overlay");
    console.log("PASS: real pointer drag, vote during drag, independent placement, failed save and retry");

    assert.equal(await monitor.$('[data-target="heading"]'), null);
    assert.equal(await overlay.$('[data-target="heading"]'), null);
    assert.notEqual(await monitor.$eval('#canvas', el => getComputedStyle(el).backgroundImage), 'none');
    await monitor.click('#showGrid');
    assert.equal(await monitor.$eval('#canvas', el => getComputedStyle(el).backgroundImage), 'none');
    await monitor.click('#showGrid');
    await monitor.select("#selection", "card:0");
    await monitor.$eval("#scaleX", (el) => {
      el.value = "150"; el.dispatchEvent(new Event("input"));
    });
    await monitor.$eval("#scaleY", (el) => {
      el.value = "75"; el.dispatchEvent(new Event("input"));
    });
    state.voted = 2;
    push();
    await monitor.waitForFunction(() => lastState.voted === 2);
    assert.equal(await monitor.$eval("#scaleX", (el) => el.value), "150");
    assert.equal(await monitor.$eval("#scaleY", (el) => el.value), "75");
    await monitor.$eval("#scaleX", (el) => el.dispatchEvent(new Event("change")));
    await monitor.$eval("#scaleY", (el) => el.dispatchEvent(new Event("change")));
    await settled();
    assert.equal(state.pieces['card:0'].resultScaleX, 1.5);
    assert.equal(state.pieces['card:0'].resultScaleY, 0.75);
    await monitor.click("#centerX");
    await settled();
    assert.equal(state.pieces['card:0'].x, 0.5);
    close(await geometry(monitor, "card:0"), await geometry(overlay, "card:0"), "card geometry matches");
    await monitor.select("#selection", "card:0");
    const phone = await browser.newPage();
    await phone.setViewport({ width: 390, height: 844 });
    phone.on("pageerror", (error) => errors.push(error.message));
    await phone.goto(base + "/");
    await phone.$eval("#pin", (el) => { el.value = "000001"; });
    await phone.click("#enter");
    await phone.waitForFunction(() => !document.getElementById("vote").classList.contains("hidden"));
    await phone.click("#red");
    await phone.waitForFunction(() => document.getElementById("red").classList.contains("picked"));
    await phone.waitForFunction(() => lastState.you.vote === "red");
    await monitor.bringToFront();
    await monitor.click('[data-vote="green"]');
    await monitor.waitForFunction(() => lastState.presidents[0].vote === "green");
    assert.equal(state.presidents[0].vote, "green");
    await phone.waitForFunction(() => document.getElementById("green").classList.contains("picked"), { polling: 100 });
    assert.equal(await phone.$eval("#red", (el) => el.classList.contains("picked")), false,
      "operator override replaces the phone's confirmed colour");
    assert.equal(await monitor.$eval('[data-target="card:0"] .result', (el) => getComputedStyle(el).display), "block");
    assert.equal(await overlay.$eval('[data-target="card:0"] .result', (el) => getComputedStyle(el).display), "block");
    const imageBeforeResultScale = await geometry(monitor, "card:0");
    const dimensions = page => page.$eval('[data-target="card:0"] .slot', el => {
      const img = el.querySelector('.card-image').getBoundingClientRect();
      const result = el.querySelector('.result').getBoundingClientRect();
      return { imageWidth: img.width, imageHeight: img.height,
        resultWidth: result.width, resultHeight: result.height };
    });
    const beforeResize = await dimensions(monitor);
    await monitor.$eval("#scaleX", (el) => { el.value = "50"; el.dispatchEvent(new Event("input")); });
    await monitor.$eval("#scaleY", (el) => { el.value = "200"; el.dispatchEvent(new Event("input")); });
    await monitor.$eval("#scaleX", (el) => el.dispatchEvent(new Event("change")));
    await monitor.$eval("#scaleY", (el) => el.dispatchEvent(new Event("change")));
    await settled();
    assert.equal(state.pieces["card:0"].resultScaleX, 0.5);
    assert.equal(state.pieces["card:0"].resultScaleY, 2);
    const afterResize = await dimensions(monitor);
    assert.equal(afterResize.imageWidth, beforeResize.imageWidth);
    assert.equal(afterResize.imageHeight, beforeResize.imageHeight);
    assert.ok(Math.abs(afterResize.resultWidth / beforeResize.resultWidth - 1 / 3) < 0.01);
    assert.ok(Math.abs(afterResize.resultHeight / beforeResize.resultHeight - 2 / 0.75) < 0.01);
    for (const page of [monitor, overlay]) {
      await page.waitForFunction(() => getComputedStyle(document.querySelector('[data-target="card:0"] .slot')).backgroundColor === 'rgba(0, 0, 0, 0)', { polling: 50 });
      assert.equal(await page.$eval('[data-target="card:0"] .slot', el => getComputedStyle(el).backgroundColor),
        'rgba(0, 0, 0, 0)', 'no full-card colour behind the resized result');
      assert.equal(await page.$eval('[data-target="card:0"] .card-image', el => getComputedStyle(el).zIndex), '1');
      assert.equal(await page.$eval('[data-target="card:0"] .result', el => getComputedStyle(el).zIndex), '0');
    }
    close(await geometry(monitor, "card:0"), imageBeforeResultScale, "result scale leaves PNG card size unchanged");
    assert.equal(await overlay.$eval('[data-target="card:0"] .result', (el) => getComputedStyle(el).transform),
      await monitor.$eval('[data-target="card:0"] .result', (el) => getComputedStyle(el).transform),
      "result scale reaches the overlay");
    await monitor.click('[data-layer="front"]');
    await monitor.waitForFunction(() => Object.keys(lastState.layers).length === lastState.presidents.length + 1, { timeout: 5000 });
    const topLayer = Math.max(...Object.values(state.layers));
    assert.equal(state.layers["card:0"], topLayer, "selected card reaches front layer");
    assert.equal(await monitor.$eval('[data-target="card:0"]', (el) => el.style.zIndex), String(topLayer));
    assert.equal(await overlay.$eval('[data-target="card:0"]', (el) => el.style.zIndex), String(topLayer));
    const pinned = await geometry(monitor, "card:0");
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
    console.log("PASS: independent width/height, full-card operator vote, SSE-safe controls and keyboard controls");

    const beforeTemplate = await geometry(monitor, "card:0");
    await monitor.click('[data-layer="result"]');
    for (const [id, value] of [["templateX", 35], ["templateY", -20], ["templateWidth", 220], ["templateHeight", 100]]) {
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
    }
    assert.equal(state.cardTemplate, undefined, "draft never broadcasts");
    close(await geometry(monitor, "card:0"), beforeTemplate, "draft does not move live cards");
    await monitor.click("#templateUp");
    failSaves = true;
    await monitor.click("#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("บันทึกไม่สำเร็จ"));
    assert.equal(state.cardTemplate, undefined);
    failSaves = false;
    for (const [id, value] of [["templateX", 35], ["templateY", -20], ["templateWidth", 220], ["templateHeight", 100]]) {
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
    }
    await monitor.click("#templateUp");
    await monitor.click("#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบกับทุกการ์ดแล้ว"));
    assert.equal(state.cardTemplate.result.x, 35);
    assert.equal(state.cardTemplate.result.width, 220);
    assert.deepEqual(state.cardTemplate.order, ["image", "result"]);
    await overlay.waitForFunction(() => document.querySelector('.slot .result').style.left === "35px");
    for (const page of [monitor, overlay]) {
      assert.equal(await page.$$eval('#board .result', els => els.every(el => el.style.width === "220px" && el.style.zIndex === "1")), true);
    }
    await monitor.$eval("#templateX", el => { el.value = 99; el.dispatchEvent(new Event("change")); });
    await monitor.click("#templateCancel");
    assert.equal(await monitor.$eval("#templateX", el => el.value), "35");
    await monitor.click("#templateZoomIn");
    const start = await monitor.$eval("#templateSelection", el => {
      const r = el.getBoundingClientRect(); return { x: r.x + 10, y: r.y + 10 };
    });
    const zoom = await monitor.$eval("#templateStage", el => new DOMMatrix(getComputedStyle(el).transform).a);
    await monitor.mouse.move(start.x, start.y);
    await monitor.mouse.down();
    await monitor.mouse.move(start.x + 20 * zoom, start.y + 10 * zoom, { steps: 3 });
    await monitor.mouse.up();
    assert.equal(await monitor.$eval("#templateX", el => el.value), "55");
    assert.equal(await monitor.$eval("#templateY", el => el.value), "-10");
    await monitor.click("#templateCancel");
    console.log("PASS: template draft, save failure/retry, global rendering, layers, cancel and zoom-correct drag");

    const templateBox = () => monitor.$eval('#templateSelection', el => ({
      x: parseFloat(el.style.left), y: parseFloat(el.style.top),
      width: parseFloat(el.style.width), height: parseFloat(el.style.height)
    }));
    await monitor.click('#templateFit');
    for (const layer of ["image", "result"]) {
      const zoom = await monitor.$eval("#templateStage", el => new DOMMatrix(getComputedStyle(el).transform).a);
      await monitor.click('[data-layer="' + layer + '"]');
      const original = await templateBox();
      const handle = await monitor.$eval('#templateResize', el => {
        const r = el.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
      });
      await monitor.mouse.move(handle.x, handle.y);
      await monitor.mouse.down();
      await monitor.keyboard.down('Shift');
      await monitor.mouse.move(handle.x + 30 * zoom, handle.y + 5 * zoom, { steps: 3 });
      let box = await templateBox();
      assert.ok(Math.abs(box.width / box.height - original.width / original.height) < 0.00001);
      assert.ok(box.width > original.width);
      assert.equal(box.x, original.x); assert.equal(box.y, original.y);
      await monitor.keyboard.up('Shift');
      await monitor.mouse.move(handle.x + 40 * zoom, handle.y + 5 * zoom);
      box = await templateBox();
      assert.ok(Math.abs(box.width / box.height - original.width / original.height) > 0.01,
        'releasing Shift restores free resize during the same drag');
      await monitor.keyboard.down('Shift');
      await monitor.mouse.move(handle.x - 20 * zoom, handle.y - 5 * zoom);
      box = await templateBox();
      assert.ok(box.width < original.width);
      assert.ok(Math.abs(box.width / box.height - original.width / original.height) < 0.00001);
      // Exercise both dimension bounds through the captured drag handler.
      for (const delta of [10000, -10000]) {
        await monitor.$eval('#templateViewport', (el, p) => el.dispatchEvent(new PointerEvent('pointermove', {
          clientX: p.x, clientY: p.y, shiftKey: true
        })), { x: handle.x + delta * zoom, y: handle.y });
        box = await templateBox();
        assert.ok(box.width >= 1 && box.height >= 1 && box.width <= 2000 && box.height <= 2000);
        assert.ok(Math.abs(box.width / box.height - original.width / original.height) < 0.00001);
      }
      await monitor.mouse.up();
      await monitor.keyboard.up('Shift');
      await monitor.click('#templateCancel');
    }
    for (const page of [monitor, overlay]) {
      assert.equal(await page.$$eval('.slot, .slot .result', els => els.every(el =>
        getComputedStyle(el).borderRadius === '0px')), true, 'cards and result layers have square corners');
    }
    await monitor.click('[data-layer="result"]');
    console.log("PASS: Shift resize on both placeholders, free resize, zoom, bounds and square corners");

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
    await overlay.waitForFunction(() => document.getElementById("wrap").hidden, { polling: 50 });
    assert.deepEqual(state.pieces, saved);
    await monitor.reload();
    await ready(6);
    close(await geometry(monitor, "card:0"), placed, "reload restores placement");
    assert.equal(await monitor.$eval("#templateWidth", el => el.value), "220");
    await monitor.select("#selection", "card:0");
    await monitor.click("#reset");
    await settled();
    assert.equal(state.pieces["card:0"], undefined);
    assert.deepEqual(state.pieces.heading, saved.heading);
    await monitor.click("#resetAll");
    await settled();
    assert.deepEqual(state.pieces, {});
    assert.deepEqual(state.layers, {});
    assert.deepEqual(state.layout, { x: 0.5, y: 0.5, scale: 1 });
    state.presidents = [];
    state.total = 0;
    push();
    await ready(0);
    assert.equal(await monitor.$eval("#selection", (el) => el.value), "");

    // Both views share bottom-bar geometry; selecting an edit mode never changes on-air mode.
    for (const count of [0, 1, 14, 5]) {
      state = fixture(count); state.phase = "revealed";
      state.bottomBar = { layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {}, logoPresidentId: "0" };
      state.presidents.forEach(p => { p.bottomBarUrl = CARD_PNG; p.logoUrl = CARD_PNG; p.vote = "green"; });
      state.displayMode = "bottomBar"; push();
      await monitor.select("#editMode", "bottomBar");
      await ready(count + 2);
      await overlay.waitForFunction(n => document.querySelectorAll("#board .piece").length === n && document.querySelector(".bottom-bar"), {}, count + 2);
      for (const target of ["logo", ...state.presidents.map(p => "card:" + p.id)])
        close(await geometry(monitor, target), await geometry(overlay, target), "bottom bar shared geometry");
      if (count) {
        const metrics = await overlay.$eval('[data-target="card:0"]', el => ({ x: el.offsetLeft, width: el.offsetWidth, top: el.offsetTop }));
        assert.equal(metrics.x, 0); assert.equal(metrics.top, 830);
        assert.ok(Math.abs(metrics.width - 850 / Math.ceil(count / 2)) < 1);
      }
    }
    await monitor.select("#selection", "card:0");
    for (const value of [0, 50, 100]) {
      await monitor.$eval("#imageOpacity", (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
      await settled();
      await overlay.waitForFunction(value => document.querySelector('[data-target="card:0"] img').style.opacity === String(value / 100), {}, value);
      assert.equal(state.bottomBar.pieces["card:0"].imageOpacity, value / 100);
      assert.deepEqual(state.pieces, {});
    }
    // A response arriving after switching editors must update only its original mode.
    layoutDelay = 150;
    await monitor.$eval("#resultOpacity", el => { el.value = 50; el.dispatchEvent(new Event("change")); });
    await monitor.select("#editMode", "scoreboard");
    await settled(); layoutDelay = 0;
    assert.equal(state.bottomBar.pieces["card:0"].resultOpacity, 0.5);
    assert.deepEqual(state.pieces, {});
    assert.equal(state.displayMode, "bottomBar");
    assert.equal(await overlay.$eval("#board", el => el.classList.contains("bottom-bar")), true);
    await monitor.select("#editMode", "bottomBar");
    // Template opacity is shared until explicitly overridden on a piece.
    await monitor.select("#selection", "card:1");
    await monitor.click('[data-layer="image"]');
    await monitor.$eval("#templateOpacity", el => { el.value = 50; el.dispatchEvent(new Event("change")); });
    await monitor.click("#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบกับทุกการ์ดแล้ว"));
    await overlay.waitForFunction(() => document.querySelector('[data-target="card:1"] img').style.opacity === "0.5");
    assert.equal(await overlay.$eval('[data-target="card:0"] img', el => el.style.opacity), "1");
    assert.equal(state.cardTemplate, undefined);
    assert.equal(await overlay.$eval('[data-target="logo"] img', el => el.style.opacity), "1", "card template opacity does not affect logo");
    // Cover remains independent from card templates and scoreboard placement.
    state.bottomBar.coverUrl = CARD_PNG; push();
    await monitor.waitForFunction(() => document.querySelector('[data-target="cover"] img').naturalWidth > 0);
    await overlay.waitForFunction(() => document.querySelector('[data-target="cover"] img').naturalWidth > 0, { polling: 50 });
    await monitor.select("#selection", "cover");
    const coverBefore = await geometry(monitor, "cover");
    const cardBeforeCover = await geometry(monitor, "card:0");
    const coverDrag = await monitor.$eval("#canvas", el => {
      const r = el.getBoundingClientRect();
      return { x: r.x + r.width * 0.2, y: r.y + r.height * 0.2, dx: r.width * 0.08, dy: r.height * 0.06 };
    });
    await monitor.mouse.move(coverDrag.x, coverDrag.y);
    await monitor.mouse.down();
    assert.equal(await monitor.evaluate(() => dragging && selected === "cover"), true);
    await monitor.mouse.move(coverDrag.x + coverDrag.dx, coverDrag.y + coverDrag.dy, { steps: 5 });
    await monitor.mouse.up(); await settled();
    assert.ok(state.bottomBar.pieces.cover.x > 0.5);
    assert.ok(state.bottomBar.pieces.cover.y > 0.5);
    close(await geometry(monitor, "card:0"), cardBeforeCover, "cover drag leaves cards in place");
    for (const [id, value] of [["pieceWidth", 75], ["pieceHeight", 125]]) {
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
      await settled();
    }
    assert.equal(state.bottomBar.pieces.cover.scaleX, 0.75);
    assert.equal(state.bottomBar.pieces.cover.scaleY, 1.25);
    const resizedCover = await geometry(monitor, "cover");
    assert.ok(Math.abs(resizedCover.width / coverBefore.width - 0.75) < 0.01);
    assert.ok(Math.abs(resizedCover.height / coverBefore.height - 1.25) < 0.01);
    close(resizedCover, await geometry(overlay, "cover"), "cover resize reaches overlay");
    for (const value of [0, 50, 100]) {
      await monitor.$eval("#imageOpacity", (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
      await settled();
      await overlay.waitForFunction(value => document.querySelector('[data-target="cover"] img').style.opacity === String(value / 100), { polling: 50 }, value);
      assert.equal(state.bottomBar.pieces.cover.imageOpacity, value / 100);
      if (value === 50) {
        await monitor.screenshot({ path: "/private/tmp/fff-cover-monitor-review.png" });
        await overlay.screenshot({ path: "/private/tmp/fff-cover-overlay-review.png", omitBackground: true });
      }
    }
    await monitor.click('[data-layer="back"]');
    await monitor.waitForFunction(() => lastState.layers.cover === 0);
    await overlay.waitForFunction(() => document.querySelector('[data-target="cover"]').style.zIndex === "0", { polling: 50 });
    await monitor.click('[data-layer="front"]');
    await monitor.waitForFunction(() => lastState.layers.cover === Math.max(...Object.values(lastState.layers)));
    const coverFront = Math.max(...Object.values(state.bottomBar.layers));
    await overlay.waitForFunction(layer => document.querySelector('[data-target="cover"]').style.zIndex === String(layer), { polling: 50 }, coverFront);
    assert.deepEqual(state.pieces, {});
    assert.deepEqual(state.layers, {});
    await monitor.click("#reset"); await settled();
    assert.equal(state.bottomBar.pieces.cover, undefined);
    close(await geometry(monitor, "cover"), coverBefore, "cover reset restores geometry");
    assert.equal(await overlay.$eval('[data-target="cover"] img', el => el.style.opacity), "1", "card template opacity does not affect cover");
    state.bottomBar.coverUrl = ""; push();
    await overlay.waitForFunction(() => document.querySelector('[data-target="cover"] img').hidden, { polling: 50 });
    console.log("PASS: Cover pointer drag, resize, opacity, layer save, reset/removal and template/mode isolation");
    templateDelay = 500;
    await monitor.$eval("#templateOpacity", el => { el.value = 25; el.dispatchEvent(new Event("change")); });
    await monitor.click("#templateApply");
    await monitor.select("#editMode", "scoreboard");
    await monitor.select("#editMode", "bottomBar");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบกับทุกการ์ดแล้ว"));
    assert.equal(await monitor.$eval("#templateOpacity", el => el.value), "25", "mode round-trip refreshes saved template draft");
    templateDelay = 0;
    state.bottomBar.logoPresidentId = "missing"; push();
    await overlay.waitForFunction(() => document.querySelector('[data-target="logo"] img').hidden);
    assert.equal(await overlay.$eval('[data-target="logo"]', el => getComputedStyle(el).backgroundColor), "rgba(0, 0, 0, 0)");
    await monitor.click("#templateFit");
    await monitor.screenshot({ path: "/private/tmp/fff-bottom-bar-monitor.png" });
    await overlay.screenshot({ path: "/private/tmp/fff-bottom-bar-overlay.png", omitBackground: true });
    console.log("Clear precondition:", await overlay.evaluate(() => ({ visibility: document.visibilityState, stream: stream.readyState, phase: lastState.phase, transition: transition && { entering: transition.entering, generation: transition.generation } })));
    state.phase = "collecting"; push();
    await overlay.waitForFunction(() => lastState.phase === "collecting", { polling: 50 });
    await overlay.waitForFunction(() => document.getElementById("wrap").hidden, { polling: 50 });
    console.log("PASS: bottom-bar 0/1/14/odd geometry, mode isolation, opacity and Clear");
    assert.deepEqual(errors, []);
    console.log("PASS: roster changes, clear, reload, selected/all resets, empty roster, no JS errors");
  } finally {
    await browser.close();
    for (const stream of overlayStreams) stream.end();
    for (const stream of phoneStreams.keys()) stream.end();
    server.close();
  }
}

main().catch((error) => { console.error(error); process.exit(1); });

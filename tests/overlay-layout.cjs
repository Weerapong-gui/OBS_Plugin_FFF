// Real browser checks against a small HTTP/SSE fixture; no OBS config is touched.
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const puppeteer = require("puppeteer-core");

const web = path.resolve(__dirname, "../data/web");
const CARD_PNG = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScL3TAAAAABJRU5ErkJggg==";
const fixture = (count) => ({
  phase: "collecting", round: 1, total: count, voted: 0,
  layout: { x: 0.43, y: 0.57, scale: 1.1 }, pieces: {}, layers: {},
  presidents: Array.from({ length: count }, (_, i) => ({
    id: String(i), name: "นายกคนที่ " + (i + 1), school: "สำนักวิชาทดสอบ",
    cardUrl: i === 0 ? CARD_PNG : "", vote: "none",
    // The scoreboard draws statusUrl; the server resolves it from the artwork
    // for the president's status, falling back to the card behind it.
    status: "waiting", statusArt: {}, statusUrl: i === 0 ? CARD_PNG : ""
  }))
});
const resolveStatus = (president) => {
  president.statusUrl = president.statusArt[president.status] || president.cardUrl || "";
  for (const kind of ["qualified", "unqualified", "waiting"])
    president[kind + "Url"] = president.statusArt[kind] || "";
};
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
    const { mode, piece, ...template } = JSON.parse(body);
    const key = piece === "logo" ? "logoTemplate" : piece === "count" ? "countTemplate" : "cardTemplate";
    (mode === "bottomBar" ? state.bottomBar : state)[key] = template; push();
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
    const targets = [...(request.mode === "bottomBar" ? ["logo"] : ["heading"]), ...state.presidents.map((item) => "card:" + item.id), ...(request.mode === "bottomBar" ? ["count:red", "count:green", "cover"] : [])];
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
  if (url.pathname === "/api/status") {
    let body = "";
    for await (const chunk of req) body += chunk;
    const request = JSON.parse(body);
    const president = state.presidents.find((item) => item.id === request.presidentId);
    if (!president) { res.writeHead(404); res.end(); return; }
    if (!["waiting", "unqualified", "qualified"].includes(request.status)) { res.writeHead(400); res.end(); return; }
    president.status = request.status;
    resolveStatus(president);
    push();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end('{"ok":true}');
    return;
  }
  if (url.pathname === "/api/asset") {
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    const bytes = Buffer.concat(chunks);
    const president = state.presidents.find((item) => item.id === url.searchParams.get("presidentId"));
    const kind = url.searchParams.get("kind");
    if (!president) { res.writeHead(404); res.end(); return; }
    if (!["waiting", "unqualified", "qualified"].includes(kind)) { res.writeHead(400); res.end(); return; }
    if (bytes.length && !bytes.subarray(0, 8).equals(Buffer.from("89504e470d0a1a0a", "hex"))) {
      res.writeHead(400); res.end(); return;
    }
    if (bytes.length) president.statusArt[kind] = `data:image/png;base64,${bytes.toString("base64")}`;
    else delete president.statusArt[kind];
    resolveStatus(president);
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
    "/board.js": "board.js", "/template-editor.js": "template-editor.js", "/view-zoom.js": "view-zoom.js", "/monitor-ui.js": "monitor-ui.js",
    "/app.css": "app.css" }[req.url];
  if (!file) { res.writeHead(404); res.end(); return; }
  res.setHeader("Content-Type", file.endsWith(".js") ? "application/javascript" :
    file.endsWith(".css") ? "text/css" : "text/html");
  res.end(fs.readFileSync(path.join(web, file)));
});

async function geometry(page, target) {
  return page.evaluate((key) => {
    const piece = board.querySelector(`[data-target="${key}"]`);
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

// Most tools live in side-panel tabs now; open the owning tab the way an operator would.
async function reveal(page, selector) {
  await page.evaluate((sel) => {
    const panel = document.querySelector(sel)?.closest('[role="tabpanel"]');
    if (panel && panel.hidden) document.getElementById(panel.getAttribute("aria-labelledby")).click();
  }, selector);
}

async function press(page, selector) {
  await reveal(page, selector);
  await page.click(selector);
}

// Edit mode is a radio group; dispatch like a select did so a repeat still re-renders.
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
      await monitor.waitForFunction((n) => board.querySelectorAll(".piece").length === n, {}, count);
    };
    await ready(6);
    assert.equal(await overlay.evaluate(() => wrap.hidden), true);
    for (const count of [1, 5, 6]) {
      state = fixture(count);
      state.phase = "revealed";
      push();
      await ready(count);
      await overlay.waitForFunction((n) => !wrap.hidden &&
        board.querySelectorAll(".piece").length === n, {}, count);
      if (count === 1 || count === 5 || count === 6) {
        await monitor.waitForFunction(() => board.querySelector('[data-target="card:0"] .card-image').naturalWidth > 0);
        await overlay.waitForFunction(() => board.querySelector('[data-target="card:0"] .card-image').naturalWidth > 0);
      }
      for (const target of ["card:0"])
        close(await geometry(monitor, target), await geometry(overlay, target), "legacy geometry matches");
    }
    assert.equal(await monitor.$eval('[data-target="card:0"] .card-image', (el) => el.hidden), false);
    assert.equal(await monitor.$eval('[data-target="card:1"] .card-image', (el) => el.hidden), true);
    assert.equal(await monitor.$eval('[data-target="card:0"] .slot', (el) => Math.round(el.getBoundingClientRect().height / el.getBoundingClientRect().width)), 1,
      "PNG aspect ratio controls card height");
    // The placement frame marks a slot with nothing to show, so it belongs to the
    // empty card. A slot carrying finished artwork is transparent in both views.
    assert.notEqual(
      await monitor.$eval('[data-target="card:1"] .slot', (el) => getComputedStyle(el).backgroundColor),
      await overlay.evaluate(() =>
        getComputedStyle(board.querySelector('[data-target="card:1"] .slot')).backgroundColor),
      "Monitor keeps a placement background while the overlay is transparent"
    );
    assert.equal(await monitor.$eval('[data-target="card:0"] .slot', (el) => getComputedStyle(el).backgroundColor),
      "rgba(0, 0, 0, 0)", "artwork is never dimmed by the placement frame");
    console.log("PASS: 1/5/6 cards, PNG and empty-card fallback, monitor/overlay geometry");

    // The scoreboard is a status board: one finished PNG per president, chosen
    // per card from the monitor, and pressing Show Status puts it on air.
    {
      state = fixture(3);
      state.phase = "revealed";
      push();
      await ready(3);
      // In front, so this page keeps producing frames while the block runs.
      await monitor.bringToFront();
      await monitor.select("#selection", "card:0");
      // Drive the real change handler with a real File. Puppeteer's uploadFile
      // hangs the CDP call in this headless build, and the picker itself is not
      // what is worth covering — reading the bytes out and posting them is.
      await monitor.evaluate((dataUrl) => {
        const bytes = Uint8Array.from(atob(dataUrl.split(",")[1]), ch => ch.charCodeAt(0));
        const input = document.querySelector('[data-asset="qualified"]');
        const transfer = new DataTransfer();
        transfer.items.add(new File([bytes], "qualified.png", { type: "image/png" }));
        input.files = transfer.files;
        input.dispatchEvent(new Event("change"));
      }, CARD_PNG);
      // Timed polling, not rAF: this page is not in front, and a backgrounded
      // page stops producing animation frames.
      await monitor.waitForFunction(() => lastState.presidents[0].statusArt.qualified !== undefined,
        { polling: 50 });
      assert.ok(state.presidents[0].statusArt.qualified, "the monitor uploads artwork for one status");
      assert.equal(state.presidents[0].statusUrl, state.presidents[0].cardUrl,
        "artwork alone does not change what is showing");

      const shown = (page, id) => page.evaluate(key =>
        board.querySelector(`[data-target="${key}"] .card-image`).getAttribute("src"), id);
      const othersBefore = await shown(overlay, "card:1");
      await press(monitor, '[data-status="qualified"]');
      await monitor.waitForFunction(() => lastState.presidents[0].status === "qualified", { polling: 50 });
      assert.equal(state.presidents[0].status, "qualified", "the status is chosen per card");
      await overlay.waitForFunction(() =>
        board.querySelector('[data-target="card:0"] .card-image').getAttribute("src") ===
        lastState.presidents[0].statusUrl, { polling: 50 });
      assert.equal(await shown(overlay, "card:1"), othersBefore, "choosing one status leaves other cards alone");
      // A status change on air has to read as a change.
      assert.equal(await overlay.evaluate(() =>
        board.querySelector('[data-target="card:0"] .slot').classList.contains("reveal-in")), true,
        "a status change on air replays the entrance");
      assert.equal(await monitor.evaluate(() =>
        [...document.querySelectorAll("[data-status]")].find(b => b.getAttribute("aria-pressed") === "true")
          .dataset.status), "qualified", "the monitor shows which status is chosen");

      await press(monitor, '[data-status="unqualified"]');
      await monitor.waitForFunction(() => lastState.presidents[0].status === "unqualified", { polling: 50 });
      await overlay.waitForFunction(() =>
        board.querySelector('[data-target="card:0"] .card-image').getAttribute("src") ===
        lastState.presidents[0].cardUrl, { polling: 50 });
      assert.equal(state.presidents[0].statusUrl, state.presidents[0].cardUrl,
        "a status with no artwork yet falls back to the card");
      assert.equal(await overlay.evaluate(() =>
        getComputedStyle(board.querySelector('[data-target="card:0"] .result')).display), "none",
        "the status board paints no flag colour");
      assert.equal(await monitor.evaluate(() =>
        document.querySelector('[data-asset="qualified"]').closest("label").classList.contains("has-asset")), true,
        "the monitor marks a status that already has artwork");
      state = fixture(6);
      state.phase = "collecting";
      push();
      await ready(6);
      console.log("PASS: status artwork upload, per-card status, fallback and on-air replay");
    }

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
    await press(monitor, '#showGrid');
    assert.equal(await monitor.$eval('#canvas', el => getComputedStyle(el).backgroundImage), 'none');
    await press(monitor, '#showGrid');
    await monitor.select("#selection", "card:0");
    for (const [id, value] of [["pieceWidth", "150"], ["pieceHeight", "75"]])
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
    state.voted = 2;
    push();
    await monitor.waitForFunction(() => lastState.voted === 2);
    assert.equal(await monitor.$eval("#pieceWidth", (el) => el.value), "150", "a vote does not reset a pending edit");
    assert.equal(await monitor.$eval("#pieceHeight", (el) => el.value), "75");
    await settled();
    assert.equal(state.pieces['card:0'].scaleX, 1.5);
    assert.equal(state.pieces['card:0'].scaleY, 0.75);
    await monitor.$eval("#pieceWidth", (el) => { el.value = "100"; el.dispatchEvent(new Event("change")); });
    await monitor.$eval("#pieceHeight", (el) => { el.value = "100"; el.dispatchEvent(new Event("change")); });
    await settled();
    await press(monitor, "#centerX");
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
    await press(monitor, '[data-vote="green"]');
    await monitor.waitForFunction(() => lastState.presidents[0].vote === "green");
    assert.equal(state.presidents[0].vote, "green");
    await phone.waitForFunction(() => document.getElementById("green").classList.contains("picked"), { polling: 100 });
    assert.equal(await phone.$eval("#red", (el) => el.classList.contains("picked")), false,
      "operator override replaces the phone's confirmed colour");
    // The scoreboard is a status board now: it shows one finished PNG per
    // president and leaves the flag colour to the bottom bar, so a vote records
    // and reaches the phone without painting a backdrop here.
    for (const page of [monitor, overlay])
      assert.equal(await page.evaluate(() =>
        getComputedStyle(board.querySelector('[data-target="card:0"] .result')).display), "none",
        "a vote paints no backdrop on the status board");
    for (const page of [monitor, overlay]) {
      await page.waitForFunction(() => getComputedStyle(board.querySelector('[data-target="card:0"] .slot')).backgroundColor === 'rgba(0, 0, 0, 0)', { polling: 50 });
      assert.equal(await page.$eval('[data-target="card:0"] .slot', el => getComputedStyle(el).backgroundColor),
        'rgba(0, 0, 0, 0)', 'no full-card colour behind the result');
      assert.equal(await page.$eval('[data-target="card:0"] .card-image', el => getComputedStyle(el).zIndex), '1');
      assert.equal(await page.$eval('[data-target="card:0"] .result', el => getComputedStyle(el).zIndex), '0');
    }
    await press(monitor, '[data-layer="front"]');
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
    console.log("PASS: piece width/height, full-card operator vote, SSE-safe controls and keyboard controls");

    const beforeTemplate = await geometry(monitor, "card:0");
    await press(monitor, '[data-layer="result"]');
    for (const [id, value] of [["templateX", 35], ["templateY", -20], ["templateWidth", 220], ["templateHeight", 100]]) {
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
    }
    assert.equal(state.cardTemplate, undefined, "draft never broadcasts");
    close(await geometry(monitor, "card:0"), beforeTemplate, "draft does not move live cards");
    await press(monitor, "#templateUp");
    failSaves = true;
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("บันทึกไม่สำเร็จ"));
    assert.equal(state.cardTemplate, undefined);
    failSaves = false;
    for (const [id, value] of [["templateX", 35], ["templateY", -20], ["templateWidth", 220], ["templateHeight", 100]]) {
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
    }
    await press(monitor, "#templateUp");
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบกับทุกการ์ดแล้ว"));
    assert.equal(state.cardTemplate.result.x, 35);
    assert.equal(state.cardTemplate.result.width, 220);
    assert.deepEqual(state.cardTemplate.order, ["image", "result"]);
    await overlay.waitForFunction(() => document.querySelector('.slot .result').style.left === "35px");
    for (const page of [monitor, overlay]) {
      assert.equal(await page.evaluate(() => [...board.querySelectorAll(".result")].every(el =>
        el.style.width === "220px" && el.style.zIndex === "1")), true);
    }
    // Sizing the flag backdrop is the template's job now. Both layers are boxed
    // explicitly, so the invariant worth holding is that touching the result
    // layer alone never moves the artwork it sits behind.
    // The result layer is not displayed on the status board, so its client rect
    // is all zeros; the template units it was given are what to compare.
    const slotBoxes = () => monitor.$eval('[data-target="card:0"] .slot', el => {
      const image = el.querySelector('.card-image').getBoundingClientRect();
      const result = el.querySelector('.result');
      return { image: { width: image.width, height: image.height },
        result: { width: parseFloat(result.style.width), height: parseFloat(result.style.height) } };
    });
    const templated = await slotBoxes();
    await monitor.$eval("#templateHeight", el => { el.value = 60; el.dispatchEvent(new Event("change")); });
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => lastState.cardTemplate?.result?.height === 60);
    const resized = await slotBoxes();
    assert.deepEqual(resized.image, templated.image, "resizing the result leaves the PNG alone");
    // The canvas is zoomed, so compare the ratio rather than raw client pixels.
    assert.ok(Math.abs(resized.result.height / templated.result.height - 0.6) < 0.01,
      "the result box followed the template");
    assert.equal(await monitor.$eval('[data-target="card:0"] .result', el => el.style.height), "60px");
    await monitor.$eval("#templateHeight", el => { el.value = 100; el.dispatchEvent(new Event("change")); });
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => lastState.cardTemplate?.result?.height === 100);
    await monitor.$eval("#templateX", el => { el.value = 99; el.dispatchEvent(new Event("change")); });
    await press(monitor, "#templateCancel");
    assert.equal(await monitor.$eval("#templateX", el => el.value), "35");
    await press(monitor, "#templateZoomIn");
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
    await press(monitor, "#templateCancel");
    console.log("PASS: template draft, save failure/retry, global rendering, layers, cancel and zoom-correct drag");

    const templateBox = () => monitor.$eval('#templateSelection', el => ({
      x: parseFloat(el.style.left), y: parseFloat(el.style.top),
      width: parseFloat(el.style.width), height: parseFloat(el.style.height)
    }));
    await press(monitor, '#templateFit');
    for (const layer of ["image", "result"]) {
      const zoom = await monitor.$eval("#templateStage", el => new DOMMatrix(getComputedStyle(el).transform).a);
      await press(monitor, '[data-layer="' + layer + '"]');
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
      await press(monitor, '#templateCancel');
    }
    for (const page of [monitor, overlay]) {
      assert.equal(await page.$$eval('.slot, .slot .result', els => els.every(el =>
        getComputedStyle(el).borderRadius === '0px')), true, 'cards and result layers have square corners');
    }
    await press(monitor, '[data-layer="result"]');
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
    await overlay.waitForFunction(() => wrap.hidden, { polling: 50 });
    assert.deepEqual(state.pieces, saved);
    await monitor.reload();
    await ready(6);
    close(await geometry(monitor, "card:0"), placed, "reload restores placement");
    assert.equal(await monitor.$eval("#templateWidth", el => el.value), "220");
    await monitor.select("#selection", "card:0");
    await press(monitor, "#reset");
    await settled();
    assert.equal(state.pieces["card:0"], undefined);
    assert.deepEqual(state.pieces.heading, saved.heading);
    await press(monitor, "#resetAll");
    await press(monitor, "#resetAll");
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
    for (const count of [0, 1, 14, 40, 5]) {
      state = fixture(count); state.phase = "revealed";
      state.bottomBar = { layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {}, logoPresidentId: "0" };
      state.presidents.forEach(p => { p.bottomBarUrl = CARD_PNG; p.logoUrl = CARD_PNG; p.vote = "green"; });
      state.displayMode = "bottomBar"; push();
      await setEditMode(monitor, "bottomBar");
      await ready(count + 4);
      await overlay.waitForFunction(n => board.querySelectorAll(".piece").length === n && document.querySelector(".bottom-bar"), {}, count + 4);
      await overlay.waitForFunction(() => !transition, { polling: 20 });
      for (const page of [overlay, monitor]) assert.equal(await page.evaluate(() => [...document.querySelectorAll(".bottom-motion")].every(el => getComputedStyle(el).clipPath === "none" && getComputedStyle(el).maskImage === "none" && !el.querySelector(".bottom-wipe"))), true, "resting views contain no motion masks");
      for (const target of ["logo", "count:red", "count:green", ...state.presidents.map(p => "card:" + p.id)])
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
      await overlay.waitForFunction(value => board.querySelector('[data-target="card:0"] img').style.opacity === String(value / 100), {}, value);
      assert.equal(state.bottomBar.pieces["card:0"].imageOpacity, value / 100);
      assert.deepEqual(state.pieces, {});
    }
    // A response arriving after switching editors must update only its original mode.
    layoutDelay = 150;
    await monitor.$eval("#resultOpacity", el => { el.value = 50; el.dispatchEvent(new Event("change")); });
    await setEditMode(monitor, "scoreboard");
    await settled(); layoutDelay = 0;
    assert.equal(state.bottomBar.pieces["card:0"].resultOpacity, 0.5);
    assert.deepEqual(state.pieces, {});
    assert.equal(state.displayMode, "bottomBar");
    assert.equal(await overlay.evaluate(() => board.classList.contains("bottom-bar")), true);
    await setEditMode(monitor, "bottomBar");
    // Template opacity is shared until explicitly overridden on a piece.
    await monitor.select("#selection", "card:1");
    await press(monitor, '[data-layer="image"]');
    await monitor.$eval("#templateOpacity", el => { el.value = 50; el.dispatchEvent(new Event("change")); });
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบกับทุกการ์ดแล้ว"));
    await overlay.waitForFunction(() => board.querySelector('[data-target="card:1"] img').style.opacity === "0.5");
    assert.equal(await overlay.$eval('[data-target="card:0"] img', el => el.style.opacity), "1");
    assert.equal(state.cardTemplate, undefined);
    assert.equal(await overlay.$eval('[data-target="logo"] img', el => el.style.opacity), "1", "card template opacity does not affect logo");
    // Cover remains independent from card templates and scoreboard placement.
    state.bottomBar.coverUrl = CARD_PNG; push();
    await monitor.waitForFunction(() => board.querySelector('[data-target="cover"] img').naturalWidth > 0);
    await overlay.waitForFunction(() => board.querySelector('[data-target="cover"] img').naturalWidth > 0, { polling: 50 });
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
      await overlay.waitForFunction(value => board.querySelector('[data-target="cover"] img').style.opacity === String(value / 100), { polling: 50 }, value);
      assert.equal(state.bottomBar.pieces.cover.imageOpacity, value / 100);
      if (value === 50) {
        await monitor.screenshot({ path: "/private/tmp/fff-cover-monitor-review.png" });
        await overlay.screenshot({ path: "/private/tmp/fff-cover-overlay-review.png", omitBackground: true });
      }
    }
    await press(monitor, '[data-layer="back"]');
    await monitor.waitForFunction(() => lastState.layers.cover === 0);
    await overlay.waitForFunction(() => board.querySelector('[data-target="cover"]').style.zIndex === "0", { polling: 50 });
    await press(monitor, '[data-layer="front"]');
    await monitor.waitForFunction(() => lastState.layers.cover === Math.max(...Object.values(lastState.layers)));
    const coverFront = Math.max(...Object.values(state.bottomBar.layers));
    await overlay.waitForFunction(layer => board.querySelector('[data-target="cover"]').style.zIndex === String(layer), { polling: 50 }, coverFront);
    assert.deepEqual(state.pieces, {});
    assert.deepEqual(state.layers, {});
    await press(monitor, "#reset"); await settled();
    assert.equal(state.bottomBar.pieces.cover, undefined);
    close(await geometry(monitor, "cover"), coverBefore, "cover reset restores geometry");
    assert.equal(await overlay.$eval('[data-target="cover"] img', el => el.style.opacity), "1", "card template opacity does not affect cover");
    state.bottomBar.coverUrl = ""; push();
    await overlay.waitForFunction(() => board.querySelector('[data-target="cover"] img').hidden, { polling: 50 });
    console.log("PASS: Cover pointer drag, resize, opacity, layer save, reset/removal and template/mode isolation");

    // Flag counters are pieces in their own right: placed, sized and faded
    // apart from the cards and from each other.
    await monitor.select("#selection", "count:red");
    const cardBeforeCounter = await geometry(monitor, "card:0");
    const counterDrag = await monitor.$eval('[data-target="count:red"]', el => {
      const r = el.getBoundingClientRect();
      return { x: r.x + r.width / 2, y: r.y + r.height / 2, dx: 40, dy: -30 };
    });
    await monitor.mouse.move(counterDrag.x, counterDrag.y);
    await monitor.mouse.down();
    assert.equal(await monitor.evaluate(() => dragging && selected === "count:red"), true);
    await monitor.mouse.move(counterDrag.x + counterDrag.dx, counterDrag.y + counterDrag.dy, { steps: 5 });
    await monitor.mouse.up(); await settled();
    assert.ok(state.bottomBar.pieces["count:red"], "the red counter saves its own placement");
    assert.equal(state.bottomBar.pieces["count:green"], undefined, "moving one counter leaves the other alone");
    close(await geometry(monitor, "card:0"), cardBeforeCounter, "counter drag leaves cards in place");
    close(await geometry(monitor, "count:red"), await geometry(overlay, "count:red"), "counter geometry is shared");
    for (const [id, key, value] of [["pieceWidth", "scaleX", 150], ["pieceHeight", "scaleY", 80]]) {
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
      await settled();
      assert.equal(state.bottomBar.pieces["count:red"][key], value / 100);
    }
    await monitor.$eval("#imageOpacity", el => { el.value = 40; el.dispatchEvent(new Event("change")); });
    await settled();
    await overlay.waitForFunction(() => board.querySelector('[data-target="count:red"] .count-value').style.opacity === "0.4", { polling: 50 });
    assert.equal(await overlay.$eval('[data-target="count:green"] .count-value', el => el.style.opacity), "1",
      "counter opacity is per piece");

    // The logo and the counters each carry a template of their own.
    const cardTemplateBefore = JSON.stringify(state.bottomBar.cardTemplate ?? null);
    await monitor.select("#selection", "logo");
    assert.equal(await monitor.$eval("#templateTitle", el => el.textContent), "แม่แบบโลโก้กลาง");
    assert.equal(await monitor.$eval("#templateLayers", el => el.hidden), true, "a one-box template hides the layer list");
    for (const [id, value] of [["templateX", 12], ["templateY", -8], ["templateWidth", 260], ["templateHeight", 200]])
      await monitor.$eval("#" + id, (el, value) => { el.value = value; el.dispatchEvent(new Event("change")); }, value);
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบโลโก้แล้ว"));
    assert.deepEqual(state.bottomBar.logoTemplate.image, { x: 12, y: -8, width: 260, height: 200 });
    assert.equal(JSON.stringify(state.bottomBar.cardTemplate ?? null), cardTemplateBefore,
      "the logo template leaves the card template alone");
    await overlay.waitForFunction(() => board.querySelector('[data-target="logo"] img').style.width === "260px", { polling: 50 });
    // The counter number is the one piece of text on the bar, so it takes a
    // family installed on this machine and each colour is set on its own.
    await monitor.select("#selection", "count:green");
    assert.equal(await monitor.$eval("#templateFontFields", el => el.hidden), false, "counters expose the font controls");
    await monitor.$eval("#templateFontManual", el => { el.value = "Menlo"; el.dispatchEvent(new Event("change")); });
    await monitor.$eval("#templateFontSize", el => { el.value = 140; el.dispatchEvent(new Event("change")); });
    await reveal(monitor, "#templateFontWeight");
    await monitor.select("#templateFontWeight", "300");
    await monitor.$eval("#templateColorValue", el => { el.value = "#00cc66"; el.dispatchEvent(new Event("input")); });
    // The CSSOM drops the quotes around a family name that is a bare identifier.
    const family = el => el.style.fontFamily.replace(/["']/g, "");
    assert.equal(await monitor.$eval("#templateFontPreview", el => el.style.fontFamily.replace(/["']/g, "")), "Menlo",
      "the sample uses the family that was chosen");
    // The weight used to be pinned at 700 in the stylesheet while the sample
    // carried none at all, so the panel and the stream could never agree.
    assert.equal(await monitor.$eval("#templateFontPreview", el => el.style.fontWeight), "300",
      "the sample carries the weight that was chosen");
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบตัวเลขแล้ว"));
    assert.equal(state.bottomBar.countTemplate.fontFamily, "Menlo");
    assert.equal(state.bottomBar.countTemplate.fontSize, 140);
    assert.equal(state.bottomBar.countTemplate.fontWeight, 300);
    assert.equal(state.bottomBar.countTemplate.colors.green, "#00cc66");
    assert.equal(state.bottomBar.countTemplate.colors.red, "#e23c3c", "the other counter keeps its colour");
    await monitor.select("#selection", "count:red");
    await monitor.$eval("#templateColorValue", el => { el.value = "#ff0055"; el.dispatchEvent(new Event("input")); });
    await press(monitor, "#templateApply");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบตัวเลขแล้ว"));
    assert.equal(state.bottomBar.countTemplate.colors.red, "#ff0055");
    assert.equal(state.bottomBar.countTemplate.colors.green, "#00cc66", "setting one colour keeps the other");
    // One family and size for both counters, but a colour each.
    for (const page of [overlay, monitor]) {
      await page.waitForFunction(() => {
        const red = board.querySelector('[data-target="count:red"] .count-value');
        const green = board.querySelector('[data-target="count:green"] .count-value');
        const name = el => el.style.fontFamily.replace(/["']/g, "");
        return name(red) === "Menlo" && name(green) === "Menlo" &&
          red.style.fontSize === "140px" && red.style.fontWeight === "300" &&
          green.style.fontWeight === "300" && red.style.color === "rgb(255, 0, 85)" &&
          green.style.color === "rgb(0, 204, 102)";
      }, { polling: 50 });
    }
    // The reported bug: the panel showed one weight and the stream another. The
    // counter on air and the sample in the panel must resolve to the same face.
    const weights = await monitor.evaluate(() => ({
      sample: getComputedStyle(document.getElementById("templateFontPreview")).fontWeight,
      stage: getComputedStyle(document.getElementById("templateCount")).fontWeight,
      counter: getComputedStyle(board.querySelector('[data-target="count:red"] .count-value')).fontWeight }));
    assert.deepEqual(weights, { sample: "300", stage: "300", counter: "300" },
      "sample, stage and the counter on air all render at the chosen weight");
    assert.equal(await overlay.evaluate(() =>
      getComputedStyle(board.querySelector('[data-target="count:red"] .count-value')).fontSynthesisWeight), "none",
      "the overlay never fakes a weight the font has no face for");
    // Space belongs to whatever field has focus before it belongs to the hand.
    await reveal(monitor, "#templateFontManual");
    await monitor.focus("#templateFontManual");
    await monitor.keyboard.press("Space");
    assert.equal(await monitor.$eval("#frame", el => el.classList.contains("is-panning")), false,
      "Space in a text field does not arm the hand tool");
    assert.ok((await monitor.$eval("#templateFontManual", el => el.value)).includes(" "),
      "Space in a text field types a space");
    await monitor.$eval("#templateFontManual", el => { el.value = "Menlo"; el.blur(); });
    console.log("PASS: counter placement, per-piece opacity, logo and counter templates");

    // Zoom and pan are view state. Nothing the operator saved may move because
    // the preview is being looked at from closer up.
    const canvasInfo = () => monitor.evaluate(() => {
      const rect = document.getElementById("canvas").getBoundingClientRect();
      return { scale: rect.width / 1920, left: rect.left, top: rect.top,
        label: document.getElementById("canvasZoomValue").textContent };
    });
    const fitted = await canvasInfo();
    assert.equal(fitted.label, Math.round(fitted.scale * 100) + "%", "the readout matches the live scale");
    const spot = await monitor.$eval("#frame", el => {
      const r = el.getBoundingClientRect();
      return { x: Math.round(r.x + r.width * 0.3), y: Math.round(r.y + r.height * 0.7) };
    });
    const pointUnder = info => ({ x: (spot.x - info.left) / info.scale, y: (spot.y - info.top) / info.scale });
    const anchored = pointUnder(fitted);
    await monitor.mouse.move(spot.x, spot.y);
    await monitor.mouse.wheel({ deltaY: -400 });
    const zoomed = await canvasInfo();
    assert.ok(zoomed.scale > fitted.scale * 1.2, "the wheel zooms in");
    assert.equal(zoomed.label, Math.round(zoomed.scale * 100) + "%", "the readout follows the wheel");
    const stillThere = pointUnder(zoomed);
    assert.ok(Math.abs(stillThere.x - anchored.x) < 1 && Math.abs(stillThere.y - anchored.y) < 1,
      `zoom stays anchored at the cursor: ${JSON.stringify(anchored)} vs ${JSON.stringify(stillThere)}`);

    const piecesBefore = JSON.stringify(state.bottomBar.pieces);
    // preventScroll, or focusing here scrolls the page and every client
    // coordinate measured above goes stale.
    const selectedBefore = await monitor.evaluate(() => { canvas.focus({ preventScroll: true }); return selected; });
    await monitor.keyboard.down("Space");
    assert.equal(await monitor.$eval("#frame", el => el.classList.contains("is-panning")), true,
      "holding Space arms the hand tool");
    await monitor.mouse.move(spot.x, spot.y);
    await monitor.mouse.down();
    await monitor.mouse.move(spot.x + 60, spot.y - 40, { steps: 5 });
    await monitor.mouse.up();
    await monitor.keyboard.up("Space");
    const panned = await canvasInfo();
    assert.ok(Math.abs(panned.left - zoomed.left - 60) < 2 && Math.abs(panned.top - zoomed.top + 40) < 2,
      "Space-drag moves the view by the drag distance");
    assert.ok(Math.abs(panned.scale - zoomed.scale) < 0.001, "panning leaves the zoom alone");
    assert.equal(JSON.stringify(state.bottomBar.pieces), piecesBefore, "panning moves no piece");
    assert.equal(await monitor.evaluate(() => selected), selectedBefore, "panning selects nothing");
    assert.equal(await monitor.$eval("#frame", el => el.classList.contains("is-panning")), false,
      "releasing Space puts the hand away");

    await press(monitor, "#canvasZoomActual");
    const actual = await canvasInfo();
    assert.ok(Math.abs(actual.scale - 1) < 0.001, "1:1 shows the canvas at its own pixels");
    assert.equal(actual.label, "100%");
    await press(monitor, "#canvasZoomFit");
    assert.ok(Math.abs((await canvasInfo()).scale - fitted.scale) < 0.002, "Fit returns to the framed view");

    // A drag has to follow the pointer in canvas pixels at whatever zoom is on
    // screen. Anchoring the wheel on the card keeps it under the cursor.
    await monitor.select("#selection", "card:0");
    const cardAt = await monitor.$eval('[data-target="card:0"]', el => {
      const r = el.getBoundingClientRect();
      return { x: Math.round(r.x + r.width / 2), y: Math.round(r.y + r.height / 2) };
    });
    await monitor.mouse.move(cardAt.x, cardAt.y);
    await monitor.mouse.wheel({ deltaY: -750 });
    const dragScale = (await canvasInfo()).scale;
    assert.ok(dragScale > fitted.scale * 2, "the card is zoomed well past the fitted view");
    const before = await geometry(monitor, "card:0");
    await monitor.mouse.down();
    await monitor.mouse.move(cardAt.x + 60, cardAt.y - 40, { steps: 5 });
    await monitor.mouse.up(); await settled();
    const after = await geometry(monitor, "card:0");
    const moved = { x: after.x - before.x, y: after.y - before.y };
    // Generous, because a drag still snaps; the point is that it followed the
    // zoomed scale and not the fitted one, which would be three times further.
    assert.ok(Math.abs(moved.x - 60 / dragScale) < 30 && Math.abs(moved.y + 40 / dragScale) < 30,
      `a drag while zoomed follows the pointer: ${JSON.stringify(moved)} at scale ${dragScale}`);
    await press(monitor, "#canvasZoomFit");

    // The template stage is the other preview and behaves the same way.
    await monitor.select("#selection", "count:red");
    await monitor.evaluate(() => document.activeElement.blur());
    const stageInfo = () => monitor.evaluate(() => {
      const stage = document.getElementById("templateStage");
      const r = stage.getBoundingClientRect();
      return { scale: new DOMMatrix(getComputedStyle(stage).transform).a, left: r.left, top: r.top,
        label: document.getElementById("templateZoomValue").textContent };
    });
    const overStage = await monitor.$eval("#templateViewport", el => {
      const r = el.getBoundingClientRect();
      return { x: Math.round(r.x + r.width / 2), y: Math.round(r.y + r.height / 2) };
    });
    const stageBefore = await stageInfo();
    const stagePoint = info => ({ x: (overStage.x - info.left) / info.scale, y: (overStage.y - info.top) / info.scale });
    const stageAnchored = stagePoint(stageBefore);
    await monitor.mouse.move(overStage.x, overStage.y);
    await monitor.mouse.wheel({ deltaY: -300 });
    const stageZoomed = await stageInfo();
    assert.ok(stageZoomed.scale > stageBefore.scale, "the stage wheel zooms in");
    const stageStill = stagePoint(stageZoomed);
    assert.ok(Math.abs(stageStill.x - stageAnchored.x) < 1 && Math.abs(stageStill.y - stageAnchored.y) < 1,
      "stage zoom stays anchored at the cursor");
    const boxBefore = JSON.stringify(state.bottomBar.countTemplate.value);
    await monitor.keyboard.down("Space");
    await monitor.mouse.down();
    await monitor.mouse.move(overStage.x - 30, overStage.y + 20, { steps: 4 });
    await monitor.mouse.up();
    await monitor.keyboard.up("Space");
    const stagePanned = await stageInfo();
    assert.ok(Math.abs(stagePanned.left - stageZoomed.left + 30) < 2 &&
      Math.abs(stagePanned.top - stageZoomed.top - 20) < 2, "Space-drag pans the stage");
    assert.equal(JSON.stringify(state.bottomBar.countTemplate.value), boxBefore, "panning the stage edits no box");
    assert.equal(await monitor.$eval("#templateApply", el => el.disabled), true, "panning the stage dirties no draft");
    await press(monitor, "#templateFit");
    console.log("PASS: cursor-anchored zoom, Space pan, 1:1/Fit and zoom-correct dragging");
    await monitor.select("#selection", "card:0");
    templateDelay = 500;
    await monitor.$eval("#templateOpacity", el => { el.value = 25; el.dispatchEvent(new Event("change")); });
    await press(monitor, "#templateApply");
    await setEditMode(monitor, "scoreboard");
    await setEditMode(monitor, "bottomBar");
    await monitor.waitForFunction(() => document.getElementById("templateStatus").textContent.includes("ใช้แม่แบบกับทุกการ์ดแล้ว"));
    assert.equal(await monitor.$eval("#templateOpacity", el => el.value), "25", "mode round-trip refreshes saved template draft");
    templateDelay = 0;
    state.bottomBar.logoPresidentId = "missing"; push();
    await overlay.waitForFunction(() => board.querySelector('[data-target="logo"] img').hidden);
    assert.equal(await overlay.$eval('[data-target="logo"]', el => getComputedStyle(el).backgroundColor), "rgba(0, 0, 0, 0)");
    await press(monitor, "#templateFit");
    await monitor.screenshot({ path: "/private/tmp/fff-bottom-bar-monitor.png" });
    await overlay.screenshot({ path: "/private/tmp/fff-bottom-bar-overlay.png", omitBackground: true });
    console.log("Clear precondition:", await overlay.evaluate(() => ({ visibility: document.visibilityState, stream: stream.readyState, phase: lastState.phase, transition: transition && { entering: transition.entering, generation: transition.generation } })));
    state.phase = "collecting"; push();
    await overlay.waitForFunction(() => lastState.phase === "collecting", { polling: 50 });
    await overlay.waitForFunction(() => wrap.hidden, { polling: 50 });
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

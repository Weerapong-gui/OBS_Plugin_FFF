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
  window.slotMotions = () => [...board.querySelectorAll(':scope > .piece[data-target^="card:"]')].map(piece => {
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

    // The lengths are the timing module's to state; these checks are about the
    // shape of the motion and about the page honouring whatever it is given.
    const DEFAULTS = await page.evaluate(() => JSON.parse(JSON.stringify(FFF_TIMING_DEFAULTS)));
    const SB = DEFAULTS.scoreboard;

    const five = roster(5);
    await page.evaluate(s => render(s), five);
    assert.equal(await page.evaluate(() => getComputedStyle(board.querySelector(":scope > .piece > .slot")).willChange),
      "transform, opacity", "cards on deck are promoted before the entrance");
    // The title animates with them, so it is promoted with them: creating its
    // layer on the reveal frame is exactly what this rule exists to prevent.
    assert.equal(await page.evaluate(() =>
      getComputedStyle(board.querySelector('[data-target="heading"] .heading-text')).willChange),
      "transform, opacity", "and so is the title");

    const entrance = await page.evaluate(s => {
      render({ ...s, phase: "revealed" });
      return { hidden: wrap.hidden, motions: slotMotions() };
    }, five);
    assert.equal(entrance.hidden, false, "the press shows the board in the same task");
    entrance.motions.forEach((motion, i) => {
      assert.ok(motion, `card ${i} animates`);
      assert.equal(motion.delay, i * SB.stagger, `card ${i} enters after the one before it`);
      assert.equal(motion.duration, SB.card);
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
      assert.equal(motion.delay, i * SB.exitStagger, `card ${i} leaves after the one before it`);
      assert.equal(motion.duration, SB.exit);
      assert.equal(motion.last, "translateX(48px)", "cards leave to the right");
      assert.deepEqual(motion.properties, ["opacity", "transform"]);
    });
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    const fourteen = roster(14);
    await page.evaluate(s => render(s), fourteen);
    const longEntrance = await page.evaluate(s => { render({ ...s, phase: "revealed" }); return slotMotions().map(m => m.delay); }, fourteen);
    assert.ok(Math.abs(longEntrance[13] - SB.maxStagger) < 0.001 && longEntrance.every((d, i) => i === 0 || d > longEntrance[i - 1]),
      "a long roster still starts every card within the entrance's stagger cap, in order");
    await page.waitForFunction(() => !transition, { polling: 20 });
    const longExit = await page.evaluate(s => { render(s); return slotMotions().map(m => m.delay); }, fourteen);
    assert.ok(Math.abs(longExit[13] - SB.maxExitStagger) < 0.001, "and leaves within the exit's cap");
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    await page.evaluate(s => render({ ...s, phase: "revealed" }), five);
    await page.waitForFunction(() => !transition, { polling: 20 });
    const switching = await page.evaluate(s => {
      render({ ...s, phase: "revealed", displayMode: "bottomBar" });
      return { entering: transition.entering, scoreboardShown: !views.scoreboard.wrap.hidden };
    }, five);
    assert.deepEqual(switching, { entering: false, scoreboardShown: true }, "switching to BOTTOM BAR lets the cards leave first");
    await page.waitForFunction(() => views.scoreboard.wrap.hidden && !views.bottomBar.wrap.hidden, { polling: 20 });

    // ---- the card's box is the PNG's box --------------------------------
    // The operator places these by eye, so a letterbox margin inside the slot
    // is the difference between a card that looks centred and one that is.
    const pngOf = (width, height) => page.evaluate((w, h) => {
      const canvas = document.createElement("canvas");
      canvas.width = w; canvas.height = h;
      const context = canvas.getContext("2d");
      context.fillStyle = "#e23c3c";
      context.fillRect(0, 0, w, h);
      return canvas.toDataURL("image/png");
    }, width, height);
    const boxes = () => page.evaluate(() => [...board.querySelectorAll(':scope > .piece[data-target^="card:"]')].map(piece => {
      const slot = piece.querySelector(".slot"), image = piece.querySelector(".card-image");
      const slotBox = slot.getBoundingClientRect(), imageBox = image.getBoundingClientRect();
      return { slot: { width: slotBox.width, height: slotBox.height },
        image: { width: imageBox.width, height: imageBox.height },
        natural: { width: image.naturalWidth, height: image.naturalHeight },
        offsetX: imageBox.left - slotBox.left, offsetY: imageBox.top - slotBox.top };
    }));
    const sized = [[300, 120], [90, 200], [512, 512]];
    const art = [];
    for (const [width, height] of sized) art.push(await pngOf(width, height));
    const shaped = {
      ...roster(3), phase: "revealed",
      presidents: roster(3).presidents.map((president, i) => ({ ...president, cardUrl: art[i], statusUrl: art[i] }))
    };
    await page.evaluate(s => render({ ...s, phase: "collecting" }), shaped);
    await page.waitForFunction(() => [...board.querySelectorAll(".card-image")].every(el => el.naturalWidth > 0),
      { polling: 20 });
    await page.evaluate(s => render(s), shaped);
    await page.waitForFunction(() => !transition, { polling: 20 });
    const measured = await boxes();
    measured.forEach((box, i) => {
      assert.deepEqual(box.natural, { width: sized[i][0], height: sized[i][1] }, `card ${i} serves its own PNG`);
      assert.deepEqual(box.slot, box.natural, `card ${i}'s box is its PNG's box`);
      assert.deepEqual(box.image, box.slot, `card ${i} fills its box`);
      assert.deepEqual({ x: box.offsetX, y: box.offsetY }, { x: 0, y: 0 }, `card ${i} has no letterbox margin`);
    });

    // A card template no longer sets the size on Show Status, and replacing the
    // artwork moves the box with it.
    const templated = { ...shaped,
      cardTemplate: { image: { x: 40, y: 25, width: 416, height: 148 },
        result: { x: 0, y: 0, width: 200, height: 100 }, order: ["result", "image"] } };
    await page.evaluate(s => render(s), templated);
    assert.deepEqual((await boxes()).map(box => box.slot), measured.map(box => box.natural),
      "a card template does not resize a Show Status card");
    const taller = await pngOf(150, 400);
    const swapped = { ...templated,
      presidents: templated.presidents.map((president, i) => i === 0
        ? { ...president, cardUrl: taller, statusUrl: taller } : president) };
    await page.evaluate(s => render(s), swapped);
    await page.waitForFunction(() => board.querySelector(".card-image").naturalWidth === 150, { polling: 20 });
    await page.waitForFunction(() => board.querySelector(".slot").getBoundingClientRect().width === 150,
      { polling: 20 });
    assert.deepEqual((await boxes())[0].slot, { width: 150, height: 400 },
      "a replaced PNG takes the card's box with it");

    // ---- the stack sits where the broadcast artwork draws it ------------
    // Measured off data/web reference artwork: rows 55px from the left, the
    // first at 201px, each 57.23px below the one before it.
    const STACK = await page.evaluate(() => ({ ...STATUS_STACK }));
    assert.deepEqual(STACK, { left: 55, top: 201, pitch: 57.23 });
    const origins = () => page.evaluate(() => [...board.querySelectorAll(':scope > .piece[data-target^="card:"]')]
      .map(piece => { const box = piece.getBoundingClientRect(); return { left: box.left, top: box.top }; }));
    for (const count of [1, 5, 14]) {
      const many = { ...roster(count), phase: "revealed",
        presidents: roster(count).presidents.map(president => ({ ...president, cardUrl: art[0], statusUrl: art[0] })) };
      await page.evaluate(s => render({ ...s, phase: "collecting" }), many);
      await page.waitForFunction(n => board.querySelectorAll('.piece[data-target^="card:"]').length === n,
        { polling: 20 }, count);
      await page.evaluate(s => render(s), many);
      await page.waitForFunction(() => !transition, { polling: 20 });
      const placed = await origins();
      assert.equal(placed.length, count, `roster ${count} draws every row`);
      placed.forEach((box, i) => {
        assert.ok(Math.abs(box.left - STACK.left) < 0.5, `roster ${count} row ${i} starts at the left margin`);
        assert.ok(Math.abs(box.top - (STACK.top + i * STACK.pitch)) < 0.5,
          `roster ${count} row ${i} sits on the artwork's pitch`);
        // A row on a half pixel is a row the compositor resamples.
        assert.equal(box.top, Math.round(box.top), `roster ${count} row ${i} lands on a whole pixel`);
      });
      await page.evaluate(s => render({ ...s, phase: "collecting" }), many);
      await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });
    }
    // A roster with no artwork yet is still one readable row per president:
    // the empty frame is the reference card's size, not a box three rows tall.
    const blank = { ...roster(5), phase: "revealed",
      presidents: roster(5).presidents.map(president => ({ ...president, cardUrl: "", statusUrl: "" })) };
    await page.evaluate(s => render({ ...s, phase: "collecting" }), blank);
    await page.evaluate(s => render(s), blank);
    await page.waitForFunction(() => !transition, { polling: 20 });
    const empty = await page.evaluate(() => [...board.querySelectorAll(':scope > .piece[data-target^="card:"]')]
      .map(piece => piece.querySelector(".slot").getBoundingClientRect())
      .map(box => ({ top: box.top, height: box.height, width: box.width })));
    empty.forEach((box, i) => {
      assert.deepEqual([box.width, box.height], [346, 51], `empty row ${i} is the reference card's size`);
      if (i) assert.ok(box.top >= empty[i - 1].top + empty[i - 1].height,
        `empty row ${i} does not overlap the row above it`);
    });
    await page.evaluate(s => render({ ...s, phase: "collecting" }), blank);
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    // A saved placement still wins: the stack is only where an untouched card
    // goes, never where a card the operator moved is put back.
    const pinned = { ...shaped, pieces: { "card:1": { x: 0.8, y: 0.25, scale: 1 } } };
    await page.evaluate(s => render(s), pinned);
    await page.waitForFunction(() => !transition, { polling: 20 });
    const withOverride = await origins();
    assert.ok(Math.abs(withOverride[0].left - STACK.left) < 0.5, "an untouched card keeps its place");
    assert.ok(withOverride[1].left > 1000, "a placed card stays where it was put");
    await page.evaluate(s => render({ ...s, phase: "collecting" }), pinned);
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    // ---- the title is a piece with its own words and type ---------------
    const titled = { ...shaped,
      headingTemplate: { box: { x: 0, y: 0, width: 500, height: 120 }, text: "FFF\nROUND 2",
        fontFamily: "", fontSize: 44, fontWeight: 900, lineHeight: 50, align: "center", color: "#21b04a" } };
    await page.evaluate(s => render(s), titled);
    await page.waitForFunction(() => !transition, { polling: 20 });
    const heading = await page.evaluate(() => {
      const piece = board.querySelector('[data-target="heading"]');
      const text = piece.querySelector(".heading-text"), style = getComputedStyle(text);
      const box = piece.getBoundingClientRect();
      return { content: text.textContent, size: style.fontSize, weight: style.fontWeight,
        lineHeight: style.lineHeight, align: style.textAlign, color: style.color,
        wrap: style.whiteSpace, left: box.left, top: box.top,
        width: text.getBoundingClientRect().width, height: text.getBoundingClientRect().height };
    });
    assert.equal(heading.content, "FFF\nROUND 2", "the title says what the session says");
    assert.deepEqual([heading.size, heading.weight, heading.lineHeight, heading.align, heading.color],
      ["44px", "900", "50px", "center", "rgb(33, 176, 74)"], "and carries its own type and colour");
    assert.equal(heading.wrap, "pre-line", "so a two-line title really is two lines");
    assert.deepEqual([heading.width, heading.height], [500, 120], "the template's box is the title's box");
    assert.deepEqual([heading.left, heading.top], [54, 72], "drawn where the artwork puts it");
    await page.evaluate(s => render({ ...s, phase: "collecting" }), titled);
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    // ---- the lengths are the session's to set ---------------------------
    const slower = { ...swapped,
      timing: { scoreboard: { card: 900, stagger: 40, exit: 500 }, board: { reveal: 1000 } } };
    await page.evaluate(s => render({ ...s, phase: "collecting" }), slower);
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });
    const slowEntrance = await page.evaluate(s => { render(s); return slotMotions(); }, slower);
    slowEntrance.forEach((motion, i) => {
      assert.equal(motion.duration, 900, `card ${i} uses the session's entrance length`);
      assert.equal(motion.delay, i * 40, `card ${i} uses the session's stagger`);
      assert.equal(motion.easing, "cubic-bezier(0.22, 1, 0.36, 1)", "the easing is the design and never moves");
    });
    assert.equal(await page.evaluate(() =>
      getComputedStyle(document.documentElement).getPropertyValue("--fff-dur-reveal").trim()), "1000ms",
      "CSS reads its lengths from the same place");
    await page.waitForFunction(() => !transition, { polling: 20 });
    const slowExit = await page.evaluate(s => {
      render({ ...s, phase: "collecting" });
      return slotMotions().map(motion => motion.duration);
    }, slower);
    assert.deepEqual(slowExit, [500, 500, 500], "the exit follows too");
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });
    // A length the session leaves out keeps its default rather than becoming zero.
    const partial = { ...slower, timing: { scoreboard: { card: 900 } } };
    const mixed = await page.evaluate(s => { render(s); return slotMotions(); }, partial);
    assert.equal(mixed[1].delay, DEFAULTS.scoreboard.stagger, "an unset length keeps its default");
    await page.waitForFunction(() => !transition, { polling: 20 });
    await page.evaluate(s => render({ ...s, phase: "collecting" }), partial);
    await page.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });

    assert.deepEqual(errors, []);
    console.log("PASS: Show Status slides in and out card by card on compositor properties");
    console.log("PASS: a card's box is its PNG's box, and every length comes from the session");
    console.log("PASS: the stack and the title open where the broadcast artwork draws them");
  } finally {
    await browser.close();
    server.close();
  }
})().catch(error => { console.error(error); server.close(); process.exitCode = 1; });

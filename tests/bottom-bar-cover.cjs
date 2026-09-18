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
const PNG_BYTES = Buffer.from(PNG.split(",")[1], "base64");
// Counted per URL so the reveal can be checked for doing no fetching at all.
const served = new Map();
const server = http.createServer((req, res) => {
  if (req.url.startsWith("/img/")) {
    served.set(req.url, (served.get(req.url) || 0) + 1);
    res.writeHead(200, { "Content-Type": "image/png", "Cache-Control": "public, max-age=31536000, immutable" });
    res.end(PNG_BYTES);
    return;
  }
  const file = path.join(web, req.url === "/monitor" ? "monitor.html" : req.url === "/overlay" ? "overlay.html" : req.url.slice(1));
  if (!file.startsWith(web + path.sep) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.setHeader("Content-Type", file.endsWith(".js") ? "text/javascript" : file.endsWith(".css") ? "text/css" : "text/html");
  res.end(fs.readFileSync(file));
});
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
// The flag counters roll on a clock of their own. Every check below is about
// pieces sliding on and off air, so they all ignore counter motion.
const installEntranceHelper = () => {
  window.entranceAnimations = () => board.getAnimations({ subtree: true })
    .filter(animation => !animation.effect.target.classList.contains("count-value"));
  window.countRolls = () => board.getAnimations({ subtree: true })
    .filter(animation => animation.effect.target.classList.contains("count-value"));
};
(async () => {
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const browser = await puppeteer.launch({ executablePath: process.env.FFF_BROWSER || "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser", headless: true, args: ["--no-sandbox"] });
  const errors = [];
  try {
    const overlay = await browser.newPage(), monitor = await browser.newPage();
    for (const [page, route] of [[overlay, "overlay"], [monitor, "monitor"]]) {
      page.on("pageerror", error => errors.push(error.message));
      await page.evaluateOnNewDocument(() => { window.EventSource = class {}; });
      await page.evaluateOnNewDocument(installEntranceHelper);
      await page.setViewport({ width: 1920, height: 1080 });
      await page.goto(`http://127.0.0.1:${server.address().port}/${route}`);
    }
    const push = async () => {
      await overlay.evaluate(s => render(s), state);
      await monitor.evaluate(s => { editMode = "bottomBar"; render(s); }, state);
    };
    // The lengths belong to the timing module; these checks are about the shape
    // of the entrance and about the page honouring whatever it is given.
    const BB = await overlay.evaluate(() => JSON.parse(JSON.stringify(FFF_TIMING_DEFAULTS.bottomBar)));
    await push();
    const entrance = await overlay.evaluate(() => Object.fromEntries([...board.querySelectorAll(".piece")].map(piece => {
      const motion = piece.querySelector(".bottom-motion");
      const animation = motion.getAnimations().find(a => a.effect.getKeyframes().some(k => k.opacity !== undefined));
      return [piece.dataset.target, { timing: animation.effect.getTiming(), frames: animation.effect.getKeyframes() }];
    })));
    assert.equal(entrance.logo.timing.duration, BB.logo, "logo enters on the logo timing");
    assert.equal(entrance.cover.timing.duration, BB.cover, "cover fades on the cover timing");
    for (const target of ["card:a", "card:b"]) {
      assert.equal(entrance[target].timing.duration, BB.card);
      assert.equal(entrance[target].timing.delay, BB.firstPair);
      assert.equal(entrance[target].timing.easing, "cubic-bezier(0.22, 1, 0.36, 1)");
      assert.match(entrance[target].frames[0].transform, /18px/);
      assert.ok(entrance[target].frames.some(frame => frame.clipPath && frame.clipPath !== "none"), "cards reveal through a clip");
    }
    assert.match(entrance.logo.frames[0].transform, /12px/);
    for (const target of ["count:red", "count:green"]) {
      assert.equal(entrance[target].timing.duration, BB.logo, "counters enter on the logo's timing");
      assert.match(entrance[target].frames[0].transform, /12px/);
    }
    assert.ok(entrance.cover.frames.every(frame => !frame.transform || frame.transform === "none"), "cover never travels");
    const wipeEdge = await overlay.evaluate(() => {
      const el = board.querySelector('[data-target="card:a"] .bottom-motion');
      for (const a of el.getAnimations({ subtree: true })) { a.pause(); a.currentTime = 250; }
      const wipe = el.querySelector(".bottom-wipe"), result = el.querySelector(".result");
      const css = getComputedStyle(wipe);
      return { top: new DOMMatrix(css.transform).m42, clip: parseFloat(getComputedStyle(el).clipPath.slice(6)),
        height: parseFloat(css.height), color: css.backgroundColor, resultColor: getComputedStyle(result).backgroundColor };
    });
    assert.equal(wipeEdge.height, 12);
    assert.equal(wipeEdge.color, wipeEdge.resultColor);
    assert.ok(Math.abs(wipeEdge.top - wipeEdge.clip) < 0.05, "12px wipe follows reveal edge without shrinking behind clip");
    const continuous = await overlay.evaluate(s => {
      const before = entranceAnimations();
      for (const animation of before) { animation.pause(); animation.currentTime = 100; }
      s.presidents[1].vote = "red";
      s.bottomBar.coverUrl = "";
      render(s);
      const after = entranceAnimations();
      const result = before.length > 0 && before.every(a => after.includes(a) && a.currentTime === 100);
      for (const animation of before) animation.play();
      return result;
    }, state);
    assert.equal(continuous, true, "vote during entrance preserves animation identity and progress");
    await overlay.waitForFunction(() => entranceAnimations().length === 0, { polling: 20 });
    const resting = async page => assert.equal(await page.evaluate(() => [...board.querySelectorAll(".bottom-motion")].every(el => {
      const css = getComputedStyle(el);
      return css.clipPath === "none" && css.maskImage === "none" && css.transform === "none" &&
        css.opacity === "1" && css.willChange === "auto" && !el.querySelector(".bottom-wipe");
    })), true, "resting and Monitor pieces release clipping, wipes, transforms and compositing hints");
    await resting(overlay); await resting(monitor);
    for (const page of [overlay, monitor]) {
      const mutations = await page.evaluate(s => {
        const observer = new MutationObserver(() => {});
        observer.observe(board, { childList: true });
        s.presidents[0].vote = "red";
        render(s);
        const count = observer.takeRecords().length;
        observer.disconnect();
        return count;
      }, state);
      assert.equal(mutations, 0, "live vote keeps existing pieces attached and in place");
    }
    // Isolated pages give each roster a genuine fresh reveal, with all effects frozen
    // in the same JS turn so timing assertions do not depend on scheduler speed.
    for (const count of [0, 1, 5, 14, 40]) {
      const page = await browser.newPage();
      await page.evaluateOnNewDocument(() => { window.EventSource = class {}; });
      await page.evaluateOnNewDocument(installEntranceHelper);
      await page.goto(`http://127.0.0.1:${server.address().port}/overlay`);
      const roster = { ...state, presidents: Array.from({ length: count }, (_, i) => ({
        id: String(i), name: "Fixture " + (i + 1), school: "Test school", vote: i % 2 ? "none" : i % 4 ? "red" : "green", bottomBarUrl: PNG
      })) };
      const capture = process.env.FFF_MOTION_SCREENSHOTS && count === 14;
      if (capture) {
        await page.setViewport({ width: 1920, height: 1080 });
        const artwork = await page.evaluate(() => {
          // Portrait school-card specimens, drawn at 2x the default slot size.
          // Alpha windows expose the real result layer, including waiting slots.
          const c = document.createElement("canvas"); c.width = 242; c.height = 500;
          const ctx = c.getContext("2d");
          const cards = Array.from({ length: 14 }, (_, i) => {
            ctx.clearRect(0, 0, c.width, c.height);
            ctx.fillStyle = "rgba(17, 25, 35, .88)"; ctx.fillRect(6, 8, 230, 372);
            ctx.strokeStyle = "#b9c8d2"; ctx.lineWidth = 2; ctx.strokeRect(9, 11, 224, 478);
            ctx.fillStyle = "#e2c585"; ctx.fillRect(22, 24, 32, 4);
            ctx.strokeStyle = "#e2c585"; ctx.lineWidth = 3;
            ctx.beginPath(); ctx.moveTo(121, 86); ctx.lineTo(183, 124);
            ctx.lineTo(170, 211); ctx.lineTo(121, 251); ctx.lineTo(72, 211);
            ctx.lineTo(59, 124); ctx.closePath(); ctx.stroke();
            ctx.textAlign = "center"; ctx.fillStyle = "#f1f4f6";
            ctx.font = "bold 54px sans-serif"; ctx.fillText(String(i + 1).padStart(2, "0"), 121, 190);
            ctx.font = "22px sans-serif"; ctx.fillText("SCHOOL", 121, 304);
            ctx.font = "16px sans-serif"; ctx.fillText("FIXTURE ARTWORK", 121, 337);
            // The bottom window is transparent; it is never painted red/green here.
            ctx.fillStyle = "#111923"; ctx.fillRect(6, 454, 230, 40);
            ctx.fillStyle = "#c0cbd4"; ctx.font = "14px sans-serif"; ctx.fillText("STUDENT COUNCIL", 121, 479);
            return c.toDataURL();
          });
          c.width = 440; c.height = 500;
          ctx.strokeStyle = "#e2c585"; ctx.lineWidth = 3; ctx.fillStyle = "#111923";
          ctx.beginPath(); ctx.moveTo(220, 56); ctx.lineTo(366, 230); ctx.lineTo(220, 402);
          ctx.lineTo(74, 230); ctx.closePath(); ctx.fill(); ctx.stroke();
          ctx.textAlign = "center"; ctx.fillStyle = "#f1f4f6";
          ctx.font = "bold 66px sans-serif"; ctx.fillText("FFF", 220, 249);
          ctx.fillStyle = "#e2c585"; ctx.font = "18px sans-serif"; ctx.fillText("FIGHT FOR FLAG", 220, 440);
          const logo = c.toDataURL();
          c.width = 1920; c.height = 1080;
          ctx.fillStyle = "#b9c8d2"; ctx.fillRect(0, 824, 850, 2); ctx.fillRect(1070, 824, 850, 2);
          ctx.fillStyle = "#e2c585"; ctx.fillRect(850, 824, 18, 2); ctx.fillRect(1052, 824, 18, 2);
          return { cards, logo, cover: c.toDataURL() };
        });
        roster.bottomBar = { ...roster.bottomBar, coverUrl: artwork.cover, logoPresidentId: "0" };
        roster.presidents.forEach((p, i) => { p.bottomBarUrl = artwork.cards[i]; p.logoUrl = artwork.logo; });
      }
      if (capture) await page.evaluate(() => {
        window.motionFixtureTimeout = window.setTimeout;
        window.setTimeout = (fn, ms, ...args) => motionFixtureTimeout(fn, Math.max(ms, 60000), ...args);
      });
      const entries = await page.evaluate(s => {
        render(s);
        for (const a of entranceAnimations()) { a.pause(); a.currentTime = 0; }
        return [...board.querySelectorAll('[data-target^="card:"]')].map(el => ({
          timing: el.querySelector(".bottom-motion").getAnimations()[0].effect.getTiming(),
          wipeVisible: [...el.querySelectorAll(".bottom-wipe")].some(w => !w.hidden && getComputedStyle(w).display !== "none" && getComputedStyle(w).opacity !== "0")
        }));
      }, roster);
      if (capture) {
        await page.setViewport({ width: 1920, height: 1080 });
        await page.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(img => img.decode().catch(() => {}))); });
        for (const time of [0, 240, 470, 800, 1060]) {
          await page.evaluate(async time => {
            for (const a of entranceAnimations()) {
              if (time >= a.effect.getComputedTiming().endTime) a.finish();
              else { a.pause(); a.currentTime = time; }
            }
            // Let per-piece cleanup and compositor updates settle before capture.
            await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
          }, time);
          await pause(100);
          await page.screenshot({ path: `/private/tmp/fff-motion-${time}.png`, omitBackground: true });
        }
        await page.evaluate(() => {
          window.setTimeout = window.motionFixtureTimeout;
          // Start a fresh, frozen presentation for the interruption checks below.
          animateBottom(true);
          for (const a of entranceAnimations()) { a.pause(); a.currentTime = 0; }
        });
      }
      assert.equal(entries.length, count);
      for (let i = 0; i < count; i++) {
        const half = Math.ceil(count / 2), pair = i < half ? half - 1 - i : i - half;
        assert.equal(entries[i].timing.delay, BB.firstPair + Math.min(pair * BB.pair, BB.maxStagger),
          `roster ${count} card ${i} center-out delay`);
        assert.equal(entries[i].timing.duration, BB.card);
        if (i % 2) assert.equal(entries[i].wipeVisible, false, "waiting card never paints vote wipe");
      }
      if (count > 1) {
        const maintained = await page.evaluate(s => {
          const survivor = board.querySelector('[data-target="card:0"] .bottom-motion');
          const before = survivor.getAnimations()[0]; before.currentTime = 130;
          s.presidents.pop();
          s.bottomBar.cardTemplate = { image: { x: -120, y: -50, width: 1100, height: 350 }, result: { x: -30, y: -20, width: 950, height: 300 }, order: ["result", "image"] };
          render(s);
          const bounds = before.effect.getKeyframes().at(-1).clipPath.match(/-?[\d.]+/g).map(Number);
          return { continuous: before === survivor.getAnimations()[0] && before.currentTime === 130 && transition !== null, bounds };
        }, roster);
        assert.equal(maintained.continuous, true, "roster removal and template overflow edit preserve surviving motion");
        assert.ok(maintained.bounds[0] <= -49 && maintained.bounds[3] <= -119, "clip union includes negative template overflow");
        const updates = await page.evaluate(s => {
          const el = board.querySelector('[data-target="card:0"] .bottom-motion');
          const a = el.getAnimations()[0]; a.currentTime = 130;
          const results = [];
          for (const opacity of [0, 0.5, 1]) {
            s.bottomBar.pieces = { "card:0": { resultOpacity: opacity } };
            s.presidents[0].vote = opacity === 0.5 ? "red" : "green";
            render(s);
            const wipe = getComputedStyle(el.querySelector(".bottom-wipe")), result = getComputedStyle(el.querySelector(".result"));
            results.push(wipe.opacity === String(opacity) && wipe.backgroundColor === result.backgroundColor && el.getAnimations()[0] === a && a.currentTime === 130);
          }
          s.presidents.push({ id: "added", name: "Added", vote: "red" }); render(s);
          const added = board.querySelector('[data-target="card:added"] .bottom-motion');
          const identities = new Map([...board.children].map(piece => [piece.dataset.target, piece]));
          s.presidents.reverse(); render(s);
          const order = [...board.children].map(piece => piece.dataset.target);
          return { results, addedRest: added.getAnimations({ subtree: true }).length === 0 && getComputedStyle(added).clipPath === "none",
            reordered: order.join() === [...s.presidents.map(p => "card:" + p.id), "logo", "count:red", "count:green", "cover"].join() &&
              [...board.children].every(piece => identities.get(piece.dataset.target) === piece) && el.getAnimations()[0] === a };
        }, roster);
        assert.ok(updates.results.every(Boolean), "vote colors and result opacity update wipe without replay");
        assert.equal(updates.addedRest, true, "card added mid-entrance appears at rest");
        assert.equal(updates.reordered, true, "roster reorder preserves keyed pieces and surviving animations");
      }
      await page.evaluate(() => { for (const a of entranceAnimations()) a.finish(); });
      await page.waitForFunction(() => !transition, { polling: 20 });
      await resting(page);
      if (capture) {
        await page.evaluate(s => render(s), roster);
        // Let the counters land before the frame is taken; a roll in flight
        // would put a random digit in an otherwise reproducible capture.
        await page.waitForFunction(() => countRolls().length === 0, { polling: 20 });
        await page.screenshot({ path: "/private/tmp/fff-motion-rest.png", omitBackground: true });
        await monitor.evaluate(s => {
          editMode = "bottomBar"; document.getElementById("editModeBottomBar").checked = true;
          render(s);
          document.getElementById("editModeBottomBar").dispatchEvent(new Event("change", { bubbles: true }));
        }, roster);
        await monitor.waitForFunction(() => countRolls().length === 0, { polling: 20 });
        await monitor.screenshot({ path: "/private/tmp/fff-motion-monitor.png" });
        await monitor.evaluate(s => { editMode = "bottomBar"; render(s); }, state);
      }
      await page.close();
    }
    // The press that also switches mode used to tear the board down and rebuild
    // it inside the very frame that reveals it, which cost an order of magnitude
    // more than any later press and dropped frames on air. Both modes are
    // prepared while the stream is still clean, so the first press is as cheap
    // as the third.
    {
      const timing = await browser.newPage();
      timing.on("pageerror", error => errors.push(error.message));
      await timing.evaluateOnNewDocument(() => { window.EventSource = class {}; });
      await timing.evaluateOnNewDocument(installEntranceHelper);
      await timing.setViewport({ width: 1920, height: 1080 });
      await timing.goto(`http://127.0.0.1:${server.address().port}/overlay`);
      const roster = { ...state, bottomBar: { ...state.bottomBar, coverUrl: PNG },
        presidents: Array.from({ length: 14 }, (_, i) => ({ id: String(i), name: "P" + i, school: "S",
          vote: i % 2 ? "none" : i % 4 ? "red" : "green", cardUrl: PNG, bottomBarUrl: PNG, logoUrl: PNG })) };
      const press = async (round) => timing.evaluate(s => {
        const start = performance.now();
        render(s);
        return performance.now() - start;
      }, { ...roster, displayMode: "bottomBar", phase: "revealed", round });
      const settle = async (round) => {
        await timing.evaluate(s => render(s), { ...roster, displayMode: "bottomBar", phase: "collecting", round });
        await timing.waitForFunction(() => !transition, { polling: 20 });
        await pause(120);
      };
      // A session that was last left on the scoreboard, still off air.
      await timing.evaluate(s => render(s), { ...roster, displayMode: "scoreboard", phase: "collecting" });
      await timing.waitForFunction(() => [...board.querySelectorAll("img[src]")].every(img => img.complete),
        { polling: 20 });
      await pause(200);
      const first = await press(1);
      await settle(1);
      const later = [];
      for (const round of [2, 3, 4]) { later.push(await press(round)); await settle(round); }
      later.sort((a, b) => a - b);
      const median = later[1];
      // A ratio with a small absolute floor: raw milliseconds differ per machine,
      // but a first press that rebuilds everything is many times a warm one.
      assert.ok(first < median * 3 + 2,
        `the first press costs about what a later one does: ${first.toFixed(1)}ms vs ${median.toFixed(1)}ms`);
      await timing.close();
    }

    // Flag counters. A tally that changes while a roll is running has to be the
    // one the board lands on; the older number must never come back on air.
    const counters = () => overlay.evaluate(() => Object.fromEntries(["count:red", "count:green"].map(target =>
      [target, document.querySelector(`[data-target="${target}"] .count-value`).textContent])));
    await push();
    await overlay.waitForFunction(() => countRolls().length === 0, { polling: 20 });
    assert.deepEqual(await counters(), { "count:red": "0", "count:green": "1" }, "counters settle on the live tally");
    const landing = await overlay.evaluate(async s => {
      const red = board.querySelector('[data-target="count:red"] .count-value');
      const redRoll = () => countRolls().find(a => a.effect.target === red);
      s.presidents[1].vote = "red"; render(s);
      const started = redRoll();
      // A second flag lands while the first roll is still running.
      s.presidents[0].vote = "red"; render(s);
      const kept = !!started && redRoll() === started;
      await new Promise(resolve => setTimeout(resolve, COUNT_ROLL().duration + 200));
      return { started: !!started, kept, settled: red.textContent, resting: countRolls().length };
    }, state);
    assert.equal(landing.started, true, "a changed tally starts a roll");
    assert.equal(landing.kept, true, "a tally arriving mid-roll never restarts the roll");
    assert.equal(landing.settled, "2", "the roll lands on the newest tally, not the one it started with");
    assert.equal(landing.resting, 0, "a settled roll releases its animation");
    await overlay.evaluate(s => render({ ...s, phase: "collecting" }), state);
    await overlay.waitForFunction(() => !transition && wrap.hidden, { polling: 20 });
    assert.equal(await overlay.evaluate(() =>
      [...board.querySelectorAll(".count-value")].every(el => el._countValue === null)), true,
      "leaving the air drops the settled tally");
    await overlay.evaluate(s => render({ ...s, phase: "revealed" }), state);
    assert.ok(await overlay.evaluate(() => countRolls().length) > 0, "coming back on air rolls again");
    await overlay.waitForFunction(() => countRolls().length === 0, { polling: 20 });
    assert.deepEqual(await counters(), { "count:red": "0", "count:green": "1" }, "the fresh roll lands on the live tally");
    // The counter roll settles on its own clock, shorter than the bottom bar's
    // entrance; wait for the entrance itself before treating the board as at rest.
    await overlay.waitForFunction(() => entranceAnimations().length === 0, { polling: 20 });
    const coverInfo = page => page.evaluate(() => {
      const p = board.querySelector('[data-target="cover"]');
      return { width: p.offsetWidth, height: p.offsetHeight, transform: p.style.transform,
        opacity: p.querySelector("img").style.opacity, fit: getComputedStyle(p.querySelector("img")).objectFit,
        z: p.style.zIndex, pointer: getComputedStyle(p).pointerEvents };
    });
    assert.deepEqual(await overlay.evaluate(() => ["logo", "card:a", "card:b", "count:red", "count:green", "cover"].map(target =>
      board.querySelector(`[data-target="${target}"]`).style.zIndex)), ["0", "1", "2", "3", "4", "5"], "default stacking agrees with native layer actions");
    const cover = await coverInfo(overlay);
    assert.equal(cover.width, 1920); assert.equal(cover.height, 1080); assert.equal(cover.opacity, "1");
    assert.equal(cover.fit, "contain"); assert.equal(cover.z, "5");
    assert.equal((await coverInfo(monitor)).transform, cover.transform);
    assert.equal((await coverInfo(monitor)).pointer, "none");
    await monitor.select("#selection", "cover");
    assert.equal((await coverInfo(monitor)).pointer, "auto");
    assert.equal(await monitor.$eval("#resultOpacity", el => el.disabled), true);
    assert.equal(await monitor.evaluate(() => entranceAnimations().length), 0);
    for (const opacity of [0, 0.5, 1]) {
      state.bottomBar.pieces.cover = { x: 0.5, y: 0.5, scaleX: 1, scaleY: 1, imageOpacity: opacity };
      state.bottomBar.cardTemplate = { image: { x: 0, y: 0, width: 850, height: 250, opacity: 0.2 }, result: { x: 0, y: 0, width: 850, height: 250 }, order: ["result", "image"] };
      await push();
      assert.equal((await coverInfo(overlay)).opacity, String(opacity));
      assert.equal((await coverInfo(monitor)).opacity, String(opacity));
      assert.equal(await overlay.evaluate(() => entranceAnimations().length), 0, "edits do not replay entrance");
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
      await page.waitForFunction(() => board.querySelector('[data-target="cover"] img').naturalWidth === 4, { polling: 20 });
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
    await overlay.evaluate(s => {
      // Begin a new round then explicitly sample a partly revealed card.
      render({ ...s, phase: "collecting", round: s.round + 1 });
      for (const a of entranceAnimations()) a.finish();
    }, state);
    await overlay.waitForFunction(() => !transition, { polling: 20 });
    const exitSample = await overlay.evaluate(s => {
      render(s);
      for (const a of entranceAnimations()) { a.pause(); a.currentTime = 160; }
      const motion = board.querySelector('[data-target="card:a"] .bottom-motion');
      const before = getComputedStyle(motion);
      const sampled = { opacity: before.opacity, transform: before.transform, clipPath: before.clipPath };
      render({ ...s, phase: "collecting", round: s.round + 1 });
      const a = motion.getAnimations().find(a => a.effect.getKeyframes().some(k => k.opacity !== undefined));
      const frames = a.effect.getKeyframes();
      const decoration = ["logo", "cover"].map(target => board.querySelector(`[data-target="${target}"] .bottom-motion`).getAnimations()[0].effect.getKeyframes());
      return { sampled, first: frames[0], last: frames.at(-1), timing: a.effect.getTiming(), decoration };
    }, state);
    assert.equal(exitSample.timing.duration, BB.exit);
    assert.ok(Math.abs(Number(exitSample.first.opacity) - Number(exitSample.sampled.opacity)) < 0.001, "exit starts at current opacity");
    assert.equal(exitSample.first.transform, exitSample.sampled.transform, "exit starts at current transform");
    assert.equal(exitSample.first.clipPath, exitSample.sampled.clipPath, "exit keeps current reveal extent");
    assert.ok(exitSample.decoration.every(frames => frames[0].transform === frames.at(-1).transform), "logo and cover exit by fading only");
    await overlay.waitForFunction(() => !transition, { polling: 20 });
    await push(); await overlay.waitForFunction(() => !transition, { polling: 20 });
    state.phase = "collecting"; state.round++; state.presidents[0].vote = "none";
    await push();
    const frozenFlag = () => overlay.evaluate(() =>
      board.querySelector('[data-target="card:a"] .slot').classList.contains("green"));
    assert.equal(await frozenFlag(), true, "clear freezes old flag");
    assert.equal(await overlay.evaluate(() => wrap.hidden), false);
    state.presidents[0].vote = "red";
    await push();
    assert.equal(await frozenFlag(), true, "votes cannot alter exit snapshot");
    await overlay.waitForFunction(() => wrap.hidden, { polling: 20 });
    assert.equal(await overlay.evaluate(() => wrap.hidden), true);
    state.phase = "revealed"; await push();
    await pause(35);
    state.displayMode = "scoreboard"; await push();
    await pause(35);
    state.displayMode = "bottomBar"; await push();
    await overlay.waitForFunction(() => !transition, { polling: 20 });
    assert.equal(await overlay.evaluate(() => board.classList.contains("bottom-bar")), true);
    assert.equal(await overlay.evaluate(() => wrap.hidden), false);
    assert.equal(await overlay.evaluate(() => entranceAnimations().length), 0);
    state.displayMode = "scoreboard"; await push();
    await overlay.waitForFunction(() => !board.classList.contains("bottom-bar"), { polling: 20 });
    assert.equal(await overlay.evaluate(() => board.classList.contains("bottom-bar")), false);
    // The bottom bar stays prepared off air, so the cover still exists; what
    // matters is that the scoreboard on air carries no cover of its own.
    assert.equal(await overlay.evaluate(() => board.querySelector('[data-target="cover"]')), null);
    // Old sessions have no cover and still render their bottom bar.
    state.displayMode = "bottomBar"; delete state.bottomBar.coverUrl;
    await push(); await overlay.waitForFunction(() => !transition, { polling: 20 });
    assert.equal(await overlay.$eval('[data-target="cover"] img', el => el.hidden), true);
    // Clear and immediate reveal must finish the outgoing snapshot first.
    await overlay.evaluate(s => {
      window.savedTimeouts = [];
      const original = window.setTimeout;
      window.setTimeout = (callback, delay, ...args) => {
        savedTimeouts.push({ callback, delay });
        return original(callback, delay, ...args);
      };
      render({ ...s, phase: "collecting", round: s.round + 1 });
      render({ ...s, phase: "revealed", round: s.round + 1 });
    }, state);
    assert.equal(await overlay.evaluate(() => transition.entering), false);
    await overlay.waitForFunction(() => transition?.entering, { polling: 10 });
    const staleIgnored = await overlay.evaluate(() => {
      const active = transition;
      savedTimeouts[0].callback();
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
      const before = entranceAnimations();
      for (const animation of before) { animation.pause(); animation.currentTime = 100; }
      s.presidents[0].vote = "green"; render(s);
      const after = entranceAnimations();
      return before.length > 0 && before.every(a => after.includes(a) && a.currentTime === 100);
    }, state);
    assert.equal(scoreboardContinuous, true, "scoreboard vote preserves reveal animation");

    // Artwork has to be fetched, decoded and laid out while the stream is still
    // clean, so the press itself only flips visibility and starts animations.
    const warm = await browser.newPage();
    warm.on("pageerror", error => errors.push(error.message));
    await warm.evaluateOnNewDocument(() => { window.EventSource = class {}; });
    await warm.evaluateOnNewDocument(installEntranceHelper);
    await warm.setViewport({ width: 1920, height: 1080 });
    await warm.goto(`http://127.0.0.1:${server.address().port}/overlay`);
    const asset = `http://127.0.0.1:${server.address().port}/img/`;
    const warming = { phase: "collecting", round: 9, displayMode: "bottomBar", voted: 0, total: 3,
      layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {},
      bottomBar: { coverUrl: asset + "cover.png", pieces: {}, layers: {}, logoPresidentId: "a" },
      presidents: ["a", "b", "c"].map(id => ({ id, name: id, school: "S", vote: "none",
        bottomBarUrl: `${asset}${id}.png`, logoUrl: `${asset}${id}-logo.png`,
        logoRound1Url: `${asset}${id}-logo.png`, logoRound2Url: `${asset}${id}-logo2.png` })) };
    await warm.evaluate(s => render(s), warming);
    await warm.waitForFunction(() => warmed.size > 0 && [...warmed.values()].every(image => image.complete),
      { polling: 20 });
    const blank = await warm.evaluate(() => {
      const images = [...board.querySelectorAll("img[src]")];
      const other = views.scoreboard.wrap;
      return { hidden: wrap.hidden, pieces: board.querySelectorAll(".piece").length, images: images.length,
        ready: images.every(image => image.complete && image.naturalWidth > 0),
        // On deck: painted, so the raster and the textures are warm, but wholly
        // transparent so the stream stays clean.
        opacity: getComputedStyle(wrap).opacity, visibility: getComputedStyle(wrap).visibility,
        promoted: getComputedStyle(board.querySelector(".bottom-motion")).willChange,
        // Off deck: built and measured, but never painted.
        otherHidden: other.hidden, otherPainted: getComputedStyle(other).visibility,
        otherLaidOut: [...other.querySelectorAll(".piece")].every(piece => piece.offsetWidth > 0),
        laidOut: [...board.querySelectorAll(".piece")].every(piece => piece.offsetWidth > 0) };
    });
    assert.equal(blank.hidden, true, "a collecting round stays off the stream");
    assert.equal(blank.opacity, "0", "the prepared board puts nothing on the stream");
    assert.equal(blank.visibility, "visible", "the mode on deck is painted, so its raster is warm before the press");
    assert.equal(blank.promoted, "transform, opacity", "pieces on deck are promoted before the entrance, not during it");
    assert.equal(blank.otherHidden, true, "the mode that is not on deck stays off the stream");
    assert.equal(blank.otherPainted, "hidden", "the mode that is not on deck costs no raster");
    assert.equal(blank.otherLaidOut, true, "the mode that is not on deck is still measured, so a switch rebuilds nothing");
    assert.equal(blank.pieces, 7, "cards, logo, both counters and cover are mounted before the press");
    assert.equal(blank.images, 5, "every on-air image already has its source");
    assert.equal(blank.ready, true, "every on-air image is decoded before the press");
    assert.equal(blank.laidOut, true, "the blank board keeps its geometry");
    const beforeReveal = JSON.stringify([...served].sort());
    const opened = await warm.evaluate(s => {
      render({ ...s, phase: "revealed" });
      return { shown: !wrap.hidden, animating: entranceAnimations().length };
    }, warming);
    assert.equal(opened.shown, true, "the press reveals in the same task");
    assert.ok(opened.animating > 0, "the entrance starts in the same task");
    await warm.waitForFunction(() => !transition, { polling: 20 });
    assert.equal(JSON.stringify([...served].sort()), beforeReveal, "revealing fetches nothing");
    // Switching the centre logo to round 2 on air uses artwork fetched before
    // the press, so the logo changes without touching the network.
    const switched = await warm.evaluate(s => {
      const round2 = { ...s, phase: "revealed", bottomBar: { ...s.bottomBar, logoRound: 2 },
        presidents: s.presidents.map(p => ({ ...p, logoUrl: p.logoRound2Url })) };
      render(round2);
      const logo = board.querySelector('[data-target="logo"] img');
      const shown = logo.getAttribute("src");
      render({ ...round2, presidents: round2.presidents.map(p => p.id === "a" ? { ...p, logoUrl: "" } : p) });
      return { shown, missing: logo.hasAttribute("src") };
    }, warming);
    assert.ok(switched.shown.endsWith("/img/a-logo2.png"), "round 2 puts round 2 artwork in the centre");
    assert.equal(switched.missing, false, "a school without round 2 artwork leaves the centre empty");
    assert.equal(JSON.stringify([...served].sort()), beforeReveal, "switching rounds fetches nothing");
    await warm.evaluate(s => render({ ...s, phase: "revealed" }), warming);
    // A mode button pressed twice blanks the stream without ending the round,
    // so the same votes have to be able to come straight back.
    const hidden = { ...warming, phase: "revealed" };
    await warm.evaluate(s => render(s), hidden);
    await warm.waitForFunction(() => !transition, { polling: 20 });
    await warm.evaluate(s => render({ ...s, phase: "collecting" }), hidden);
    const exiting = await warm.evaluate(() => ({
      entering: transition && transition.entering,
      duration: board.querySelector('[data-target="card:a"] .bottom-motion').getAnimations()[0].effect.getTiming().duration
    }));
    assert.equal(exiting.entering, false, "hiding plays the exit, not a cut");
    assert.equal(exiting.duration, BB.exit, "hiding uses the documented exit timing");
    await warm.waitForFunction(() => wrap.hidden, { polling: 20 });
    const restored = await warm.evaluate(s => {
      render(s);
      return { shown: !wrap.hidden, round: s.round, animating: entranceAnimations().length };
    }, hidden);
    assert.equal(restored.shown, true, "the same round comes straight back");
    assert.ok(restored.animating > 0, "and replays the entrance");
    await warm.waitForFunction(() => !transition, { polling: 20 });

    // Warmed artwork is held for what is on the roster and nothing else, so
    // rounds and roster edits cannot grow it without bound.
    const warmedSizes = await warm.evaluate(s => {
      const full = warmed.size;
      render({ ...s, presidents: s.presidents.slice(0, 1), total: 1,
        bottomBar: { ...s.bottomBar, coverUrl: "" }, phase: "collecting", round: s.round + 1 });
      return { full, trimmed: warmed.size };
    }, warming);
    assert.equal(warmedSizes.full, 10, "three cards, three logos per round and a cover are warmed");
    assert.equal(warmedSizes.trimmed, 3, "artwork that left the roster is dropped");

    // The lengths are the session's to set, on this board too, and a counter
    // roll is motion of its own with its own pair of them.
    const slower = { ...warming, phase: "revealed",
      timing: { bottomBar: { logo: 500, cover: 700, card: 950, firstPair: 30, pair: 10 },
        board: { countRoll: 900, countTick: 90 } } };
    await warm.evaluate(s => render({ ...s, phase: "collecting" }), slower);
    await warm.waitForFunction(() => wrap.hidden && !transition, { polling: 20 });
    const slowEntrance = await warm.evaluate(s => {
      render(s);
      return Object.fromEntries([...board.querySelectorAll(".piece")].map(piece => {
        const animation = piece.querySelector(".bottom-motion").getAnimations()
          .find(a => a.effect.getKeyframes().some(k => k.opacity !== undefined || k.clipPath !== undefined));
        return [piece.dataset.target, animation ? animation.effect.getTiming() : null];
      }));
    }, slower);
    assert.equal(slowEntrance.logo.duration, 500, "the logo follows the session");
    assert.equal(slowEntrance.cover.duration, 700, "so does the cover");
    assert.equal(slowEntrance["count:red"].duration, 500, "and the counters");
    // Three cards: two on the left in centre-out order, one on the right, so
    // the pair indices are 1, 0 and 0 and the delays are the session's.
    const slowCards = Object.entries(slowEntrance).filter(([target]) => target.startsWith("card:"))
      .map(([, timing]) => timing);
    assert.equal(slowCards.length, 3);
    assert.ok(slowCards.every(timing => timing.duration === 950), "and the cards");
    assert.deepEqual([...new Set(slowCards.map(timing => timing.delay))].sort((a, b) => a - b), [30, 40],
      "centre-out delays are the session's first-pair wait plus its pair step");
    const rollTiming = await warm.evaluate(() =>
      countRolls().map(animation => animation.effect.getTiming().duration));
    assert.ok(rollTiming.every(duration => duration === 900), "a counter roll takes the session's length");
    await warm.waitForFunction(() => !transition, { polling: 20 });
    await warm.waitForFunction(() => countRolls().length === 0, { polling: 20 });
    await warm.close();

    assert.deepEqual(errors, []);
    console.log("bottom-bar cover and transition browser checks passed");
  } finally { await browser.close(); server.close(); }
})().catch(error => { console.error(error); server.close(); process.exitCode = 1; });

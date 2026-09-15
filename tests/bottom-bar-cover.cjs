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
    const entrance = await overlay.evaluate(() => Object.fromEntries([...board.querySelectorAll(".piece")].map(piece => {
      const motion = piece.querySelector(".bottom-motion");
      const animation = motion.getAnimations().find(a => a.effect.getKeyframes().some(k => k.opacity !== undefined));
      return [piece.dataset.target, { timing: animation.effect.getTiming(), frames: animation.effect.getKeyframes() }];
    })));
    assert.equal(entrance.logo.timing.duration, 180, "logo enters in 180ms");
    assert.equal(entrance.cover.timing.duration, 300, "cover fades in 300ms");
    for (const target of ["card:a", "card:b"]) {
      assert.equal(entrance[target].timing.duration, 450);
      assert.equal(entrance[target].timing.delay, 80);
      assert.equal(entrance[target].timing.easing, "cubic-bezier(0.22, 1, 0.36, 1)");
      assert.match(entrance[target].frames[0].transform, /18px/);
      assert.ok(entrance[target].frames.some(frame => frame.clipPath && frame.clipPath !== "none"), "cards reveal through a clip");
    }
    assert.match(entrance.logo.frames[0].transform, /12px/);
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
      const before = board.getAnimations({ subtree: true });
      for (const animation of before) { animation.pause(); animation.currentTime = 100; }
      s.presidents[1].vote = "red";
      s.bottomBar.coverUrl = "";
      render(s);
      const after = board.getAnimations({ subtree: true });
      const result = before.length > 0 && before.every(a => after.includes(a) && a.currentTime === 100);
      for (const animation of before) animation.play();
      return result;
    }, state);
    assert.equal(continuous, true, "vote during entrance preserves animation identity and progress");
    await overlay.waitForFunction(() => board.getAnimations({ subtree: true }).length === 0, { polling: 20 });
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
        for (const a of board.getAnimations({ subtree: true })) { a.pause(); a.currentTime = 0; }
        return [...board.querySelectorAll('[data-target^="card:"]')].map(el => ({
          timing: el.querySelector(".bottom-motion").getAnimations()[0].effect.getTiming(),
          wipeVisible: [...el.querySelectorAll(".bottom-wipe")].some(w => !w.hidden && getComputedStyle(w).display !== "none" && getComputedStyle(w).opacity !== "0")
        }));
      }, roster);
      if (capture) {
        await page.setViewport({ width: 1920, height: 1080 });
        await page.evaluate(async () => { await document.fonts.ready; await Promise.all([...document.images].map(img => img.decode().catch(() => {}))); });
        for (const time of [0, 180, 350, 600, 800]) {
          await page.evaluate(async time => {
            for (const a of board.getAnimations({ subtree: true })) {
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
          for (const a of board.getAnimations({ subtree: true })) { a.pause(); a.currentTime = 0; }
        });
      }
      assert.equal(entries.length, count);
      for (let i = 0; i < count; i++) {
        const half = Math.ceil(count / 2), pair = i < half ? half - 1 - i : i - half;
        assert.equal(entries[i].timing.delay, 80 + Math.min(pair * 45, 420), `roster ${count} card ${i} center-out delay`);
        assert.equal(entries[i].timing.duration, 450);
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
            reordered: order.join() === [...s.presidents.map(p => "card:" + p.id), "logo", "cover"].join() &&
              [...board.children].every(piece => identities.get(piece.dataset.target) === piece) && el.getAnimations()[0] === a };
        }, roster);
        assert.ok(updates.results.every(Boolean), "vote colors and result opacity update wipe without replay");
        assert.equal(updates.addedRest, true, "card added mid-entrance appears at rest");
        assert.equal(updates.reordered, true, "roster reorder preserves keyed pieces and surviving animations");
      }
      await page.evaluate(() => { for (const a of board.getAnimations({ subtree: true })) a.finish(); });
      await page.waitForFunction(() => !transition, { polling: 20 });
      await resting(page);
      if (capture) {
        await page.evaluate(s => render(s), roster);
        await page.screenshot({ path: "/private/tmp/fff-motion-rest.png", omitBackground: true });
        await monitor.evaluate(s => {
          editMode = "bottomBar"; document.getElementById("editMode").value = editMode;
          render(s);
          document.getElementById("editMode").dispatchEvent(new Event("change"));
        }, roster);
        await monitor.screenshot({ path: "/private/tmp/fff-motion-monitor.png" });
        await monitor.evaluate(s => { editMode = "bottomBar"; render(s); }, state);
      }
      await page.close();
    }
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
    await overlay.evaluate(s => {
      // Begin a new round then explicitly sample a partly revealed card.
      render({ ...s, phase: "collecting", round: s.round + 1 });
      for (const a of board.getAnimations({ subtree: true })) a.finish();
    }, state);
    await overlay.waitForFunction(() => !transition, { polling: 20 });
    const exitSample = await overlay.evaluate(s => {
      render(s);
      for (const a of board.getAnimations({ subtree: true })) { a.pause(); a.currentTime = 160; }
      const motion = board.querySelector('[data-target="card:a"] .bottom-motion');
      const before = getComputedStyle(motion);
      const sampled = { opacity: before.opacity, transform: before.transform, clipPath: before.clipPath };
      render({ ...s, phase: "collecting", round: s.round + 1 });
      const a = motion.getAnimations().find(a => a.effect.getKeyframes().some(k => k.opacity !== undefined));
      const frames = a.effect.getKeyframes();
      const decoration = ["logo", "cover"].map(target => board.querySelector(`[data-target="${target}"] .bottom-motion`).getAnimations()[0].effect.getKeyframes());
      return { sampled, first: frames[0], last: frames.at(-1), timing: a.effect.getTiming(), decoration };
    }, state);
    assert.equal(exitSample.timing.duration, 240);
    assert.ok(Math.abs(Number(exitSample.first.opacity) - Number(exitSample.sampled.opacity)) < 0.001, "exit starts at current opacity");
    assert.equal(exitSample.first.transform, exitSample.sampled.transform, "exit starts at current transform");
    assert.equal(exitSample.first.clipPath, exitSample.sampled.clipPath, "exit keeps current reveal extent");
    assert.ok(exitSample.decoration.every(frames => frames[0].transform === frames.at(-1).transform), "logo and cover exit by fading only");
    await overlay.waitForFunction(() => !transition, { polling: 20 });
    await push(); await overlay.waitForFunction(() => !transition, { polling: 20 });
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

// Shared by /overlay and /monitor so the operator's preview and the stream
// never drift apart.
(function () {
  // The overlay decodes its artwork before the reveal, so painting it
  // synchronously keeps the first frame whole. The monitor has no such
  // preparation and would rather not block on a decode.
  const onAir = document.body.classList.contains("overlay");

  // The frame the operator sees when a status has no artwork yet. Nothing on
  // air is ever this size: a slot holding a PNG is the size of that PNG. It
  // matches the reference artwork's own card so an empty roster still reads as
  // one row per president instead of a stack of overlapping boxes.
  const EMPTY_SLOT = Object.freeze({ width: 346, height: 51 });

  // Show Status is a status board pinned to the upper left of frame, the way
  // the broadcast artwork draws it, rather than a block centred on the canvas.
  // The row pitch is fixed rather than taken from each card's height, so a PNG
  // exported a pixel or two taller cannot push every row below it out of step.
  const STATUS_STACK = Object.freeze({ left: 55, top: 201, pitch: 57.23 });
  window.STATUS_STACK = STATUS_STACK;

  // The title above the stack. Its box and its type are the operator's, so only
  // what the artwork opens at lives here.
  // The box is in the piece's own pixels and opens matching its CSS box, the
  // same arrangement the centre logo and the flag counters keep.
  const HEADING_DEFAULT = Object.freeze({
    box: Object.freeze({ x: 0, y: 0, width: 420, height: 100 }),
    text: "COMPETITION\nSTATUS", fontFamily: "", fontSize: 38, fontWeight: 800,
    lineHeight: 42, align: "left", color: "#000000"
  });
  window.HEADING_DEFAULT = HEADING_DEFAULT;

  // The /score page's two pieces. They are the same two shapes the Show Status
  // title and the Bottom Bar counters have, so they are described by the same
  // templates — only what they open at is their own. The defaults live here,
  // beside every other default, because the page and the monitor's panel both
  // read them and would otherwise drift apart.
  // The title's box is in canvas pixels: .score .heading-piece is the whole
  // 1920x1080 frame, so moving the title is moving this box. The counters keep
  // the Bottom Bar's arrangement instead — the stylesheet places the two cards
  // and this box is the digit's rectangle inside each of them.
  const SCORE_HEADING_DEFAULT = Object.freeze({
    box: Object.freeze({ x: 660, y: 300, width: 600, height: 120 }),
    text: "พี่เนย", fontFamily: "", fontSize: 84, fontWeight: 800,
    lineHeight: 100, align: "center", color: "#ffffff"
  });
  const SCORE_COUNT_DEFAULT = Object.freeze({
    value: Object.freeze({ x: 0, y: 0, width: 240, height: 240 }),
    fontFamily: "", fontSize: 160, fontWeight: 800,
    colors: Object.freeze({ red: "#e23c3c", green: "#21b04a" })
  });
  window.SCORE_HEADING_DEFAULT = SCORE_HEADING_DEFAULT;
  window.SCORE_COUNT_DEFAULT = SCORE_COUNT_DEFAULT;

  // Every field is optional, the same way headingOf() treats the Show Status
  // title: a session saved before this page existed draws the defaults above.
  window.scoreHeadingOf = function (template) {
    const saved = template || {};
    return { box: { ...SCORE_HEADING_DEFAULT.box, ...(saved.box || {}) },
      text: typeof saved.text === "string" ? saved.text : SCORE_HEADING_DEFAULT.text,
      fontFamily: saved.fontFamily || SCORE_HEADING_DEFAULT.fontFamily,
      fontSize: saved.fontSize || SCORE_HEADING_DEFAULT.fontSize,
      fontWeight: saved.fontWeight || SCORE_HEADING_DEFAULT.fontWeight,
      lineHeight: saved.lineHeight || SCORE_HEADING_DEFAULT.lineHeight,
      align: saved.align || SCORE_HEADING_DEFAULT.align,
      color: saved.color || SCORE_HEADING_DEFAULT.color };
  };

  window.scoreCountOf = function (template) {
    const saved = template || {};
    return { value: { ...SCORE_COUNT_DEFAULT.value, ...(saved.value || {}) },
      fontFamily: saved.fontFamily || SCORE_COUNT_DEFAULT.fontFamily,
      fontSize: saved.fontSize || SCORE_COUNT_DEFAULT.fontSize,
      fontWeight: saved.fontWeight || SCORE_COUNT_DEFAULT.fontWeight,
      colors: { ...SCORE_COUNT_DEFAULT.colors, ...(saved.colors || {}) } };
  };

  // Every field is optional: a session saved before this card existed, or one
  // the operator has not touched, draws the artwork's own title.
  window.headingOf = function (template) {
    const saved = template || {};
    return { box: { ...HEADING_DEFAULT.box, ...(saved.box || {}) },
      text: typeof saved.text === "string" ? saved.text : HEADING_DEFAULT.text,
      fontFamily: saved.fontFamily || HEADING_DEFAULT.fontFamily,
      fontSize: saved.fontSize || HEADING_DEFAULT.fontSize,
      fontWeight: saved.fontWeight || HEADING_DEFAULT.fontWeight,
      lineHeight: saved.lineHeight || HEADING_DEFAULT.lineHeight,
      align: saved.align || HEADING_DEFAULT.align,
      color: saved.color || HEADING_DEFAULT.color };
  };

  // The box comes from applyPieceTemplate like every other single-box piece;
  // the type is this card's own. A family name installed on this machine
  // travels as text, and the session refuses everything that could break out
  // of the declaration, so wrapping it here cannot escape it.
  window.applyHeadingTemplate = function (element, template) {
    if (!element) return;
    const heading = headingOf(template);
    applyPieceTemplate(element, heading.box);
    element.textContent = heading.text;
    element.style.fontFamily = heading.fontFamily ? '"' + heading.fontFamily + '"' : "";
    element.style.fontSize = heading.fontSize + "px";
    element.style.fontWeight = String(heading.fontWeight);
    element.style.lineHeight = heading.lineHeight + "px";
    element.style.textAlign = heading.align;
    element.style.color = heading.color;
    element.parentElement?.classList.toggle("empty-heading", !heading.text.trim());
  };

  // A Show Status card is its artwork and nothing else, so the slot takes the
  // PNG's own pixels. That makes the slot's box the picture's box: what the
  // operator drags, what the symmetry readout measures and what the overlay's
  // reveal clips to are all the same rectangle, with no letterbox margin to
  // guess at. Artwork larger than the stream already arrives shrunk to fit
  // (FffAssetRendition), so naturalWidth is bounded by 1920x1080.
  function fitCardToImage(slot) {
    if (slot.piece.closest(".bottom-bar")) return;
    if (!slot.img.naturalWidth || !slot.img.naturalHeight) return;
    slot.root.style.width = slot.img.naturalWidth + "px";
    slot.root.style.height = slot.img.naturalHeight + "px";
  }

  // A single template box, in the piece's own pixels. The card template places
  // two of these; the logo and the flag counters place one each.
  window.applyPieceTemplate = function (element, box) {
    if (!element) return;
    if (!box) { element.removeAttribute("style"); return; }
    Object.assign(element.style, { position: "absolute", left: box.x + "px", top: box.y + "px",
      width: box.width + "px", height: box.height + "px", right: "auto", bottom: "auto",
      opacity: String(box.opacity ?? 1) });
  };
  window.applyCountTemplate = function (element, template, vote) {
    if (!element) return;
    applyPieceTemplate(element, template && template.value);
    // A family name installed on this machine. The session refuses quotes, so
    // wrapping it here cannot escape the declaration. Empty keeps the page's
    // own stack, and an empty colour keeps the stylesheet's flag colour.
    const family = (template && template.fontFamily) || "";
    element.style.fontFamily = family ? '"' + family + '"' : "";
    element.style.fontSize = ((template && template.fontSize) || 96) + "px";
    element.style.fontWeight = String((template && template.fontWeight) || 700);
    element.style.color = (template && template.colors && template.colors[vote]) || "";
  };
  window.templateOpacity = function (state, key) {
    if (key.startsWith("card:")) return state.cardTemplate?.image?.opacity;
    if (key === "logo") return state.logoTemplate?.image?.opacity;
    if (key === "heading") return state.headingTemplate?.box?.opacity;
    if (key.startsWith("count:")) return state.countTemplate?.value?.opacity;
    return 1;
  };

  // The digits a roll shows are a pure function of the roll's own animation
  // clock, so pausing or seeking lands on the same number every time and a
  // frozen frame can be captured reproducibly.
  // Read when a roll starts, not when the file loads: the operator can change
  // the length from the monitor while the board is up. One roll keeps the pair
  // it started with, so its frames and its clock can never disagree.
  function countRoll() {
    const board = window.fffTiming.board;
    return { duration: board.countRoll, tick: board.countTick };
  }
  window.COUNT_ROLL = countRoll;
  function rollFrames(seed, max, roll) {
    const frames = [];
    let value = ((seed * 2654435761) >>> 0) || 1;
    for (let index = 0; index < Math.ceil(roll.duration / roll.tick); index++) {
      value = (value ^ (value << 13)) >>> 0;
      value = (value ^ (value >>> 17)) >>> 0;
      value = (value ^ (value << 5)) >>> 0;
      frames.push(value % (max + 1));
    }
    return frames;
  }

  function endRoll(element) {
    clearTimeout(element._countTick);
    clearTimeout(element._countTimer);
    element._countRoll?.cancel();
    element._countRoll = null;
    element._countTick = 0;
    element._countTimer = 0;
  }

  // Flags keep arriving while a roll is running. The landing number is read at
  // the end and never captured at the start, so a roll always settles on the
  // newest count and can never put a stale one back on air.
  function rollCount(element, value, max) {
    element._countTarget = value;
    if (element._countRoll || element._countValue === value) return;
    const roll = countRoll();
    element._countValue = null;
    element._countGeneration = (element._countGeneration || 0) + 1;
    element._countFrames = rollFrames(element._countGeneration * 31 + value + 1, Math.max(1, max), roll);
    const animation = element.animate([{ transform: "scale(1.18)" }, { transform: "scale(1)" }],
      { duration: roll.duration, easing: "cubic-bezier(0.22, 1, 0.36, 1)" });
    element._countRoll = animation;
    const tick = () => {
      if (element._countRoll !== animation) return;
      const time = Number(animation.currentTime) || 0;
      element.textContent = String(element._countFrames[
        Math.min(element._countFrames.length - 1, Math.floor(time / roll.tick))]);
      element._countTick = setTimeout(tick, roll.tick);
    };
    const settle = () => {
      if (element._countRoll !== animation) return;
      endRoll(element);
      element._countValue = element._countTarget;
      element.textContent = String(element._countTarget);
    };
    tick();
    // The roll has to end even on a frame the browser never paints, the same
    // wall-clock guard the entrance transition keeps.
    element._countTimer = setTimeout(settle, roll.duration);
    animation.finished.then(settle, () => {});
  }
  // /score has no board of its own to render, only the same two numbers, so it
  // drives them through this directly rather than repeating the roll.
  window.rollCount = rollCount;

  // Leaving the air ends any roll in flight so the next entrance counts up from
  // nothing again instead of resuming a stale one.
  window.resetCountRolls = function (root) {
    for (const element of root.querySelectorAll(".count-value")) {
      endRoll(element);
      element._countValue = null;
    }
  };

  // `natural` is Show Status, where the slot is already the PNG's own box: the
  // template's image rectangle would only letterbox the artwork inside it, so
  // the picture fills the slot and the template keeps what still means
  // something there — its stacking order and its opacity.
  window.applyCardTemplate = function (slot, template, opts) {
    const natural = !!(opts && opts.natural);
    for (const [name, selector] of [["image", ".card-image"], ["result", ".result"]]) {
      const el = slot.querySelector(selector);
      if (!template) { el.removeAttribute("style"); continue; }
      const box = template[name];
      const fill = natural && name === "image";
      Object.assign(el.style, { position: "absolute",
        left: fill ? "0px" : box.x + "px", top: fill ? "0px" : box.y + "px",
        width: fill ? "100%" : box.width + "px", height: fill ? "100%" : box.height + "px",
        right: "auto", bottom: "auto",
        transform: "none", zIndex: String(template.order.indexOf(name)), opacity: String(box.opacity ?? 1) });
    }
  };
  function buildSlot() {
    const piece = document.createElement("div");
    piece.className = "piece";
    const root = document.createElement("div");
    const img = document.createElement("img");
    const result = document.createElement("div");
    img.className = "card-image";
    img.alt = "";
    img.decoding = onAir ? "sync" : "async";
    result.className = "result";
    root.append(img, result);
    piece.appendChild(root);
    const slot = { piece: piece, root: root, img: img, result: result };
    img.addEventListener("load", () => fitCardToImage(slot));
    return slot;
  }

  // Both boards are the whole 1920x1080 canvas now and place every piece
  // themselves, so neither has a meaningful whole-board offset or zoom left:
  // moving a piece is the only thing that moves anything. Pinning the layout
  // here also retires whatever a session saved back when the scoreboard was a
  // grid centred on that point, which would otherwise drag the stack off frame.
  window.modeState = function (state, mode) {
    if (mode !== "bottomBar") return { ...state, layout: { x: 0.5, y: 0.5, scale: 1 }, mode: "scoreboard" };
    return { ...state, layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {},
      cardTemplate: null, ...state.bottomBar, mode: "bottomBar" };
  };
  function renderBottomBar(root, state, opts) {
    const leftCount = Math.ceil(state.presidents.length / 2);
    const rightCount = state.presidents.length - leftCount;
    const seen = new Set();
    state.presidents.forEach((president, index) => {
      let slot = root._slots.get(president.id);
      if (!slot) {
        slot = buildSlot();
        const motion = document.createElement("div"); motion.className = "bottom-motion";
        motion.appendChild(slot.root); slot.piece.appendChild(motion);
        root._slots.set(president.id, slot);
      }
      seen.add(president.id);
      slot.piece.dataset.target = "card:" + president.id;
      const left = index < leftCount, count = left ? leftCount : rightCount;
      slot.piece.dataset.revealPair = String(left ? leftCount - 1 - index : index - leftCount);
      const width = 850 / count;
      Object.assign(slot.piece.style, { position: "absolute", left: ((left ? 0 : 1070) + (left ? index : index - leftCount) * width) + "px", top: "830px", width: width + "px", height: "250px" });
      Object.assign(slot.root.style, { width: "100%", height: "100%" });
      const url = president.bottomBarUrl;
      if (url) { if (slot.img.getAttribute("src") !== url) slot.img.src = url; }
      else slot.img.removeAttribute("src");
      slot.img.hidden = !url;
      slot.root.className = "slot " + (president.vote !== "none" && (opts.revealed || opts.showVotes) ? president.vote : "waiting");
      // Leave connected pieces in place on ordinary SSE updates. Only an
      // actual roster reorder should move a keyed card in the DOM.
      if (root.children[index] !== slot.piece)
        root.insertBefore(slot.piece, root.children[index] || null);
    });
    for (const [id, slot] of root._slots) if (!seen.has(id)) {
      slot.piece.querySelector(".bottom-motion")?._cancelPresentation?.();
      slot.piece.remove(); root._slots.delete(id);
    }
    if (!root._logo) {
      root._logo = document.createElement("div"); root._logo.className = "piece center-logo";
      root._logo.dataset.target = "logo";
      root._logo.innerHTML = '<div class="bottom-motion"><img class="card-image" alt="" /></div>';
    }
    const logo = root._logo.querySelector("img");
    const url = state.presidents.find(p => p.id === state.logoPresidentId)?.logoUrl;
    if (url) { if (logo.getAttribute("src") !== url) logo.src = url; } else logo.removeAttribute("src");
    logo.hidden = !url;
    root._logo.classList.toggle("empty-logo", !url);
    if (root.children[state.presidents.length] !== root._logo)
      root.insertBefore(root._logo, root.children[state.presidents.length] || null);
    if (!root._counts) {
      root._counts = new Map();
      for (const vote of ["red", "green"]) {
        const piece = document.createElement("div");
        piece.className = "piece count-card count-" + vote;
        piece.dataset.target = "count:" + vote;
        piece.innerHTML = '<div class="bottom-motion"><div class="count-value"></div></div>';
        root._counts.set(vote, piece);
      }
    }
    let place = state.presidents.length + 1;
    for (const [vote, piece] of root._counts) {
      const tally = opts.revealed || opts.showVotes
        ? state.presidents.filter(president => president.vote === vote).length : 0;
      rollCount(piece.querySelector(".count-value"), tally, state.presidents.length);
      if (root.children[place] !== piece) root.insertBefore(piece, root.children[place] || null);
      place++;
    }
    if (!root._cover) {
      root._cover = document.createElement("div"); root._cover.className = "piece bottom-cover";
      root._cover.dataset.target = "cover";
      root._cover.innerHTML = '<div class="bottom-motion"><img class="card-image" alt="" /></div>';
    }
    const cover = root._cover.querySelector("img");
    if (state.coverUrl) {
      if (cover.getAttribute("src") !== state.coverUrl) cover.src = state.coverUrl;
    } else cover.removeAttribute("src");
    cover.hidden = !state.coverUrl;
    if (root.lastElementChild !== root._cover) root.appendChild(root._cover);
  }

  // Slots are reused across updates: rebuilding the grid on every vote would
  // restart PNG loads and flicker on air.
  window.renderBoard = function (root, state, opts) {
    if (!root._slots) root._slots = new Map();
    const bottom = state.mode === "bottomBar";
    root.classList.toggle("bottom-bar", bottom);
    root.parentElement.classList.toggle("bottom-bar-wrap", bottom);
    if (root._mode !== state.mode) {
      root.replaceChildren(); root._slots.clear(); root._logo = null; root._cover = null;
      root._counts = null; root._heading = null; root._mode = state.mode;
    }
    if (bottom) { renderBottomBar(root, state, opts); return; }
    const slots = root._slots;

    const revealed = opts.revealed;
    const seen = new Set();

    state.presidents.forEach((president, index) => {
      let slot = slots.get(president.id);
      if (!slot) {
        slot = buildSlot();
        slot.piece.dataset.target = "card:" + president.id;
        slots.set(president.id, slot);
      }
      seen.add(president.id);
      // One row per president, each at its own fixed place on the canvas.
      // Rounded: a row on a half pixel is a row the compositor resamples, and
      // artwork with a soft edge is the one thing this board cannot afford.
      Object.assign(slot.piece.style, { position: "absolute", left: STATUS_STACK.left + "px",
        top: Math.round(STATUS_STACK.top + index * STATUS_STACK.pitch) + "px" });

      // The scoreboard shows one finished PNG per president: the artwork for
      // whichever status they are on, falling back to the card behind it. The
      // flag colour belongs to the bottom bar, so no vote class is set here and
      // `.slot .result` stays at its default display:none.
      const url = president.statusUrl || president.cardUrl || "";
      if (url) {
        if (slot.img.getAttribute("src") !== url) slot.img.src = url;
        slot.img.hidden = false;
        fitCardToImage(slot);
      } else {
        slot.img.removeAttribute("src");
        slot.img.hidden = true;
        slot.root.style.width = EMPTY_SLOT.width + "px";
        slot.root.style.height = EMPTY_SLOT.height + "px";
      }

      // `waiting` is the monitor's empty-slot frame, so it is only for a status
      // that has no artwork yet — never over a finished PNG.
      const className = "slot" + (url ? " has-card" : " waiting");
      // Keep a reveal animation alive when another update arrives mid-animation.
      const animating = slot.root.classList.contains("reveal-in");
      if (slot.root.className !== className) slot.root.className = className + (animating ? " reveal-in" : "");

      // Replay on the press, and again whenever a president's status changes on
      // air, so a change mid-show reads as a change.
      const changed = slot._status !== president.status;
      slot._status = president.status;
      if (opts.justRevealed || (changed && revealed)) {
        slot.root.classList.remove("reveal-in");
        void slot.root.offsetWidth;
        slot.root.classList.add("reveal-in");
      }

      if (root.children[index] !== slot.piece)
        root.insertBefore(slot.piece, root.children[index] || null);
    });

    for (const [id, slot] of slots) {
      if (!seen.has(id)) {
        slot.piece.remove();
        slots.delete(id);
      }
    }

    // The title is a piece like any other: it drags, layers and resets through
    // the same API, and it carries its own type instead of the stylesheet's.
    if (!root._heading) {
      root._heading = document.createElement("div");
      root._heading.className = "piece heading-piece";
      root._heading.dataset.target = "heading";
      root._heading.innerHTML = '<div class="heading-text"></div>';
    }
    if (root.lastElementChild !== root._heading) root.appendChild(root._heading);
  };

  window.applyLayout = function (wrap, layout) {
    const l = layout || { x: 0.5, y: 0.5, scale: 1 };
    wrap.style.left = l.x * 100 + "%";
    wrap.style.top = l.y * 100 + "%";
    wrap.style.transform = `translate(-50%, -50%) scale(${l.scale})`;
  };

  // Keep the natural grid as a measuring frame for old sessions and resets.
  // Moving the outer piece leaves that frame intact; the inner card can still
  // animate on reveal without overwriting its saved position or scale.
  // Three passes on purpose: everything that can move a box is written first,
  // every measurement is taken together, and the rest of the writes touch only
  // transform, opacity and z-index. That costs one layout flush instead of one
  // per piece, which matters most on the frame the overlay opens.
  window.applyPieceLayouts = function (wrap, state, overrides) {
    const base = state.layout || { x: 0.5, y: 0.5, scale: 1 };
    const pieces = [...wrap.querySelectorAll(".piece")];
    applyLayout(wrap, base);
    for (const piece of pieces) {
      const slot = piece.querySelector(".slot");
      if (slot) { applyCardTemplate(slot, state.cardTemplate, { natural: state.mode !== "bottomBar" }); continue; }
      const target = piece.dataset.target;
      if (target === "logo") applyPieceTemplate(piece.querySelector("img"), state.logoTemplate?.image);
      else if (target === "heading")
        applyHeadingTemplate(piece.querySelector(".heading-text"), state.headingTemplate);
      else if (target.startsWith("count:"))
        applyCountTemplate(piece.querySelector(".count-value"), state.countTemplate, target.slice(6));
    }

    const measured = pieces.map(piece => {
      let x = piece.offsetWidth / 2;
      let y = piece.offsetHeight / 2;
      for (let node = piece; node && node !== wrap; node = node.offsetParent) {
        x += node.offsetLeft;
        y += node.offsetTop;
      }
      return { piece: piece, x: x, y: y };
    });
    const wrapWidth = wrap.offsetWidth;
    const wrapHeight = wrap.offsetHeight;

    const layouts = new Map();
    for (const { piece, x, y } of measured) {
      const natural = {
        x: base.x + (x - wrapWidth / 2) * base.scale / 1920,
        y: base.y + (y - wrapHeight / 2) * base.scale / 1080,
        scale: base.scale
      };
      const key = piece.dataset.target;
      const saved = overrides && overrides.has(key) ? overrides.get(key) : (state.pieces || {})[key];
      const layout = saved || natural;
      const dx = (layout.x - natural.x) * 1920 / base.scale;
      const dy = (layout.y - natural.y) * 1080 / base.scale;
      const scaleX = layout.scaleX || layout.scale || 1;
      const scaleY = layout.scaleY || layout.scale || 1;
      piece.style.transform = `translate(${dx}px, ${dy}px) scale(${scaleX / base.scale}, ${scaleY / base.scale})`;
      const resultScaleX = layout.resultScaleX || 1;
      const resultScaleY = layout.resultScaleY || 1;
      const result = piece.querySelector(".result");
      if (result && !state.cardTemplate) result.style.transform = `scale(${resultScaleX}, ${resultScaleY})`;
      const image = piece.querySelector(".card-image") || piece.querySelector(".count-value") ||
        piece.querySelector(".heading-text");
      if (image) image.style.opacity = String(layout.imageOpacity ?? templateOpacity(state, key) ?? 1);
      if (result) result.style.opacity = String(layout.resultOpacity ?? state.cardTemplate?.result?.opacity ?? 1);
      const layer = state.layers && state.layers[key];
      // Match native layer ordering before the first explicit layer edit.
      const defaultLayer = key === "cover" ? state.presidents.length + 3 :
        key === "count:green" ? state.presidents.length + 2 :
        key === "count:red" ? state.presidents.length + 1 :
        key === "logo" || key === "heading" ? 0 : state.presidents.findIndex(p => "card:" + p.id === key) + 1;
      // Both boards place every piece themselves now, so the DOM order a piece
      // happens to sit in never decides what covers what.
      piece.style.zIndex = String(Number.isInteger(layer) ? layer : defaultLayer);
      layouts.set(key, { element: piece, layout: { ...layout, scaleX: scaleX, scaleY: scaleY,
        resultScaleX: resultScaleX, resultScaleY: resultScaleY } });
    }
    return layouts;
  };
})();

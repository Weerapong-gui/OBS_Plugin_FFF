// Shared by /overlay and /monitor so the operator's preview and the stream
// never drift apart.
(function () {
  // The overlay decodes its artwork before the reveal, so painting it
  // synchronously keeps the first frame whole. The monitor has no such
  // preparation and would rather not block on a decode.
  const onAir = document.body.classList.contains("overlay");

  function fitCardToImage(slot) {
    if (slot.piece.closest(".bottom-bar")) return;
    if (!slot.img.naturalWidth || !slot.img.naturalHeight) return;
    // Keep the board width consistent while letting every imported PNG keep
    // its own aspect ratio. The template intentionally owns its dimensions.
    if (slot.piece.closest(".board-wrap")?._templateActive) return;
    const contentWidth = 416;
    const contentHeight = Math.max(1, Math.round(contentWidth * slot.img.naturalHeight / slot.img.naturalWidth));
    slot.root.style.width = "420px";
    slot.root.style.height = contentHeight + 4 + "px";
  }

  window.applyCardTemplate = function (slot, template) {
    for (const [name, selector] of [["image", ".card-image"], ["result", ".result"]]) {
      const el = slot.querySelector(selector);
      if (!template) { el.removeAttribute("style"); continue; }
      const box = template[name];
      Object.assign(el.style, { position: "absolute", left: box.x + "px", top: box.y + "px",
        width: box.width + "px", height: box.height + "px", right: "auto", bottom: "auto",
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

  window.modeState = function (state, mode) {
    if (mode !== "bottomBar") return { ...state, mode: "scoreboard" };
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
      root.replaceChildren(); root._slots.clear(); root._logo = null; root._cover = null; root._mode = state.mode;
    }
    if (bottom) { renderBottomBar(root, state, opts); return; }
    const slots = root._slots;

    const columns = Math.min(Math.max(Math.ceil(Math.sqrt(state.presidents.length)), 1), 3);
    const template = `repeat(${columns}, 420px)`;
    if (root.style.gridTemplateColumns !== template) root.style.gridTemplateColumns = template;

    const revealed = opts.revealed;
    const seen = new Set();

    for (const president of state.presidents) {
      let slot = slots.get(president.id);
      if (!slot) {
        slot = buildSlot();
        slot.piece.dataset.target = "card:" + president.id;
        slots.set(president.id, slot);
      }
      seen.add(president.id);

      if (president.cardUrl) {
        if (slot.img.getAttribute("src") !== president.cardUrl) slot.img.src = president.cardUrl;
        slot.img.hidden = false;
        fitCardToImage(slot);
      } else {
        slot.img.removeAttribute("src");
        slot.img.hidden = true;
        slot.root.style.width = "420px";
        slot.root.style.height = "152px";
      }

      const showVote = president.vote !== "none" && (revealed || opts.showVotes);
      const className = "slot " + (showVote ? president.vote : "waiting") + (president.cardUrl ? " has-card" : "");
      // Keep a reveal animation alive when another vote arrives mid-animation.
      const animating = slot.root.classList.contains("reveal-in");
      if (slot.root.className !== className) slot.root.className = className + (animating ? " reveal-in" : "");

      if (opts.justRevealed) {
        slot.root.classList.remove("reveal-in");
        void slot.root.offsetWidth;
        slot.root.classList.add("reveal-in");
      }

      if (root.children[seen.size - 1] !== slot.piece)
        root.insertBefore(slot.piece, root.children[seen.size - 1] || null);
    }

    for (const [id, slot] of slots) {
      if (!seen.has(id)) {
        slot.piece.remove();
        slots.delete(id);
      }
    }
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
    wrap._templateActive = !!state.cardTemplate;
    for (const piece of pieces) {
      const slot = piece.querySelector(".slot");
      if (slot) applyCardTemplate(slot, state.cardTemplate);
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
      const image = piece.querySelector(".card-image");
      if (image) image.style.opacity = String(layout.imageOpacity ?? (!key.startsWith("card:") ? 1 : state.cardTemplate?.image?.opacity) ?? 1);
      if (result) result.style.opacity = String(layout.resultOpacity ?? state.cardTemplate?.result?.opacity ?? 1);
      const layer = state.layers && state.layers[key];
      // Match native layer ordering before the first explicit layer edit.
      const defaultLayer = key === "cover" ? state.presidents.length + 1 :
        key === "logo" ? 0 : state.presidents.findIndex(p => "card:" + p.id === key) + 1;
      piece.style.zIndex = Number.isInteger(layer) ? String(layer) : (state.mode === "bottomBar" ? String(defaultLayer) : "");
      layouts.set(key, { element: piece, layout: { ...layout, scaleX: scaleX, scaleY: scaleY,
        resultScaleX: resultScaleX, resultScaleY: resultScaleY } });
    }
    return layouts;
  };
})();

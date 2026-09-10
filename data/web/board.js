// Shared by /overlay and /monitor so the operator's preview and the stream
// never drift apart.
(function () {
  function fitCardToImage(slot) {
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
        transform: "none", zIndex: String(template.order.indexOf(name)) });
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
    result.className = "result";
    root.append(img, result);
    piece.appendChild(root);
    const slot = { piece: piece, root: root, img: img, result: result };
    img.addEventListener("load", () => fitCardToImage(slot));
    return slot;
  }

  // Slots are reused across updates: rebuilding the grid on every vote would
  // restart PNG loads and flicker on air.
  window.renderBoard = function (root, state, opts) {
    if (!root._slots) root._slots = new Map();
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
  window.applyPieceLayouts = function (wrap, state, overrides) {
    const base = state.layout || { x: 0.5, y: 0.5, scale: 1 };
    applyLayout(wrap, base);
    const layouts = new Map();
    for (const piece of wrap.querySelectorAll(".piece")) {
      let x = piece.offsetWidth / 2;
      let y = piece.offsetHeight / 2;
      for (let node = piece; node && node !== wrap; node = node.offsetParent) {
        x += node.offsetLeft;
        y += node.offsetTop;
      }
      const natural = {
        x: base.x + (x - wrap.offsetWidth / 2) * base.scale / 1920,
        y: base.y + (y - wrap.offsetHeight / 2) * base.scale / 1080,
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
      const slot = piece.querySelector(".slot");
      if (slot) {
        piece.closest(".board-wrap")._templateActive = !!state.cardTemplate;
        applyCardTemplate(slot, state.cardTemplate);
      }
      if (result && !state.cardTemplate) result.style.transform = `scale(${resultScaleX}, ${resultScaleY})`;
      const layer = state.layers && state.layers[key];
      piece.style.zIndex = Number.isInteger(layer) ? String(layer) : "";
      layouts.set(key, { element: piece, layout: { ...layout, scaleX: scaleX, scaleY: scaleY,
        resultScaleX: resultScaleX, resultScaleY: resultScaleY } });
    }
    return layouts;
  };
})();

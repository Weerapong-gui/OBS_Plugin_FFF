// Shared by /overlay and /monitor so the operator's preview and the stream
// never drift apart.
(function () {
  function buildSlot() {
    const piece = document.createElement("div");
    piece.className = "piece";
    const root = document.createElement("div");
    const face = document.createElement("div");
    const img = document.createElement("img");
    const name = document.createElement("div");
    const school = document.createElement("div");
    const result = document.createElement("div");
    face.className = "face";
    face.appendChild(img);
    name.className = "name";
    school.className = "school";
    result.className = "result";
    root.append(face, name, school, result);
    piece.appendChild(root);
    return { piece: piece, root: root, face: face, img: img, name: name, school: school };
  }

  // Slots are reused across updates: rebuilding the grid on every vote would
  // restart the photo loads and flicker on air.
  window.renderBoard = function (root, state, opts) {
    if (!root._slots) root._slots = new Map();
    const slots = root._slots;

    const columns = Math.min(Math.max(state.presidents.length, 1), 5);
    const template = `repeat(${columns}, 240px)`;
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

      if (slot.name.textContent !== president.name) slot.name.textContent = president.name;
      if (slot.school.textContent !== president.school) slot.school.textContent = president.school;

      if (president.photoUrl) {
        if (slot.img.getAttribute("src") !== president.photoUrl) slot.img.src = president.photoUrl;
        slot.img.hidden = false;
        const zoom = president.photoZoom || 1;
        const x = president.photoX || 0;
        const y = president.photoY || 0;
        const transform = `translate(${x}%, ${y}%) scale(${zoom})`;
        if (slot.img.style.transform !== transform) slot.img.style.transform = transform;
      } else {
        slot.img.removeAttribute("src");
        slot.img.hidden = true;
      }

      let className = "slot ";
      if (!revealed) {
        className += president.vote === "none" ? "waiting" : "pending";
      } else {
        className += president.vote === "none" ? "missing" : president.vote;
      }
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
      piece.style.transform = `translate(${dx}px, ${dy}px) scale(${layout.scale / base.scale})`;
      layouts.set(key, { element: piece, layout: { ...layout } });
    }
    return layouts;
  };
})();

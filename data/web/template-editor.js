// Drafts stay in this editor until the operator explicitly applies them.
(function () {
  const el = id => document.getElementById(id);
  const viewport = el("templateViewport"), stage = el("templateStage"), slot = el("templateSlot");
  const outline = el("templateSelection"), placeholder = el("templatePlaceholder");
  const fields = { x: el("templateX"), y: el("templateY"), width: el("templateWidth"), height: el("templateHeight") };
  let state, selectedCard = "", draft, active = "result", dirty = false, saving = false, zoom = 1, drag;
  const clone = value => JSON.parse(JSON.stringify(value));
  const clamp = (v, min, max) => Math.min(max, Math.max(min, v));
  function initial() {
    if (state?.cardTemplate) return clone(state.cardTemplate);
    const layout = state?.pieces?.[selectedCard] || {};
    const width = 416 * (layout.resultScaleX || 1), height = 148 * (layout.resultScaleY || 1);
    return { image: { x: 0, y: 0, width: 416, height: 148 },
      result: { x: (416 - width) / 2, y: (148 - height) / 2, width, height },
      order: ["result", "image"] };
  }
  function position(element, box) {
    Object.assign(element.style, { left: box.x + "px", top: box.y + "px", width: box.width + "px", height: box.height + "px" });
  }
  function paint() {
    if (!draft) return;
    applyCardTemplate(slot, draft);
    slot.className = "slot " + el("templateColor").value;
    const person = state?.presidents.find(p => "card:" + p.id === selectedCard);
    const image = slot.querySelector("img");
    if (person?.cardUrl) { if (image.getAttribute("src") !== person.cardUrl) image.src = person.cardUrl; }
    else image.removeAttribute("src");
    image.hidden = !person?.cardUrl;
    placeholder.hidden = !!person?.cardUrl;
    position(placeholder, draft.image);
    placeholder.style.zIndex = String(draft.order.indexOf("image"));
    position(outline, draft[active]);
    stage.style.transform = "scale(" + zoom + ")";
    el("templateZoomValue").textContent = Math.round(zoom * 100) + "%";
    el("templateLayers").replaceChildren(...[...draft.order].reverse().map(name => {
      const button = document.createElement("button");
      button.textContent = name === "image" ? "PNG การ์ด · Placeholder" : "พื้นสีผลธง · Placeholder";
      button.dataset.layer = name;
      button.setAttribute("aria-pressed", String(name === active));
      button.onclick = () => { active = name; paint(); };
      return button;
    }));
    for (const [key, input] of Object.entries(fields))
      if (document.activeElement !== input) input.value = Math.round(draft[active][key]);
    el("templateApply").disabled = !dirty || saving;
    el("templateCancel").disabled = saving;
    el("templateUp").disabled = saving || draft.order.indexOf(active) === 1;
    el("templateDown").disabled = saving || draft.order.indexOf(active) === 0;
  }
  function changed() { dirty = true; el("templateStatus").textContent = "ร่างยังไม่ใช้กับทุกการ์ด"; paint(); }
  function fit() { zoom = clamp((viewport.clientWidth - 100) / 420, 0.25, 4); paint(); }
  el("templateFit").onclick = fit;
  el("templateZoomIn").onclick = () => { zoom = clamp(zoom * 1.25, 0.25, 4); paint(); };
  el("templateZoomOut").onclick = () => { zoom = clamp(zoom / 1.25, 0.25, 4); paint(); };
  el("templateColor").onchange = paint;
  for (const [key, input] of Object.entries(fields)) input.onchange = () => {
    if (saving) return;
    const n = input.valueAsNumber;
    if (!Number.isFinite(n)) { input.value = Math.round(draft[active][key]); return; }
    draft[active][key] = clamp(n, key === "x" || key === "y" ? -2000 : 1, 2000);
    input.value = draft[active][key];
    changed();
  };
  for (const [id, step] of [["templateUp", 1], ["templateDown", -1]]) el(id).onclick = () => {
    if (saving) return;
    const index = draft.order.indexOf(active), next = index + step;
    if (next < 0 || next > 1) return;
    [draft.order[index], draft.order[next]] = [draft.order[next], draft.order[index]];
    changed();
  };
  viewport.onpointerdown = event => {
    if (saving || !draft || event.button !== 0 || drag) return;
    const bounds = stage.getBoundingClientRect();
    const x = (event.clientX - bounds.left) / zoom - 2, y = (event.clientY - bounds.top) / zoom - 2;
    const resize = event.target === el("templateResize");
    if (!resize && event.target !== outline) {
      const hit = [...draft.order].reverse().find(name => {
        const b = draft[name]; return x >= b.x && y >= b.y && x <= b.x + b.width && y <= b.y + b.height;
      });
      if (!hit) return;
      active = hit;
    }
    drag = { x: event.clientX, y: event.clientY, box: { ...draft[active] }, resize };
    viewport.setPointerCapture(event.pointerId);
    event.preventDefault();
    paint();
  };
  viewport.onpointermove = event => {
    if (!drag) return;
    const dx = (event.clientX - drag.x) / zoom, dy = (event.clientY - drag.y) / zoom;
    const b = draft[active];
    if (drag.resize) {
      if (event.shiftKey) {
        const w = drag.box.width, h = drag.box.height;
        const changeX = dx / w, changeY = dy / h;
        const factor = clamp(1 + (Math.abs(changeX) >= Math.abs(changeY) ? changeX : changeY),
          Math.max(1 / w, 1 / h), Math.min(2000 / w, 2000 / h));
        // Keep fractional dimensions so rounding cannot distort the ratio.
        b.width = w * factor;
        b.height = h * factor;
      } else {
        b.width = clamp(Math.round(drag.box.width + dx), 1, 2000);
        b.height = clamp(Math.round(drag.box.height + dy), 1, 2000);
      }
    } else {
      b.x = clamp(Math.round(drag.box.x + dx), -2000, 2000);
      b.y = clamp(Math.round(drag.box.y + dy), -2000, 2000);
    }
    changed();
  };
  viewport.onpointerup = viewport.onpointercancel = viewport.onlostpointercapture = () => { drag = null; };
  el("templateCancel").onclick = () => { dirty = false; draft = initial(); el("templateStatus").textContent = ""; paint(); };
  el("templateApply").onclick = async () => {
    if (saving || !dirty) return;
    saving = true; paint();
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 5000);
    try {
      const response = await fetch("/api/template", { method: "POST",
        headers: { "Content-Type": "application/json" }, body: JSON.stringify(draft), signal: controller.signal });
      if (!response.ok) throw new Error("save failed");
      state.cardTemplate = clone(draft);
      dirty = false;
      el("templateStatus").textContent = "ใช้แม่แบบกับทุกการ์ดแล้ว";
    } catch (error) { el("templateStatus").textContent = "บันทึกไม่สำเร็จ — ร่างยังอยู่ กดใช้เพื่อลองใหม่"; }
    finally { clearTimeout(timer); saving = false; paint(); }
  };
  window.templateEditor = { update(next, card) {
    state = next; selectedCard = card;
    if (!dirty && !saving) draft = initial();
    paint();
  }};
  new ResizeObserver(fit).observe(viewport);
})();

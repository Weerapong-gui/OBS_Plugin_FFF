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
    const baseWidth = state?.mode === "bottomBar" ? 850 / Math.max(1, Math.ceil(state.presidents.length / 2)) : 416;
    const baseHeight = state?.mode === "bottomBar" ? 250 : 148;
    const width = baseWidth * (layout.resultScaleX || 1), height = baseHeight * (layout.resultScaleY || 1);
    return { image: { x: 0, y: 0, width: baseWidth, height: baseHeight },
      result: { x: (baseWidth - width) / 2, y: (baseHeight - height) / 2, width, height },
      order: ["result", "image"] };
  }
  function position(element, box) {
    Object.assign(element.style, { left: box.x + "px", top: box.y + "px", width: box.width + "px", height: box.height + "px" });
  }
  function paint() {
    if (!draft) return;
    applyCardTemplate(slot, draft);
    slot.className = "slot " + el("templateColor").value + (state?.mode === "bottomBar" ? " bottom-template" : "");
    const person = state?.presidents.find(p => "card:" + p.id === selectedCard);
    const image = slot.querySelector("img");
    const url = state?.mode === "bottomBar" ? person?.bottomBarUrl : person?.cardUrl;
    if (url) { if (image.getAttribute("src") !== url) image.src = url; }
    else image.removeAttribute("src");
    image.hidden = !url;
    placeholder.hidden = !!url;
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
    el("templateOpacity").value = Math.round((draft[active].opacity ?? 1) * 100);
    el("templateApply").disabled = !dirty || saving;
    el("templateCancel").disabled = saving;
    el("templateUp").disabled = saving || draft.order.indexOf(active) === 1;
    el("templateDown").disabled = saving || draft.order.indexOf(active) === 0;
  }
  function changed() { dirty = true; el("templateStatus").textContent = "ร่างยังไม่ใช้กับทุกการ์ด"; paint(); }
  function fit() {
    const width = draft ? Math.max(draft.image.x + draft.image.width, draft.result.x + draft.result.width) : 420;
    const height = draft ? Math.max(draft.image.y + draft.image.height, draft.result.y + draft.result.height) : 152;
    zoom = clamp(Math.min((viewport.clientWidth - 100) / Math.max(width, 1), (viewport.clientHeight - 110) / Math.max(height, 1)), 0.25, 4); paint();
  }
  el("templateFit").onclick = fit;
  el("templateZoomIn").onclick = () => { zoom = clamp(zoom * 1.25, 0.25, 4); paint(); };
  el("templateZoomOut").onclick = () => { zoom = clamp(zoom / 1.25, 0.25, 4); paint(); };
  el("templateColor").onchange = paint;
  el("templateOpacity").onchange = event => {
    if (saving || !Number.isFinite(event.target.valueAsNumber)) return;
    draft[active].opacity = clamp(event.target.valueAsNumber / 100, 0, 1); changed();
  };
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
    const requestMode = state.mode, submitted = clone(draft), savedState = state;
    saving = true; paint();
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 5000);
    try {
      const response = await fetch("/api/template", { method: "POST",
        headers: { "Content-Type": "application/json" }, body: JSON.stringify({ ...submitted, mode: requestMode }), signal: controller.signal });
      if (!response.ok) throw new Error("save failed");
      savedState.cardTemplate = submitted;
      if (state.mode !== requestMode) return;
      state.cardTemplate = submitted;
      draft = clone(submitted);
      dirty = false;
      el("templateStatus").textContent = "ใช้แม่แบบกับทุกการ์ดแล้ว";
    } catch (error) { if (state.mode === requestMode) { dirty = false; draft = initial(); el("templateStatus").textContent = "บันทึกไม่สำเร็จ — คืนค่าก่อนแก้ไขแล้ว"; } }
    finally { clearTimeout(timer); saving = false; paint(); }
  };
  window.templateEditor = { update(next, card) {
    if (state?.mode !== next.mode) { dirty = false; drag = null; draft = null; el("templateStatus").textContent = ""; }
    state = next; selectedCard = card;
    if (!draft || (!dirty && !saving)) draft = initial();
    paint();
  }};
  new ResizeObserver(fit).observe(viewport);
})();

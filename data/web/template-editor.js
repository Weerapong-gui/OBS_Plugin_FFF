// Drafts stay in this editor until the operator explicitly applies them.
// One panel edits three templates: the shared card artwork, the centre logo and
// the flag counters. The logo and the counters are Bottom Bar furniture with no
// counterpart on the scoreboard, so each keeps its own boxes instead of
// borrowing the card ones.
(function () {
  // Network patience, not motion: how long a template save may hang before it
  // is abandoned.
  const SAVE_TIMEOUT_MS = 5000;
  const el = id => document.getElementById(id);
  const viewport = el("templateViewport"), stage = el("templateStage"), slot = el("templateSlot");
  const outline = el("templateSelection"), placeholder = el("templatePlaceholder"), counter = el("templateCount");
  const headingText = el("templateHeading");
  const fields = { x: el("templateX"), y: el("templateY"), width: el("templateWidth"), height: el("templateHeight") };
  const BOXES = { card: ["result", "image"], logo: ["image"], count: ["value"], heading: ["box"] };
  // The two kinds that are made of words rather than artwork share the font
  // picker, the weight ladder and the colour field.
  const TYPED = new Set(["count", "heading"]);
  const TITLES = { card: "แม่แบบทุกการ์ด", logo: "แม่แบบโลโก้กลาง", count: "แม่แบบตัวเลขจำนวนธง",
    heading: "ข้อความหัวเรื่อง" };
  const HINTS = { card: "แก้ตัวอย่าง แล้วกดใช้เพื่ออัปเดตทุกการ์ด",
    logo: "กล่องของโลโก้กลาง ปรับแยกจากแม่แบบการ์ด",
    count: "กล่องและฟอนต์ของตัวเลข ใช้กับทั้งธงแดงและธงเขียว",
    heading: "ข้อความ ฟอนต์ และสีของหัวเรื่องเหนือกองการ์ด" };
  const APPLY = { card: "ใช้แม่แบบกับทุกการ์ด", logo: "ใช้แม่แบบโลโก้", count: "ใช้แม่แบบตัวเลข",
    heading: "ใช้หัวเรื่อง" };
  const APPLIED = { card: "ใช้แม่แบบกับทุกการ์ดแล้ว", logo: "ใช้แม่แบบโลโก้แล้ว", count: "ใช้แม่แบบตัวเลขแล้ว",
    heading: "ใช้หัวเรื่องแล้ว" };
  const DRAFTED = { card: "ร่างยังไม่ใช้กับทุกการ์ด", logo: "ร่างโลโก้ยังไม่ถูกใช้", count: "ร่างตัวเลขยังไม่ถูกใช้",
    heading: "ร่างหัวเรื่องยังไม่ถูกใช้" };
  const KEYS = { card: "cardTemplate", logo: "logoTemplate", count: "countTemplate",
    heading: "headingTemplate" };
  let state, selectedCard = "", draft, kind = "card", active = "result", dirty = false, saving = false, drag;
  // The stage is a preview like the board canvas, so it gets the same wheel
  // zoom and Space-drag hand rather than a scrollbar.
  const stageView = createViewZoom({
    frame: viewport, content: stage, padding: 24, size: contentSize,
    onChange: scale => { el("templateZoomValue").textContent = Math.round(scale * 100) + "%"; }
  });
  function contentSize() {
    if (!draft) return { width: 420, height: 152 };
    const boxes = order().map(name => draft[name]);
    return { width: Math.max(...boxes.map(box => box.x + box.width), 1),
      height: Math.max(...boxes.map(box => box.y + box.height), 1) };
  }
  const clone = value => JSON.parse(JSON.stringify(value));
  const clamp = (v, min, max) => Math.min(max, Math.max(min, v));
  function kindOf(next, target) {
    if (next.mode !== "bottomBar") return target === "heading" ? "heading" : "card";
    if (target === "logo") return "logo";
    return target.startsWith("count:") ? "count" : "card";
  }
  // Only the card template has a stacking order; the other two are one box.
  function order() { return kind === "card" ? draft.order : BOXES[kind]; }
  function initial() {
    const saved = state?.[KEYS[kind]];
    if (saved) {
      const draft = clone(saved);
      if (kind === "count" && !draft.fontWeight) draft.fontWeight = 700;
      return draft;
    }
    // Each default matches the piece's own CSS box, so opening the panel never
    // moves anything that the operator has not touched yet.
    if (kind === "logo") return { image: { x: 0, y: 0, width: 220, height: 250 } };
    if (kind === "count") return { value: { x: 0, y: 0, width: 110, height: 130 }, fontFamily: "", fontSize: 96,
      fontWeight: 700, colors: { red: "#e23c3c", green: "#21b04a" } };
    // headingOf() in board.js is the one place the title's defaults live, so
    // the panel opens on exactly what the board would have drawn.
    if (kind === "heading") return headingOf(state?.headingTemplate);
    const layout = state?.pieces?.[selectedCard] || {};
    // On Show Status the card's box is its PNG's; 416x148 is only what the
    // panel opens at before the artwork has loaded (fitDraftToImage corrects it).
    const stageImage = slot.querySelector("img");
    const baseWidth = state?.mode === "bottomBar" ? 850 / Math.max(1, Math.ceil(state.presidents.length / 2))
      : stageImage.naturalWidth || 416;
    const baseHeight = state?.mode === "bottomBar" ? 250 : stageImage.naturalHeight || 148;
    const width = baseWidth * (layout.resultScaleX || 1), height = baseHeight * (layout.resultScaleY || 1);
    return { image: { x: 0, y: 0, width: baseWidth, height: baseHeight },
      result: { x: (baseWidth - width) / 2, y: (baseHeight - height) / 2, width, height },
      order: ["result", "image"] };
  }
  function position(element, box) {
    Object.assign(element.style, { left: box.x + "px", top: box.y + "px", width: box.width + "px", height: box.height + "px" });
  }
  function setSource(image, url) {
    if (url) { if (image.getAttribute("src") !== url) image.src = url; }
    else image.removeAttribute("src");
    image.hidden = !url;
  }
  // Show Status sizes a card from its own PNG, so the panel cannot pretend the
  // image rectangle is the operator's to set: it shows the picture's box.
  function naturalCard() { return kind === "card" && state?.mode !== "bottomBar"; }
  // The PNG's own box, so there is nothing here for the operator to drag.
  function boxLocked() { return naturalCard() && active === "image"; }
  function fitDraftToImage(image) {
    if (!draft || !naturalCard() || !image.naturalWidth || !image.naturalHeight) return;
    const box = draft.image;
    if (box.x === 0 && box.y === 0 && box.width === image.naturalWidth &&
        box.height === image.naturalHeight)
      return;
    Object.assign(box, { x: 0, y: 0, width: image.naturalWidth, height: image.naturalHeight });
    paint();
  }
  function paintStage() {
    const image = slot.querySelector("img"), result = slot.querySelector(".result");
    if (kind === "card") {
      const natural = naturalCard();
      applyCardTemplate(slot, draft, { natural: natural });
      slot.className = "slot " + el("templateColor").value + (state?.mode === "bottomBar" ? " bottom-template" : "");
      const person = state?.presidents.find(p => "card:" + p.id === selectedCard);
      setSource(image, state?.mode === "bottomBar" ? person?.bottomBarUrl : person?.cardUrl);
      // The stage slot is the card's box, so on Show Status it is the PNG's.
      if (natural) {
        fitDraftToImage(image);
        Object.assign(slot.style, { width: draft.image.width + "px", height: draft.image.height + "px" });
      } else {
        slot.style.removeProperty("width");
        slot.style.removeProperty("height");
      }
      result.hidden = false;
      counter.hidden = true;
      headingText.hidden = true;
      placeholder.hidden = !image.hidden;
      placeholder.textContent = "PNG ของแต่ละการ์ด";
      position(placeholder, draft.image);
      placeholder.style.zIndex = String(draft.order.indexOf("image"));
      return;
    }
    slot.className = "slot bottom-template" +
      (kind === "count" ? (selectedCard === "count:green" ? " count-green" : " count-red") : "");
    slot.style.removeProperty("width");
    slot.style.removeProperty("height");
    result.hidden = true;
    headingText.hidden = kind !== "heading";
    if (kind === "heading") {
      image.hidden = true;
      counter.hidden = true;
      placeholder.hidden = true;
      applyHeadingTemplate(headingText, draft);
      return;
    }
    if (kind === "logo") {
      applyPieceTemplate(image, draft.image);
      setSource(image, state?.presidents.find(p => p.id === state.logoPresidentId)?.logoUrl);
      counter.hidden = true;
      placeholder.hidden = !image.hidden;
      placeholder.textContent = "PNG โลโก้กลาง";
      position(placeholder, draft.image);
      placeholder.style.zIndex = "0";
      return;
    }
    image.hidden = true;
    counter.hidden = false;
    const vote = selectedCard === "count:green" ? "green" : "red";
    applyCountTemplate(counter, draft, vote);
    // Preview the live tally rather than a made-up digit, so the operator sizes
    // the box against the number that will actually go on air.
    counter.textContent = String((state?.presidents || []).filter(p => p.vote === vote).length);
    placeholder.hidden = true;
  }
  function paint() {
    if (!draft) return;
    const single = kind !== "card";
    el("templateTitle").textContent = TITLES[kind];
    el("templateHint").textContent = HINTS[kind];
    el("templateApply").textContent = APPLY[kind];
    el("templateColorField").hidden = single;
    el("templateLayers").hidden = single;
    el("templateLayerTools").hidden = single;
    el("templateFontFields").hidden = !TYPED.has(kind);
    el("templateHeadingFields").hidden = kind !== "heading";
    el("templateFontSize").min = String(kind === "heading" ? 8 : 24);
    paintStage();
    position(outline, draft[active]);
    // Rebuilt only when the order really changed. A repaint for some other
    // reason — a late PNG, an SSE frame — must not replace a button the
    // operator is in the middle of pressing.
    const layers = el("templateLayers");
    const wanted = single ? "" : draft.order.join();
    if (layers.dataset.order !== wanted) {
      layers.dataset.order = wanted;
      layers.replaceChildren(...(single ? [] : [...draft.order].reverse().map(name => {
        const button = document.createElement("button");
        button.textContent = name === "image" ? "PNG การ์ด · Placeholder" : "พื้นสีผลธง · Placeholder";
        button.dataset.layer = name;
        button.onclick = () => { active = name; paint(); };
        return button;
      })));
    }
    for (const button of layers.children)
      button.setAttribute("aria-pressed", String(button.dataset.layer === active));
    const fixed = boxLocked();
    for (const [key, input] of Object.entries(fields)) {
      if (document.activeElement !== input) input.value = Math.round(draft[active][key]);
      input.disabled = saving || fixed;
    }
    el("templateResize").disabled = saving || fixed;
    outline.style.cursor = fixed ? "default" : "";
    el("templateSizeNote").hidden = !fixed;
    el("templateOpacity").value = Math.round((draft[active].opacity ?? 1) * 100);
    if (TYPED.has(kind)) paintFont();
    el("templateApply").disabled = !dirty || saving;
    el("templateCancel").disabled = saving;
    el("templateUp").disabled = saving || single || draft.order.indexOf(active) === 1;
    el("templateDown").disabled = saving || single || draft.order.indexOf(active) === 0;
  }
  function changed() { dirty = true; el("templateStatus").textContent = DRAFTED[kind]; paint(); }
  el("templateFit").onclick = () => stageView.fit();
  el("templateZoomActual").onclick = () => stageView.actual();
  el("templateZoomIn").onclick = () => stageView.zoomBy(1.25);
  el("templateZoomOut").onclick = () => stageView.zoomBy(1 / 1.25);
  el("templateColor").onchange = paint;
  el("templateOpacity").onchange = event => {
    if (saving || !Number.isFinite(event.target.valueAsNumber)) return;
    draft[active].opacity = clamp(event.target.valueAsNumber / 100, 0, 1); changed();
  };
  // The counter's number is the one thing on the bar that is text, so it gets a
  // font installed on this machine rather than one of a few baked-in stacks.
  // queryLocalFonts needs a gesture and a permission, and is missing entirely in
  // some browsers, so the typed field is always there as the way that works.
  function countVote() { return selectedCard === "count:green" ? "green" : "red"; }
  // One colour each for the two counters, one for the title.
  function typedColor() { return kind === "count" ? draft.colors[countVote()] : draft.color; }
  function setTypedColor(value) {
    if (kind === "count") draft.colors = { ...draft.colors, [countVote()]: value };
    else draft.color = value;
  }

  // FontData carries a style name, never a number, so the face's weight has to
  // be read out of that name. A family the operator typed by hand, or a browser
  // that will not list fonts, falls back to the full ladder.
  const WEIGHTS = [[100, "Thin"], [200, "ExtraLight"], [300, "Light"], [400, "Regular"],
    [500, "Medium"], [600, "SemiBold"], [700, "Bold"], [800, "ExtraBold"], [900, "Black"]];
  const STYLE_WEIGHTS = [[900, /\b(black|heavy)\b/i], [800, /\b(extra|ultra)\s?bold\b/i],
    [600, /\b(semi|demi)\s?bold\b/i], [700, /\bbold\b/i], [500, /\bmedium\b/i],
    [300, /\blight\b/i], [200, /\b(extra|ultra)\s?light\b/i], [100, /\bthin\b/i]];
  const faceWeights = new Map();
  function weightOf(style) {
    for (const [weight, pattern] of STYLE_WEIGHTS) if (pattern.test(style)) return weight;
    return 400;
  }
  function weightsFor(family) {
    const found = faceWeights.get(family);
    return found && found.size ? [...found].sort((a, b) => a - b) : WEIGHTS.map(([weight]) => weight);
  }
  function paintFont() {
    const family = draft.fontFamily || "";
    const select = el("templateFont"), manual = el("templateFontManual");
    select.value = [...select.options].some(option => option.value === family) ? family : "";
    if (document.activeElement !== manual) manual.value = family;
    if (document.activeElement !== el("templateFontSize")) el("templateFontSize").value = Math.round(draft.fontSize);
    const weights = weightsFor(family);
    const names = new Map(WEIGHTS);
    const picker = el("templateFontWeight");
    const wanted = weights.join();
    if (picker.dataset.weights !== wanted) {
      picker.dataset.weights = wanted;
      picker.replaceChildren(...weights.map(weight =>
        new Option(weight + " " + (names.get(weight) || ""), String(weight))));
    }
    picker.value = String(draft.fontWeight);
    // The sample has to carry the weight too. It not doing so is exactly why the
    // panel and the stream used to disagree.
    const preview = el("templateFontPreview");
    preview.style.fontFamily = family ? '"' + family + '"' : "";
    preview.style.fontWeight = String(draft.fontWeight);
    preview.style.color = typedColor();
    preview.textContent = kind === "heading"
      ? (draft.text.split("\n")[0] || family || "ค่าเริ่มต้นของระบบ")
      : "0123456789 · " + (family || "ค่าเริ่มต้นของระบบ");
    el("templateColorValue").value = typedColor();
    if (kind === "heading") {
      if (document.activeElement !== el("templateText")) el("templateText").value = draft.text;
      if (document.activeElement !== el("templateLineHeight"))
        el("templateLineHeight").value = Math.round(draft.lineHeight);
      el("templateAlign").value = draft.align;
    }
    // Best effort, and only about this browser: it says nothing about what the
    // browser source inside OBS can resolve.
    const missing = family && document.fonts && !document.fonts.check(`${draft.fontWeight} 16px "${family}"`);
    el("templateFontNote").textContent = missing
      ? `เครื่องนี้ไม่มี "${family}" น้ำหนัก ${draft.fontWeight} จะถูกแทนด้วยฟอนต์อื่น`
      : el("templateFontNote").dataset.found || "";
  }
  function chooseFamily(family) {
    if (saving || !TYPED.has(kind) || draft.fontFamily === family) return;
    draft.fontFamily = family;
    // Keep the weight if the new family really has that face, otherwise take the
    // nearest one it does, so switching family never silently fakes a weight.
    const weights = weightsFor(family);
    if (!weights.includes(draft.fontWeight))
      draft.fontWeight = weights.reduce((best, weight) =>
        Math.abs(weight - draft.fontWeight) < Math.abs(best - draft.fontWeight) ? weight : best, weights[0]);
    changed();
  }
  el("templateFont").onchange = event => chooseFamily(event.target.value);
  el("templateFontManual").onchange = event => chooseFamily(event.target.value.trim());
  el("templateFontWeight").onchange = event => {
    if (saving || !TYPED.has(kind)) return;
    draft.fontWeight = clamp(Number(event.target.value) || 400, 100, 900);
    changed();
  };
  el("templateColorValue").oninput = event => {
    if (saving || !TYPED.has(kind)) return;
    setTypedColor(event.target.value);
    changed();
  };
  // The title is the one field on this panel that is words, so it is the one
  // that repaints as it is typed rather than when the field is left.
  el("templateText").oninput = event => {
    if (saving || kind !== "heading") return;
    draft.text = event.target.value.slice(0, 200).split("\n").slice(0, 8).join("\n");
    if (event.target.value !== draft.text) event.target.value = draft.text;
    changed();
  };
  el("templateLineHeight").onchange = event => {
    if (saving || kind !== "heading") return;
    const height = event.target.valueAsNumber;
    if (!Number.isFinite(height)) { event.target.value = Math.round(draft.lineHeight); return; }
    draft.lineHeight = clamp(Math.round(height), 8, 400);
    event.target.value = draft.lineHeight;
    changed();
  };
  el("templateAlign").onchange = event => {
    if (saving || kind !== "heading") return;
    draft.align = event.target.value;
    changed();
  };
  el("templateFontPick").onclick = async () => {
    const note = el("templateFontNote"), select = el("templateFont");
    if (!window.queryLocalFonts) {
      note.dataset.found = "เบราว์เซอร์นี้อ่านรายชื่อฟอนต์ไม่ได้ ให้พิมพ์ชื่อฟอนต์เอง";
      paintFont();
      return;
    }
    try {
      // Every face, not just every family: the weight the operator wants lives in
      // the face list, and dropping it is why a weight could not be chosen.
      faceWeights.clear();
      for (const font of await window.queryLocalFonts()) {
        if (!faceWeights.has(font.family)) faceWeights.set(font.family, new Set());
        faceWeights.get(font.family).add(weightOf(font.style || ""));
      }
      const families = [...faceWeights.keys()].sort();
      select.replaceChildren(new Option("ค่าเริ่มต้นของระบบ", ""),
        ...families.map(family => new Option(family, family)));
      note.dataset.found = `พบ ${families.length} ฟอนต์ในเครื่อง`;
      el("templateFontWeight").dataset.weights = "";
      paintFont();
    } catch (error) {
      note.dataset.found = "ไม่ได้รับสิทธิ์อ่านรายชื่อฟอนต์ ให้พิมพ์ชื่อฟอนต์เอง";
      paintFont();
    }
  };
  el("templateFontSize").onchange = event => {
    if (saving || !TYPED.has(kind)) return;
    const size = event.target.valueAsNumber;
    if (!Number.isFinite(size)) { event.target.value = Math.round(draft.fontSize); return; }
    // A title can be small; a counter the board reads at a glance cannot.
    draft.fontSize = clamp(Math.round(size), kind === "heading" ? 8 : 24, 400);
    event.target.value = draft.fontSize;
    changed();
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
    if (saving || kind !== "card") return;
    const index = draft.order.indexOf(active), next = index + step;
    if (next < 0 || next > 1) return;
    [draft.order[index], draft.order[next]] = [draft.order[next], draft.order[index]];
    changed();
  };
  viewport.onpointerdown = event => {
    if (saving || !draft || event.button !== 0 || drag) return;
    const bounds = stage.getBoundingClientRect();
    const x = (event.clientX - bounds.left) / stageView.scale, y = (event.clientY - bounds.top) / stageView.scale;
    const resize = event.target === el("templateResize");
    if (!resize && event.target !== outline) {
      // A locked box is not pickable either: on Show Status the artwork's
      // rectangle is the PNG's, so a drag there would only be undone.
      const pickable = naturalCard() ? order().filter(name => name !== "image") : order();
      const hit = [...pickable].reverse().find(name => {
        const b = draft[name]; return x >= b.x && y >= b.y && x <= b.x + b.width && y <= b.y + b.height;
      });
      if (!hit) return;
      active = hit;
    }
    if (boxLocked()) { paint(); return; }
    drag = { x: event.clientX, y: event.clientY, box: { ...draft[active] }, resize };
    viewport.setPointerCapture(event.pointerId);
    event.preventDefault();
    paint();
  };
  viewport.onpointermove = event => {
    if (!drag) return;
    const dx = (event.clientX - drag.x) / stageView.scale, dy = (event.clientY - drag.y) / stageView.scale;
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
    const requestMode = state.mode, requestKind = kind, submitted = clone(draft), savedState = state;
    saving = true; paint();
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), SAVE_TIMEOUT_MS);
    try {
      const response = await fetch("/api/template", { method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ ...submitted, mode: requestMode, piece: requestKind }), signal: controller.signal });
      if (!response.ok) throw new Error("save failed");
      savedState[KEYS[requestKind]] = submitted;
      if (state.mode !== requestMode || kind !== requestKind) return;
      state[KEYS[requestKind]] = submitted;
      draft = clone(submitted);
      dirty = false;
      el("templateStatus").textContent = APPLIED[requestKind];
    } catch (error) {
      if (state.mode === requestMode && kind === requestKind) {
        dirty = false; draft = initial(); el("templateStatus").textContent = "บันทึกไม่สำเร็จ คืนค่าก่อนแก้ไขแล้ว";
      }
    }
    finally { clearTimeout(timer); saving = false; paint(); }
  };
  window.templateEditor = { update(next, card) {
    const nextKind = kindOf(next, card || "");
    // Switching mode or piece abandons a draft rather than carrying boxes from
    // one template into another, where they would mean something else.
    const switched = kind !== nextKind;
    if (state?.mode !== next.mode || switched) {
      dirty = false; drag = null; draft = null; el("templateStatus").textContent = "";
      // Box names belong to a kind, so only a kind change invalidates the
      // selected box. Switching mode keeps the operator on the same layer.
      if (switched) { kind = nextKind; active = BOXES[kind][0]; }
    }
    state = next; selectedCard = card;
    if (!draft || (!dirty && !saving)) draft = initial();
    paint();
    // A different template is a different subject; frame it rather than leaving
    // the previous one's zoom pointing at empty space.
    if (switched) stageView.fit();
  }};
  // The panel opens before the artwork is decoded, so the box is taken again
  // the moment it arrives — the same correction the board makes on air.
  slot.querySelector("img").addEventListener("load", event => fitDraftToImage(event.target));
  new ResizeObserver(() => stageView.refit()).observe(viewport);
})();

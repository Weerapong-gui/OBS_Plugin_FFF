// View controls for the editing previews: the wheel zooms around the cursor and
// holding Space turns the pointer into a hand. None of it touches a saved
// coordinate — it only changes how much of the work the operator can see.
(function () {
  const MIN = 0.1, MAX = 8;
  const clamp = (value, low, high) => Math.min(high, Math.max(low, value));
  const views = new Set();
  let spaceHeld = false;

  function setSpace(held) {
    if (spaceHeld === held) return;
    spaceHeld = held;
    for (const view of views) view.frame.classList.toggle("is-panning", held);
  }
  // Space scrolls the page and types a space in a field. It only becomes the
  // hand tool when the operator is in neither, which is the same guard the
  // arrow-key nudges use.
  document.addEventListener("keydown", (event) => {
    if (event.code !== "Space" || event.repeat) return;
    if (event.target.closest("input, select, textarea, button, [contenteditable]")) return;
    event.preventDefault();
    setSpace(true);
  });
  document.addEventListener("keyup", (event) => { if (event.code === "Space") setSpace(false); });
  // Alt-tabbing away while the key is down would otherwise leave the hand stuck.
  window.addEventListener("blur", () => setSpace(false));

  window.createViewZoom = function (options) {
    const frame = options.frame, content = options.content;
    const size = options.size || (() => ({ width: options.width, height: options.height }));
    const view = { frame: frame, scale: 1, panX: 0, panY: 0, panning: false, touched: false };
    let start = null;

    view.apply = function () {
      content.style.transform = `translate(${view.panX}px, ${view.panY}px) scale(${view.scale})`;
      if (options.onChange) options.onChange(view.scale);
    };

    // Put the content point that is under the cursor back under the cursor at
    // the new scale. Reading the origin off the live rect keeps the frame's
    // border and padding out of the arithmetic.
    view.zoomAt = function (factor, clientX, clientY) {
      const next = clamp(view.scale * factor, MIN, MAX);
      if (next === view.scale) return;
      const rect = content.getBoundingClientRect();
      const pointX = (clientX - rect.left) / view.scale;
      const pointY = (clientY - rect.top) / view.scale;
      view.panX = clientX - (rect.left - view.panX) - pointX * next;
      view.panY = clientY - (rect.top - view.panY) - pointY * next;
      view.scale = next;
      view.touched = true;
      view.apply();
    };

    view.zoomBy = function (factor) {
      const box = frame.getBoundingClientRect();
      view.zoomAt(factor, box.left + box.width / 2, box.top + box.height / 2);
    };

    function center(scale) {
      const box = frame.getBoundingClientRect();
      const measure = size();
      view.scale = scale;
      view.panX = 0;
      view.panY = 0;
      view.apply();
      // One more read so the offset is measured against where pan zero actually
      // lands, whatever the frame's own box turns out to be.
      const rect = content.getBoundingClientRect();
      view.panX = box.left + (box.width - measure.width * scale) / 2 - rect.left;
      view.panY = box.top + (box.height - measure.height * scale) / 2 - rect.top;
      view.apply();
    }

    view.fit = function () {
      const box = frame.getBoundingClientRect();
      const measure = size();
      const pad = options.padding || 0;
      center(clamp(Math.min((box.width - pad * 2) / Math.max(measure.width, 1),
        (box.height - pad * 2) / Math.max(measure.height, 1)), MIN, MAX));
      // A fresh fit is the resting state again, so a window resize may refit.
      view.touched = false;
    };

    view.actual = function () { center(1); view.touched = true; };

    // A resize refits only while the operator has not chosen a zoom of their
    // own; once they have, the view stays where they put it.
    view.refit = function () { if (!view.touched) view.fit(); };

    frame.addEventListener("wheel", (event) => {
      event.preventDefault();
      // Exponential so a trackpad's small deltas and a wheel's large ones feel
      // like the same gesture.
      view.zoomAt(Math.exp(-event.deltaY * 0.0015), event.clientX, event.clientY);
    }, { passive: false });

    // Capture phase, and immediate: while Space is held the pieces inside never
    // see the pointer at all, so panning can neither select nor move one of
    // them. Immediate matters because an editor may have its own handler on
    // this very element, where the capture flag no longer orders anything.
    frame.addEventListener("pointerdown", (event) => {
      if (!spaceHeld || event.button !== 0 || event.target.closest(".zoom-bar")) return;
      event.preventDefault();
      event.stopImmediatePropagation();
      view.panning = true;
      frame.classList.add("grabbing");
      frame.setPointerCapture(event.pointerId);
      start = { x: event.clientX, y: event.clientY, panX: view.panX, panY: view.panY };
    }, true);

    frame.addEventListener("pointermove", (event) => {
      if (!view.panning) return;
      event.stopImmediatePropagation();
      view.panX = start.panX + event.clientX - start.x;
      view.panY = start.panY + event.clientY - start.y;
      view.touched = true;
      view.apply();
    }, true);

    const release = (event) => {
      if (!view.panning) return;
      view.panning = false;
      frame.classList.remove("grabbing");
      event.stopImmediatePropagation();
    };
    for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
      frame.addEventListener(name, release, true);

    views.add(view);
    frame.classList.toggle("is-panning", spaceHeld);
    view.apply();
    return view;
  };
})();

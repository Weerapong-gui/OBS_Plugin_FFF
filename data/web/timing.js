// Every duration the presentation spends on screen, in one place. Shared by
// /overlay and /monitor, the same way board.js is, so the operator's preview
// and the stream keep the same rhythm.
//
// Only the lengths live here. Easings, keyframes and the order things move in
// are the design and stay where they are drawn: changing a number below makes
// the same animation longer or shorter, never a different one.
(function () {
  const DEFAULTS = Object.freeze({
    // Show Status: cards slide in from the left one after another.
    scoreboard: Object.freeze({
      card: 600, stagger: 120, maxStagger: 1200,
      exit: 320, exitStagger: 60, maxExitStagger: 600
    }),
    // BOTTOM BAR: logo and counters, then the cover, then the cards in
    // centre-out pairs.
    bottomBar: Object.freeze({
      logo: 240, cover: 400, card: 600,
      firstPair: 100, pair: 60, maxStagger: 560, exit: 320
    }),
    // Motion a single piece makes on its own, on either board.
    board: Object.freeze({ reveal: 350, countRoll: 520, countTick: 50, slotFrame: 250 }),
    // The monitor's own furniture. Nothing here reaches the stream.
    monitor: Object.freeze({ guide: 80, control: 150, confirm: 3000 }),
    // The voter's page: the flag buttons lighting up as one is chosen.
    phone: Object.freeze({ flag: 120 })
  });

  // What a value is allowed to be. The session validates against the same
  // ranges, so a number the panel accepts is a number the plugin will store.
  const LIMITS = Object.freeze({ countTick: [10, 500], confirm: [500, 30000] });
  const DEFAULT_LIMIT = Object.freeze([0, 5000]);
  function limitFor(key) { return LIMITS[key] || DEFAULT_LIMIT; }

  // CSS carries its own share of the motion, so the durations it needs are
  // published as custom properties rather than duplicated in the stylesheet.
  const PROPERTIES = Object.freeze({
    "--fff-dur-reveal": ["board", "reveal"],
    "--fff-dur-slot": ["board", "slotFrame"],
    "--fff-dur-guide": ["monitor", "guide"],
    "--fff-dur-control": ["monitor", "control"],
    "--fff-dur-flag": ["phone", "flag"]
  });

  function merge(saved) {
    const merged = {};
    for (const [group, values] of Object.entries(DEFAULTS)) {
      const incoming = (saved && typeof saved[group] === "object" && saved[group]) || {};
      merged[group] = {};
      for (const [key, value] of Object.entries(values)) {
        const [min, max] = limitFor(key);
        const given = incoming[key];
        merged[group][key] = typeof given === "number" && Number.isFinite(given) &&
          given >= min && given <= max ? given : value;
      }
    }
    return merged;
  }

  window.FFF_TIMING_DEFAULTS = DEFAULTS;
  window.FFF_TIMING_LIMITS = { limitFor: limitFor };
  window.fffTiming = merge(null);

  // Called with the session's `timing` before every render on both pages, so a
  // change made on the monitor reaches the overlay on the next SSE frame
  // without either page being reloaded.
  window.applyTiming = function (saved) {
    window.fffTiming = merge(saved);
    const root = document.documentElement;
    for (const [property, [group, key]] of Object.entries(PROPERTIES))
      root.style.setProperty(property, window.fffTiming[group][key] + "ms");
    return window.fffTiming;
  };
  window.applyTiming(null);
})();

// Page furniture for the monitor: side tabs, two-step confirmation, the on-air
// status and telling a revoked key apart from a dropped connection. Nothing
// here edits the board, so monitor.html stays the single owner of layout state.
(function () {
  const TAB_KEY = "fff.monitor.tab";

  function storedTab() {
    try { return localStorage.getItem(TAB_KEY); } catch (err) { return null; }
  }
  function rememberTab(name) {
    // A private window may refuse; remembering the tab is only a convenience.
    try { localStorage.setItem(TAB_KEY, name); } catch (err) { /* ignore */ }
  }

  window.createTabs = function (tablist, options) {
    const opts = options || {};
    const tabs = [...tablist.querySelectorAll('[role="tab"]')];
    const nameOf = (tab) => tab.id.replace(/^tab-/, "");
    const panelOf = (tab) => document.getElementById(tab.getAttribute("aria-controls"));
    const api = { current: null };

    api.show = function (name, remember) {
      const tab = tabs.find((item) => nameOf(item) === name);
      if (!tab || tab.disabled) return false;
      for (const item of tabs) {
        const active = item === tab;
        item.setAttribute("aria-selected", String(active));
        item.tabIndex = active ? 0 : -1;
        panelOf(item).hidden = !active;
      }
      const changed = api.current !== name;
      api.current = name;
      if (remember !== false) rememberTab(name);
      if (changed && opts.onShow) opts.onShow(name);
      return true;
    };

    api.setDisabled = function (name, disabled, reason) {
      const tab = tabs.find((item) => nameOf(item) === name);
      if (!tab) return;
      tab.disabled = disabled;
      tab.title = disabled ? (reason || "") : "";
      if (disabled && api.current === name) api.show(opts.fallback || nameOf(tabs[0]), false);
    };

    for (const tab of tabs) tab.addEventListener("click", () => api.show(nameOf(tab)));
    tablist.addEventListener("keydown", (event) => {
      if (event.key !== "ArrowRight" && event.key !== "ArrowLeft") return;
      const enabled = tabs.filter((tab) => !tab.disabled);
      const index = enabled.findIndex((tab) => nameOf(tab) === api.current);
      const step = event.key === "ArrowRight" ? 1 : enabled.length - 1;
      const next = enabled[(index + step) % enabled.length];
      if (!next) return;
      event.preventDefault();
      api.show(nameOf(next));
      next.focus();
    });

    const stored = storedTab();
    if (!(stored && api.show(stored, false))) api.show(opts.fallback || nameOf(tabs[0]), false);
    return api;
  };

  // A destructive button asks twice in the page itself: a browser confirm()
  // may never appear inside an OBS custom dock.
  window.armConfirm = function (button, onConfirm, options) {
    const opts = options || {};
    // Read at the press rather than at set-up, so the length the timing panel
    // publishes is the one the next press actually waits.
    const armedFor = () => opts.timeoutMs || window.fffTiming.monitor.confirm;
    const label = button.textContent;
    let timer = null;
    const control = {
      get armed() { return timer !== null; },
      disarm() {
        if (timer === null) return;
        clearTimeout(timer);
        timer = null;
        button.textContent = label;
        button.classList.remove("confirm-armed");
      }
    };
    button.addEventListener("click", () => {
      if (timer === null) {
        button.textContent = opts.armedLabel || "กดอีกครั้งเพื่อยืนยัน";
        button.classList.add("confirm-armed");
        timer = setTimeout(() => control.disarm(), armedFor());
        return;
      }
      control.disarm();
      onConfirm();
    });
    return control;
  };

  // What the stream shows, in words and colour, plus a warning when the mode
  // being edited is the one viewers are watching.
  window.updateAirStatus = function (elements, incoming, editMode) {
    const revealed = incoming.phase === "revealed";
    const aired = incoming.displayMode === "bottomBar" ? "BOTTOM BAR" : "Show Status";
    elements.status.textContent = revealed ? "● ออกอากาศ · " + aired : "○ จอว่าง";
    elements.status.dataset.air = revealed ? "on" : "blank";
    elements.round.textContent = `รอบ ${incoming.round ?? 1} · โหวตแล้ว ${incoming.voted ?? 0}/${incoming.total ?? 0}`;
    elements.warning.hidden = !(revealed && (incoming.displayMode || "scoreboard") === editMode);
  };

  // EventSource cannot tell a refused key from a dropped network, so ask.
  window.checkAccess = async function (elements) {
    try {
      const res = await fetch("/api/monitor/access", { cache: "no-store" });
      const revoked = res.status === 403;
      elements.denied.classList.toggle("hidden", !revoked);
      elements.warn.classList.toggle("hidden", revoked);
      return !revoked && res.ok;
    } catch (err) {
      elements.denied.classList.add("hidden");
      elements.warn.classList.remove("hidden");
      return false;
    }
  };
})();

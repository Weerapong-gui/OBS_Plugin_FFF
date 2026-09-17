// Page furniture for the monitor: side tabs now; the on-air warning, two-step
// confirmation and access check join in the next change. Nothing here edits
// the board, so monitor.html stays the single owner of layout state.
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
})();

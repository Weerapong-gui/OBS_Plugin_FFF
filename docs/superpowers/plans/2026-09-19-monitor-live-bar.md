# Monitor Live Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `/monitor` the four on-air buttons, the centre-logo controls and the who-voted list that only the OBS dock has today, without relaxing who is allowed to drive the stream.

**Architecture:** Two existing local-only routes grow one extra request shape each (`/api/display` learns `hide`, `/api/logo` learns `round`); one new local-only route clears the round; one new monitor-only route tells the page whether it is allowed to press anything. The page reads everything it displays from the SSE frame it already receives — no SSE field is added. On-air buttons live in the pinned top strip; the logo controls and vote list live in a new side tab.

**Tech Stack:** C++17 / Qt 6 (QTcpServer, QJsonDocument), vanilla HTML/CSS/JS (no framework, no build step for `data/web/`), ctest for native tests, puppeteer-core scripts for browser tests.

**Spec:** `docs/superpowers/specs/2026-09-19-monitor-live-bar-design.md`

## Global Constraints

- **No connection behaviour may change.** SSE, HTTP framing, `Connection: close`, heartbeat, retry, reconnect all stay exactly as they are. The deferred networking work lives in a separate discussion.
- **No field may be added to the SSE payload.** `overlayStateJson()` is built once and broadcast to every listener including LAN monitors.
- **`/api/display`, `/api/logo` and the new `/api/round` stay `Endpoint::LocalOnly`.** A LAN monitor holding a valid key must still be refused.
- **Existing request shapes must behave identically.** `{"mode":"bottomBar"}` and `{"presidentId":"…"}` keep their exact current behaviour and status codes.
- **`dock-tests` must pass without editing the test file.** The dock is not being changed.
- **`GET /api/monitor/access` must keep answering `204`** — `checkAccess()` in `monitor-ui.js` depends on it.
- **A 900px-wide window must have no horizontal scroll.** `tests/monitor-ui.cjs` already asserts this and it is the acceptance test for the new layout.
- C/C++ uses tabs, `clang-format` with `IndentWidth 8`. Format with `/opt/homebrew/bin/clang-format-19 -i <file>` before committing.
- Thai is the operator-facing language; all new user-visible strings are Thai.
- No unrelated refactoring.

**Build and test commands used throughout:**

```bash
# native
cmake --build /private/tmp/fff-layout-native
ctest --test-dir /private/tmp/fff-layout-native --output-on-failure

# browser (once per session)
FFF_TEST_DEPS=$(mktemp -d /private/tmp/fff-browser-tests.XXXXXX)
npm install --prefix "$FFF_TEST_DEPS" puppeteer-core
export NODE_PATH="$FFF_TEST_DEPS/node_modules"
node tests/monitor-ui.cjs
```

If `/private/tmp/fff-layout-native` does not exist:

```bash
cmake -S tests -B /private/tmp/fff-layout-native \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/obs-deps-qt6-2025-07-11-universal" \
  -DOBS_INCLUDE_DIR="$PWD/.deps/Frameworks/libobs.framework/Headers"
```

---

### Task 1: `/api/display` learns `hide`, `/api/logo` learns `round`

Both routes share one `if` block in `route()` and one test file, and both are the same
move: add a second request shape to a local-only route without touching the first.

**Files:**
- Modify: `src/fff-http-server.cpp` — the `if (path == "/api/display" || path == "/api/logo")` block in `route()`
- Test: `tests/layout-tests.cpp` — insert after the line containing `"score has no layer order"` (line ~243)

**Interfaces:**
- Consumes: `FffSession::hideDisplay()`, `FffSession::setLogoRound(int)`, `FffSession::validMode()` — all already exist
- Produces: `POST /api/display {"hide":true}` → 200/400/500; `POST /api/logo {"round":1|2}` → 200/400/500

- [ ] **Step 1: Write the failing tests**

Insert into `tests/layout-tests.cpp` immediately after the `"score has no layer order"` check:

```cpp
	// Taking the board down and choosing the logo's round are the two things
	// the dock could do that no request could express. Each travels as its own
	// field so the shapes that already work are untouched.
	check(post(R"({"mode":"bottomBar"})", QStringLiteral("display")) == 200 &&
		      session.phase() == FffPhase::Revealed &&
		      session.displayMode() == QLatin1String("bottomBar"),
	      "a mode request still puts a board on air");
	check(post(R"({"hide":true})", QStringLiteral("display")) == 200 &&
		      session.phase() == FffPhase::Collecting,
	      "hide takes the board down");
	check(session.displayMode() == QLatin1String("bottomBar"),
	      "hiding remembers the mode, the way the dock's button does");
	check(post(R"({"hide":true,"mode":"bottomBar"})", QStringLiteral("display")) == 400 &&
		      session.phase() == FffPhase::Collecting,
	      "one request cannot both show and hide");
	check(post(R"({"hide":false})", QStringLiteral("display")) == 400, "only true hides");
	check(post(R"({"mode":"nonsense"})", QStringLiteral("display")) == 400,
	      "an unknown mode is still refused");

	check(session.logoRound() == 1, "the logo starts on round 1");
	check(post(R"({"round":2})", QStringLiteral("logo")) == 200 && session.logoRound() == 2,
	      "the logo round can be chosen over HTTP");
	check(post(R"({"round":3})", QStringLiteral("logo")) == 400 && session.logoRound() == 2,
	      "a round that has no artwork set is refused");
	check(post(R"({"round":2,"presidentId":"one"})", QStringLiteral("logo")) == 400,
	      "one request cannot both pick a school and a round");
	check(post(R"({"presidentId":"one"})", QStringLiteral("logo")) == 200 &&
		      session.logoPresidentId() == QLatin1String("one"),
	      "choosing a school still works");
	check(post(R"({"round":1})", QStringLiteral("logo")) == 200 && session.logoRound() == 1,
	      "back to round 1");
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build /private/tmp/fff-layout-native && ctest --test-dir /private/tmp/fff-layout-native -R layout-tests --output-on-failure
```

Expected: FAIL at `"hide takes the board down"` — `{"hide":true}` has no `mode`, so `validMode("")` is false and the route answers 400.

- [ ] **Step 3: Implement the two extra shapes**

In `src/fff-http-server.cpp`, replace the opening of the display/logo block. The
current code is:

```cpp
		if (path == QLatin1String("/api/display") || path == QLatin1String("/api/logo")) {
			const auto request = QJsonDocument::fromJson(body).object();
			const QString value = request.value(path == QLatin1String("/api/display")
								    ? QStringLiteral("mode")
								    : QStringLiteral("presidentId"))
						      .toString();
```

Insert the two new branches between the `request` line and the `value` line:

```cpp
		if (path == QLatin1String("/api/display") || path == QLatin1String("/api/logo")) {
			const auto request = QJsonDocument::fromJson(body).object();
			const bool display = path == QLatin1String("/api/display");
			// Taking the board down is the one display change validMode()
			// cannot express, and the logo's round is the one logo change
			// presidentId cannot. Each rides its own field so a request that
			// worked before works exactly the same way now; carrying both
			// fields is a caller that has not decided what it wants.
			if (display && request.contains(QStringLiteral("hide"))) {
				if (request.contains(QStringLiteral("mode")) ||
				    !request.value(QStringLiteral("hide")).toBool()) {
					sendJson(socket, 400, "{\"error\":\"invalid selection\"}");
					return;
				}
				const bool saved = m_session->hideDisplay();
				sendJson(socket, saved ? 200 : 500,
					 saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
				return;
			}
			if (!display && request.contains(QStringLiteral("round"))) {
				const int round = request.value(QStringLiteral("round")).toInt();
				if (request.contains(QStringLiteral("presidentId")) || (round != 1 && round != 2)) {
					sendJson(socket, 400, "{\"error\":\"invalid selection\"}");
					return;
				}
				const bool saved = m_session->setLogoRound(round);
				sendJson(socket, saved ? 200 : 500,
					 saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
				return;
			}
			const QString value = request.value(display ? QStringLiteral("mode")
								    : QStringLiteral("presidentId"))
						      .toString();
```

Then in the same block replace the two remaining `path == QLatin1String("/api/display")`
comparisons with `display` so the local reads once:

```cpp
			if ((display && !FffSession::validMode(value)) ||
			    (!display && !value.isEmpty() && !m_session->presidentById(value))) {
				sendJson(socket, 400, "{\"error\":\"invalid selection\"}");
				return;
			}
			const bool saved = display ? m_session->showMode(value)
						   : m_session->setLogoPresident(value);
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
/opt/homebrew/bin/clang-format-19 -i src/fff-http-server.cpp tests/layout-tests.cpp
cmake --build /private/tmp/fff-layout-native && ctest --test-dir /private/tmp/fff-layout-native --output-on-failure
```

Expected: 7/7 pass. `monitor-lan-tests` must still pass unchanged — it asserts a LAN
cookie gets 403 at `/api/display` and `/api/logo`, which these branches do not reach.

- [ ] **Step 5: Commit**

```bash
git add src/fff-http-server.cpp tests/layout-tests.cpp
git commit -m "feat(api): let a request take the board down and pick the logo's round

Both were things only the dock could do: hideDisplay() and setLogoRound()
had no request that could reach them. Each arrives as its own field, so
every request shape that worked before behaves exactly as it did.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: `POST /api/round` clears the round

**Files:**
- Modify: `src/fff-http-server.cpp` — new route in the POST section of `route()`, plus the `sendDenied()` JSON branch
- Modify: `src/fff-monitor-access.cpp` — `classify()`
- Test: `tests/layout-tests.cpp`, `tests/monitor-access-tests.cpp`

**Interfaces:**
- Consumes: `FffSession::clearRound()` — already exists
- Produces: `POST /api/round {"action":"clear"}` → 200/400/500, `Endpoint::LocalOnly`

- [ ] **Step 1: Write the failing tests**

In `tests/monitor-access-tests.cpp`, add to the existing `for` loop over on-air controls:

```cpp
	for (const char *path : {"/api/display", "/api/logo", "/api/round"})
		check(classify("POST", QString::fromLatin1(path)) == Endpoint::LocalOnly, "on-air controls");
```

(replacing the current two-item loop, which reads `{"/api/display", "/api/logo"}`)

In `tests/layout-tests.cpp`, add immediately after the logo-round checks from Task 1:

```cpp
	// Ending a round is the dock's most destructive button. The confirmation
	// belongs to whichever UI asks; the route only refuses a request that did
	// not say plainly what it wanted.
	check(session.setVote(QStringLiteral("one"), FffVote::Green), "a vote to clear");
	const int roundBefore = session.round();
	check(post(R"({})", QStringLiteral("round")) == 400 && session.round() == roundBefore &&
		      session.votedCount() == 1,
	      "an empty request never ends a round");
	check(post(R"({"action":"reset"})", QStringLiteral("round")) == 400 && session.round() == roundBefore,
	      "only the named action clears");
	check(post(R"({"action":"clear"})", QStringLiteral("round")) == 200 &&
		      session.round() == roundBefore + 1 && session.votedCount() == 0,
	      "the round is cleared and the next one begins");
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build /private/tmp/fff-layout-native && ctest --test-dir /private/tmp/fff-layout-native --output-on-failure
```

Expected: `monitor-access-tests` FAILs at `"on-air controls"` (`/api/round` classifies
as `Public`), and `layout-tests` FAILs at `"an empty request never ends a round"`
(the route does not exist, so the server answers 404).

- [ ] **Step 3: Implement the route and its class**

In `src/fff-monitor-access.cpp`, in `classify()`, replace:

```cpp
		if (path == QLatin1String("/api/display") || path == QLatin1String("/api/logo"))
			return Endpoint::LocalOnly;
```

with:

```cpp
		if (path == QLatin1String("/api/display") || path == QLatin1String("/api/logo") ||
		    path == QLatin1String("/api/round"))
			return Endpoint::LocalOnly;
```

In `src/fff-http-server.cpp`, add the route immediately after the display/logo block
closes:

```cpp
		if (path == QLatin1String("/api/round")) {
			const auto request = QJsonDocument::fromJson(body).object();
			// Named rather than implied: a stray empty POST must never be
			// able to throw a round of votes away.
			if (request.value(QStringLiteral("action")).toString() != QLatin1String("clear")) {
				sendJson(socket, 400, "{\"error\":\"invalid action\"}");
				return;
			}
			const bool saved = m_session->clearRound();
			sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
			return;
		}
```

In the same file, in `sendDenied()`, extend the JSON branch so a refused round answers
JSON like the other on-air controls rather than plain text:

```cpp
		if (path == QLatin1String("/monitor") || path == QLatin1String("/score"))
			send(target, 403, "text/html; charset=utf-8", monitorDeniedPage());
		else if (path == QLatin1String("/api/display") || path == QLatin1String("/api/logo") ||
			 path == QLatin1String("/api/round"))
			sendJson(target, 403, "{\"error\":\"local only\"}");
```

Do **not** add `/api/round` to `deniedText()` — that map is only read by the plain-text
branch this route no longer takes.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
/opt/homebrew/bin/clang-format-19 -i src/fff-http-server.cpp src/fff-monitor-access.cpp tests/layout-tests.cpp tests/monitor-access-tests.cpp
cmake --build /private/tmp/fff-layout-native && ctest --test-dir /private/tmp/fff-layout-native --output-on-failure
```

Expected: 7/7 pass.

- [ ] **Step 5: Commit**

```bash
git add src/fff-http-server.cpp src/fff-monitor-access.cpp tests/layout-tests.cpp tests/monitor-access-tests.cpp
git commit -m "feat(api): a local-only route that starts the next round

clearRound() had no way in from outside the dock. Local-only like every
other control that changes what the stream shows, and it refuses a
request that did not name the action, so no stray POST can end a round.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: `GET /api/monitor/capabilities`

**Files:**
- Modify: `src/fff-http-server.cpp` — new route in the GET section of `route()`
- Modify: `src/fff-monitor-access.cpp` — `classify()`
- Test: `tests/monitor-access-tests.cpp`, `tests/monitor-lan-tests.cpp`

**Interfaces:**
- Consumes: `QTcpSocket::peerAddress()` — already used in `readFrom()`
- Produces: `GET /api/monitor/capabilities` → `{"onAir":true}` from loopback, `{"onAir":false}` otherwise, `Endpoint::MonitorApi`

- [ ] **Step 1: Write the failing tests**

In `tests/monitor-access-tests.cpp`, extend the existing monitor-reads loop:

```cpp
	for (const char *path : {"/api/events/overlay", "/api/monitor/access", "/api/monitor/capabilities"})
		check(classify("GET", QString::fromLatin1(path)) == Endpoint::MonitorApi, "monitor reads");
```

In `tests/monitor-lan-tests.cpp`, add after the line checking
`"the cookie passes the access probe"`:

```cpp
	check(bodyOf(call("GET", host, "/api/monitor/capabilities", cookie)) == "{\"onAir\":false}",
	      "a LAN monitor is told it may not drive the stream");
	check(bodyOf(call("GET", local, "/api/monitor/capabilities")) == "{\"onAir\":true}",
	      "this machine is told it may");
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build /private/tmp/fff-layout-native && ctest --test-dir /private/tmp/fff-layout-native --output-on-failure
```

Expected: `monitor-access-tests` FAILs at `"monitor reads"`; `monitor-lan-tests` FAILs
on the body comparison (the server answers 404 `not found`). If the machine has no LAN
address `monitor-lan-tests` prints `SKIP` and passes — in that case rely on
`monitor-access-tests` plus the manual check in Task 5.

- [ ] **Step 3: Implement the route and its class**

In `src/fff-monitor-access.cpp`, in `classify()`, replace:

```cpp
		if (path == QLatin1String("/api/events/overlay") || path == QLatin1String("/api/monitor/access"))
			return Endpoint::MonitorApi;
```

with:

```cpp
		if (path == QLatin1String("/api/events/overlay") || path == QLatin1String("/api/monitor/access") ||
		    path == QLatin1String("/api/monitor/capabilities"))
			return Endpoint::MonitorApi;
```

In `src/fff-http-server.cpp`, add the route immediately after the
`/api/monitor/access` route in the GET section:

```cpp
		if (path == QLatin1String("/api/monitor/capabilities")) {
			// The page asks once, at load, whether the buttons that change
			// what the stream shows are for it. Nothing secret rides here:
			// it is the same loopback test readFrom() already made, said out
			// loud so the page can grey a button rather than let an operator
			// press it and watch nothing happen.
			sendJson(socket, 200,
				 socket->peerAddress().isLoopback() ? "{\"onAir\":true}" : "{\"onAir\":false}");
			return;
		}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
/opt/homebrew/bin/clang-format-19 -i src/fff-http-server.cpp src/fff-monitor-access.cpp tests/monitor-access-tests.cpp tests/monitor-lan-tests.cpp
cmake --build /private/tmp/fff-layout-native && ctest --test-dir /private/tmp/fff-layout-native --output-on-failure
```

Expected: 7/7 pass.

- [ ] **Step 5: Commit**

```bash
git add src/fff-http-server.cpp src/fff-monitor-access.cpp tests/monitor-access-tests.cpp tests/monitor-lan-tests.cpp
git commit -m "feat(api): tell the monitor page whether it may drive the stream

The page can now grey the on-air buttons for a LAN operator instead of
offering a press that the server will refuse. The answer is the loopback
test the server already performs, said out loud.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: the four on-air buttons in the pinned top strip

**Files:**
- Modify: `data/web/monitor.html` — markup in `.top-tools`, and the script block
- Modify: `data/web/app.css` — one rule for the new group
- Test: `tests/monitor-ui.cjs`

**Interfaces:**
- Consumes: `POST /api/display` (Task 1), `POST /api/round` (Task 2), `GET /api/monitor/capabilities` (Task 3), `window.armConfirm(button, onConfirm, options)` from `monitor-ui.js`, the existing `sourceState`, `saveStatus` and `SAVE_TIMEOUT_MS` in `monitor.html`
- Produces: `paintAir()` — repaints the four buttons from `sourceState` and `onAir`; called from `render()`

- [ ] **Step 1: Write the failing test**

Add to `tests/monitor-ui.cjs`. First extend the fixture server, immediately before the
static-file lookup:

```js
  if (req.url === "/api/monitor/capabilities") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ onAir: capabilityOnAir }));
    return;
  }
  if (req.method === "POST" && (req.url === "/api/display" || req.url === "/api/round")) {
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      airRequests.push({ url: req.url, body: JSON.parse(body) });
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end('{"ok":true}');
    });
    return;
  }
```

and add the two module-level variables beside `let accessStatus = 204;`:

```js
let capabilityOnAir = true;
let airRequests = [];
```

Then add this case inside `main()`, after the existing timing-panel case:

```js
    // The on-air buttons: the dock's toggles, in the strip that never scrolls.
    airRequests = [];
    await page.reload({ waitUntil: "load" });
    await page.waitForFunction(() => !document.getElementById("airScoreboard").disabled);
    assert.equal(await page.$eval("#airBottomBar", (el) => el.getAttribute("aria-pressed")), "false");

    await page.click("#airBottomBar");
    await page.waitForFunction(() => window.__airSettled === true);
    assert.deepEqual(airRequests.at(-1), { url: "/api/display", body: { mode: "bottomBar" } });

    // With that mode on air, the same button asks for the board to come down.
    state.phase = "revealed"; state.displayMode = "bottomBar"; push();
    await page.waitForFunction(() =>
      document.getElementById("airBottomBar").getAttribute("aria-pressed") === "true");
    await page.click("#airBottomBar");
    await page.waitForFunction(() => window.__airSettled === true);
    assert.deepEqual(airRequests.at(-1), { url: "/api/display", body: { hide: true } });

    await page.click("#airHide");
    await page.waitForFunction(() => window.__airSettled === true);
    assert.deepEqual(airRequests.at(-1), { url: "/api/display", body: { hide: true } });

    // Ending a round asks twice, the way resetting every placement does.
    const before = airRequests.length;
    await page.click("#airNewRound");
    assert.equal(airRequests.length, before, "one press only arms the button");
    await page.click("#airNewRound");
    await page.waitForFunction(() => window.__airSettled === true);
    assert.deepEqual(airRequests.at(-1), { url: "/api/round", body: { action: "clear" } });

    // A LAN operator sees the buttons and is told why they do nothing.
    capabilityOnAir = false;
    await page.reload({ waitUntil: "load" });
    await page.waitForFunction(() => document.getElementById("airScoreboard").disabled);
    for (const id of ["airScoreboard", "airBottomBar", "airHide", "airNewRound"]) {
      assert.equal(await page.$eval("#" + id, (el) => el.disabled), true, id + " is disabled");
      assert.match(await page.$eval("#" + id, (el) => el.title), /เครื่องที่รัน OBS/);
    }
    capabilityOnAir = true;
    state.phase = "collecting"; push();
    await page.reload({ waitUntil: "load" });
    console.log("PASS: the on-air buttons toggle, confirm twice and grey for a LAN operator");
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
node tests/monitor-ui.cjs
```

Expected: FAIL — `waitForFunction` times out because `#airScoreboard` does not exist.

- [ ] **Step 3: Add the markup**

In `data/web/monitor.html`, insert this block immediately after the `</div>` that closes
`<div id="cardControls" class="piece-options">` and before the `</div>` that closes
`.top-tools`:

```html
    <div id="airControls" class="piece-options" role="group" aria-label="ออกอากาศ">
      <span class="group-label">ออกอากาศ</span>
      <button id="airScoreboard" aria-pressed="false">Show Status</button>
      <button id="airBottomBar" aria-pressed="false">BOTTOM BAR</button>
      <button id="airHide">■ ซ่อนจอ</button>
      <button id="airNewRound">↻ เริ่มรอบใหม่</button>
    </div>
```

- [ ] **Step 4: Add the stylesheet rule**

Append to `data/web/app.css`, after the existing `.monitor-top .piece-options button`
rule:

```css
/* The on-air buttons sit in the strip that never scrolls away, for the same
   reason the dock keeps its live bar above the tabs. They wrap rather than
   widen: a 900px window must still have no horizontal scroll. */
#airControls { flex-wrap: wrap; }
#airControls button[aria-pressed="true"] { background: var(--accent); border-color: var(--accent);
  color: #0e1116; }
```

- [ ] **Step 5: Add the behaviour**

In `data/web/monitor.html`, add to the script block immediately before the
`fitCanvas();` line:

```js
// ---------- the on-air buttons: the dock's live bar, in the pinned strip ----
// The page never paints an outcome it only asked for. A press sends the
// request and the next SSE frame is what moves the buttons, so what is on
// screen here and what is on the stream cannot drift apart.
let onAir = false;
let airBusy = false;
const AIR_BUTTONS = ["airScoreboard", "airBottomBar", "airHide", "airNewRound"];
const airEl = (id) => document.getElementById(id);

function paintAir() {
  const revealed = sourceState && sourceState.phase === "revealed";
  const aired = revealed ? sourceState.displayMode : null;
  airEl("airScoreboard").setAttribute("aria-pressed", String(aired === "scoreboard"));
  airEl("airBottomBar").setAttribute("aria-pressed", String(aired === "bottomBar"));
  for (const id of AIR_BUTTONS) {
    const button = airEl(id);
    button.disabled = !onAir || airBusy;
    button.title = onAir ? "" : "ใช้ได้เฉพาะเครื่องที่รัน OBS";
  }
}

async function sendAir(url, body) {
  if (!onAir || airBusy) return;
  airBusy = true; paintAir();
  window.__airSettled = false;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), SAVE_TIMEOUT_MS);
  try {
    const response = await fetch(url, { method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body), signal: controller.signal });
    if (!response.ok) throw new Error("refused");
    saveStatus.textContent = "";
  } catch (error) {
    saveStatus.textContent = "สั่งไม่สำเร็จ";
  } finally {
    clearTimeout(timer);
    airBusy = false; paintAir();
    window.__airSettled = true;
  }
}

// A mode already on air comes down; any other press puts one up. Same rule as
// FffLivePanel::toggleMode().
function pressAirMode(mode) {
  const live = sourceState && sourceState.phase === "revealed" && sourceState.displayMode === mode;
  sendAir("/api/display", live ? { hide: true } : { mode: mode });
}
airEl("airScoreboard").onclick = () => pressAirMode("scoreboard");
airEl("airBottomBar").onclick = () => pressAirMode("bottomBar");
airEl("airHide").onclick = () => sendAir("/api/display", { hide: true });
armConfirm(airEl("airNewRound"), () => sendAir("/api/round", { action: "clear" }),
  { armedLabel: "กดอีกครั้งเพื่อล้างรอบ" });

fetch("/api/monitor/capabilities")
  .then((response) => response.json())
  .then((data) => { onAir = data.onAir === true; paintAir(); })
  .catch(() => { onAir = false; paintAir(); });
paintAir();
```

Then add one line inside `render(incoming)`, immediately after the existing
`updateAirStatus(...)` call:

```js
  paintAir();
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
node tests/monitor-ui.cjs
```

Expected: PASS, including the pre-existing `"a 900px window has no horizontal scroll"`
assertion. If that one fails, the button labels are too wide — shorten `↻ เริ่มรอบใหม่`
to `↻ รอบใหม่` in both the markup and the test rather than removing `flex-wrap`.

- [ ] **Step 7: Check nothing else moved**

```bash
node tests/overlay-layout.cjs && node tests/status-motion.cjs && node tests/bottom-bar-cover.cjs && node tests/score-page.cjs && node tests/phone-session.cjs
```

Expected: all pass without any test file being edited.

- [ ] **Step 8: Commit**

```bash
git add data/web/monitor.html data/web/app.css tests/monitor-ui.cjs
git commit -m "feat(monitor): the on-air buttons, in the strip that never scrolls

Show Status, BOTTOM BAR, hide and start-a-round are the presses an
operator makes while the show is running, so they go where the dock puts
them rather than behind a tab. A LAN operator sees them greyed with the
reason, because the server would refuse the press anyway.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: the ไลฟ์ tab — centre logo and who has voted

**Files:**
- Modify: `data/web/monitor.html` — tab button, panel markup, script
- Modify: `data/web/app.css` — the vote list
- Modify: `README.md`, `tests/README.md`
- Test: `tests/monitor-ui.cjs`

**Interfaces:**
- Consumes: `POST /api/logo` with `{"round":1|2}` and `{"presidentId":"…"}` (Task 1), `sendAir(url, body)` and `onAir` (Task 4), `sourceState`
- Produces: `paintLive()` — repaints the logo controls, the warning and the vote list; called from `render()`

- [ ] **Step 1: Write the failing test**

Add to `tests/monitor-ui.cjs`, inside `main()` after the Task 4 case. First give the
fixture a president with no round-2 artwork by extending the state at the top of the
file — change the `presidents` mapper to:

```js
  presidents: [0, 1, 2].map((i) => ({
    id: String(i), name: "นายก " + i, school: "สำนัก " + i, cardUrl: "",
    vote: i === 0 ? "green" : "none", status: "waiting", statusUrl: "",
    logoRound2Url: i === 0 ? "data:image/png;base64,x" : ""
  }))
```

and extend the fixture's `bottomBar` to `{ layout: { x: 0.5, y: 0.5, scale: 1 }, pieces: {}, layers: {}, logoPresidentId: "", logoRound: 1 }`.

Then the case:

```js
    // The centre logo and the vote list: the dock's live tab.
    airRequests = [];
    await page.evaluate(() => document.getElementById("tab-live").click());
    await page.waitForFunction(() => !document.getElementById("panel-live").hidden);

    assert.deepEqual(
      await page.$$eval("#voteList li", (items) => items.map((li) => li.textContent)),
      ["● เขียว · นายก 0", "○ ยังไม่กด · นายก 1", "○ ยังไม่กด · นายก 2"]);

    assert.equal(await page.$eval("#logoRound1", (el) => el.getAttribute("aria-pressed")), "true");
    await page.click("#logoRound2");
    await page.waitForFunction(() => window.__airSettled === true);
    assert.deepEqual(airRequests.at(-1), { url: "/api/logo", body: { round: 2 } });

    // Round 2 with no artwork behind it leaves the centre empty on air, so say so.
    state.bottomBar.logoRound = 2;
    state.bottomBar.logoPresidentId = "1";
    push();
    await page.waitForFunction(() => !document.getElementById("logoWarning").hidden);
    assert.match(await page.$eval("#logoWarning", (el) => el.textContent), /สำนัก 1/);
    state.bottomBar.logoPresidentId = "0";
    push();
    await page.waitForFunction(() => document.getElementById("logoWarning").hidden);

    capabilityOnAir = false;
    await page.reload({ waitUntil: "load" });
    await page.evaluate(() => document.getElementById("tab-live").click());
    await page.waitForFunction(() => document.getElementById("logoRound1").disabled);
    assert.equal(await page.$eval("#logoSchool", (el) => el.disabled), true);
    assert.equal(await page.$$eval("#voteList li", (items) => items.length), 3,
      "a LAN operator still reads the votes");
    capabilityOnAir = true;
    console.log("PASS: the live tab picks the centre logo and lists who has voted");
```

`airRequests` must also accept `/api/logo`, so widen the fixture route added in Task 4:

```js
  if (req.method === "POST" && ["/api/display", "/api/round", "/api/logo"].includes(req.url)) {
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
node tests/monitor-ui.cjs
```

Expected: FAIL — `#tab-live` does not exist.

- [ ] **Step 3: Add the tab and panel**

In `data/web/monitor.html`, add the tab button after `tab-score`:

```html
    <button type="button" role="tab" id="tab-live" aria-controls="panel-live" aria-selected="false" tabindex="-1">ไลฟ์</button>
```

and the panel after `</section>` of `panel-score`:

```html
  <section id="panel-live" class="controls" role="tabpanel" aria-labelledby="tab-live" hidden>
    <div class="control-group" role="group" aria-label="โลโก้กลาง">
      <span class="group-label">โลโก้กลาง</span>
      <button id="logoRound1" aria-pressed="false">Round 1</button>
      <button id="logoRound2" aria-pressed="false">Round 2</button>
    </div>
    <div class="control-group">
      <label class="top-select">สำนัก <select id="logoSchool"></select></label>
    </div>
    <p id="logoWarning" class="hint" hidden></p>
    <h3>ใครโหวตแล้ว</h3>
    <ul id="voteList" class="vote-list"></ul>
  </section>
```

- [ ] **Step 4: Add the stylesheet rule**

Append to `data/web/app.css`:

```css
/* One row per president, the same words the dock's live tab uses. */
.vote-list { margin: 8px 0 0; padding: 0; list-style: none; }
.vote-list li { padding: 6px 0; border-bottom: 1px solid var(--edge); }
.vote-list li:last-child { border-bottom: 0; }
#logoWarning { color: var(--amber); }
#logoWarning[hidden] { display: none; }
```

- [ ] **Step 5: Add the behaviour**

In `data/web/monitor.html`, add to the script block immediately after the Task 4 block:

```js
// ---------- the live tab: the centre logo and who has voted ----------------
const VOTE_WORDS = { red: "แดง", green: "เขียว" };

function paintLive() {
  const state = sourceState;
  if (!state) return;
  const bottom = state.bottomBar || {};
  const round = bottom.logoRound === 2 ? 2 : 1;
  airEl("logoRound1").setAttribute("aria-pressed", String(round === 1));
  airEl("logoRound2").setAttribute("aria-pressed", String(round === 2));
  for (const id of ["logoRound1", "logoRound2", "logoSchool"]) {
    airEl(id).disabled = !onAir || airBusy;
    airEl(id).title = onAir ? "" : "ใช้ได้เฉพาะเครื่องที่รัน OBS";
  }

  // Rebuilding the list would close an open popup, so only a roster change does.
  const presidents = state.presidents || [];
  const select = airEl("logoSchool");
  const signature = JSON.stringify(presidents.map((p) => [p.id, p.name, p.school]));
  if (select.dataset.schools !== signature) {
    select.dataset.schools = signature;
    select.replaceChildren(new Option("ไม่เลือกโลโก้", ""),
      ...presidents.map((p) => new Option(p.school || p.name, p.id)));
  }
  if (document.activeElement !== select) select.value = bottom.logoPresidentId || "";

  // Round 2 with no artwork behind it leaves the centre of the bar empty on
  // air, which is only visible once it is too late. Say it here instead.
  const chosen = presidents.find((p) => p.id === bottom.logoPresidentId);
  const missing = round === 2 && chosen && !chosen.logoRound2Url;
  const warning = airEl("logoWarning");
  warning.hidden = !missing;
  warning.textContent = missing
    ? `⚠ ${chosen.school || chosen.name} ยังไม่มี PNG ของ Round 2` : "";

  airEl("voteList").replaceChildren(...presidents.map((president) => {
    const item = document.createElement("li");
    const word = VOTE_WORDS[president.vote] || "ยังไม่กด";
    item.textContent = `${president.vote === "red" || president.vote === "green" ? "●" : "○"} ${word} · ${president.name || president.school}`;
    return item;
  }));
}

airEl("logoRound1").onclick = () => sendAir("/api/logo", { round: 1 });
airEl("logoRound2").onclick = () => sendAir("/api/logo", { round: 2 });
airEl("logoSchool").onchange = (event) => sendAir("/api/logo", { presidentId: event.target.value });
```

Then extend the `paintAir()` call added to `render()` in Task 4 so it reads:

```js
  paintAir();
  paintLive();
```

and add `paintLive();` beside the `paintAir();` call inside `sendAir()`'s `finally` and
inside the capabilities `.then`/`.catch`, so the tab greys with the top strip.

- [ ] **Step 6: Run the test to verify it passes**

```bash
node tests/monitor-ui.cjs
```

Expected: PASS, including the 900px assertion and the tab-memory assertion.

- [ ] **Step 7: Run every suite**

```bash
cmake --build /private/tmp/fff-layout-native && ctest --test-dir /private/tmp/fff-layout-native --output-on-failure
node tests/monitor-ui.cjs && node tests/overlay-layout.cjs && node tests/status-motion.cjs && node tests/bottom-bar-cover.cjs && node tests/score-page.cjs && node tests/phone-session.cjs
cmake --build --preset macos
```

Expected: ctest 7/7, all six browser suites pass, `** BUILD SUCCEEDED **`.

- [ ] **Step 8: Document it**

In `README.md`, in the **จอมอนิเตอร์ (`/monitor`)** section, add after the paragraph
listing the tabs:

```markdown
แท็บ **ไลฟ์** กับแถวปุ่ม **ออกอากาศ** ในแถบบนทำสิ่งเดียวกับแถบไลฟ์ใน dock: ขึ้น/ลงจอทั้งสองโหมด ซ่อนจอ เริ่มรอบใหม่ เลือก Round และสำนักของโลโก้กลาง และดูว่าใครกดสีอะไรแล้ว — **ปุ่มเหล่านี้กดได้เฉพาะเครื่องที่รัน OBS** เครื่องใน LAN เห็นปุ่มเป็นสีเทาพร้อมเหตุผล เพราะ `/api/display`, `/api/logo` และ `/api/round` เป็น local-only เหมือนเดิม ปุ่ม **↻ เริ่มรอบใหม่** ต้องกดสองครั้งภายใน 3 วินาที แบบเดียวกับ **รีเซ็ตทั้งหมด**
```

In `tests/README.md`, add a section before `## Dock panels`:

```markdown
## แถบไลฟ์บนหน้า Monitor

`POST /api/display` รับ `{"hide":true}` เพิ่มจาก `{"mode":…}` และ `POST /api/logo` รับ
`{"round":1|2}` เพิ่มจาก `{"presidentId":…}` — หนึ่งคำขอมีได้เจตนาเดียว ส่งสองฟิลด์พร้อมกัน
ตอบ 400 `POST /api/round {"action":"clear"}` เป็น local-only เหมือนสองเส้นนั้น และปฏิเสธ
คำขอที่ไม่ได้ระบุ action เพื่อไม่ให้ POST ว่าง ๆ ล้างรอบโดยบังเอิญ

`GET /api/monitor/capabilities` ตอบ `{"onAir":true|false}` ตามว่าผู้เรียกเป็น loopback
หรือไม่ หน้าเพจเรียกครั้งเดียวตอนโหลดเพื่อตัดสินว่าจะเปิดหรือปิดปุ่ม ไม่ได้ poll

`monitor-ui.cjs` ครอบคลุม: ปุ่มโหมดเป็น toggle (โหมดที่ออกอากาศอยู่ส่ง `{"hide":true}`),
`↻ เริ่มรอบใหม่` กดครั้งเดียวไม่ส่งอะไร, `onAir:false` ทำให้ปุ่มทั้งสี่กับตัวเลือกโลโก้
เป็นสีเทาพร้อม title, รายการคนโหวตยังอ่านได้จาก LAN, คำเตือน Round 2 โผล่เฉพาะเมื่อสำนัก
ที่เลือกไม่มี `logoRound2Url` และหน้าต่าง 900px ยังไม่มี horizontal scroll
```

- [ ] **Step 9: Commit**

```bash
git add data/web/monitor.html data/web/app.css tests/monitor-ui.cjs README.md tests/README.md
git commit -m "feat(monitor): a live tab for the centre logo and the votes

The centre logo's round and school and the list of who has pressed what
are used between rounds rather than during them, so they get a tab with
room instead of a place in the pinned strip. The warning that round 2
has no artwork behind it moves across with them.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Manual verification (after Task 5, in real OBS)

```bash
./dev-install.sh
```

1. Open `/monitor` on the OBS machine. Press each of the four buttons — the Browser
   Source changes exactly as it does when the same button is pressed in the dock.
2. Open the dock and `/monitor` side by side. Press in one, and the other's buttons
   follow within a frame or two.
3. Press a mode that is already on air — the board comes down, and the dock's button
   unchecks too.
4. Press `↻ เริ่มรอบใหม่` twice — votes clear and every phone returns to no colour.
5. Switch to the **ไลฟ์** tab, pick Round 2 for a school with no round-2 PNG — the
   warning appears and the centre of the bar is empty on air.
6. Open `/monitor?key=…` from another machine on the LAN — the four buttons and the
   logo controls are grey with a tooltip, while dragging, templates and the vote list
   all still work.
7. Reload — the last tab is still remembered.

## Out of scope (phase 2, needs its own spec)

The รายชื่อ tab (add, remove, rename, PIN, regenerate PIN), artwork kinds
`card` / `bottomBar` / `logo` / `logo2`, and the Cover PNG. Phase 2 takes the PIN out
of the process for the first time and must decide on a local-only `GET /api/roster`.

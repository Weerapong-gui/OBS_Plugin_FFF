# Layout regression checks

Native checks compile the real session and HTTP server, replacing only the OBS
host functions with a temporary config directory. They cover layout API writes,
validation, persistence, reveal/clear, deletion, resets and failed disk writes.
The main plugin build does not depend on these test tools.

From the repository root on a configured macOS development machine:

```sh
cmake -S tests -B /private/tmp/fff-layout-native \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/obs-deps-qt6-2025-07-11-universal" \
  -DOBS_INCLUDE_DIR="$PWD/.deps/Frameworks/libobs.framework/Headers"
cmake --build /private/tmp/fff-layout-native
ctest --test-dir /private/tmp/fff-layout-native --output-on-failure
```

Browser checks serve the actual web files with a fixture HTTP/SSE server. They
exercise real pointer events, monitor/overlay geometry, piece width/height,
votes during editing, failed saves/retries, keyboard controls and roster changes.
Use an installed Chromium browser; the default executable is Brave on macOS.
Dependencies go into a temporary directory, not the plugin project:

```sh
FFF_TEST_DEPS=$(mktemp -d /private/tmp/fff-browser-tests.XXXXXX)
npm install --prefix "$FFF_TEST_DEPS" puppeteer-core
NODE_PATH="$FFF_TEST_DEPS/node_modules" node tests/overlay-layout.cjs
```

Set `FFF_BROWSER` to another Chromium executable if needed. These checks do not
install the plugin or alter the operator's OBS session; loading it inside OBS
and checking the Browser Source remains an integration smoke test.

## Layout interface

`POST /api/layout` answers this machine, or another machine holding the current
LAN monitor key (see Monitor LAN access). A piece update is
`{"target":"card:<president-id>","layout":{"x":0.5,"y":0.5,"scale":1}}`;
use `"heading"` for the title and round label. Centres are canvas fractions
clamped to 0–1, with scale clamped to 0.5–2. Missing or nonnumeric layout values
return 400, unknown targets return 404, and failed saves return 500.

`{"target":"card:<president-id>","reset":true}` removes that piece's override.
`{"target":"all","reset":true}` removes every override and resets the legacy
grid to centre/100%. Requests without a target retain the original whole-board
layout API. Session JSON and overlay SSE add a `pieces` object keyed by these
targets; absent entries use the existing grid and legacy `layout` values.

`POST /api/layer` ใช้สิทธิ์แบบเดียวกับ layout และรับ
`{"target":"card:<president-id>","action":"front|forward|backward|back"}`
เพื่อจัดลำดับซ้อนของการ์ดหรือ `heading` แบบบันทึกถาวร. SSE state ส่ง `layers`
เป็น map ของ target ไปยัง z-index; reset all ล้าง map นี้.

## BOTTOM BAR

State adds `displayMode: "scoreboard" | "bottomBar"` and a `bottomBar` object
containing `layout`, `pieces`, `layers`, `cardTemplate`, `logoTemplate`,
`countTemplate`, and `logoPresidentId`. Roster entries add `bottomBarUrl` and
`logoUrl`. Existing scoreboard fields and requests remain compatible. Sessions
without the new fields start in scoreboard.

Layout/layer/template POST bodies accept `mode` (omitted means `scoreboard`).
Bottom Bar uses `card:<id>`, `logo`, `cover`, `count:red` and `count:green`
targets. Optional layout `imageOpacity` and `resultOpacity` range from 0 to 1;
omitted values inherit template opacity, then 1. Template `image.opacity` and
`result.opacity` apply to all inheriting slots.

### Logo, counters and their templates

The centre logo and the two flag counters are Bottom Bar furniture with no
counterpart on the scoreboard, so each carries its own template instead of
borrowing the card one. `POST /api/template` takes an optional `piece`
(omitted means `card`, which keeps the original body shape). `piece` of `logo`
or `count` requires `"mode":"bottomBar"` and is rejected with 400 otherwise.

```jsonc
{"mode":"bottomBar","piece":"logo",
 "image":{"x":0,"y":0,"width":220,"height":250,"opacity":1}}
{"mode":"bottomBar","piece":"count",
 "value":{"x":0,"y":0,"width":110,"height":130,"opacity":1},
 "fontFamily":"Bai Jamjuree","fontSize":96,
 "colors":{"red":"#e23c3c","green":"#21b04a"}}
```

Boxes use the same limits as the card template: x/y within ±2000, width/height
1–2000, opacity 0–1. `fontSize` is 24–400 px. Anything else returns 400, and the
stored template never keeps the `mode` or `piece` routing fields.

`fontWeight` is 100–900. The stylesheet used to pin it at 700 while the panel's
sample carried no weight at all, so the panel and the stream disagreed no matter
what font was chosen — that was the whole of the reported mismatch. Weight now
belongs to the template, `applyCountTemplate()` writes it for every view, and
both the sample and the counter set `font-synthesis: none` so a weight the face
does not really have falls back the same way in both instead of being faked
differently in each. A template saved before the field existed is migrated to
700, the weight it was always drawn at.

The monitor builds the weight list from the **faces** `queryLocalFonts()`
reports, not just the families: `FontData` carries a style name and never a
number, so the name is mapped (Thin 100 … Black 900). Choosing a family keeps
the current weight when that family really has the face and otherwise moves to
the nearest one it does.

`fontFamily` names a font installed on the operator's machine, so it travels as
text; empty means the page's own stack. `FffSession::validFontFamily()` caps it
at 120 characters and refuses control characters and `" ' ; { } < > \ / ( )`,
which is everything that could break out of a CSS `font-family` value. Thai and
other non-ASCII family names are fine. The monitor offers the installed families
through `queryLocalFonts()` behind a button — the API needs both a user gesture
and a permission, and is missing in some browsers, so a typed field is always
available and a sample renders the chosen family before it goes on air. OBS's
browser source resolves the name against the same installed fonts, so nothing
has to be embedded.

`colors.red` and `colors.green` are `#rrggbb` and are both required. One
template carries the box, family and size for both counters, but a colour each.
Counters that predate these fields (`"font"` naming one of five built-in keys)
are migrated on load to `fontFamily: ""` plus the stylesheet's colours rather
than being dropped.

`count:red` and `count:green` are ordinary pieces: they take `pieces` and
`layers` entries, drag, resize and fade independently, and share one
`countTemplate` for their box, family and size. The tally itself is not part of the
state — both views derive it from `presidents[].vote`. Natural stacking is
logo, cards, `count:red`, `count:green`, then `cover` on top.

When a tally changes, the number rolls through random digits for 520 ms in
50 ms steps (`COUNT_ROLL` in `board.js`) and lands on the newest count. A flag
arriving mid-roll updates the landing value without restarting the roll, so the
board can never settle back on a stale number. Digits come from a seeded
sequence read off the roll's own animation clock, so a paused frame is
reproducible; a wall-clock guard settles the roll even where the browser paints
no frames. Leaving the air clears the settled tally so the next entrance rolls
again.

`POST /api/display` with `{"mode":"bottomBar"}` reveals that mode without clearing
votes; `POST /api/logo` with `{"presidentId":"id"}` selects only the central logo
(use an empty ID to clear). Both are localhost-only. The dock's mode buttons are
toggles: pressing the mode that is on air calls `FffSession::hideDisplay()`, which
returns the phase to collecting while leaving votes, round and the remembered
display mode untouched, so the same round can go straight back up.

Extended native/browser checks cover mode isolation, old session defaults,
0/1/14/odd rosters, logo deletion, live votes, Clear, hide and restore, opacity,
image replacement, geometry parity, and persistence rollback. They also cover
counter placement and per-piece opacity, the logo and counter templates, font
family and colour validation, and a tally that changes mid-roll. Browser screenshots
use fixture PNGs; actual school artwork and loading the built plugin in OBS remain manual checks.

## Show Status (the scoreboard)

The scoreboard is a status board. Each president carries one finished PNG per
status and the status they are currently on, all in `session.json`, so both
survive a restart:

```jsonc
{ "id": "…", "card": "…",
  "qualified": "qualified-<uuid>.png", "unqualified": "…", "waiting": "…",
  "status": "waiting" }
```

SSE adds `status` and `statusUrl` per president. `statusUrl` is the artwork for
the current status and **falls back to `cardUrl`** when that status has none, so
a roster built before these fields still goes on air unchanged. `board.js` draws
`statusUrl` and sets no vote class in this mode, which leaves `.slot .result` at
its default `display: none` — the flag colour belongs to the bottom bar. The
`waiting` class stays what it always was, the monitor's marker for a slot with
nothing to show, so it never sits over finished artwork.

`POST /api/status` `{"presidentId", "status"}` is localhost-only and rejects a
status that is not one of the three rather than silently falling back to
waiting (`FffSession::validStatusName()`).

`POST /api/asset?presidentId=<id>&kind=<status>` takes **raw PNG bytes** as the
body — one artwork per request, so a form encoding would add nothing but a
parser to get wrong. An empty body clears that status's artwork.
`FffSession::storeAsset()` holds the PNG magic-byte check and the UUID naming
that `importAsset()` used to own, so the dock's file picker and the monitor's
upload validate and name identically.

Uploads are the only thing that outgrows the 64 KB request cap, so the cap is
two-tier: 64 KB until the headers are complete and for every non-loopback peer,
8 MB for a loopback request that has declared its `Content-Length`. A phone on
the venue's network can never make the plugin buffer megabytes.

The dock's reveal button and the monitor's edit-mode label read **Show Status**;
the mode value stays `scoreboard`, so nothing about the API changed. Pressing it
reveals the status board, pressing it again hides it without ending the round,
and changing a card's status while on air replays that card's `reveal-in`.

### Preparing both modes off air

`showMode()` sets `displayMode` and `phase` in one change, so the SSE frame that
tells the overlay "the mode is bottomBar now" is the same frame that says "and
you are on air". The overlay used to keep one board and wipe it whenever the
mode changed, which put a full teardown, rebuild of every piece, reassignment of
every image `src` and a three-pass `applyPieceLayouts` inside the reveal frame.
Measured over 14 cards, that press blocked for 17.9 ms against 1.1–1.6 ms for
every later one — a dropped frame on air, before any rasterising.

`overlay.html` now keeps **one wrap/board pair per mode** under `#stage`
(`.board-wrap` carries the per-mode `layout` transform, so both halves have to
be split, not just the board). While off air, `render()` prepares both; on air it
prepares only the mode showing. `preparedKey` is per mode, and the coalesced
relayout listener sits on `#stage` so it covers both boards. The globals `wrap`
and `board` point at the pair for the current mode, which is what the browser
checks read.

Because there are two boards, `[data-target="card:<id>"]` is no longer unique in
the document — `cover`, `logo`, `count:*` and `.bottom-motion` still are, since
only the bottom bar has them. Checks scope their lookups to `board`.

Only the mode **on deck** is painted: `.overlay .board-wrap[hidden].on-deck` is
`visibility: visible` with `opacity: 0`, so its raster tiles and PNG textures are
ready before the operator presses anything while the stream stays clean. A
`visibility: hidden` subtree paints nothing, which also means it is never
rasterised, so the reveal used to be the first time any of it was drawn — that is
the part of the stutter that showed up even without a mode change.
`will-change: opacity` stays on across the reveal so the compositor keeps the
same layer, and the pieces inside are promoted while still off air, with
`.is-animating` carrying the hint from there so resting pieces still release it.
The mode that is not on deck keeps `visibility: hidden`: built and measured, but
costing no raster.

`tests/bottom-bar-cover.cjs` guards this two ways: the prepared-board checks
assert the on-deck/off-deck split, and a timing check presses once with a mode
switch, then three times without, and requires the first to cost about what the
median later one does. The threshold is a ratio with a small absolute floor, so
it travels between machines; it fails on the single-board design.

### Preview zoom and pan

`data/web/view-zoom.js` (served at `/view-zoom.js`, monitor only) gives both
editing previews — the board `.frame`/`.canvas` and the template
`#templateViewport`/`#templateStage` — the same view controls. The wheel zooms
around the cursor, holding Space turns the pointer into a hand and dragging
moves the view, and each preview carries a floating `.zoom-bar` with the zoom
percentage, 1:1 and Fit. All of it is view state: it writes a transform on the
preview's content element, is never saved to the session, and resets to Fit on
reload. A window resize refits only while the operator has not chosen a zoom.

Two things this has to keep true. Space belongs to a focused field first — the
hand arms only when the event target is not an `input`, `select`, `textarea`,
`button` or `contenteditable`, the same guard the arrow-key nudges use. And
while Space is held the pan handler runs in the capture phase and calls
`stopImmediatePropagation()`, so a piece can be neither selected nor moved;
immediate matters because an editor may have its own handler on the very same
element, where the capture flag no longer orders anything.

Anything measuring in canvas pixels reads the scale that is actually on screen
(`canvasScale()` in `monitor.html`, `stageView.scale` in the editor) rather than
assuming the preview is at fit, so dragging follows the pointer at any zoom.
The browser checks cover cursor anchoring, panning leaving every saved
coordinate and the selection untouched, typing a space in a field, 1:1/Fit, and
a drag while zoomed. `geometry()` at `tests/overlay-layout.cjs:145` already
normalises by the live canvas rect, so the older checks are zoom- and
pan-agnostic without change.

### Bottom Bar presentation regression

```sh
NODE_PATH="$FFF_TEST_DEPS/node_modules" node tests/phone-session.cjs
NODE_PATH="$FFF_TEST_DEPS/node_modules" node tests/overlay-layout.cjs
NODE_PATH="$FFF_TEST_DEPS/node_modules" node tests/bottom-bar-cover.cjs
cmake --build --preset macos
```

The focused cover suite checks the actual Web Animations timings: logo and both
counters 180ms, cover 300ms, cards 450ms with an 80ms start and center-out pairs 45ms apart
(stagger capped at 420ms), and interrupted exits 240ms. It samples animation
progress directly, exercises roster changes and template overflow during entry,
and checks that resting Overlay and Monitor retain no temporary wipe or clip.
Live vote updates must leave existing pieces attached; roster reordering keeps
their identities and in-flight animations. Counter rolls are motion of their
own, so the entrance checks read `entranceAnimations()`, which filters them out,
and `countRolls()` covers them directly. Resting pieces also release animation
transforms and compositor hints (`will-change`).
The 14-card entrance ends at 800ms; the maximum entrance is 950ms.

Set `FFF_MOTION_SCREENSHOTS=1` for deterministic 1920×1080 fixture captures:
`/private/tmp/fff-motion-{0,180,350,600,800,rest,monitor}.png`. The test pauses
Web Animations at the requested times and uses synthetic transparent PNGs;
it never reads or writes the real OBS session.
The preview artwork uses portrait school specimens with transparent vote windows,
a central fixture logo and a transparent Cover containing only thin top rules.
These graphics exist only in the test. Finished effects are settled and the
compositor is given time to rasterize each frozen frame before capture.

## Monitor LAN access

`monitor-access-tests` covers the pure rules in `src/fff-monitor-access.*`: key
format, constant-time comparison, cookie parsing, which routes belong to the
monitor, and every allow/redirect/deny decision. `/api/display` and `/api/logo`
stay local-only even with a valid key.

`monitor-lan-tests` runs the real session and HTTP server. The LAN switch and
key persist in `session.json`, a malformed key loads switched off, failed writes
roll back without announcing an access change, and neither value ever appears in
overlay or phone state. OBS host stubs shared by the native tests live in
`tests/obs-stubs.*`.

Server checks connect through the machine's first LAN address, so the peer is
not loopback. They cover the 403/303/cookie flow, the one-second brake on a
wrong key, `GET /api/monitor/access`, the upload cap lifted only for a valid
cookie, and on-air routes refusing the cookie. Without a LAN address they print
`SKIP: no LAN address` and pass.

## Dock panels

`dock-tests` builds the dock panels without OBS (`src/fff-dock.cpp`, the only
file that needs the frontend API, stays out) and drives them offscreen with
`QT_QPA_PLATFORM=offscreen`. `fffSetConfirmOverride()` answers confirmations so
no modal loop runs. The tests CMake points Qt's OpenGL wrapper at OpenGL because
current macOS SDKs no longer ship AGL.

## Monitor page furniture

`tests/monitor-ui.cjs` serves the real monitor page with a small fixture: the
top tools stay pinned while the page scrolls, flag/status/artwork controls are
disabled rather than hidden so the canvas never moves, the grid tab rests in
BOTTOM BAR, the last side tab survives a reload, the template stage is fitted
once its tab shows, and a 900px window has no horizontal scroll. Since Task 10
it also covers on-air status and the editing warning, the two-step "reset all"
button (the first press sends nothing, arming expires after 3 s, and changing
tab disarms it), and the revoked-key banner replacing the connection warning.

```sh
NODE_PATH="$FFF_TEST_DEPS/node_modules" node tests/monitor-ui.cjs
```

`overlay-layout.cjs` opens a tool's tab through `press()` before clicking it and
switches edit mode with `setEditMode()`.

## Asset renditions

`asset-rendition-tests` covers `src/fff-asset-rendition.*` and the server path
that uses it. Artwork larger than 1920×1080 is shrunk to fit (4500×8000 becomes
607×1080) with its transparency and colours intact, the original file stays as
imported, and the size is read from the PNG header so the UI thread never
decodes. Over loopback, an oversized asset is served as its rendition, a small
one byte for byte, a new cover is shrunk before anything requests it, and every
asset URL carries `fit=1920x1080` so a browser cache holding full-size bytes is
never reused.

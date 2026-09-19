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

When a tally changes, the number rolls through random digits for `countRoll` in
`countTick` steps (`COUNT_ROLL()` in `board.js`, read at the start of each roll
so one roll's frames and clock can never disagree) and lands on the newest
count. A flag
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

### Where the board sits

Show Status is a status board pinned to the upper left of frame, the way the
broadcast artwork draws it, rather than a block centred on the canvas. `.board`
is the whole 1920x1080 canvas on both boards now and places every piece itself:
`STATUS_STACK` in `board.js` puts row *i* at `left: 55px, top: 201 + i * 57.23`,
measured off the reference artwork (rows 346x51 with a 6px gap). The pitch is
fixed rather than taken from each card's height, so a PNG exported a pixel or
two taller cannot push every row below it out of step — artwork much taller
than the pitch will overlap, which is the one thing to watch when exporting.

Because a board is now the canvas, neither mode has a meaningful whole-board
offset or zoom left: `modeState()` pins `layout` to centre/100% for both, the
way it already did for BOTTOM BAR, and moving a piece is the only thing that
moves anything. That also retires whatever a session saved back when the
scoreboard was a grid centred on that point, which would otherwise drag the
stack off frame. The whole-board `POST /api/layout` still stores and returns
its value; it simply no longer draws anything.

A saved `pieces` entry still wins over the stack, so a card the operator placed
stays where it was put, and `reset` returns it to its row.

### The title above the stack

`heading` was always a layout and layer target; it is now a piece again, and the
one on that board made of words rather than artwork. It carries its own
`headingTemplate`, the way the flag counters carry theirs:

```jsonc
{"mode":"scoreboard","piece":"heading",
 "box":{"x":0,"y":0,"width":420,"height":100,"opacity":1},
 "text":"COMPETITION\nSTATUS", "fontFamily":"Bai Jamjuree",
 "fontSize":38, "fontWeight":800, "lineHeight":42,
 "align":"left", "color":"#000000"}
```

`piece` of `heading` requires `"mode":"scoreboard"` and is rejected with 400
otherwise, the mirror of `logo` and `count` requiring `bottomBar`. The box is in
the piece's own pixels and opens matching its CSS box (54, 72 on the canvas),
which is where the reference artwork puts the title. `text` is at most 200
characters over at most 8 lines; newlines are the two-line title and every other
control character is refused. `fontSize` and `lineHeight` are 8–400 px,
`fontWeight` 100–900, `align` one of `left`/`center`/`right`, and `color` is
`#rrggbb`. `validFontFamily()` and `validHexColor()` are the same ones the
counter template uses, so a family name still travels as text and still cannot
break out of the CSS declaration.

`headingOf()` in `board.js` is the one place the defaults live, so a session
saved before the title existed — or one the operator has not touched — draws the
artwork's own words, and the panel opens on exactly what the board would draw.
The monitor's **ชิ้นงาน** list offers the title in Show Status, so it drags,
layers and resets like any other piece, and the template panel gains a fourth
kind for it, reusing the installed-font picker (`queryLocalFonts`, the face to
weight mapping and the missing-font note) that the counters already had. The
title previews on the stage as it is typed and reaches the board only on apply.

The title is the board's furniture, not one of its rows: it enters with the
first card rather than taking a place in the entrance stagger, so adding it left
every card's timing exactly where it was. `layoutKey()` in `overlay.html`
includes `headingTemplate`, or a change to the words alone would not be measured
on a board that is off air.

### A card's box is its PNG's box

A Show Status card is its artwork and nothing else, so `.slot` takes the PNG's
own pixels: `fitCardToImage()` in `board.js` writes `naturalWidth`/
`naturalHeight` onto the slot, and `applyCardTemplate()` gives `.card-image`
`inset: 0` on this board instead of the template's rectangle. The slot's box is
therefore the picture's box — what the operator drags, what `#symmetry` reads
and what the overlay's reveal clips to are one rectangle, with no letterbox
margin to guess at. The placement frame is an `outline`, not a `border`, because
under the stylesheet's `border-box` sizing a 2px border would eat 4px of the
picture; `.monitor .slot.waiting` still marks an empty slot, which keeps the
420×152 default (`EMPTY_SLOT`).

Artwork larger than the stream already arrives shrunk (see **Asset
renditions**), so a card can never outgrow 1920×1080. Cards no longer share one
size, so the monitor's "ใช้ตาราง" measures each card rather than assuming a fixed
cell.

The card template keeps what still means something here — stacking order and
opacity — and its image rectangle does not. The panel says so: on Show Status
the image layer's X/Y/width/height report the PNG's box, are disabled, and
cannot be dragged or resized. BOTTOM BAR is unchanged; its cards are still laid
out by `renderBottomBar()` and sized by `bottomBar.cardTemplate`.

Sizes ride no API and are never saved: they are read from the decoded image, so
replacing a president's PNG moves the box with it on the next `load`. The
monitor coalesces that `load` into one relayout the way the overlay does;
without it the preview measures boxes that have since grown.

Row tops are rounded to whole pixels. A row on a half pixel is a row the
compositor resamples, and it also lands closer to the reference artwork than
the raw pitch does. `EMPTY_SLOT` is the reference card's own 346x51 so a roster
with no artwork yet still reads as one row per president rather than a stack of
overlapping boxes, and the monitor names any card whose PNG is taller than the
pitch (`#stackWarning`) because a fixed pitch cannot absorb it.

## Animation lengths

`data/web/timing.js` (served at `/timing.js`, loaded by both `/monitor` and
`/overlay`) holds every duration the presentation spends on screen, grouped by
the family of motion it belongs to:

```jsonc
{"scoreboard": {"card", "stagger", "maxStagger", "exit", "exitStagger", "maxExitStagger"},
 "bottomBar":  {"logo", "cover", "card", "firstPair", "pair", "maxStagger", "exit"},
 "board":      {"reveal", "countRoll", "countTick", "slotFrame"},
 "monitor":    {"guide", "control", "confirm"},
 "phone":      {"flag"}}
```

Only the lengths live there. Easings, keyframes and the order things move in are
the design and stay where they are drawn, so a number here makes the same
animation longer or shorter, never a different one. `applyTiming(state.timing)`
runs before every render on both pages: it merges the session's values over the
defaults and publishes `--fff-dur-reveal`, `--fff-dur-slot`, `--fff-dur-guide`
`--fff-dur-control` and `--fff-dur-flag` for the stylesheet's share of the
motion. A value that
is missing, the wrong type or out of range keeps its default rather than
becoming zero, so a partial object is a valid one.

`POST /api/timing` takes that object and uses the same permissions as
`/api/layout`. Durations and staggers are 0–5000 ms, `countTick` is 10–500 ms
and `confirm` is 500–30000 ms; an unknown group or key, a nonnumeric value or a
group that is not an object returns 400. `FffSession::validTiming()` holds the
ranges and `kTimingKeys` mirrors `FFF_TIMING_DEFAULTS`, so a number the panel
offers is a number the plugin will store. Sessions written before this existed
have no `timing` and keep every default; session JSON stays at version 5 and
overlay SSE carries `timing` once at the root, not per mode.

The voter's page animates too, so `phoneStateJson()` carries `timing` and
`/phone` loads the same module. That is the only part of the session a phone
gets beyond its own president — placements, templates and the roster all stay
with the operator.

The monitor's **จังหวะ** tab builds one field per length straight from
`FFF_TIMING_DEFAULTS`, drafts them the way the template panel drafts a box —
nothing reaches the stream until apply — clamps to the ranges above, rolls the
panel back on a refused save, and follows the session when a value arrives over
SSE. `armConfirm()` reads `monitor.confirm` at the press rather than at set-up,
so a change takes effect without a reload.

Network patience is not motion and is not offered here: the save timeouts
(`SAVE_TIMEOUT_MS`), the retry pump (`SAVE_RETRY_MS`) and the server's heartbeat
are named constants in their own files.

The motion checks read their expected lengths from `FFF_TIMING_DEFAULTS` rather
than repeating them, and cover a session that sets its own: Show Status
entrance, stagger and exit, BOTTOM BAR logo/cover/card/centre-out delays, the
counter roll, the phone's flag buttons, and the CSS custom properties. Nothing
in `data/web/` declares a `transition` or `animation` duration that does not
come from `fffTiming` or a `var(--fff-dur-*)` with the matching default as its
fallback.

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

The focused cover suite checks the actual Web Animations timings against
`FFF_TIMING_DEFAULTS.bottomBar`: logo and both counters on `logo`, the cover on
`cover`, cards on `card` starting after `firstPair` with centre-out pairs `pair`
apart (capped at `maxStagger`), and interrupted exits on `exit`. It samples animation
progress directly, exercises roster changes and template overflow during entry,
and checks that resting Overlay and Monitor retain no temporary wipe or clip.
Live vote updates must leave existing pieces attached; roster reordering keeps
their identities and in-flight animations. Counter rolls are motion of their
own, so the entrance checks read `entranceAnimations()`, which filters them out,
and `countRolls()` covers them directly. Resting pieces also release animation
transforms and compositor hints (`will-change`).
The 14-card entrance ends at 1060ms; the maximum entrance is 1260ms. Switching the
centre logo between Round 1 and Round 2 on air uses artwork fetched before the
press, and a school without Round 2 artwork leaves the centre empty.

Set `FFF_MOTION_SCREENSHOTS=1` for deterministic 1920×1080 fixture captures:
`/private/tmp/fff-motion-{0,240,470,800,1060,rest,monitor}.png`. The test pauses
Web Animations at the requested times and uses synthetic transparent PNGs;
it never reads or writes the real OBS session.
The preview artwork uses portrait school specimens with transparent vote windows,
a central fixture logo and a transparent Cover containing only thin top rules.
These graphics exist only in the test. Finished effects are settled and the
compositor is given time to rasterize each frozen frame before capture.

## The session a fresh install starts from

`data/default-session/` ships with the plugin — `session.json` plus the artwork
it points at — and `FffSession::seedFromBundle()` copies it into the config
directory the first time the plugin runs on a machine that has no session of
its own. `load()` calls it only when `session.json` does not exist, then opens
and parses the copy through the ordinary path, so every existing validation and
clamp still applies. A build with no bundle behaves exactly as before.

What the bundle deliberately leaves out is the point of it. A session written
by the plugin carries every president's PIN and the LAN monitor key, and this
repository is public, so `monitorKey`, `monitorLanEnabled`, `port`, `votes` and
every `pin` are dropped, and `phase`, `round` and each president's `status` are
forced back to a blank board on round one. `load()` mints a fresh PIN for any
president that arrives without one and saves once, which is what turns the
seeded copy into a session of that machine's own. With no key, `load()` already
forces `monitorLanEnabled` off, so LAN access starts closed.

Artwork ships at the size that reaches the screen: anything over 1920x1080 is
already served as a rendition, so the rendition travels under the original
name. The receiving machine sees a file that needs no shrinking, and the
picture on air is identical. For the current roster that is 10 MB rather than
82 MB, out of a `cards/` directory holding 192 MB — replacing a president's
artwork leaves the old file behind, so most of what is on disk is not
referenced at all and never ships.

Rebuild the bundle from a working session with:

```sh
build-aux/make-default-session [config-dir]   # defaults to this machine's
cmake --preset macos                          # data/ is globbed at configure time
```

It refuses to write a bundle whose secrets survived sanitising, prints what it
dropped, and fails if the session names a PNG that is not on disk.

`layout-tests` covers seeding end to end against a temporary module data
directory (`fffTestDataPath` in `tests/obs-stubs.*`, the mirror of
`fffTestConfigPath`): a fresh install takes the roster, artwork, templates and
timing; PINs are minted, six digits, and differ; the LAN key is not inherited;
the board starts blank on round one; the seeded session is saved so the next
launch keeps its PINs; a session that already exists is never replaced and no
artwork is copied over it; and a build carrying no bundle still starts empty.

## หน้า Score

`/score` เป็น Browser Source ของตัวเอง ไม่ใช่ display mode ที่สาม: มันไม่อ่าน
`phase` หรือ `displayMode` เลย และนับธงจาก `presidents[].vote` ที่ SSE
`/api/events/overlay` ส่งมาอยู่แล้ว จึงไม่มี endpoint ใหม่ เส้นทางนี้เป็น
`Endpoint::MonitorPage` เท่ากับ `/monitor` — เครื่องนี้เปิดได้เสมอ เครื่องใน LAN
ต้องมีกุญแจ และ `?key=` ถูกแลกเป็น cookie แล้ว 303 กลับมาที่ `/score` เอง
(redirect ใช้ `path` ได้เพราะ `classify()` คืน `MonitorPage` เฉพาะสตริง `/monitor`
กับ `/score` เป๊ะ ๆ เท่านั้น จึงไม่มี CRLF จาก URL หลุดเข้าเฮดเดอร์)

หน้านี้มีสองชิ้น และทั้งคู่ใช้ validator เดิม ไม่มีกฎใหม่:

```jsonc
{"mode":"score","piece":"heading",
 "box":{"x":660,"y":300,"width":600,"height":120},"text":"พี่เนย",
 "fontFamily":"Bai Jamjuree","fontSize":84,"fontWeight":800,
 "lineHeight":100,"align":"center","color":"#ffffff"}
{"mode":"score","piece":"count",
 "value":{"x":0,"y":0,"width":240,"height":240},
 "fontFamily":"","fontSize":160,"fontWeight":800,
 "colors":{"red":"#e23c3c","green":"#21b04a"}}
```

`piece` ของ `heading` ใช้ `validHeadingTemplate()` และ `count` ใช้
`validCountTemplate()` ตัวเดียวกับ Show Status และ BOTTOM BAR; `card` กับ `logo`
ถูกปฏิเสธด้วย 400 เพราะหน้านี้ไม่มี ค่าที่เก็บไปอยู่ที่ราก session เป็น
`scoreHeadingTemplate` และ `scoreCountTemplate` (ไม่ใช่ในก้อน `bottomBar`) และ
overlay SSE ส่งทั้งสองที่รากเช่นกัน session version ยังเป็น 5 เพราะทั้งคู่เป็น
optional — ไฟล์เก่าเปิดได้ตามเดิมและวาดค่า default

`mode` ของ `score` รับเฉพาะที่ `POST /api/template` ผ่าน
`FffSession::validTemplateMode()`; `validMode()` ยังเป็น `scoreboard|bottomBar`
เท่านั้น ดังนั้น `/api/display`, `/api/layout` และ `/api/layer` ตอบ 400 — หน้านี้
ไม่มีการลากบนกระดานและไม่มีลำดับเลเยอร์

ค่า default อยู่ที่เดียวใน `board.js` (`SCORE_HEADING_DEFAULT`,
`SCORE_COUNT_DEFAULT` กับ `scoreHeadingOf()` / `scoreCountOf()`) เพราะทั้งหน้า
`/score` และแผงในจอมอนิเตอร์อ่านจากตัวเดียวกัน `rollCount()` ถูก export ออกมา
เพื่อให้ตัวเลขบนหน้านี้หมุนด้วยจังหวะเดียวกับตัวนับใน BOTTOM BAR
`.score .heading-piece` กินเต็มผืนผ้า กล่องหัวเรื่องจึงเป็นพิกัดบนผืนผ้าตรง ๆ
ส่วน `.score .count-card` ถูกวางด้วย stylesheet แบบเดียวกับ BOTTOM BAR กล่อง
ตัวเลขหนึ่งกล่องจึงอธิบายทั้งสองตัวได้โดยไม่ทับกัน

`tests/score-page.cjs` เสิร์ฟไฟล์จริงด้วย fixture แล้วตรวจ: หน้าเปิดมาบนค่า
default, นับธงขณะ `phase: "collecting"`, ตัวนับสองตัวคนละที่คนละสี, ธงที่มาถึง
ระหว่างเลขกำลังหมุนลงเลขใหม่, ล้างรอบแล้วกลับเป็น 0/0, รายชื่อว่างก็ยังไม่พัง,
แท็บ **หน้า Score** แก้หน้าได้เฉพาะตอนกดใช้ (ร่างไม่ถึงจอ) และคืนค่าเมื่อเซฟ
ไม่สำเร็จ และไม่มี JS error

```sh
NODE_PATH="$FFF_TEST_DEPS/node_modules" node tests/score-page.cjs
```

`layout-tests` ครอบคลุมฝั่ง native: เซฟและอ่านกลับ, routing field ไม่ติดไปกับ
ค่าที่เก็บ, `piece` ที่หน้านี้ไม่มีถูกปฏิเสธ, สีและ align ที่ผิดถูกปฏิเสธ,
`mode:"score"` ถูกปฏิเสธที่ display/layout/layer, ค่าอยู่รอดการเปิดใหม่ และเซฟ
ล้มเหลวคืนค่าเดิม ส่วน `monitor-access-tests` ครอบคลุมว่ามีแต่ `GET /score`
เป๊ะ ๆ ที่เป็น `MonitorPage`

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

## Hotkeys

`hotkey-tests` ขับ `src/fff-hotkeys.cpp` ของจริงกับ libobs ปลอมใน
`tests/obs-hotkey-stubs.*` (`obs_hotkey_register_frontend/_unregister/_save/_load`
กับ `obs_data_*` เท่าที่ปลั๊กอินเรียก) ตัว stub เก็บ binding เป็นตัวเลขแทนคีย์จริง
เพราะ binding เป็นของทึบสำหรับปลั๊กอินอยู่แล้ว — สิ่งที่ต้องพิสูจน์คือ binding ของ
hotkey ไหนกลับไปเข้า hotkey นั้น

ครอบคลุม: ลงทะเบียนครบสี่ชื่อ (`fff.toggle_show_status`, `fff.toggle_bottom_bar`,
`fff.hide_screen`, `fff.new_round`) พร้อมข้อความที่ Settings → Hotkeys แสดง;
callback ที่ `pressed == false` ไม่ทำอะไรเลย ปล่อยคีย์จึงไม่ยิงซ้ำ;
callback ที่ `pressed == true` ยังไม่ทำงานจนกว่า event loop จะหมุน (มันถูก
queue ข้ามจากเธรด hotkey ของ libobs มาที่เธรด Qt) แล้วผลตรงกับปุ่มของมันและ dock
ตามทัน; คีย์ `fff.new_round` ล้างรอบโดยไม่เรียก `fffConfirm()` เลย;
`saveBindings()` เขียน `hotkeys.json` ครบทุกคีย์ภายใต้ชื่อของตัวเอง; สร้าง
`FffHotkeys` ใหม่แล้วได้ binding เดิมกลับมาบน id ใหม่และยังกดใช้งานได้;
ไม่มีไฟล์ = ทุกคีย์ว่างแต่ยังทำงาน; และออกจากสโคปแล้ว unregister ครบ

ชื่อทั้งสี่เป็นส่วนหนึ่งของรูปแบบที่เซฟไว้ เปลี่ยนชื่อเมื่อไหร่คีย์ที่ operator ตั้งไว้หาย

`dock-tests` คุมฝั่งที่ไม่ต้องมี OBS: `toggleMode()` / `hideScreen()` ให้ผลเท่ากับการ
คลิกปุ่มของมัน และ `startNewRound(bool)` ถามยืนยันเฉพาะตอน `confirm == true` —
ปุ่มใน dock ส่ง `true`, hotkey ส่ง `false`

```sh
ctest --test-dir /private/tmp/fff-layout-native -R "hotkey-tests|dock-tests" --output-on-failure
```

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

## Centre logo rounds

`logo-round-tests` covers Round 1/Round 2 centre-logo artwork. `logo2` and
`bottomBar.logoRound` persist, an unknown round loads as 1, a failed save keeps
the round, and `clearRound()` never touches it. Overlay state puts the chosen
round's URL in `logoUrl` (empty when that school has no Round 2 PNG) and names
both rounds in `logoRound1Url`/`logoRound2Url`. `GET /api/logo2/<id>` serves the
artwork through the same shrink-to-stream renditions as every other asset.

## Show Status motion

`tests/status-motion.cjs` drives the real overlay. Show Status cards on deck are
promoted before the press; on reveal each card slides in from `translateX(-48px)`
over 600ms, 120ms after the one before it (a long roster still starts every card
within 1.2s); on hide each leaves to `translateX(48px)` over 320ms, 60ms apart
(within 0.6s), and the board hides only after the last card. Only `transform`
and `opacity` animate, a status change on air replays just that card, and
switching to BOTTOM BAR lets the cards leave first.

```sh
NODE_PATH="$FFF_TEST_DEPS/node_modules" node tests/status-motion.cjs
```

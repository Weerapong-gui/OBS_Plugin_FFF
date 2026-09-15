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
exercise real pointer events, monitor/overlay geometry, independent scaling,
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

`POST /api/layout` remains localhost-only. A piece update is
`{"target":"card:<president-id>","layout":{"x":0.5,"y":0.5,"scale":1}}`;
use `"heading"` for the title and round label. Centres are canvas fractions
clamped to 0–1, with scale clamped to 0.5–2. Missing or nonnumeric layout values
return 400, unknown targets return 404, and failed saves return 500.

`{"target":"card:<president-id>","reset":true}` removes that piece's override.
`{"target":"all","reset":true}` removes every override and resets the legacy
grid to centre/100%. Requests without a target retain the original whole-board
layout API. Session JSON and overlay SSE add a `pieces` object keyed by these
targets; absent entries use the existing grid and legacy `layout` values.

`POST /api/layer` ก็เป็น localhost-only และรับ
`{"target":"card:<president-id>","action":"front|forward|backward|back"}`
เพื่อจัดลำดับซ้อนของการ์ดหรือ `heading` แบบบันทึกถาวร. SSE state ส่ง `layers`
เป็น map ของ target ไปยัง z-index; reset all ล้าง map นี้.

## BOTTOM BAR

State adds `displayMode: "scoreboard" | "bottomBar"` and a `bottomBar` object
containing `layout`, `pieces`, `layers`, `cardTemplate`, and `logoPresidentId`.
Roster entries add `bottomBarUrl` and `logoUrl`. Existing scoreboard fields and
requests remain compatible. Sessions without the new fields start in scoreboard.

Layout/layer/template POST bodies accept `mode` (omitted means `scoreboard`).
Bottom Bar uses `card:<id>` and `logo` targets. Optional layout `imageOpacity`
and `resultOpacity` range from 0 to 1; omitted values inherit template opacity,
then 1. Template `image.opacity` and `result.opacity` apply to all inheriting slots.
`POST /api/display` with `{"mode":"bottomBar"}` reveals that mode without clearing
votes; `POST /api/logo` with `{"presidentId":"id"}` selects only the central logo
(use an empty ID to clear). Both are localhost-only.

Extended native/browser checks cover mode isolation, old session defaults,
0/1/14/odd rosters, logo deletion, live votes, Clear, opacity, image replacement,
geometry parity, and persistence rollback. Browser screenshots use fixture PNGs;
actual school artwork and loading the built plugin in OBS remain manual checks.

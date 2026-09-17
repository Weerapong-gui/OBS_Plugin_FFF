# Live Tab: โลโก้กลาง 2 Round, toggle โหมด และ animation

- วันที่: 2026-09-17
- สถานะ: design อนุมัติแล้ว รอ implementation plan
- branch: `feat/operator-ui` (ต่อจาก `9e11037`)

## เป้าหมาย

1. โลโก้กลางของ BOTTOM BAR มี PNG แยก 2 ชุดต่อสำนัก คือ Round 1 และ Round 2 ให้ operator เลือก Round ได้จาก dock
2. แถบไลฟ์ใน dock จัดใหม่: ปุ่ม Round และตัวเลือกสำนักอยู่ด้านบน, Show Status / BOTTOM BAR เป็น toggle, `ซ่อนจอ` / `เริ่มรอบใหม่` อยู่ด้านล่าง
3. Show Status เข้าแบบเลื่อนจากซ้ายไปขวาทีละใบ และออกแบบเลื่อนไปทางขวาทีละใบ
4. BOTTOM BAR ช้าลงประมาณ 1.33 เท่า โดยไม่เปลี่ยนโครงสร้าง animation

## ข้อบังคับ (ห้ามละเมิด)

- **ห้ามรื้อสิ่งที่แก้อาการกระตุกไปแล้ว:**
  - การย่อภาพฝั่ง server (`src/fff-asset-rendition.*`, `fit=1920x1080`)
  - `warmAssets()` ที่โหลด/decode ภาพล่วงหน้า
  - การเตรียมบอร์ดทั้งสองโหมดขณะจอว่าง
  - การวาดโหมดที่รอขึ้นจอไว้ล่วงหน้า (`.on-deck`) และการโปรโมต layer ล่วงหน้า
  - การวัด reveal เป็นชุด (`refreshReveals`) และ generation guard (`transition` / `motionGeneration`)
- animation ใหม่ขยับเฉพาะ `transform` กับ `opacity` ซึ่งทำบน compositor ได้
- `■ ซ่อนจอ` และ `↻ เริ่มรอบใหม่…` (รวมกล่องยืนยัน) ทำงานเหมือนเดิม
- Round ของโลโก้แยกจากตัวนับรอบโหวต (`round`) โดยสิ้นเชิง
- แก้ให้น้อยที่สุด และใช้โค้ดเดิมซ้ำ
- หน้า monitor, หน้ามือถือ, API ของ LAN monitor และ `POST /api/logo` ไม่เปลี่ยน

---

## 1. แถบไลฟ์ใน dock (`src/fff-dock-live.{h,cpp}`)

```
┌ แถบไลฟ์ (ปักด้านบนเหมือนเดิม) ───────────┐
│ ● ออกอากาศ · BOTTOM BAR                  │
│ รอบ 266 · โหวตแล้ว 14/14                 │
│ โลโก้กลาง  [▓Round 1▓][ Round 2 ]        │
│ สำนัก      [สำนักวิชา ก            ▾]    │
│ ⚠ สำนักวิชา ก ยังไม่มี PNG ของ Round 2    │
│ [ Show Status ][▓ BOTTOM BAR ▓]          │
│ [ ■ ซ่อนจอ ]   [ ↻ เริ่มรอบใหม่… ]        │
│ (ข้อความ error ถ้ามี)                     │
└──────────────────────────────────────────┘
แท็บไลฟ์: เหลือรายการโหวตอย่างเดียว
```

### ปุ่ม Round
- ปุ่ม `Round 1` และ `Round 2` เป็น `QPushButton` checkable อยู่แถวเดียวกัน กดได้ทีละปุ่ม (exclusive) ใช้ `fffModeButtonStyle()` ปุ่มที่เลือกจึงเป็นพื้นฟ้า
- มีป้าย `โลโก้กลาง` นำหน้า
- กดแล้วเรียก `FffSession::setLogoRound(1|2)` มีผลทันทีทั้งตอนออกอากาศและจอว่าง ไม่มีกล่องยืนยัน
- `refresh()` ตั้ง checked ตาม `logoRound()` เสมอ ถ้าบันทึกไม่สำเร็จ แสดง `fffSaveErrorText()`

### ตัวเลือกสำนัก
- ย้าย `QComboBox` โลโก้กลางจาก `FffLiveTab` มาอยู่ใน `FffLivePanel` ใต้ปุ่ม Round มีป้าย `สำนัก`
- พฤติกรรมเดิมทั้งหมด: รายการ `ไม่เลือกโลโก้` ตามด้วยสำนัก, สร้างรายการใหม่เฉพาะตอนรายชื่อหรือโลโก้ที่เลือกเปลี่ยน, `setLogoPresident()`
- `FffLiveTab` เหลือแค่ `votes()` จึงไม่มี `logo()` และ `errorRaised` อีกต่อไป (`FffDock` เลิกต่อ signal นั้น)

### คำเตือน
- `QLabel` สีเหลือง (`FffColor::kAmber`) ข้อความ `⚠ <ชื่อสำนัก> ยังไม่มี PNG ของ Round 2`
- แสดงเฉพาะเมื่อ `logoRound() == 2`, เลือกสำนักอยู่ และสำนักนั้น `logo2` ว่าง
- ชื่อสำนักใช้ `school` ถ้าว่างใช้ `name` เหมือนรายการใน dropdown

### Toggle Show Status / BOTTOM BAR
`pressMode(mode)`:
- โหมดนั้นออกอากาศอยู่ → `hideDisplay()`
- จอว่าง หรืออีกโหมดออกอากาศอยู่ → `showMode(mode)`
- ถ้าบันทึกไม่สำเร็จ → `showError(fffSaveErrorText())` แล้ว `refresh()`

ข้อนี้กลับพฤติกรรมเดิมที่กดซ้ำไม่มีผล ตาม requirement ใหม่

### ปุ่มด้านล่าง
- `■ ซ่อนจอ` และ `↻ เริ่มรอบใหม่…` คง label, tooltip, กล่องยืนยัน และสถานะ enabled เดิม
- ปรับให้เป็นขอบมนสูงเท่ากัน (อย่างน้อย 36px)

### ขนาด
- แถบไลฟ์ต้องใช้งานได้ที่ dock กว้าง 320px: `minimumSizeHint().width() <= 320` และปุ่มไม่ถูกตัด

---

## 2. ข้อมูลโลโก้ 2 Round

### `src/fff-session.{h,cpp}`
- **`FffPresident::logo2`:**
  - เป็น `QString` ชื่อไฟล์ PNG ของ Round 2 บันทึก/โหลดเป็น `"logo2"` ใน entry ของ presidents
  - `logo` เดิมคือ Round 1 ไฟล์ session เก่าเปิดได้ (`logo2` ว่าง)
- **`int logoRound() const`** (ค่า 1 หรือ 2 ค่าเริ่มต้น 1)
- **`bool setLogoRound(int round)`:**
  - ค่าอื่นนอกจาก 1/2 คืน false โดยไม่บันทึก
  - ค่าเดิมคืน true โดยไม่ emit
  - บันทึกล้มเหลว → rollback + `saveFailed`
  - สำเร็จ → `changed`
- **ที่เก็บ:** `bottomBar.logoRound` ตอนโหลด ค่าที่ไม่ใช่ 1/2 เป็น 1
- **ชนิดภาพ `"logo2"`:** เพิ่มใน `assetPath()`, `assetPaths()`, รายการชนิดของ `storeAsset()` และ `importAsset()` ผ่าน `storeAsset`
- **`overlayStateJson()` ต่อสำนัก:**
  - `logoUrl` = `assetUrl(logo2)` เมื่อ Round 2 ไม่งั้น `assetUrl(logo)` ว่างถ้าไม่มีไฟล์
  - `logoRound1Url` = `assetUrl(logo)`
  - `logoRound2Url` = `assetUrl(logo2)`
- **`bottomBarJson()`:** มี `logoRound`
- **`phoneStateJson()`:** ไม่เปลี่ยน
- **`clearRound()`:** ไม่แตะ `logoRound`

### `src/fff-http-server.cpp`
- เพิ่ม `logo2` ในลูปชนิดภาพของ `route()` → `GET /api/logo2/<id>` เรียก `sendCard(socket, id, "logo2")` จึงใช้การย่อภาพและ cache เดิม
- route นี้เป็น GET สาธารณะเหมือนภาพชนิดอื่น (`FffMonitorAccess::classify` ไม่ต้องแก้)

### `data/web/overlay.html`: `warmAssets()`
- สำหรับสำนักที่เป็น `logoPresidentId`:
  - URL ของ Round ที่ใช้อยู่ (`logoUrl`): eager (decode) เมื่อโหมดที่รอขึ้นจอเป็น bottomBar
  - อีก Round: ดึงไฟล์มาเก็บไว้ (ไม่ decode)
- สลับ Round ตอนออกอากาศแล้วไม่ต้องดาวน์โหลดใหม่

### `board.js`
- ไม่แก้ (อ่าน `president.logoUrl` ของ `logoPresidentId` อยู่แล้ว)

### จัดการ PNG ใน dock (`src/fff-dock-roster.{h,cpp}`)
- **เมนู PNG:**
  - ส่วนเลือก: `เลือกการ์ด PNG (สำรอง)…`, `เลือก BOTTOM BAR PNG…`, `เลือกโลโก้กลาง Round 1 PNG…`, `เลือกโลโก้กลาง Round 2 PNG…`
  - เส้นคั่น แล้วส่วนลบ: `ลบการ์ด PNG`, `ลบ BOTTOM BAR PNG`, `ลบโลโก้กลาง Round 1 PNG`, `ลบโลโก้กลาง Round 2 PNG`
- **kind ใหม่ `"logo2"`:** `chooseAction("logo2")` / `clearAction("logo2")` และ `applyAsset` ตั้ง `logo2`
- **dialog title:** `เลือกโลโก้กลาง Round 1 PNG` / `เลือกโลโก้กลาง Round 2 PNG`
- **ตาราง 7 คอลัมน์:** `ชื่อนายก | สำนักวิชา | PIN | การ์ด | BAR | โลโก้ R1 | โลโก้ R2` (✓/—)

---

## 3. Animation (`data/web/overlay.html`, `data/web/board.js`, `data/web/app.css`)

### Show Status
- **`SCOREBOARD_TIMING`** (frozen object):
  - `card: 600, stagger: 120, maxStagger: 1200`
  - `exit: 320, exitStagger: 60, maxExitStagger: 600`
  - `easing: "cubic-bezier(0.22, 1, 0.36, 1)"`
- **`animateScoreboard(entering, done)`:**
  - โครงเดียวกับ `animateBottom`: generation guard เดียวกัน, cleanup ต่อชิ้น, `setTimeout` สำรองตาม deadline
  - เป้าหมาย: `.slot` ของแต่ละ `.piece` ในบอร์ด scoreboard เรียงตามลำดับ DOM (ลำดับรายชื่อ = การ์ด 1, 2, 3, …)
  - ระยะห่างต่อใบ: `step = n > 1 ? min(stagger, maxStagger / (n - 1)) : 0`
  - เข้า: `{ opacity: 0, transform: "translateX(-48px)" } → { opacity: 1, transform: "none" }`, `duration: card`, `delay: index × step`, easing ข้างบน, `fill: "both"`
  - ออก: อ่าน `opacity`/`transform` ปัจจุบันของทุกใบก่อน cancel (รองรับการขัดกลางทาง) แล้ว `→ { opacity: 0, transform: "translateX(48px)" }`, `duration: exit`, `delay: index × min(exitStagger, maxExitStagger / (n - 1))`, `easing: "ease-in"`
  - ระหว่างเล่นใส่ class `is-animating` บน `.slot` แล้วเอาออกตอน cleanup
- **`render()`:**
  - เงื่อนไขเล่นตอนออกที่ตอนนี้ใช้กับ `displayedMode === "bottomBar"` ขยายให้ใช้กับโหมดที่แสดงอยู่ทุกโหมด คือเมื่อ `!revealed`, เปลี่ยนโหมด หรือ `round` เปลี่ยน
  - โหมด scoreboard เรียก `animateScoreboard(false, …)` โหมด bottomBar เรียก `animateBottom(false, …)`
  - callback เหมือนเดิม: ซ่อน wrap, reset, `render(lastState)`
  - ตอนเข้าโหมด scoreboard (`revealed && mode === "scoreboard" && displayedMode !== "scoreboard"`) เรียก `animateScoreboard(true)`
- **`prepare()`:** ส่ง `justRevealed: false` ให้ `renderBoard` เสมอ เพราะ overlay คุมตอนเข้าเอง `.reveal-in` ใน `board.js` จึงเหลือแค่กรณีสถานะใบนั้นเปลี่ยนระหว่างออกอากาศ ส่วน monitor ส่ง `justRevealed` เหมือนเดิม
- **`app.css`:**
  - `.overlay .board-wrap[hidden].on-deck .slot { will-change: transform, opacity; }`
  - `.overlay .board .slot.is-animating { will-change: transform, opacity; }`

### BOTTOM BAR
แก้เฉพาะค่าใน `BOTTOM_TIMING` โครงอื่นเหมือนเดิม:

| key | เดิม | ใหม่ |
|---|---|---|
| logo (รวมตัวนับ) | 180 | 240 |
| cover | 300 | 400 |
| card | 450 | 600 |
| firstPair | 80 | 100 |
| pair | 45 | 60 |
| maxStagger | 420 | 560 |
| exit | 240 | 320 |

`README.md` ย่อหน้า animation ของ BOTTOM BAR แก้ตัวเลขให้ตรง

---

## 4. การจัดการ error

| สถานการณ์ | ผล |
|---|---|
| บันทึก Round ไม่สำเร็จ | rollback, ปุ่ม Round กลับไปตรงกับ session, ป้าย error ในแถบไลฟ์ |
| Round 2 แต่สำนักไม่มี `logo2` | ช่องโลโก้บนจอว่าง, dock แสดงคำเตือน |
| toggle ขณะ exit ของอีกโหมดกำลังเล่น | ใช้ generation guard และการแช่รอบขาออกของ `render()` แบบเดิม: เล่นขาออกจบก่อนแล้วค่อยแสดง state ล่าสุด |
| นำเข้า PNG Round 2 ไม่สำเร็จ | `errorRaised("นำเข้า PNG ไม่สำเร็จ รูปเดิมยังอยู่")` เหมือนชนิดภาพอื่น |

---

## 5. การทดสอบ

### native
- **`tests/logo-round-tests.cpp` (ใหม่, target `logo-round-tests`):**
  - `logo2`/`logoRound` บันทึกแล้วโหลดกลับได้
  - `setLogoRound(3)` คืน false
  - บันทึกล้มเหลวแล้ว rollback
  - `bottomBar.logoRound` ที่ไม่ถูกต้องในไฟล์โหลดเป็น 1
  - state: `logoUrl` ตาม Round, ว่างเมื่อ Round 2 ไม่มีไฟล์, มี `logoRound1Url`/`logoRound2Url`
  - `clearRound()` ไม่เปลี่ยน `logoRound`
  - `GET /api/logo2/<id>` ตอบ 200 และภาพเกิน 1920×1080 ถูกย่อ
  - `assetPaths()` มีไฟล์ `logo2`
- **`tests/dock-tests.cpp` (แก้):**
  - ปุ่ม Round exclusive และตรงกับ session กดแล้ว `logoRound()` เปลี่ยน
  - dropdown สำนักอยู่ใน `FffLivePanel` และตั้ง `logoPresidentId` ได้
  - คำเตือนแสดงหรือซ่อนตาม `logo2`
  - toggle: กดโหมดที่ออกอากาศซ้ำ → `Collecting`, กดอีกโหมด → สลับ
  - `ซ่อนจอ` และ `เริ่มรอบใหม่` (ยกเลิก/ยืนยัน) เหมือนเดิม
  - roster 7 คอลัมน์, เมนูเลือก/ลบ `logo2`
  - `minimumSizeHint().width() <= 320`

### browser
- **`tests/bottom-bar-cover.cjs` (แก้):**
  - ตัวเลข timing ที่ assert เป็นค่าใหม่
  - "first press" ยังผ่าน
  - สลับ `logoRound`: `src` ของโลโก้เป็นไฟล์ Round 2 และว่างเมื่อไม่มีไฟล์
  - URL ของทั้งสอง Round ถูกดึงไว้ก่อนสลับ
- **`tests/status-motion.cjs` (ใหม่):**
  - ตอนเข้า keyframe แรก `translateX(-48px)`, delay เรียง `0, 120, 240…` ตามลำดับรายชื่อ, stagger รวมไม่เกิน 1200 ms
  - ตอนออก `translateX(48px)` delay เรียงตามลำดับ
  - keyframes มีเฉพาะ `transform`/`opacity`
  - เล่นจบแล้ว wrap ซ่อน
  - `.slot` ของโหมดที่รอขึ้นจอมี `will-change`
  - status ที่เปลี่ยนระหว่างออกอากาศยังเล่น `.reveal-in` ใบเดียว
- **รันซ้ำให้ผ่าน:** `overlay-layout.cjs`, `monitor-ui.cjs`, `phone-session.cjs`

### ความลื่น
- รัน spike วัดช่องว่างของ `requestAnimationFrame` ด้วย PNG จริง (ย่อผ่าน server): เปิด/ปิด Show Status และ BOTTOM BAR ครั้งแรกกับครั้งที่สอง ต้องไม่มีเฟรมค้างเกิน 50 ms

### ใน OBS จริง (หลังติดตั้ง)
checklist 12 ข้อ:
1. Round 1 แสดง PNG ของ Round 1
2. Round 2 แสดง PNG ของ Round 2
3. เพิ่ม/ลบ PNG Round 2 ได้
4. สลับ Round แล้ว UI ตรง
5. Show Status toggle ได้
6. การ์ดเลื่อนซ้าย→ขวาทีละใบ
7. BOTTOM BAR toggle ได้
8. ซ่อนจอเหมือนเดิม
9. เริ่มรอบใหม่เหมือนเดิม
10. ไม่กระตุก (View → Stats: "Frames missed" ไม่เพิ่ม)
11. dock แคบ 320px ยังใช้ได้
12. state ถูกต้องหลังปิดเปิด OBS

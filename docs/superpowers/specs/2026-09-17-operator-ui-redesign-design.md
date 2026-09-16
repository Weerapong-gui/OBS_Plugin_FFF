# Operator UI redesign: dock, monitor และ LAN monitor access

- วันที่: 2026-09-17
- สถานะ: design อนุมัติแล้ว รอ implementation plan
- ขอบเขต: `src/fff-dock*.cpp`, `src/fff-monitor-access.*`, `src/fff-session.*`, `src/fff-http-server.*`, `data/web/monitor.html`, `data/web/monitor-ui.js`, `data/web/app.css`, tests, README

## เป้าหมาย

1. ปุ่มที่ใช้ตอนไลฟ์ต้องเห็นตลอด ไม่ต้องเลื่อนหา
2. จัดลำดับตามความสำคัญ ของที่ใช้ก่อนงานยุบเป็นแท็บและเมนู dropdown
3. อ่านสถานะออกอากาศได้ในแวบเดียว ทั้งคำและสี
4. กันกดผิด ปุ่มที่ทำลายข้อมูลต้องยืนยัน ปุ่มที่ต้องเร็วห้ามช้าลง
5. เครื่องอื่นในวง LAN เปิดหน้า monitor ได้ โดยไม่เปิดช่องให้คนโหวตเห็นผลก่อนเฉลย

## ผู้ใช้และบทบาท

งานจริงใช้ 2 คนแยกหน้าที่:

| คน | หน้าจอ | ทำอะไร |
|---|---|---|
| Operator ออกอากาศ | OBS dock | ขึ้นจอ ซ่อนจอ เริ่มรอบใหม่ เลือกโลโก้กลาง ดูโหวต |
| คนจัดเลย์เอาต์ | หน้า monitor (เครื่องนี้หรือเครื่องอื่นใน LAN) | ลากจัดตำแหน่ง กำหนดธง/สถานะ แม่แบบ PNG สถานะ |

คนจัดเลย์เอาต์**ต้องไม่สามารถ**ส่งอะไรขึ้นสตรีมได้

## นอกขอบเขต

- HTTPS/TLS บน LAN
- OBS hotkeys
- หน้า `phone.html` และ `overlay.html` (ยกเว้นผลจาก CSS ที่ใช้ร่วม ซึ่งต้องไม่เปลี่ยน)
- การอนุมัติทีละเครื่อง (device approval)
- undo ของ "เริ่มรอบใหม่" (ใช้กล่องยืนยันแทน)

---

## 1. OBS dock

### 1.1 โครงหน้า

```
┌ FFF Flag Board ─────────────────────┐
│ ● ออกอากาศ · BOTTOM BAR             │  แถบสถานะ (ปักไว้)
│ รอบ 3 · โหวตแล้ว 6/6 ✓ ครบ          │
│ [ Show Status ] [▓ BOTTOM BAR ▓]    │
│ [■ ซ่อนจอ ]     [↻ เริ่มรอบใหม่…]    │
│ (ข้อความ error ถ้ามี)                │
├─[ไลฟ์]──[รายชื่อ]──[ตั้งค่า ⚠]────────┤
│ เนื้อหาแท็บ (เลื่อนได้)               │
└─────────────────────────────────────┘
```

ส่วนบนอยู่นอก `QScrollArea` จึงไม่เลื่อนหาย เฉพาะเนื้อหาแท็บที่เลื่อน

### 1.2 แถบไลฟ์ (`FffLivePanel`)

| องค์ประกอบ | พฤติกรรม |
|---|---|
| ป้ายสถานะ | `● ออกอากาศ · <โหมด>` พื้นแดง `#e23c3c` / `○ จอว่าง` พื้นเทา `#6d7580` / `⚠ เซิร์ฟเวอร์ปิด — overlay/มือถือไม่อัปเดต` พื้นเหลือง `#ffb02e` (สถานะเซิร์ฟเวอร์ปิดมีลำดับเหนือกว่า) |
| สรุป | `รอบ N · โหวตแล้ว X/Y` เมื่อครบและยังไม่ออกอากาศ เปลี่ยนเป็น `✓ ครบ X/Y พร้อมขึ้นจอ` สีเขียว `#21b04a` ตัวหนา |
| `Show Status`, `BOTTOM BAR` | checkable สูงอย่างน้อย 44px วางคู่กัน กดโหมดที่ไม่ได้ออกอากาศ = `showMode(mode)` ทันที **กดโหมดที่ออกอากาศอยู่ = ไม่ทำอะไร** (คงสถานะ checked) |
| `■ ซ่อนจอ` | `hideDisplay()` ทันที disabled เมื่อจอว่างอยู่แล้ว |
| `↻ เริ่มรอบใหม่…` | ยืนยันก่อน แล้ว `clearRound()` |
| ป้าย error | สีแดง `#ff8075` แสดงเมื่อ `FffSession::saveFailed` หรือแผงอื่นแจ้ง error ล้างเมื่อ `changed()` |

ปุ่มโหมดและ `ซ่อนจอ` ไม่มีกล่องยืนยัน เพื่อความเร็วตอนไลฟ์

**พฤติกรรมที่เปลี่ยนจากเดิม:** เดิมกดปุ่มโหมดที่ออกอากาศซ้ำ = ซ่อนจอ ใหม่ต้องใช้ `ซ่อนจอ` เท่านั้น เพื่อกันกดซ้ำโดยไม่ตั้งใจ

### 1.3 แท็บ

แท็บเริ่มต้นตอนเปิด OBS: ถ้ายังไม่มีนายกในรายชื่อ เปิด `รายชื่อ` ไม่งั้นเปิด `ไลฟ์`

**ไลฟ์**
- รายการโหวต: `● เขียว · <ชื่อ>`, `● แดง · <ชื่อ>`, `○ ยังไม่กด · <ชื่อ>`
- `โลโก้กลาง [dropdown]` (ย้ายจากกล่องรอบปัจจุบันเดิม)

**รายชื่อ** (`FffRosterTab`)
- ตาราง: `ชื่อนายก | สำนักวิชา | PIN | การ์ด | BAR | โลโก้` สามคอลัมน์หลังแสดง `✓` หรือ `—` จัดกึ่งกลาง แก้ไขได้เฉพาะชื่อและสำนักวิชา
- ปุ่มใต้ตาราง 3 ปุ่ม:
  - `+ เพิ่ม`
  - `PNG ▾` (`QToolButton` + `QMenu`, `InstantPopup`): `เลือกการ์ด PNG (สำรอง)…`, `เลือก BOTTOM BAR PNG…`, `เลือกโลโก้กลาง PNG…`, เส้นคั่น, `ลบการ์ด PNG`, `ลบ BOTTOM BAR PNG`, `ลบโลโก้กลาง PNG`
  - `⋯ ▾`: `สุ่ม PIN ใหม่…`, `ลบนายก…`
- ไม่ได้เลือกแถว: `PNG ▾` และ `⋯ ▾` disabled พร้อม tooltip `เลือกนายกในตารางก่อน`
- เมนูลบ PNG แต่ละรายการ disabled เมื่อคนนั้นไม่มีไฟล์ชนิดนั้น
- ลบ PNG ไม่ต้องยืนยัน (ไฟล์ยังอยู่ในโฟลเดอร์ cards เลือกไฟล์เดิมใหม่ได้)

**ตั้งค่า** (`FffSettingsTab`)
- กลุ่ม `เซิร์ฟเวอร์`: พอร์ต, เริ่ม/หยุด, สถานะ `กำลังฟังพอร์ต N · มือถือ N · จอในเครื่องนี้ N · monitor LAN N`, URL (Browser Source, จอมอนิเตอร์เครื่องนี้, มือถือ LAN) พร้อมปุ่มคัดลอก
- กลุ่ม `Monitor LAN`: ดูหัวข้อ 3.1
- กลุ่ม `Cover PNG`: `เลือก Cover PNG…`, `ลบ Cover`, ป้าย `มี Cover` / `ยังไม่มี Cover`
- กลุ่ม `ตำแหน่ง`: dropdown โหมด + `รีเซ็ตตำแหน่งโหมดที่เลือก…` + คำแนะนำให้ไปแก้ในหน้า monitor
- ชื่อแท็บเป็น `ตั้งค่า ⚠` เมื่อเซิร์ฟเวอร์ไม่ได้ฟังพอร์ต หรือ `lanAddresses()` ว่าง

### 1.4 กล่องยืนยัน

ใช้ `QMessageBox` ปุ่ม default = ยกเลิก ข้อความบอกผลจริง

| การกระทำ | ข้อความ (ตัวอย่าง) |
|---|---|
| เริ่มรอบใหม่ | `ล้างผลโหวตรอบ 3 (โหวตแล้ว 6 คน) แล้วขึ้นรอบ 4 จอสตรีมจะว่าง รายชื่อ PNG และ PIN ยังอยู่ครบ` |
| ลบนายก | `ลบ <ชื่อ> (<สำนักวิชา>) ออกจากรายชื่อ?` |
| สุ่ม PIN ใหม่ | `PIN เดิมของ <ชื่อ> จะใช้เข้าสู่ระบบใหม่ไม่ได้ มือถือที่เข้าอยู่แล้วยังใช้ต่อได้` |
| รีเซ็ตตำแหน่ง | `คืนตำแหน่งทุกชิ้นของโหมด <โหมด> เป็นค่าเริ่มต้น?` |
| หยุดเซิร์ฟเวอร์ | ถามเฉพาะเมื่อมี client ต่ออยู่: `มือถือ N เครื่อง จอ N และ monitor LAN N จะหลุด overlay จะไม่อัปเดต` |
| สุ่มกุญแจ monitor ใหม่ | `เครื่อง LAN ที่เปิด monitor อยู่ N เครื่องจะถูกตัด ต้องส่งลิงก์ใหม่ให้` |

ข้อความ PIN ถูกต้องตามโค้ดปัจจุบัน: token ผูกกับ president id ไม่ใช่ PIN

### 1.5 โครงไฟล์

| ไฟล์ | หน้าที่ |
|---|---|
| `src/fff-dock.cpp` | `FffDock` ประกอบแถบไลฟ์ + `QTabWidget`, ต่อ `changed()`/`clientsChanged()` เข้ากับ `refresh()` ของทุกแผง, เริ่มเซิร์ฟเวอร์, `fff_register_dock`/`fff_unregister_dock` |
| `src/fff-dock-ui.h` | ค่าสี, stylesheet ปุ่มโหมด, `bool fffConfirm(QWidget*, const QString &title, const QString &text, const QString &acceptText)` และ `void fffSetConfirmOverride(std::function<bool(const QString &title)>)` สำหรับ test |
| `src/fff-dock-live.{h,cpp}` | `FffLivePanel` (แถบบน) และ `FffLiveTab` (รายการโหวต + โลโก้กลาง) |
| `src/fff-dock-roster.{h,cpp}` | `FffRosterTab` |
| `src/fff-dock-settings.{h,cpp}` | `FffSettingsTab` |

หลักการ:
- แต่ละแผงมี `refresh()` ที่อ่านจาก `FffSession`/`FffHttpServer` ใหม่ทั้งหมด ไม่เก็บ state ซ้ำ
- แผงที่เจอ error ที่ session ไม่ส่งสัญญาณเอง (เช่น `importAsset` คืนค่าว่าง) ส่ง signal `errorRaised(QString)` ให้ `FffDock` ต่อไปที่ป้าย error ของแถบไลฟ์
- เพิ่มไฟล์ใหม่ใน `target_sources` ของ `CMakeLists.txt`
- ไม่แก้ API เดิมของ `FffSession` (เพิ่มเฉพาะหัวข้อ 3.3)

---

## 2. หน้า monitor

### 2.1 โครงหน้า

```
┌ จอมอนิเตอร์  ● ออกอากาศ · BOTTOM BAR   รอบ 3 · 5/6   บันทึกแล้ว ┐ แถบบน (sticky)
│ แก้ไขโหมด (Show Status│▓BOTTOM BAR▓)   ชิ้นงาน [การ์ด สำนัก ก ▾]  │
│ ธง [—][เขียว][แดง]   สถานะ [QUALIFIED][UNQUALIFIED][WAITING]      │
│ ⚠ กำลังแก้โหมดที่ออกอากาศอยู่ — ย้าย/เปลี่ยนธง คนดูเห็นทันที          │ (เฉพาะเมื่อตรงเงื่อนไข)
├──────────────────────────────────────┬───────────────────────────┤
│ ผืนผ้าใบ 1920×1080                    │ [ตำแหน่ง][ตาราง][แม่แบบ][PNG สถานะ] │
│ [−] 50% [+] [1:1] [พอดีจอ] ☐ เส้นตาราง│ เนื้อหาแท็บ                  │
│ คำแนะนำ 1 บรรทัด · symmetry            │                           │
└──────────────────────────────────────┴───────────────────────────┘
```

### 2.2 แถบบน (`position: sticky; top: 0`)

- **สถานะ:** `#status` แสดง `● ออกอากาศ · <โหมด>` หรือ `○ จอว่าง` สีชุดเดียวกับ dock + `รอบ N · X/Y` นับจาก state ที่ได้รับ
- **`#saveStatus`** ย้ายมาอยู่แถบบน (id เดิม)
- **แก้ไขโหมด:** เปลี่ยนจาก `<select id="editMode">` เป็น `<div id="editMode" role="radiogroup">` ที่มี `<input type="radio" name="editMode" id="editModeScoreboard" value="scoreboard">` และ `id="editModeBottomBar" value="bottomBar"` แสดงแบบ segmented ป้ายเขียนว่า "แก้ไขโหมด" เพื่อไม่ให้สับสนกับการขึ้นจอ ปุ่มนี้ไม่เรียก `/api/display`
- **`#selection`** อยู่แถบบน (id เดิม)
- **ธงและสถานะ:** `#cardControls` และ `#statusControls` แสดงตลอด ปุ่มถูก `disabled` พร้อม `title="เลือกการ์ดก่อน"` เมื่อชิ้นที่เลือกไม่ใช่การ์ด แทนการใช้ `hidden` เพื่อให้เลย์เอาต์ไม่ขยับ
- **ป้ายเตือน `#airWarning`:** แสดงเมื่อ `state.phase === "revealed"` และ `state.displayMode === editMode` ซ่อนเมื่อไม่ตรง
- **ป้ายการเชื่อมต่อ:** `#warn` เดิม (`ขาดการเชื่อมต่อกับปลั๊กอิน`) และ `#accessDenied` ใหม่ (หัวข้อ 3.5)

### 2.3 ผืนผ้าใบ

- แถบซูมเดิม + `#showGrid` ย้ายมาอยู่ท้ายแถบซูม
- คำแนะนำย่อเหลือ 1 บรรทัด และ `#symmetry` อยู่ใต้ผืนผ้าใบ

### 2.4 แผงขวาแบบแท็บ

| แท็บ | เนื้อหา (id เดิม) | เงื่อนไข |
|---|---|---|
| ตำแหน่ง | `#centerX`, `#centerY`, `#pieceWidth`, `#pieceHeight`, `#imageOpacity`, `#resultOpacity`, ปุ่ม `data-layer`, `#reset`, `#resetAll` | — |
| ตาราง | `#gridColumns`, `#gridGapX`, `#gridGapY`, `#gridPreview`, `#gridApply`, `#gridCancel` | disabled ในโหมด BOTTOM BAR พร้อม tooltip `ใช้ได้เฉพาะ Show Status` ถ้าแท็บนี้เปิดอยู่ตอนเปลี่ยนเป็น BOTTOM BAR ให้สลับไปแท็บตำแหน่ง |
| แม่แบบ | แผง `template-panel` เดิมทั้งหมด | ทุกครั้งที่เปิดแท็บ เรียก fit ของ template view ใหม่ (ตอนซ่อน viewport มีขนาด 0) |
| PNG สถานะ | `#statusAssets` | input disabled พร้อมคำอธิบาย `เลือกการ์ดก่อน` เมื่อชิ้นที่เลือกไม่ใช่การ์ด |

- markup: ปุ่มแท็บ `role="tab"` `aria-selected` `aria-controls`, แผง `role="tabpanel"`
- จำแท็บล่าสุดใน `localStorage` key `fff.monitor.tab` ครอบ try/catch ถ้าอ่านไม่ได้ใช้แท็บตำแหน่ง
- จอกว้างน้อยกว่า 1000px: แผงขวาอยู่ใต้ผืนผ้าใบ (media query เดิม) แถบบนยังปัก ห้ามมี scroll แนวนอน

### 2.5 ยืนยัน 2 จังหวะ

ใช้กับ `#resetAll`:
1. กดครั้งแรก: ไม่ส่ง request ปุ่มเปลี่ยนข้อความเป็น `กดอีกครั้งเพื่อรีเซ็ตทั้งหมด` สีแดง
2. กดครั้งที่สองภายใน 3 วินาที: ทำงานจริง
3. เกิน 3 วินาที หรือเปลี่ยนแท็บ เปลี่ยนชิ้นงาน เปลี่ยนโหมด: กลับเป็นปุ่มเดิม

ไม่ใช้ `window.confirm()` เพราะอาจไม่แสดงเมื่อเปิดใน OBS Custom Browser Dock และบล็อก puppeteer

### 2.6 ไฟล์

| ไฟล์ | หน้าที่ |
|---|---|
| `data/web/monitor.html` | ย้าย markup ตาม 2.1 เก็บ id เดิมทั้งหมด (ยกเว้น `#editMode` ที่เปลี่ยนชนิด element) ปรับ handler ของ editMode และจุดที่ใช้ `hidden` กับปุ่มธง/สถานะ |
| `data/web/monitor-ui.js` (ใหม่) | `createTabs(root, options)`, `armConfirm(button, onConfirm, { timeoutMs })`, `updateAirWarning(state, editMode)`, `checkAccess()` |
| `data/web/app.css` | กฎใต้ `body.monitor` เท่านั้น: sticky top bar, segmented radio, tabs, disabled states, ป้ายเตือน |
| `src/fff-http-server.cpp` | route `GET /monitor-ui.js` แบบเดียวกับ `view-zoom.js` |

---

## 3. LAN monitor access

### 3.1 dock: กลุ่ม Monitor LAN

```
☐ อนุญาตเครื่องอื่นใน LAN เปิด monitor
  http://192.168.1.10:9779/monitor?key=<key>   [คัดลอก]   (หนึ่งแถวต่อ LAN address)
  เชื่อมต่ออยู่: monitor LAN N                 [สุ่มกุญแจใหม่…]
  ลิงก์นี้ให้สิทธิ์เห็นผลก่อนเฉลยและแก้ธงได้ ส่งให้เฉพาะคนจัดเลย์เอาต์
  ไม่มีการเข้ารหัส ใช้ใน Wi-Fi ของทีมเท่านั้น
```

- ค่าเริ่มต้น: ปิด
- ปิดอยู่: ซ่อน URL และปุ่มสุ่มกุญแจ
- เซิร์ฟเวอร์ไม่ได้ฟังพอร์ต หรือไม่มี LAN address: แสดงเหตุผลแทน URL
- บันทึกล้มเหลว: checkbox คืนค่าเดิมด้วย `QSignalBlocker` + ป้าย error

### 3.2 `src/fff-monitor-access.{h,cpp}` (ใหม่, ฟังก์ชันล้วน)

```cpp
namespace FffMonitorAccess {
// 32 lowercase hex chars (128 bits) from QRandomGenerator::system().
QString generateKey();
// Compares every character of the longer input; length mismatch still costs the same loop.
bool keysEqual(const QString &a, const QString &b);
// Value of `name` in a raw Cookie header ("a=1; fff_monitor=abc"), empty when absent.
QString cookieValue(const QByteArray &cookieHeader, const QByteArray &name = "fff_monitor");
QByteArray setCookieHeader(const QString &key); // "fff_monitor=<key>; HttpOnly; SameSite=Strict; Path=/"

enum class Endpoint { MonitorPage, MonitorApi, LocalOnly };
enum class Decision { Allow, Redirect, Deny };

struct Request {
	bool loopback = false;
	Endpoint endpoint = Endpoint::LocalOnly;
	bool enabled = false;
	QString storedKey;
	QString queryKey;
	QString cookieKey;
};

Decision decide(const Request &request);
// True when a key was presented (query or cookie) and none matched; the caller delays a Deny only.
bool presentedWrongKey(const Request &request);
}
```

กติกาของ `decide` ตามลำดับ:
1. `loopback` → `Allow`
2. `endpoint == LocalOnly` → `Deny`
3. `!enabled` หรือ `storedKey` ว่าง → `Deny`
4. `endpoint == MonitorPage` และ `keysEqual(queryKey, storedKey)` → `Redirect`
5. `keysEqual(cookieKey, storedKey)` → `Allow`
6. อื่นๆ → `Deny`

`keysEqual` คืน `false` เมื่อข้างใดข้างหนึ่งว่าง

### 3.3 `FffSession`

- ฟิลด์ใหม่ใน JSON: `monitorLanEnabled` (bool, default `false`), `monitorKey` (string, default ว่าง)
- `bool monitorLanEnabled() const`, `QString monitorKey() const`
- `bool setMonitorLanEnabled(bool enabled)`: ถ้าเปิดและ key ว่าง สร้าง key ก่อน save ถ้า save พัง คืนค่าเดิมทั้งสองฟิลด์และ `emit saveFailed()`
- `bool regenerateMonitorKey()`: สร้าง key ใหม่ save/rollback แบบเดียวกัน
- ทั้งสองฟังก์ชัน emit `changed()` และ signal ใหม่ `monitorAccessChanged()` เมื่อสำเร็จ
- `overlayStateJson()` และ `phoneStateJson()` ห้ามมี key

### 3.4 `FffHttpServer`

**ตาราง endpoint**

| Endpoint | ประเภท |
|---|---|
| `GET /monitor` | `MonitorPage` |
| `GET /api/events/overlay`, `GET /api/monitor/access` (ใหม่), `POST /api/layout`, `POST /api/layer`, `POST /api/operator/vote`, `POST /api/status`, `POST /api/asset`, `POST /api/template` | `MonitorApi` |
| `POST /api/display`, `POST /api/logo` | `LocalOnly` |
| `/`, `/vote`, `/overlay`, ไฟล์ static, `GET /api/cover`, GET asset, `/api/auth`, `/api/vote`, `/api/events` | public ไม่เปลี่ยน |

**การเปลี่ยนแปลง**
1. แทน guard `isLoopback()` 9 จุดใน `route()` ด้วย helper ตัวเดียวที่สร้าง `Request` แล้วเรียก `decide()`
2. อ่าน header `Cookie:` ในลูป parse header เดิม
3. `Redirect`: ตอบ `303` `Location: /monitor` + `Set-Cookie` ตาม `setCookieHeader`
4. `Deny` ที่ `/monitor`: `403` `text/html; charset=utf-8` หน้าไทยสั้นๆ `ต้องเปิดจากลิงก์ใน dock (แท็บ ตั้งค่า → Monitor LAN)` ส่วน API คงข้อความ 403 เดิม
5. ผลเป็น `Deny` และ `presentedWrongKey()` เป็นจริง: หน่วงคำตอบ `kBadPinDelayMs` แบบเดียวกับ `handleAuth` (ใช้ `QPointer` + `QTimer::singleShot`)
6. `GET /api/monitor/access`: `204` เมื่อ `Allow` ไม่งั้น `403`
7. **แก้เพดานขนาดคำขอ:** ตอนนี้ `readFrom()` เลือก `limit` จาก `isLoopback()` ก่อนอ่าน header (บรรทัด 225) ทำให้เครื่อง LAN อัปโหลด PNG ไม่ได้ ของใหม่: ก่อน header ครบใช้ `kMaxRequestBytes` กับทุกคน หลัง parse header แล้ว loopback ได้ `kMaxUploadBytes` เหมือนเดิม เครื่อง LAN ได้ `kMaxUploadBytes` เฉพาะ endpoint ประเภท `MonitorApi` ที่ `decide()` เป็น `Allow` นอกนั้นใช้ `kMaxRequestBytes`
8. `Conn` เพิ่ม `bool remoteMonitor` ตั้งเมื่อ SSE `/api/events/overlay` มาจากเครื่องที่ไม่ใช่ loopback
9. ต่อ `FffSession::monitorAccessChanged` เข้ากับการปิดทุก connection ที่ `remoteMonitor == true`
10. เพิ่ม `int remoteMonitorClientCount() const` และให้ `overlayClientCount()` นับเฉพาะ loopback

### 3.5 หน้า monitor เมื่อสิทธิ์ถูกถอน

`stream.onerror` เรียก `checkAccess()` → `GET /api/monitor/access`
- `403`: แสดง `#accessDenied` สีแดง `กุญแจถูกเปลี่ยนหรือปิดสิทธิ์แล้ว — ขอลิงก์ใหม่จาก operator` และซ่อน `#warn`
- ผลอื่นหรือ network error: แสดง `#warn` เดิม

### 3.6 ความเสี่ยงที่ยอมรับ

- HTTP ไม่เข้ารหัส คนที่ดักแพ็กเก็ตใน Wi-Fi เดียวกันได้จะเห็น key และข้อมูลทั้งหมด README ต้องเขียนข้อนี้ชัดเจน
- ลิงก์พร้อม key ยังอยู่ใน browser history ของเครื่องที่เปิด (URL bar ถูกล้างด้วย redirect) ถ้าส่งต่อลิงก์ ให้กดสุ่มกุญแจใหม่
- cookie ไม่แยกพอร์ต: cookie ของ host นี้จะถูกส่งไปพอร์ตอื่นบนเครื่อง OBS ด้วย แต่ key ใช้ได้เฉพาะเซิร์ฟเวอร์นี้

---

## 4. การจัดการ error

| สถานการณ์ | ผล |
|---|---|
| session บันทึกไม่สำเร็จ | rollback (มีอยู่แล้ว) + ป้าย error ในแถบไลฟ์ |
| import PNG ไม่สำเร็จ | ป้าย error `นำเข้า PNG ไม่สำเร็จ รูปเดิมยังอยู่` |
| เปิดพอร์ตไม่ได้ | ข้อความในกลุ่มเซิร์ฟเวอร์ + ป้ายเหลืองในแถบไลฟ์ + `ตั้งค่า ⚠` |
| ติ๊ก Monitor LAN แล้วบันทึกพัง | checkbox คืนค่า + ป้าย error |
| เปลี่ยนพอร์ต | URL ของ LAN monitor สร้างใหม่ใน `refresh()` |
| monitor บันทึก layout ไม่สำเร็จ | ระบบ pending/retry และ `#saveStatus` เดิม |
| monitor หลุดการเชื่อมต่อ | `#warn` เดิม |
| กุญแจถูกเปลี่ยน/ปิดสิทธิ์ | `#accessDenied` (3.5) |

---

## 5. การทดสอบ

เขียน test ก่อน implementation ทุกข้อ (TDD)

### 5.1 `tests/monitor-access-tests.cpp` (ใหม่, Qt Core)
- `generateKey()` ยาว 32 ตัว เป็น hex ตัวเล็ก สองครั้งไม่ซ้ำกัน
- `keysEqual`: ตรง, ไม่ตรง, ยาวไม่เท่ากัน, ค่าว่าง
- `cookieValue`: cookie เดียว, หลาย cookie, มีช่องว่าง, ชื่อคล้ายกัน (`xfff_monitor`), ไม่มี
- `decide`: ครบทุกแถวของกติกาใน 3.2 รวม `LocalOnly` จาก LAN ที่มี cookie ถูกต้องต้อง `Deny`
- `presentedWrongKey`: กุญแจผิด, ไม่ได้ส่งกุญแจ, กุญแจถูก

### 5.2 `tests/layout-tests.cpp` (เพิ่ม)
- session: เปิด LAN แล้วได้ key, save/load ครบ, `regenerateMonitorKey` เปลี่ยน key, rollback เมื่อเขียนดิสก์พัง (ใช้รูปแบบ failed-write เดิม), key ไม่อยู่ใน `overlayStateJson`/`phoneStateJson`
- server loopback: flow เดิมผ่านทั้งหมด, `/api/monitor/access` = 204, `/monitor-ui.js` เสิร์ฟได้
- server จาก LAN: ต่อผ่าน address แรกของ `FffHttpServer::lanAddresses()` ตรวจ
  - `/monitor` ไม่มี key → 403
  - `/monitor?key=<ถูก>` → 303 + `Set-Cookie`
  - `/monitor` + cookie → 200
  - `/api/display` + cookie → 403
  - ปิดสวิตช์ → cookie เดิมได้ 403
  - SSE ที่เปิดอยู่ถูกปิดเมื่อ `regenerateMonitorKey()`
  - `POST /api/asset` ขนาดเกิน `kMaxRequestBytes` + cookie → ไม่ได้ 413
- ถ้าไม่มี LAN address: พิมพ์ `SKIP: no LAN address` แล้วข้ามเฉพาะชุดนี้

### 5.3 `tests/dock-tests.cpp` (ใหม่, Qt Widgets, `QT_QPA_PLATFORM=offscreen`)
- คอมไพล์ `fff-dock-live.cpp`, `fff-dock-roster.cpp`, `fff-dock-settings.cpp`, `fff-session.cpp`, `fff-http-server.cpp`, `fff-monitor-access.cpp` ไม่รวม `fff-dock.cpp`
- OBS stub ใช้ร่วมกับ `layout-tests.cpp` โดยแยกเป็น `tests/obs-stubs.{h,cpp}`
- `fffSetConfirmOverride` ตอบอัตโนมัติ
- กรณีทดสอบ:
  - ป้ายสถานะ: ข้อความและสีของ จอว่าง / ออกอากาศแต่ละโหมด / เซิร์ฟเวอร์ปิด
  - สรุปเปลี่ยนเป็นเขียวเมื่อโหวตครบและยังไม่ออกอากาศ
  - กดโหมดที่ออกอากาศอยู่ → phase ยัง `Revealed` และปุ่มยัง checked
  - `ซ่อนจอ` → `Collecting` และ disabled
  - `เริ่มรอบใหม่` override ตอบไม่ → รอบเดิม, ตอบใช่ → รอบ +1
  - ไม่เลือกแถว → `PNG ▾`, `⋯ ▾` disabled; เลือกแถวที่ไม่มี BAR → เมนูลบ BAR disabled
  - `ลบนายก…` override ตอบไม่ → รายชื่อไม่เปลี่ยน
  - checkbox Monitor LAN คืนค่าเมื่อ save พัง
  - แท็บตั้งค่ามี ⚠ เมื่อเซิร์ฟเวอร์ไม่ฟังพอร์ต

### 5.4 Browser tests (puppeteer-core)
- แก้ `tests/overlay-layout.cjs`: `select("#editMode", v)` → คลิก radio, เปิดแท็บก่อนกดปุ่มในแท็บ, `#resetAll` กด 2 ครั้ง, fixture server เสิร์ฟ `monitor-ui.js`
- แก้ `tests/bottom-bar-cover.cjs` บรรทัดที่ตั้ง `#editMode.value` → ติ๊ก radio แล้ว dispatch `change`
- ใหม่ `tests/monitor-ui.cjs`:
  - แถบบนยังอยู่ใน viewport หลังเลื่อนหน้าลงสุด
  - ปุ่มธง disabled เมื่อไม่ได้เลือกการ์ด; เลือกการ์ดแล้ว `canvas.getBoundingClientRect().top` ไม่เปลี่ยน
  - `#airWarning` แสดงเมื่อ fixture ส่ง `phase: "revealed"` + `displayMode` ตรงกับโหมดที่แก้ และหายเมื่อสลับโหมด
  - `#resetAll`: คลิกแรกไม่มี request, คลิกสองภายใน 3 วินาทีมี request, รอเกิน 3 วินาทีข้อความกลับเดิม
  - แท็บตาราง disabled ในโหมด BOTTOM BAR และสลับไปแท็บตำแหน่งถ้าเปิดค้างอยู่
  - เปิดแท็บแม่แบบแล้ว `#templateZoomValue` ไม่ใช่ `0%`
  - แท็บล่าสุดถูกจำหลัง reload
  - fixture ตอบ `/api/monitor/access` 403 หลังตัด SSE → `#accessDenied` แสดง
  - viewport กว้าง 900px: `document.documentElement.scrollWidth <= innerWidth`

### 5.5 Manual smoke ใน OBS (checklist ใน README)
- dock กว้าง 320px อ่านได้ ไม่มีปุ่มถูกตัด
- ขึ้นจอ / ซ่อนจอ / เริ่มรอบใหม่ พร้อมกล่องยืนยัน
- เปิด monitor จากเครื่องที่สองผ่านลิงก์ใน dock, สุ่มกุญแจใหม่แล้วเครื่องนั้นขึ้นป้ายแดง
- เปิด monitor เป็น Custom Browser Dock ใน OBS แล้วใช้ยืนยัน 2 จังหวะ

### 5.6 Format
- clang-format ไฟล์ C++ ที่แตะ (CI มี check-format)

---

## 6. ลำดับงาน

1. `fff-monitor-access` + test
2. session fields + test
3. server: guard, cookie, redirect, access endpoint, upload limit, ตัด connection + test
4. แยกไฟล์ dock + `fff-dock-ui.h` + dock-tests
5. UI dock ใหม่ (แถบไลฟ์, แท็บ, เมนู, กล่องยืนยัน, Monitor LAN)
6. monitor: `monitor-ui.js`, markup, CSS, แก้ test เดิม, `monitor-ui.cjs`
7. README (dock, Monitor LAN, ข้อจำกัดความปลอดภัย, smoke checklist) และ `tests/README.md` (dock-tests, monitor-access-tests, monitor-ui.cjs)

# OBS_Plugin_FFF — FFF Tools for OBS

Plugin ของสโมสรนักศึกษา มฟล. สร้างจาก [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)

มีอะไรข้างใน (v0.1.0):
- **FFF Flag Board** — dock คุมกระดานผลการออกธงของนายกแต่ละสำนักวิชา คนดูกดสีธงจากมือถือ ผลขึ้นจอสตรีมผ่าน Browser Source: เมนู Docks → FFF Flag Board
- **FFF Tint** — video filter ย้อมสีภาพ (ตัวอย่างโครง filter): คลิกขวาที่ source → Filters → + → FFF Tint

## Flag Board ทำงานยังไง

```
มือถือคนดู (n เครื่อง)               Mac ที่รัน OBS
┌──────────────┐  POST /api/vote  ┌─────────────────────────┐
│ รูปนายก      │ ───────────────> │  HTTP server ในปลั๊กอิน  │
│ [แดง][เขียว] │ <──── SSE ────── │  + สถานะรอบปัจจุบัน       │
└──────────────┘                  └─────────────────────────┘
                                             │ SSE
                              Browser Source ▼ http://127.0.0.1:9779/overlay
```

- ปลั๊กอินเปิด HTTP server เองบนเครื่อง Mac คนดูเข้าเว็บที่ปลั๊กอินเสิร์ฟ ใส่ PIN ของตัวเอง แล้วกดสีธง
- ผลจะ **ไม่ขึ้นจอจนกว่าทุกคนจะกดครบ** แล้วเปิดพร้อมกัน หรือกด Force reveal ที่ dock ถ้ามีคนไม่กด (ช่องนั้นจะขึ้น "ไม่มีข้อมูล")
- หลังผลขึ้นแล้ว คนดูยังเปลี่ยนสีได้ จอเปลี่ยนตามทันที
- กด **Clear** ที่ dock เพื่อเริ่มรอบใหม่ — รายชื่อ รูป และ PIN ยังอยู่ครบ

ทนหน้างานยังไง:
- โหวตเป็น **idempotent** (มือถือส่ง "สีปัจจุบัน" ไม่ใช่ "เหตุการณ์") ยิงซ้ำกี่ครั้งผลเท่าเดิม
- เน็ตหลุดตอนกด มือถือเก็บไว้แล้วส่งเองเมื่อกลับมา ไม่ต้อง refresh
- OBS ปิด-เปิดใหม่ มือถือ sign-in เองด้วย PIN ที่จำไว้ ไม่เด้งกลับหน้า PIN
- SSE ส่ง heartbeat ทุก 15 วินาที และ `EventSource` reconnect เอง

> ใช้ SSE ไม่ใช่ WebSocket เพราะ obs-deps ไม่ได้ build `qtwebsockets` (มีแค่ qtbase/imageformats/shadertools/multimedia/svg/tools) — `Qt6::Network` ใน qtbase พอสำหรับ QTcpServer อยู่แล้ว

## ตั้งค่าก่อนงาน

1. เปิด OBS → Docks → **FFF Flag Board**
2. กด **เพิ่ม** ทีละคนจนครบทุกนายก แก้ชื่อ/สำนักวิชาในตารางได้เลย เลือกแถวแล้วกด **เลือกรูป** เพื่อใส่รูป (รูปถูก copy เข้า config ของปลั๊กอิน ไฟล์ต้นทางย้ายได้)
3. PIN สุ่มให้อัตโนมัติ 6 หลัก กด **สุ่ม PIN** ถ้าอยากเปลี่ยน — แจก PIN ให้คนดูคนละตัว
4. เพิ่ม **Browser Source** ชี้ `http://127.0.0.1:9779/overlay`
   **สำคัญ:** ปิด "Shutdown source when not visible" และ "Refresh browser when scene becomes active" ทั้งคู่ ไม่งั้น SSE จะโดนตัดตอนสลับ scene
5. คนดูเปิดเบราว์เซอร์มือถือไปที่ `http://<IP ของ Mac>:9779` (dock บอก IP ให้)

Checklist หน้างาน:
- **Mac ห้ามหลับ** — `caffeinate -d` หรือตั้ง Energy Saver
- ทดสอบบน router ตัวที่จะใช้จริง ไม่ใช่ Wi-Fi บ้าน — Wi-Fi สาธารณะบางที่เปิด client isolation แล้วมือถือจะคุยกับ Mac ไม่ได้
- ตั้ง DHCP reservation ให้ Mac ไม่งั้น IP เปลี่ยนแล้วคนดูเข้าไม่ได้
- บอกคนดูให้ตั้งมือถือไม่ให้ล็อกหน้าจอ (หน้าเว็บขอ wake lock ให้แล้ว แต่ไม่ใช่ทุกเบราว์เซอร์รองรับ)

ข้อจำกัดที่ต้องรู้: ไม่มี TLS และเป็น LAN ปิดของทีม ใครอยู่ในวงเดียวกันแล้วเดา PIN ถูกก็โหวตแทนได้ — PIN สุ่ม 6 หลัก และคำขอที่ PIN ผิดถูกหน่วง 1 วินาที หน้า overlay เสิร์ฟให้เฉพาะ 127.0.0.1 เพราะมันเห็นผลก่อนเปิด

## Build บน macOS (เครื่องพัฒนา)

ต้องมี: Xcode 16 ตัวเต็มจาก App Store (ไม่ใช่แค่ Command Line Tools), CMake (`brew install cmake`), OBS Studio

```sh
# ครั้งแรก หรือหลังแก้ CMakeLists/buildspec/เพิ่มไฟล์ใน data/ (ดาวน์โหลด libobs+Qt ครั้งแรกจะนานหน่อย)
cmake --preset macos

# ทุกครั้งที่แก้โค้ด: build + ติดตั้งลง OBS + เซ็น ad-hoc ในคำสั่งเดียว
./dev-install.sh
```

แล้วปิด-เปิด OBS ใหม่ ดูใน log (Help → Log Files → View Current Log) ต้องเจอ:

```
[fff-tools] plugin loaded successfully (version 0.1.0)
[fff-tools] flag board listening on port 9779
```

ถ้า cmake บ่นว่าไม่พบ generator "Xcode": `sudo xcode-select -s /Applications/Xcode.app`

`data/` ถูก glob ตอน configure — เพิ่มไฟล์ใหม่ใน `data/web/` ต้องรัน `cmake --preset macos` ซ้ำ แก้เนื้อไฟล์เดิมไม่ต้อง

## Build OS อื่น

- **Windows**: `cmake --preset windows-x64` แล้ว `cmake --build --preset windows-x64 --config RelWithDebInfo` (ต้อง VS2022 + Windows SDK 10.0.22621)
- **Ubuntu 24.04**: `cmake --preset ubuntu-x86_64` แล้ว `cmake --build --preset ubuntu-x86_64`
- หรือ push ขึ้น GitHub แล้วให้ Actions build ทั้ง 3 OS ให้ (ดูแท็บ Actions → Artifacts)

## โครงโค้ด

- `src/plugin-main.c` — จุดเริ่ม ลงทะเบียน filter + dock
- `src/fff-session.h/.cpp` — รายชื่อนายก, โหวตของรอบปัจจุบัน, state machine, โหลด-เซฟ `session.json`
- `src/fff-http-server.h/.cpp` — HTTP + SSE server (QTcpServer) เสิร์ฟหน้าเว็บ รับโหวต push สถานะ
- `src/fff-dock.cpp` — dock คุมงาน (เซิร์ฟเวอร์ / รายชื่อนายก / รอบปัจจุบัน)
- `data/web/` — `phone.html` (หน้าคนดู), `overlay.html` (หน้าจอสตรีม), `app.css`
- `src/tint-filter.c` — ตัวอย่าง video filter (คู่กับ `data/tint.effect` ซึ่งเป็น shader)
- `buildspec.json` — ชื่อ/เวอร์ชัน plugin และเวอร์ชัน OBS ที่ build ด้วย

โค้ด C/C++ ใช้ tab (clang-format, IndentWidth 8) — `dock`, `session`, `http-server` compile เฉพาะตอน `ENABLE_QT` และ `ENABLE_FRONTEND_API` เปิดทั้งคู่ (preset เปิดให้แล้ว)

ข้อมูลตอนใช้งานเก็บที่ `~/Library/Application Support/obs-studio/plugin_config/fff-tools/` (`session.json` + `photos/`)

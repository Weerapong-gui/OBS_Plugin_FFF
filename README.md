# OBS_Plugin_FFF — FFF Tools for OBS

Plugin ของสโมสรนักศึกษา มฟล. สร้างจาก [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)

มีอะไรข้างใน (v0.1.0):
- **FFF Tint** — video filter ย้อมสีภาพ (ตัวอย่างโครง filter): คลิกขวาที่ source → Filters → + → FFF Tint
- **FFF Control** — dock แผงควบคุม (สลับ scene ตามชื่อ, เช็คสถานะสตรีม): เมนู Docks → FFF Control

## Build บน macOS (เครื่องพัฒนา)

ต้องมี: Xcode 16 ตัวเต็มจาก App Store (ไม่ใช่แค่ Command Line Tools), CMake (`brew install cmake`), OBS Studio

```sh
# ครั้งแรก หรือหลังแก้ CMakeLists/buildspec (ดาวน์โหลด libobs+Qt ครั้งแรกจะนานหน่อย)
cmake --preset macos

# ทุกครั้งที่แก้โค้ด: build + ติดตั้งลง OBS + เซ็น ad-hoc ในคำสั่งเดียว
./dev-install.sh
```

แล้วปิด-เปิด OBS ใหม่ ดูใน log (Help → Log Files → View Current Log) ต้องเจอ:

```
[fff-tools] plugin loaded successfully (version 0.1.0)
```

ถ้า cmake บ่นว่าไม่พบ generator "Xcode": `sudo xcode-select -s /Applications/Xcode.app`

## Build OS อื่น

- **Windows**: `cmake --preset windows-x64` แล้ว `cmake --build --preset windows-x64 --config RelWithDebInfo` (ต้อง VS2022 + Windows SDK 10.0.22621)
- **Ubuntu 24.04**: `cmake --preset ubuntu-x86_64` แล้ว `cmake --build --preset ubuntu-x86_64`
- หรือ push ขึ้น GitHub แล้วให้ Actions build ทั้ง 3 OS ให้ (ดูแท็บ Actions → Artifacts)

## โครงโค้ด

- `src/plugin-main.c` — จุดเริ่ม ลงทะเบียน filter + dock
- `src/tint-filter.c` — ตัวอย่าง video filter (คู่กับ `data/tint.effect` ซึ่งเป็น shader)
- `src/fff-dock.cpp` — dock แบบ Qt ผ่าน frontend API
- `data/locale/en-US.ini` — ข้อความใน UI
- `buildspec.json` — ชื่อ/เวอร์ชัน plugin และเวอร์ชัน OBS ที่ build ด้วย

คู่มือฉบับเต็ม (ไทย): ดู artifact "OBS Plugin FFF Playbook" ในโปรเจ็ค Claude

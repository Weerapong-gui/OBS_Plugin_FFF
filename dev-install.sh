#!/bin/zsh
# Build + install ลง OBS บนเครื่องนี้ (macOS) ในคำสั่งเดียว
# ครั้งแรกต้อง configure ก่อน:  cmake --preset macos
set -e

cmake --build --preset macos --config RelWithDebInfo

PLUGIN="fff-tools.plugin"
BUILT=$(find build_macos -name "$PLUGIN" -type d | head -1)
if [[ -z "$BUILT" ]]; then
  echo "หา $PLUGIN ไม่เจอใน build_macos/ — build ล้มเหลว?"
  exit 1
fi

DEST="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$DEST"
rm -rf "$DEST/$PLUGIN"
cp -R "$BUILT" "$DEST/"

# เซ็นแบบ ad-hoc ให้ macOS ยอมโหลด (สำหรับใช้เอง)
codesign --force --deep --sign - "$DEST/$PLUGIN"

echo "ติดตั้งแล้วที่: $DEST/$PLUGIN"
echo "ปิด OBS แล้วเปิดใหม่ จากนั้นดู log: Help > Log Files > View Current Log"

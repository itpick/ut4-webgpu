#!/usr/bin/env bash
# Serve the UT wasm bundle (COOP/COEP) and boot it in headless Chrome via CDP,
# capturing the console. Runs in the foreground; the caller keeps the SSH
# session alive (background task) so these stay up.
set -u
BOOT=/mnt/models/ss-build/ut-boot
CON=/mnt/models/ss-build/ut-boot-console.log
SRV=/mnt/models/ss-build/ut-serve.log
CHROME=/home/lucas/.cache/ms-playwright/chromium-1228/chrome-linux64/chrome
REPO=/home/lucas/workspace/ut4-webgpu-push

pkill -f 'serve.py 8802' 2>/dev/null
pkill -f 'chrome-linux64/chrome' 2>/dev/null
sleep 2

cd "$BOOT"
python3 "$REPO/host/serve.py" 8802 "$BOOT" > "$SRV" 2>&1 &
SERVE_PID=$!
sleep 3
echo "SERVE_HTTP=$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8802/ut-shim.html)"

# NixOS box: the cached Playwright Chrome needs a nix-shell-derived LD_LIBRARY_PATH
# (libcairo/libglib etc.) or it won't even start.
export LD_LIBRARY_PATH="$(cat /tmp/chrome_ld_library_path.txt)"
# Update 11: headless Chrome's ANGLE Vulkan fails here (no VK_KHR_surface);
# NO_ANGLE_VULKAN=1 makes cdp_capture drop --use-angle=vulkan so WebGPU/Dawn
# uses the working path (that's how DawnRHITest got its adapter).
export NO_ANGLE_VULKAN=1
# 240s in-page wait for the 673MB memory64 wasm to download+compile+boot.
node "$REPO/tools/cdp_capture.mjs" "$CHROME" http://127.0.0.1:8802/ut-shim.html 240000 > "$CON" 2>&1
echo "CDP_DONE exit=$?"
kill "$SERVE_PID" 2>/dev/null
echo "CONSOLE_LINES=$(wc -l < "$CON")"

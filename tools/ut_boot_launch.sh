#!/usr/bin/env bash
# Serve the UT wasm bundle (COOP/COEP) and boot it in headless Chrome via CDP,
# capturing the console. Runs in the foreground; the caller keeps the SSH
# session alive (background task) so these stay up.
set -u
BOOT=/mnt/vms/ss-build/ut-boot
CON=/mnt/vms/ss-build/ut-boot-console.log
SRV=/mnt/vms/ss-build/ut-serve.log
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
# Update 17: point Dawn's Vulkan backend (used even without --use-angle=vulkan,
# for adapter enumeration) at the real NVIDIA ICD instead of falling through to
# "Found no drivers" -> SwiftShader software fallback. Box has a real RTX 5070
# (nvidia-x11-595.84) but no /usr/share/vulkan/icd.d on this NixOS box.
export VK_ICD_FILENAMES=/nix/store/j4g4xm1x9551kk36pa9cb1pr3nc2rvmw-nvidia-x11-595.84/share/vulkan/icd.d/nvidia_icd.json
export VK_DRIVER_FILES="$VK_ICD_FILENAMES"

# Update 17: 673MB memory64 wasm needs real time to download+compile+run in
# headless Chrome, especially over software Vulkan/SwiftShader if the ICD
# above still doesn't take. Default raised from 240s to 720s (12 min). Pass a
# different value as $1 to override.
WAIT_MS="${1:-720000}"

# cdp_capture.mjs now streams each console/network line as it happens
# (flushed writes to this same redirected file), so this log can be
# `tail -f`'d / polled DURING the run instead of only after it exits.
node "$REPO/tools/cdp_capture.mjs" "$CHROME" http://127.0.0.1:8802/ut-shim.html "$WAIT_MS" > "$CON" 2>&1
echo "CDP_DONE exit=$?"
kill "$SERVE_PID" 2>/dev/null
echo "CONSOLE_LINES=$(wc -l < "$CON")"

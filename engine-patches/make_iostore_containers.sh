#!/usr/bin/env bash
# Build IoStore .utoc/.ucas containers from the LOOSE cook (menu subset) so the
# standalone runtime's AsyncLoading2 can mount + load content. Recipe mirrors
# UAT CopyBuildToStagingDirectory RunIoStore/GetIoStoreCommandArguments.
set -uo pipefail
UE=/mnt/vms/ss-build/UnrealEngine
COOK="$UE/UnrealTournament/Saved/Cooked/SimplyStream"
META="$COOK/UnrealTournament/Metadata"
OUT=/mnt/vms/ss-build/ut-iostore
UPAK="$UE/Engine/Binaries/Linux/UnrealPak"
PROJ="$UE/UnrealTournament/UnrealTournament.uproject"
mkdir -p "$OUT"
RESP="$OUT/PakListIoStore_utcontent.txt"
CMDS="$OUT/IoStoreCommands.txt"

# menu subset: Engine cooked content (defaults incl WorldGridMaterial) + UT UI/Fonts/Maps.
# Excludes the 3.6G Character/Weapons/Environments/Proto/Effects bulk to stay tab-loadable.
: > "$RESP"
add_dir() {  # <cook-relative dir>
  local d="$1"
  find "$COOK/$d" -type f \( -name '*.uasset' -o -name '*.uexp' -o -name '*.ubulk' -o -name '*.umap' -o -name '*.uptnl' \) 2>/dev/null | while read -r f; do
    local rel="${f#$COOK/}"
    printf '"%s" "../../../%s" -compress\n' "$f" "$rel" >> "$RESP"
  done
}
add_dir "Engine/Content"
add_dir "UnrealTournament/Content/RestrictedAssets/UI"
add_dir "UnrealTournament/Content/RestrictedAssets/Fonts"
add_dir "UnrealTournament/Content/RestrictedAssets/SlateLargeImages"
add_dir "UnrealTournament/Content/RestrictedAssets/Maps"
add_dir "UnrealTournament/Content/RestrictedAssets/Blueprints"
echo "response file entries: $(wc -l < "$RESP")"

printf -- '-Output=%s/utcontent.utoc -ContainerName=utcontent -ResponseFile=%s\n' "$OUT" "$RESP" > "$CMDS"

echo "=== running UnrealPak IoStore container creation ==="
nix-shell /mnt/vms/ss-build/ue-cook-shell.nix --run "$UPAK $PROJ \
  -CreateGlobalContainer=$OUT/global.utoc \
  -PackageStoreManifest=$META/packagestore.manifest \
  -ScriptObjects=$META/scriptobjects.bin \
  -CookedDirectory=$COOK \
  -Commands=$CMDS \
  -unattended" 2>&1 | tail -30
echo "=== produced containers ==="
ls -la "$OUT"/*.utoc "$OUT"/*.ucas 2>/dev/null

p="/mnt/vms/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Source/Programs/UnrealBuildTool/SimplyStreamToolChain.cs"
s=open(p).read()
if "UnrealTournament.uproject@" in s:
    print("already present"); raise SystemExit
marker="icudt64l@Engine/Content/Internationalization/icudt64l"
idx=s.find(marker)
assert idx!=-1, "ICU preload line not found"
eol=s.find("\n", idx)
add=(
    "\n\t\t\t// wndr: preload the .uproject so PreInit project-descriptor load finds it in MEMFS\n"
    "\t\t\tResult += \" --preload-file \\\"/mnt/vms/ss-build/UnrealEngine/UnrealTournament/UnrealTournament.uproject@UnrealTournament/UnrealTournament.uproject\\\"\";"
)
s=s[:eol+1]+add+s[eol+1:]
open(p,"w").write(s)
print("inserted uproject preload")

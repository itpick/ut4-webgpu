#!/usr/bin/env python3
# Bridge fix (general): strip SPV_EXT_shader_viewport_index_layer so ANY shader
# emitting SV_RenderTargetArrayIndex/SV_ViewportArrayIndex from a VS (direct or
# via instanced-stereo FStereoVSOutput) survives tint's SPIR-V reader (which
# rejects that extension outright). We drop the OpExtension + OpCapability
# ShaderViewportIndexLayerEXT(5254) and rewrite the BuiltIn Layer(9)/
# ViewportIndex(10) output decoration to a free Location + Flat (a plain flat
# int varying). WebGPU has no VS layer output; layered passes are unused on the
# menu path. Runs in dawn_tint_bridge.cpp before SpirvToWgsl.
import io, sys
F = "/mnt/vms/ss-build/ut4-webgpu-push/tools/dawn_tint_bridge.cpp"
src = io.open(F, encoding="utf-8").read()

if "StripViewportIndexLayer" in src:
    print("already patched")
    sys.exit(0)

FUNC = (
"\tvoid StripViewportIndexLayer(std::vector<uint32_t>& Spirv)\n"
"\t{\n"
"\t\tif (Spirv.size() < 5) return;\n"
"\t\tconst uint32_t OpExtensionOp = 10, OpCapabilityOp = 17, OpDecorateOp = 71;\n"
"\t\tconst uint32_t DecoBuiltIn = 11, DecoLocation = 30, DecoFlat = 14;\n"
"\t\tconst uint32_t BuiltInLayer = 9, BuiltInViewportIndex = 10;\n"
"\t\tconst uint32_t CapViewportIndexLayerEXT = 5254;\n"
"\t\t// Pass 1: collect BuiltIn Layer/ViewportIndex target ids + highest Location in use.\n"
"\t\tstd::vector<uint32_t> Targets; uint32_t MaxLoc = 0; bool AnyLoc = false;\n"
"\t\tsize_t i = 5;\n"
"\t\twhile (i < Spirv.size()) {\n"
"\t\t\tconst uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;\n"
"\t\t\tif (wc == 0 || i + wc > Spirv.size()) break;\n"
"\t\t\tif (op == OpDecorateOp && wc >= 4) {\n"
"\t\t\t\tconst uint32_t deco = Spirv[i + 2];\n"
"\t\t\t\tif (deco == DecoBuiltIn && (Spirv[i + 3] == BuiltInLayer || Spirv[i + 3] == BuiltInViewportIndex))\n"
"\t\t\t\t\tTargets.push_back(Spirv[i + 1]);\n"
"\t\t\t\telse if (deco == DecoLocation) { AnyLoc = true; if (Spirv[i + 3] > MaxLoc) MaxLoc = Spirv[i + 3]; }\n"
"\t\t\t}\n"
"\t\t\ti += wc;\n"
"\t\t}\n"
"\t\tif (Targets.empty()) return;\n"
"\t\tuint32_t NextLoc = AnyLoc ? MaxLoc + 1u : 0u;\n"
"\t\tstd::unordered_map<uint32_t, uint32_t> TargetLoc;\n"
"\t\tfor (uint32_t t : Targets) if (!TargetLoc.count(t)) TargetLoc[t] = NextLoc++;\n"
"\t\t// Pass 2: rebuild, dropping the ext/cap and rewriting the builtin decorations.\n"
"\t\tstd::vector<uint32_t> Out; Out.reserve(Spirv.size() + Targets.size() * 4);\n"
"\t\tOut.insert(Out.end(), Spirv.begin(), Spirv.begin() + 5);\n"
"\t\ti = 5;\n"
"\t\twhile (i < Spirv.size()) {\n"
"\t\t\tconst uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;\n"
"\t\t\tif (wc == 0 || i + wc > Spirv.size()) { Out.insert(Out.end(), Spirv.begin() + i, Spirv.end()); break; }\n"
"\t\t\tbool drop = false;\n"
"\t\t\tif (op == OpCapabilityOp && wc == 2 && Spirv[i + 1] == CapViewportIndexLayerEXT) drop = true;\n"
"\t\t\tif (op == OpExtensionOp) {\n"
"\t\t\t\tstd::string s; bool done = false;\n"
"\t\t\t\tfor (size_t k = i + 1; k < i + wc && !done; ++k) {\n"
"\t\t\t\t\tconst uint32_t word = Spirv[k];\n"
"\t\t\t\t\tfor (int b = 0; b < 4; ++b) { const char c = (char)((word >> (b * 8)) & 0xFFu); if (!c) { done = true; break; } s.push_back(c); }\n"
"\t\t\t\t}\n"
"\t\t\t\tif (s == \"SPV_EXT_shader_viewport_index_layer\") drop = true;\n"
"\t\t\t}\n"
"\t\t\tif (op == OpDecorateOp && wc >= 4 && Spirv[i + 2] == DecoBuiltIn) {\n"
"\t\t\t\tauto it = TargetLoc.find(Spirv[i + 1]);\n"
"\t\t\t\tif (it != TargetLoc.end() && (Spirv[i + 3] == BuiltInLayer || Spirv[i + 3] == BuiltInViewportIndex)) {\n"
"\t\t\t\t\tOut.push_back((4u << 16) | OpDecorateOp); Out.push_back(Spirv[i + 1]); Out.push_back(DecoLocation); Out.push_back(it->second);\n"
"\t\t\t\t\tOut.push_back((3u << 16) | OpDecorateOp); Out.push_back(Spirv[i + 1]); Out.push_back(DecoFlat);\n"
"\t\t\t\t\ti += wc; continue;\n"
"\t\t\t\t}\n"
"\t\t\t}\n"
"\t\t\tif (!drop) Out.insert(Out.end(), Spirv.begin() + i, Spirv.begin() + i + wc);\n"
"\t\t\ti += wc;\n"
"\t\t}\n"
"\t\tSpirv = std::move(Out);\n"
"\t}\n\n"
)

anchor_fn = "\tvoid LegalizeNonFiniteConstants(std::vector<uint32_t>& Spirv)\n\t{"
if anchor_fn not in src:
    print("FUNC ANCHOR NOT FOUND", file=sys.stderr); sys.exit(1)
src = src.replace(anchor_fn, FUNC + anchor_fn, 1)

anchor_call = "\tLegalizeNonFiniteConstants(Spirv);\n\tStripEarlyFragmentTests(Spirv);\n"
if anchor_call not in src:
    print("CALL ANCHOR NOT FOUND", file=sys.stderr); sys.exit(1)
src = src.replace(anchor_call, anchor_call + "\tStripViewportIndexLayer(Spirv);\n", 1)

io.open(F, "w", encoding="utf-8").write(src)
print("patched: added StripViewportIndexLayer + call")

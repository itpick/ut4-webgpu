p="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnDynamicRHI.cpp"
s=open(p).read()
# Extract the WGSL variable name for each binding (identifier after var<...>/var, before ':')
# so runtime bind-group construction can match static UBs (View/Primitive/SlateView) by name.
old="\t\tif (!bDup) { FDawnShaderBinding NB; NB.Group = Group; NB.Binding = Binding; NB.Kind = Kind; Out.Add(NB); }"
new=("\t\t// Variable name: the identifier after 'var'/'var<...>' and before ':'.\n"
     "\t\tFString VarName;\n"
     "\t\t{\n"
     "\t\t\tint32 VarPos = Decl.Find(TEXT(\"var\"), ESearchCase::CaseSensitive);\n"
     "\t\t\tif (VarPos != INDEX_NONE)\n"
     "\t\t\t{\n"
     "\t\t\t\tint32 Q = VarPos + 3;\n"
     "\t\t\t\tif (Q < Decl.Len() && Decl[Q] == TEXT('<')) { int32 C = Decl.Find(TEXT(\">\"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q); if (C != INDEX_NONE) { Q = C + 1; } }\n"
     "\t\t\t\twhile (Q < Decl.Len() && FChar::IsWhitespace(Decl[Q])) { ++Q; }\n"
     "\t\t\t\tint32 NameStart = Q;\n"
     "\t\t\t\twhile (Q < Decl.Len() && (FChar::IsAlnum(Decl[Q]) || Decl[Q] == TEXT('_'))) { ++Q; }\n"
     "\t\t\t\tVarName = Decl.Mid(NameStart, Q - NameStart);\n"
     "\t\t\t}\n"
     "\t\t}\n"
     "\t\tif (!bDup) { FDawnShaderBinding NB; NB.Group = Group; NB.Binding = Binding; NB.Kind = Kind; NB.Name = VarName; Out.Add(NB); }")
assert s.count(old)==1, ("anchor", s.count(old))
s=s.replace(old,new)
open(p,"w").write(s)
print("ParseWgslBindings now extracts binding names")

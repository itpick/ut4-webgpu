p="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnCommandContext.cpp"
s=open(p).read()
old='if (Entries.Num() != LG.Value.Num()) { static uint32 _bi=0; if(_bi++<24) UE_LOG(LogDawnRHI, Warning, TEXT("BGINCOMPLETE grp=%u have=%d need=%d"), Group, Entries.Num(), LG.Value.Num()); continue; }'
new=('if (Entries.Num() != LG.Value.Num())\n'
     '\t\t{\n'
     '\t\t\tstatic uint32 _bi=0;\n'
     '\t\t\tif (_bi++ < 20)\n'
     '\t\t\t{\n'
     '\t\t\t\tUE_LOG(LogDawnRHI, Warning, TEXT("BGINCOMPLETE grp=%u have=%d need=%d staticUB=%d"), Group, Entries.Num(), LG.Value.Num(), StaticUBsByName.Num());\n'
     '\t\t\t\tfor (const FDawnShaderBinding& B : LG.Value)\n'
     '\t\t\t\t{\n'
     '\t\t\t\t\tbool bHave = (Pending && Pending->Find(B.Binding));\n'
     '\t\t\t\t\tUE_LOG(LogDawnRHI, Warning, TEXT("   BGImiss bind=%u kind=%d name=%s filled=%d"), B.Binding, (int)B.Kind, *B.Name, bHave?1:0);\n'
     '\t\t\t\t}\n'
     '\t\t\t}\n'
     '\t\t\tcontinue;\n'
     '\t\t}')
assert s.count(old)==1, ("anchor", s.count(old))
s=s.replace(old,new)
open(p,"w").write(s)
print("BGINCOMPLETE now logs missing binding names")

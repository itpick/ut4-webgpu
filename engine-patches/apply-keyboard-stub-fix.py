p="/mnt/vms/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Source/Runtime/ApplicationCore/Private/SimplyStreamApplication.cpp"
s=open(p).read()
old=('EM_BOOL key_callback_browser_thread(int eventType, const EmscriptenKeyboardEvent *e, void *userData) {\n'
     '\treturn false;\n'
     '}')
new=('EM_BOOL key_callback_browser_thread(int eventType, const EmscriptenKeyboardEvent *e, void *userData) {\n'
     '\t// Was a return-false stub -> keyboard never reached Slate (could not type/login).\n'
     '\t// Forward to the ue4 thread via the queue (proxyKeyboardEvent copies into its ring).\n'
     '\t// Return false (do NOT preventDefault) so the browser still emits keypress events,\n'
     '\t// which carry the character for text entry (OnKeyChar).\n'
     '\tproxyKeyboardEvent(eventType, const_cast<EmscriptenKeyboardEvent*>(e));\n'
     '\treturn false;\n'
     '}')
assert s.count(old)==1, ("kbd stub", s.count(old))
s=s.replace(old,new)
open(p,"w").write(s)
print("keyboard stub fixed -> proxyKeyboardEvent")

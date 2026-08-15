p="/mnt/vms/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Source/Runtime/ApplicationCore/Private/SimplyStreamApplication.cpp"
s=open(p).read()

# Fix 1: mouse_callback_browser_thread was a no-op stub -> forward to proxyMouseEvent
# (pushes onto event_queue, same as wheel/keyboard) so mouse events reach the ue4 thread.
old_stub=('static EM_BOOL mouse_callback_browser_thread(int eventType, const EmscriptenMouseEvent *e, void *userData) {\n'
          '\treturn false;\n'
          '}')
new_stub=('static EM_BOOL mouse_callback_browser_thread(int eventType, const EmscriptenMouseEvent *e, void *userData) {\n'
          '\t// Forward to the ue4 thread via the event queue (proxyMouseEvent copies e into its\n'
          '\t// own ring buffer, so passing the stack event is safe). Was a return-false stub ->\n'
          '\t// mouse input never reached Slate. Drained by do_events() in PollGameDeviceState.\n'
          '\tproxyMouseEvent(eventType, const_cast<EmscriptenMouseEvent*>(e));\n'
          '\treturn pointerlockIsActive || (!pointerlockIsActive && e->targetX >= 0 && e->targetY >= 0 && e->targetX < canvas_css_w && e->targetY < canvas_css_h);\n'
          '}')
assert s.count(old_stub)==1, ("stub", s.count(old_stub))
s=s.replace(old_stub,new_stub)

# Fix 2: do_events() (the queue drain) was defined but NEVER called -> drain it each frame
# on the ue4/game thread at the top of PollGameDeviceState.
old_poll=('void FSimplyStreamApplication::PollGameDeviceState( const float TimeDelta ) {\n'
          '\tInputInterface->SendControllerEvents();')
new_poll=('void FSimplyStreamApplication::PollGameDeviceState( const float TimeDelta ) {\n'
          '\tdo_events(); // drain queued browser input (mouse/keyboard/wheel) into Slate\n'
          '\tInputInterface->SendControllerEvents();')
assert s.count(old_poll)==1, ("poll", s.count(old_poll))
s=s.replace(old_poll,new_poll)

# Fix 3: OnMouseEvent MOUSEMOVE only sent relative motion; set the ABSOLUTE cursor position
# (canvas-relative targetX/targetY, 1:1 with the 1280x720 render) so click hit-testing lands
# on the right widget. + diagnostics to verify the whole path.
old_move=('\t\t\tif (bMouseMove) {\n'
          '\t\t\t\tMessageHandler->OnRawMouseMove(mouseEvent->movementX, mouseEvent->movementY);\n'
          '\t\t\t\tMessageHandler->OnMouseMove();')
new_move=('\t\t\tif (bMouseMove) {\n'
          '\t\t\t\tif (Cursor.IsValid()) { Cursor->SetPosition((int32)mouseEvent->targetX, (int32)mouseEvent->targetY); }\n'
          '\t\t\t\tMessageHandler->OnRawMouseMove(mouseEvent->movementX, mouseEvent->movementY);\n'
          '\t\t\t\tMessageHandler->OnMouseMove();')
assert s.count(old_move)==1, ("move", s.count(old_move))
s=s.replace(old_move,new_move)

# Diagnostic at top of OnMouseEvent (ue4 thread, safe to UE_LOG)
old_ome=('EM_BOOL FSimplyStreamApplication::OnMouseEvent(int eventType, const EmscriptenMouseEvent *mouseEvent) {\n'
         '\tif (runningNode) {')
new_ome=('EM_BOOL FSimplyStreamApplication::OnMouseEvent(int eventType, const EmscriptenMouseEvent *mouseEvent) {\n'
         '\t{ static uint32 _mi=0; if(_mi++<40) UE_LOG(LogTemp, Warning, TEXT("SSINPUT evt=%d btn=%d tX=%d tY=%d runningNode=%d"), eventType, (int)mouseEvent->button, (int)mouseEvent->targetX, (int)mouseEvent->targetY, (int)runningNode); }\n'
         '\tif (runningNode) {')
assert s.count(old_ome)==1, ("ome", s.count(old_ome))
s=s.replace(old_ome,new_ome)

open(p,"w").write(s)
print("input fixes applied: browser-callback forward + do_events drain + cursor SetPosition + diag")

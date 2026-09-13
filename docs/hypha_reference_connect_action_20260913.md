# Reference connection action — 2026-09-13

The diagnostic Kirin OS renderer, after the W-3049 saved Work identity repair,
wrote a real v2 POST connection request. The already-open Pro Tools Developer
POST editor displayed CONNECT, but clicking it did not change presence or
create a binding. Reopening the editor did not resolve that failure.

Two controls occupied the guide rail. Observatory owned the visible guide
button, whose callback only opened received-guide details. The editor separately
owned an overlaid connection button. When a request arrived after layout, the
Observatory recalculated its guide bounds while the overlaid control could keep
empty bounds: the editor only relaid out on a body-bounds change. The visible
button therefore reached an empty-guide early return instead of accepting the
pending Work connection. VU also changes sibling ordering, making an overlay
the wrong owner for this action.

The existing Observatory guide button now owns both operations. Its editor
callback accepts an actual pending connection first, otherwise opens received
guide details. The separate button and all independent bounds/visibility
updates are removed for both roles and the common AU/VST3/AAX editor.
Connection remains explicit; no audio is selected or changed by receiving a
request or by opening the editor.

The real-editor surface contract also injects late guide arrival into all 40
size/domain/role cases, including manual and recording-triggered VU returns,
and checks the actual pointer target. The final local contract passed in
70.89 seconds. The source gate checks that pending connection acceptance
precedes the received-guide early return. Existing transport tests retain
request validity, expiry and role-boundary coverage.

This source change does not by itself prove an end-to-end runtime binding,
Reference preset preparation, audition, restart persistence or AAX Blind PDC.
The subsequent diagnostic host run must record those results separately.

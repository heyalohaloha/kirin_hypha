# Hypha Observation Loop / Work Reference / CAPTURE Work attachment

## Product boundary

The Kirin OS integration is three explicit gates, not a shared mutable mode.

1. Kirin OS sends a saved INSPECT or MASKING fact to one user-connected Hypha POST.
2. Hypha acknowledges receipt and projection state without judging the fact or changing audio.
3. The user may attach one frozen CAPTURE back to the same Work.

Guide receipt alone never triggers Capture, Work storage, a meter mode change, Reset, Record,
Keep, pairing, transport, or an audio operation. CAPTURE never changes `work.json` directly.

## Work Reference

Accepting a Kirin OS connection freezes an immutable Work Reference:

- `target_role=post`
- `work_id`
- `binding_id`
- `runtime_instance_id`
- a display-only Work title

The three private IDs are the authority. The title is never used to route a write. Rebinding clears
the complete reference. PRE/POST pair names, DAW track names, paths, and previous sessions are not
fallbacks. Private IDs are intentionally absent from the Observation Plate image.

The first UI surface for this reference is the CAPTURE destination submenu. Persistent top-chrome
placement is not introduced here; that visual/content decision remains independent.

## Explicit CAPTURE action

Local Save and Work attachment remain separate commands. On a connected POST, CAPTURE offers
`Attach to Work - <title>` with the existing 1200x630, 1080x1080, and 1080x1350 formats. Selecting
one item freezes the same authoritative snapshot used by local Save, encodes its PNG in memory, and
submits exactly one Work attachment request. There is no sticky attachment preference and no
automatic retry against another Work.

The producer runs on a low-priority thread and uses the separate machine-local
`plugin_data/hypha_capture/v1` transport. It writes the immutable PNG before atomically publishing
the bounded request. The request carries the exact Work Reference, explicit `capture_attach` user
action, byte count, SHA-256, dimensions, domain, ABS/DELTA target, and snapshot time. It expires
after 60 seconds.

Kirin OS revalidates the current POST binding and owns the Work transaction. Hypha accepts only a
strict receipt matching request, Work, binding, and runtime. The initiating action reports attached
or a bounded public failure; background absence remains silent. No path or internal identifier is
shown in the image or feedback text.

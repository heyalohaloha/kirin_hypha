# Pairing PRE and POST

POST pairs with one exact PRE instance. Opening POST's window pairs it automatically when exactly
one PRE in the project has no POST; otherwise choose the PRE under PRE connection. Names are
optional labels and are never used to guess a pair.

[Back to the README](../../README.md#the-full-manual)

A new POST pairs with its PRE automatically. A pair is always one exact PRE instance; it is never
inferred from track position or matching names.

1. Insert PRE before the processors and POST after them.
2. Open POST's window. If exactly one PRE in the project is not already paired with another POST,
   POST pairs with it, shows **Paired with PRE** and the PRE's name, and offers **Unpair**. After you
   unpair, that POST is not paired automatically again. Opening a saved project never pairs
   automatically.
3. To choose a PRE yourself, click the badge at POST's top right or its arrow, then select the
   intended PRE under **PRE connection**.
4. POST keeps that exact PRE identity and begins displaying Δ values.

The badge shows the pair at a glance: **NO PRE** when unpaired, **+** and the PRE's name when one PRE
is available (click it to pair), and **PRE** with its name when paired. When the name does not fit,
the badge shows **+ PRE**; hover over it to read a long name.

Giving PRE a name such as `Mix`, `Drum`, or `Vocal` makes the menu easier to scan, but naming is
optional. UTF-8 labels, including Japanese, are supported. An unnamed PRE can still be selected by
its exact instance identity. POST never accepts a typed pair name or retargets another PRE by name.

When an active PRE or POST is duplicated, the copy receives a different instance identity. Select
the intended PRE again in a copied POST. Saved identities are retained when reopening a session if
no running instance already owns them.

Multiple PRE / POST pairs can run simultaneously (up to 12 active pairs per project).

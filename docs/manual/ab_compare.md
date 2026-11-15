# A/B: hardware and plug-in comparison

A/B compares two processors on one track placed as PRE1 → A → PRE2 → B → POST; a hardware insert
can be A or B. LISTEN, MATCH, AUTO and BLIND then compare A and B at matched level, and END returns
to the normal chain.

[Back to the README](../../README.md#the-full-manual)

Place **PRE1 → A → PRE2 → B → POST** on one mono or stereo track. A hardware insert can sit in **A or B**.
As with LIVE, neither PRE may be an AAX multi-mono instance; insert it as stereo instead.
On POST, press **A/B**, choose PRE2, then confirm the diagram. During comparison, PRE2 sends the
original input to B while continuing to share A's output with POST. POST's usual LISTEN, MATCH,
AUTO and BLIND controls compare **A** and **B** at the matched level. **END** returns to A→B.
The normal PRE1 pair is retained; reopening a saved session does not restart A/B.

The diagram is a placement confirmation, not automatic routing detection. Different track names
are shown as a hint. In another arrangement, the sound can change during comparison. When timing
or the replacement range cannot be confirmed, the actual chain output is used temporarily;
BLIND keeps its assignment and resumes automatically. That interval is not counted as B heard.
In A/B, BLIND's GUESS asks **Which one is B?**, and SAVE IMAGE names the sources A and B until you
type other names ([Blind](blind.md)).
An A that arrives later than B, or whose position the audio cannot confirm, plays as it is and the
footer says so; comparison continues. When an earlier A/B of the same candidate confirmed how early A
was, and the host reports the same latencies, insert chain and delay compensation, the next start of
that candidate plays A with that amount and says **Delay not re-measured** until the audio confirms
it again. After a change in those, or with another candidate, the start is fresh; when the host
starts compensating that amount itself, it is dropped. When seeks keep A from reaching such an
amount, B stands in for at most one second in all; then A plays at its clock position with
**Delay unmeasured** until it can.
Normal measurement and capture are suspended during A/B. Finish Record / Keep before starting.
After END they resume once B has played out what it was fed during A/B (four seconds of playback),
and at the latest ten seconds after END, also while the DAW is stopped. A LISTEN, BLIND, V, C, B or
Keep pressed meanwhile starts by itself. If the ten seconds ended the wait while B was still playing
out the comparison, nothing of that audio is recorded: a Keep pressed then waits, the status line
says **Clearing the A/B tail, then Keep** (or **Play to continue, then Keep** while stopped; **Bypass is
on: Keep starts after it is off** or **Rendering offline: Keep starts after it ends** while the host holds
POST's return; an All Keep waiting for another POST says **Clearing another POST's A/B tail, then Keep**,
and that POST's window says what holds it), and it
starts by itself once four seconds of ordinary audio have passed through B. After two minutes
without that (playback kept stopping or seeking) it ends with **A/B tail not cleared: Keep not
started**. A LISTEN or BLIND started in that time does not measure PRE's alignment from that audio:
PRE plays at its clock position (or with the amount confirmed before) and the status line says
**Delay unmeasured** (or **Delay not re-measured**) until B has played it out.

Hardware and effects with feedback or long tails retain their own history when their input
changes. A/B does not reset them. Allow the effect to settle before judging it or exporting;
an immediate offline render can include that retained history.
A realtime bounce or export while A/B is on records what you hear, so press END first.

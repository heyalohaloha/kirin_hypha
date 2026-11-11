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
An A that arrives later than B, or whose position the audio cannot confirm, plays as it is and the
footer says so; comparison continues.
Normal measurement and capture are suspended during A/B. Finish Record / Keep before starting.

Hardware and effects with feedback or long tails retain their own history when their input
changes. A/B does not reset them. Allow the effect to settle before judging it or exporting;
an immediate offline render can include that retained history.
A realtime bounce or export while A/B is on records what you hear, so press END first.

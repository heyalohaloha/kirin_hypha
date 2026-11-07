# A/B comparison in a serial insert chain

PRE1 → A → PRE2 → B → POST, on one mono or stereo track. Hardware inserts can sit in A or B.
POST's A/B entry selects PRE2 and confirms this diagram once per comparison. The normal PRE1
pair and the saved state do not change. Placement is the listener's declaration, not inferred
from sound, callback order or clocks. Different host track names give guidance, not a refusal.
A misplaced PRE2 can change the sound during the explicit comparison.

POST uses the existing LIVE renderer with PRE2 as its temporary PRE: A is PRE2's input and B is
POST's input. LISTEN, MATCH, AUTO, BLIND, anonymous assignment, REVEAL, ceiling and retained
attenuation continue to use their existing rules. No third audio source or three-party timing
protocol is introduced.

PRE2 first publishes its own input a to its ring. A separate reader uses the existing LIVE
correspondence rules to read PRE1's x at A's delay, and fades its output to x at unity. It never
adds reported latency. Its read mapping and its write mapping have independent lifetimes.
POST renews a short request lease from its audio callback, not from an editor timer. Without a
current lease PRE2 fades back to its actual input. Offline and bypass do not substitute audio:
bypass leaves x over the same 5 ms fade and is exact afterwards, and an offline pass is exact
from its first sample.
A format or owner change ends the old authority; one transient block does not end a trial.

PRE2 retains disjoint contiguous ranges it actually replaced completely, bound to the request and
its current ring run. POST checks B's input range at the clock mapping (POST clock minus K),
independently of A's content-aligned audition read position, before calling the
input B, recording a MATCH window or counting a BLIND stimulus. Fades, missing receipts, old
runs and unverified timing use the actual POST input as fallback and do not count as either
candidate heard. The selected stimulus, gains and anonymous assignment remain; fresh evidence
resumes the same comparison automatically. A heartbeat being written is distinct from expiry;
a callback may use its still-valid last lease while that same owner renews it. A newer heartbeat
is valid for an earlier in-flight callback. Unavailable blocks retain earlier valid ranges so
B latency does not enlarge one hole. Discontinuity boundaries interpolate from the last output
value to current pass-through over 5 ms, without replaying old PCM at a new position. This
transition requires an actually audible A or mix; B-to-B clock loss leaves B bit identical.

Content confirmation never holds A back. An A later than B has no earlier position to read, so A
sounds at its clock position and the footer states how late it is. Audio that confirms nothing
ends the wait after one second of proven playback: A sounds at its clock position, and the footer
says the position is unconfirmed once a real window fails. Later evidence can still move A. Clock
proof, B's receipts and the ceiling gate every block as before, and a failed content check never
returns the output to B.
Offline output discards the skipped renderer's transition origin.

The B processor has its own state. A verified input range proves which input was supplied, not
that every internal tail has vanished. The transition and the first four seconds of continuously replaced input are excluded from
matching, before the ordinary four-second MATCH window. LISTEN continues during that allowance. An arbitrary
external processor can retain state indefinitely; no fixed delay proves its ideal zero-state
response. Tests distinguish B driven by the actual transition history from an independent
steady-state B(x) oracle. Offline output immediately after comparison may retain B's history;
ending comparison does not reset another vendor's processor.

Normal POST measurement is suspended before replacement is granted. The existing audition
admission excludes Record, Keep and Reference Capture; an active recording is never ended or
thrown away to start A/B. The normal meters, histories, captured files and Reference observation
receive no comparison input. END restores the serial A→B path and ends the comparison; normal
measurement resumes in a new window after downstream return. Its quarantine starts only after PRE2's final possibly replaced position has passed through
B's current clock mapping, including B delays longer than the settling allowance. A stopped PRE2
withdraws its receiver publication, waits for existing readers to leave, and publishes the final
output position without requiring another callback. Bypassed callbacks also finish withdrawal.
Chain observations outlive substitution until return completes. After withdrawal, a clock-only
observer of the ordinary PRE1-to-POST pair can follow a newly published upstream marker through
the current chain, including loops, seeks and changed delays, without PRE2's retired clock.
It never grants comparison audio or MATCH; the comparison entries remain available and retain the next explicit
start. A confirmed return survives a brief pair-proof gap in the same chain, so short loops
can finish settling. Missing host-chain metadata pauses the count; a seek, changed delay,
clock change or stopped/ineligible output requires a new return proof. A queued start remains
pending while audio is stopped. MATCH reports the remaining verified settling/window duration.
The isolated POST callback still records its input and output in an explicitly armed chain trace. Saved comparisons never restart.

The common renderer and ring layout serve macOS and Windows and the supported AU/VST3/AAX
wrappers. Synthetic product tests verify the real processor entry. Real-host acceptance and
hearing the transition remain separate from automated fixture results.

Each POST reader owns its PCM demand reference. Refusing a competing POST leaves the active
reader intact. Content alignment and monitoring belong to the actual comparison peer's runtime
generation; neither PRE1 nor PRE2 corrections survive switching the peer, even with identical K.
PRE menus use host-reported insert positions only when the public saved identity resolves
unambiguously. Otherwise user names and short identities remain; order is never inferred from audio.

A request records its first source mix, including a partial fade, until withdrawal. Only a
withdrawn request that never changed PRE2 output releases measurement isolation without a
clock or settling interval. Deleting PRE2 before its receiver is prepared also publishes this
withdrawal. After a format or source-owner change, the return-only observer opens the current
PRE1 ring off the audio thread and obtains a fresh marker; a retired mapping cannot strand
ordinary measurement during continued loop playback.

Delay-compensation notifications reach PRE2 as well as POST. OFF passes the actual input;
every change, including OFF/ON between callbacks, discards the previous correspondence.
Fresh compensated positions resume the same selection and anonymous assignment automatically.
Delayed LISTEN and BLIND guidance distinguishes a host-disabled PRE2, callback-confirmed bypass,
and unavailable timing without diagnosing the insert order or revealing the blind assignment.
Except for confirmed PRE2 disablement or bypass, specific remedies such as compensation OFF
and Cycle take precedence; the generic PRE1/PRE2 guidance is for unexplained timing gaps.

Serial-chain observation is independent of replacement permission. Hardware on either side
uses the same correspondence, including return-only delay observation. After END, ordinary LISTEN can
observe the same two-PRE chain. A coherent withdrawal can drain without either old PRE: a fresh
REAPER chain report supplies the entire current chain delay, Pro Tools' documented compensation
limit bounds it, each plus the scheduling allowance. Elsewhere fresh PRE1 timing supplies the
proof; updating timing alone does not prove correspondence in an uncertified loop. Withdrawal
is still required. Missing metadata pauses this count; a changed chain, delay, seek, stop, rate or clock resets
it. A retired PRE2 snapshot cannot re-establish the project projection after a seek: only
timing published since the preceding POST callback can establish it again. This path never
supplies K, PCM or MATCH authority.
Final processor destruction retires the retained return mapping after callbacks are quiescent;
format changes keep that mapping only while a future callback can complete the return.

The return receipt belongs to its request. A later POST can claim the same PRE2 while an earlier
POST still drains, and neither erases the other's evidence. The project capture exclusion stays
held until the return completes, across a format change: the POST's replacement engine receives
the same scope, with no moment in which All Keep from another POST could start. Only final
destruction releases it early. A failed BLIND preparation ends that trial only; A/B LISTEN, the
PRE2 request and the exclusion continue, the listener is told once, and BLIND can be pressed
again. BLIND start failure releases only a scope it acquired itself. A scope borrowed from A/B remains
owned by A/B through return completion, including when a concurrent restore ends A/B.
Non-RT ownership of PRE2's receiver is serialized by the processor's handle lock, so a
re-prepare on a host control thread cannot free a receiver or mapping the service timer is
reading, including its final continuation check. A busy owner keeps the next service tick.

REAPER's chain report reaches the audio callback from a timer, two ticks into every start or
seek. Its arrival proves the chain the absent blocks already ran on, so neither reader re-proves
K: a start or seek settles once, on the requested source. The report's revision names the
chain and delay, not one observation of them. A timer tick landing inside a callback leaves
that block with the report its predecessor used, so no single block loses its proof.

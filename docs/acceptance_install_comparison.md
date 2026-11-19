# macOS install comparison

`python3 scripts/acceptance/macos_install_compare.py EXPECTED_BUNDLE INSTALLED_BUNDLE`
is read-only. Run only on quiescent payload/install bundles. Exit 0 means the compared
content and metadata match; 1 means different; 2 means unreadable or changing.
The report excludes exactly `com.apple.provenance` from the attribute digest. It
retains quarantine and every other attribute, file bytes, mode, flags and link text.
It neither removes attributes nor suppresses warnings. Signature, Gatekeeper,
installer behavior, ownership/ACL checks and DAW acceptance remain separate gates.
Inodes, owners and times are not installation-copy equality criteria. This tool is
not a privileged installer or an adversarial-filesystem verifier.

The comparison-only filter follows the earlier clean-install acceptance approach;
all fixtures are synthetic, and importing the module performs no operations.

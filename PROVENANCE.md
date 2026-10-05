# Implementation provenance

Kirin Hypha is distributed under the GPLv3 license in [LICENSE](LICENSE). Third-party components
retain their applicable licenses and notices. This document describes contribution policy and
recorded sources; it does not certify the origin of every historical line or asset.

## Recorded sources

The psychoacoustic implementation in `crates/kirin_measure/src/phase_d/` contains historical
references and ports associated with public MoSQITo v1.2.1 Python modules, including coefficient
and lookup tables. The initial source also records adaptation from Kirin's Lens measurement engine.
The public MoSQITo material is Apache-2.0 licensed; see [the component notice](THIRD_PARTY_NOTICES.md)
and the retained license text. Rust adaptation, streaming and display integration have their own
subsequent repository history. These records do not establish a greenfield origin for the whole core.

The Cargo lockfile, vendored source notices, pinned JUCE submodule and tracked JUCE patches identify
additional dependencies. Source distribution and binary packaging must preserve the notices and
source availability applicable to the components actually included. Build success alone does not
verify those distribution obligations.

Public standards, papers, API documentation and numerical references should be cited where they
were actually used. Adding a citation later does not establish that it was the original design source.
Retain implementation commits, design records, tests, corrections and reference versions so that
individual features can be traced without relying on a general independence claim.

## Contributions and product research

Use materials that contributors may lawfully provide under the applicable licenses. Record
third-party code, data and asset origins, modifications and required attribution. AI assistance
does not replace source or asset provenance checks. See [the contribution rule](CONTRIBUTING.md#contribution-provenance).

The project does not accept leaked source, unauthorized non-public materials, or code obtained by
decompiling or disassembling a third-party proprietary product. Public feature comparisons, public
manuals and normal user-visible product observations should be distinguished from implementation
sources and interoperability research. Preserve uncertainty for review rather than declaring an
unsupported origin or deleting a reference solely because it names another product.

# Implementation progress

Plan: docs/superpowers/plans/2026-09-12-native-toolkit.md

User approved development on 12 September 2026. Base: 20a2633.

Ruling: work on an implementation branch in the existing clean workspace; all agents have disjoint file ownership. Parallel implementation follows the developer instruction for independent subtasks. Root alone owns shared build/UI files and Git commits.

| Task pair | Interface / overlap review |
| --- | --- |
| Core / UI | Core publishes subnet.hpp before integration; UI consumes its public operations. |
| Network / UI | Network publishes request/results/cancellation; workers never own windows. |
| Help / UI | Agreed show_help/show_about signatures; root initializes COM/common controls. |
| All / build | Root owns CMake; each component provides tests and source paths. |
| Tasks 1–5 | Each has explicit files, verification and approved spec; no shared implementation file ownership. |

- Task 1: complete — exact core and IANA classification verified.
- Task 2: complete — native networking and pane tests verified.
- Task 3: complete — generated native help resources, official icon packaging, Help/About Win32 UI and resource regression tests verified.
- Task 4: complete — integrated native frame, files and safe shutdown verified.
- Task 5: complete locally — Release, 10 CTest groups, pipeline regressions and source reviews pass; hosted workflows gate each publication.

Release validation added locally on 13 September 2026: the native workflow builds MSVC x64 Release with `/MT`, runs generated-resource freshness checks and CTest (including loopback/frame coverage), verifies PE imports/manifest/version identity, packages checksums and immutable metadata, attests final files, and stages audited draft releases through the NetTools-only VEU App token. Main publishes versioned prereleases; `vX.Y.Z-rc.N` and `vX.Y.Z` tags are validated against main history. Stable publication remains gated on an Authenticode signer and the exact-SHA `VEU clean-machine compatibility` check; no stable release is currently claimed.
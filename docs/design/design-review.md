# Design review — 2026-09-12

Status: historical first review. See the [final readiness review](final-readiness-review.md) for the subsequent audit, shared 21-lesson manual and GitHub Pages setup. Windows application development still requires user approval.

## Findings and corrections

| Finding | Correction |
| --- | --- |
| Search could hide the selected lesson while retaining unrelated article content | Matching topic selected automatically; no-match article state; Clear search action and announced result count |
| Lesson pager ignored the current filter and lost focus when replacing itself | Pager now traverses matching lessons and focuses the new heading |
| Practice feedback described a previous radio selection | Changing the selection clears the previous result |
| Download implied a binary existed | Primary link now says Download status; release section explicitly says no executable exists |
| About showed no accessible licence text | Added a local full MIT licence link to the preview |
| App illustration suggested unfinished diagnostic features would ship immediately | Written scope explicitly omits diagnostic controls from the first executable; image caption identifies later diagnostics |
| Address-family selector could conflict with entered text | Family inferred from literal address; explicit prefix/mask field; missing prefix never guessed |
| IPv6 capacity and IPv4 usable-host rules could be conflated | Separate IPv6 bounds/counts; prefix-based IPv6 allocation; explicit edge-case rules |
| Invalid edits, navigation boundaries and unsaved plans lacked defined behaviour | Added validation/stale-result, no-wrap, cancellation, save-recovery and dirty-plan rules |
| Compatibility and persistence remained ambiguous | Defined compatibility target and fallbacks, Desktop Experience scope, session-only default and explicit plan files |
| Release diagram showed PRs reaching publication | Added a trusted-event gate and PR artifact-only branch |
| Help was embedded after compilation and doc-only changes skipped needed builds | Resource generation moved before compilation; embedded content triggers application builds |
| Manual copies could drift | Restricted canonical topic schema and generated outputs defined for implementation |

## Review limits

Verified on the current webpage: one-result search selects the matching article and disables both pager directions; no-result search shows an empty state; Clear search restores 15 topics; explicit lesson navigation focuses the new heading; changing a practice answer clears old feedback. Desktop (1280px) and narrow (390px) page widths fit their viewports; the About dialog fits the narrow viewport and exposes the local MIT licence. JavaScript syntax, local assets/anchors, documentation links and reference-table calculations were checked before publishing the review.

This review covers the current static website and written native-app/release designs. There is no Windows executable or running release workflow to test. Clean Windows compatibility, UI automation, runtime dependency checks, actual code signing and release publication will be verified during implementation. Signing infrastructure remains a release-time decision. GitHub Pages was selected during the subsequent review.

The app illustration remains a visual guide; the reviewed product design is authoritative for behaviour and first-release scope. Design review reduces known problems but cannot prove an unimplemented application has no defects.

## Historical development approval scope

Implement the four-view IPv4/IPv6 subnet workbench, native help/About, explicit plan save/load and export, and build/test infrastructure. Build the release automation to the documented channel design. Do not publish a stable executable until its release gates pass. Add live diagnostic modules in later releases.

The subsequent user-approved design expansion includes all original diagnostic tools and local/external IP information. The current scope is in [product-design.md](product-design.md); this historical subnet-only proposal no longer defines release scope.

# Final design and readiness review

Date: 12 September 2026. Status: design reviewed and expanded; Windows application development awaits explicit approval. Website/documentation changes are local. Public publication awaits the VEU organisation App.

## Reviewed scope

The target is the full Velocity NetTools suite: My PC & IP, IPv4/IPv6 subnet workbench, visual ping, traceroute, DNS, interface/route/neighbour inspection, TCP checks, HTTP/TLS, Wake-on-LAN and MTU probing, with native offline help/About and explicit plans/exports.

Use C++20/MSVC/Win32, one portable x64 EXE, static application runtime, and only system Windows dependencies. Windows 10 build 10240+, Windows 11 and Server 2016+ Desktop Experience remain compatibility targets to verify. ipify is approved for explicit external IPv4/IPv6 checks; no public lookup or network probes run on startup.

## Findings resolved in the design

| Area | Reviewed rule |
| --- | --- |
| IPv4/IPv6 maths | Strict literals/masks, explicit family/prefix, preserved host, exact /0 and full-width counts/endpoints, no wraparound, IPv4 /31-/32 and IPv6 /127-/128 exceptions |
| Aggregation/Compare | Exact sets, deduplication, aligned sibling merging, extra coverage labelled, intersection and both directional differences |
| VLSM | Stable insertion-order ties, pins and existing allocations preserved, alignment/fragmentation errors, explicit partial application |
| Scale | Indexed paging, finite working/export limits, cancellation and revision checks; no IPv6 host enumeration |
| Local/external IP | Multiple adapters/scopes/states, valid reported prefixes, separate observed external families/provider/timestamp, no false Internet-connected inference |
| Diagnostics | Distinct pending/cancel/error outcomes, defined completed-probe loss denominator, exact RTT-statistic conventions, gaps preserved in charts |
| Traceroute/MTU | Hop silence is not proven forwarding loss; MTU uses header math and evidence, with IPv6 source-fragmentation limits stated |
| Windows APIs | Baseline imports versus dynamic optional functions, explicit resource ownership, DPI fallbacks, callback drain and old-OS tests |
| ICMP timeout | Synchronous timed calls on bounded workers preserve responsive UI; native async IPv6 ignores its Timeout parameter and needs separately proved lifetime/deadline handling |
| Saves/help | Recoverable replacement failures, dirty snapshots, constrained embedded help, explicit accessibility/navigation/printing implementation |
| Downloads | Immutable verified versioned bytes, checksum ordering, stable/preview separation, SemVer preview ordering, stable-tag validation and no fake EXE |
| Identity | Exact corporate logo assets, VEU author/committer, organisation App publication; no personal-auth fallback |
| Help/search | One source generates GitHub lessons, interactive lessons, complete static lesson pages and sitemap |

Detailed contracts: [maths](math-contract.md), [diagnostic measurements](diagnostic-maths.md), [Windows runtime](windows-api-contract.md), [network tools](network-tools-contract.md), [help/About](help-and-about.md), [releases](release-pipeline.md).

## Verification evidence

Independent reference checks: 190 subnet/boundary assertions, 25 follow-up lesson/interaction checks, and 26 diagnostic-statistic/MTU checks passed. These validate the written examples, not an unimplemented application.

The Pages preparation review passed 17 acceptance tests, including valid empty releases, preview/stable states, exact download integrity, immutable-release enforcement, ambiguous/invalid versions and an older preview published later. The integrated Pages workflow passed actionlint, and the site-preparation build against the real public GitHub API passed with the truthful no-executable state.

The canonical manual contains 28 lessons. Both generation freshness checks pass. Static lesson generation has checks for links, metadata, JSON-LD, sitemap and stale output detection. The website has 30 canonical URLs: home, manual index and 28 lessons. Fourteen final browser checks passed, covering lesson search/empty/reset states, both practice outcomes and feedback reset, readable static IP help without executable JavaScript, original corporate assets, the expanded concept, desktop/mobile widths and the mobile About dialog. All 30 local HTML pages passed local-link/anchor, duplicate-ID and JSON-LD checks. Candidate public text and computed Git author/committer identities passed the VEU identity check.

The main toolkit and subnet images are layout concepts, not running software. Exact original logo/icon assets, not generated approximations, must be used when compiling the application.

## Remaining implementation and publication gates

- Obtain explicit approval to begin Windows application development.
- Implement and test the pure maths engine before connecting it to controls; use independent expected values, property checks and parsing/plan-file fuzz cases.
- Implement each diagnostic backend with saved test fixtures plus controlled network/VM scenarios, including errors, timeouts, races, source changes and repeated cancellation.
- Verify baseline Windows launches, DPI/accessibility, runtime imports, offline operation and save failures on clean VMs. Modern hosted-runner success is insufficient.
- Implement the complete command/error/F1 mappings and embedded help renderer alongside each shipped module.
- Register/install the velocityeu-owned VEU App on NetTools and provision its key securely. The available browser reaches GitHub sign-in; App setup has not been performed. Keep publication on hold and do not use the current personal CLI login.
- Enable Pages with GitHub Actions as its source, publish through the App and verify live URLs. Search Console ownership/sitemap submission and actual search indexing remain external activation steps.
- Provision signing and clean-machine release checks before stable executable publication. No application build/release workflow or executable exists today.

No remaining mathematical contradiction was found in the reviewed examples. Design review cannot prove that future code will contain no bugs; these contracts define what must be implemented and tested.

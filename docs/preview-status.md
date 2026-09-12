# Development preview status

The application is implemented as C++20/Win32, initially x64. Development was approved on 12 September 2026.

## Verified locally

- MSVC Release with static runtime (/MT); one executable with embedded manifest, official icon, licence and 28 help topics.
- Exact subnet/classification tests include every IPv4/IPv6 prefix, 2^128, strict parsing, ranges, sets and deterministic VLSM.
- Independent Python IP arithmetic and IANA XML classification comparisons were run during implementation.
- Native control tests exercise edits, invalidation, paging, VLSM Apply/Undo/Redo, continuous multi-target ping, pause/rolling views, DPI scrolling, cancellation and shutdown.
- Loopback tests cover IPv4/IPv6 ICMP, TCP, HTTP and ICMP scheduling under contention.
- Saved-plan tests cover Unicode, malformed/versioned JSON, size limits, CSV escaping and recovery after replacement failure.
- Embedded-help/resource and shared website/help freshness checks pass.

These checks establish specific tested behavior, not a claim that the program is bug-free.

## Preview limits and release gates

- Windows 10 build 10240 and Windows Server 2016 Desktop Experience still require clean VM validation. The development machine is a current Windows 11 system.
- Signing infrastructure is not provisioned. Previews are explicitly unsigned; stable publication is disabled.
- Real enterprise DNS cancellation/custom-resolver, proxy/TLS failures and mixed-monitor/screen-reader behavior need broader lab coverage.
- Intermediate traceroute RTT, negotiated cipher details and redirect-chain header detail are explicitly unavailable where the backend cannot establish them.
- IPv6 MTU observations establish payload reachability, not proof that source fragmentation was absent.
- Equal-split export is the displayed page, up to 200 subnets. It is labelled accordingly.
- Retained ping history is bounded; the session view explicitly distinguishes retained statistics from lifetime totals. P95 is unavailable for an evicted lifetime cohort.
- Native file publication drains safely instead of being interrupted mid-replacement. Recovery files retain prior destination bytes.

Report reproducible issues with the app version, Windows version and input/options, omitting sensitive network data where unnecessary. Use [GitHub Issues](https://github.com/velocityeu/NetTools/issues).

## Public infrastructure

VEU-NetTools (App ID 4924807) is owned by velocityeu and installed only on NetTools. App authentication was verified with a short-lived, repository-scoped token and then revoked. The Actions private key is encrypted and excluded from Git. Release immutability and Pages Actions deployment are enabled. Stable release checks remain separate from preview publication.

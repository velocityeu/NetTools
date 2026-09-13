# Velocity NetTools PWA — design proposal

Status: revision 02 approved for implementation on 13 September 2026.
The user authorised PWA implementation after reviewing revision 02.
Visual concept: [revision 02](pwa-concept-v2.png). The written behaviour below is
authoritative where the illustrative image omits a state or shows duplicate shortcuts.
Owner/public identity: VEU / Velocity EU Inc. Corporate site: https://www.velocity-eu.com/

## Confirmed scope
- IPv4 and IPv6 subnet calculator, equal splits, VLSM and public-IP display.
- Equal priority for phone, tablet and desktop; iOS-inspired appearance.
- iOS/iPadOS 17+ and current Android/desktop browsers.
- Public-IP check on launch after explicit opt-in, plus manual Recheck; no timed polling.
- Named plans saved explicitly on this device. Other recent inputs remain session-only.
- Static UI design and behaviour review first. Running-app tests follow implementation.

## Navigation and visual design
Four destinations: Calculate, Plan, Public IP and Learn.
Phone: bottom navigation and stacked, readable result cards.
Tablet: sidebar; inputs and results side by side when space allows.
Desktop: sidebar and wider input/results workspace, with readable line lengths.
Adapt by available width, including tablet split view, rather than user-agent/device names.
System fonts, restrained blue actions, grouped surfaces, light/dark system appearance,
44 CSS-pixel minimum touch targets, keyboard focus, reduced-motion support, and VoiceOver labels.
No horizontal scrolling is required for primary results. Long IPv6 addresses wrap or use
a dedicated full-width row with Copy. Avoid scaling text down just to fit a card.
Use the existing exact corporate mark; generated concept typography is not replacement artwork.
A PWA follows iOS interaction patterns but does not become a UIKit application.

Reviewed refinements:
- Preserve each destination's current edits/scroll position during the session.
- Keep all four destinations available when offline; Public IP explains unavailable checks.
- Saved plans is a utility, not a fifth phone tab: a single Plan header action on compact
  layouts and a sidebar utility on wide layouts. Do not show both shortcuts at once.
- Phone editors use stacked cards and a dedicated requirement sheet/page; the sheet
  has a labelled Done/Cancel action. Desktop may use a compact editable requirement list.
- Wide layouts use side-by-side editor/results only when both have enough readable width.
  At text zoom or narrow split-view widths, return to the stacked layout.
- Use at least 16 CSS-pixel text in editable controls and comfortable body text; never
  disable pinch/text zoom. Larger type may cause stacking rather than horizontal overflow.
- Keep IP values monospaced, selectable and complete. Wrap at address separators where
  possible, without inserting characters into copied values. Show exact long counts
  on multiple lines; a power-of-two explanation supplements, never replaces, the count.
- Use text plus shape/icon for state; colour alone must not mean success, failure or pin.
- One primary action per stage. Keep action areas clear of safe areas and the on-screen
  keyboard; tab navigation must not cover the last result row or focused control.
- Use labelled generic web icons with appropriate licensing. Do not assume Apple's
  proprietary symbol assets are automatically licensed for this cross-platform PWA.
- Show clear empty states with Load example, which fills inputs only and never triggers
  a public-IP request. First visit remains useful without installation or permission.

## Calculate
IPv4/IPv6 selector; address or CIDR field; prefix input; optional contiguous IPv4 mask.
Explicit Calculate action (keyboard submission too). Validate completed edits on blur,
paste or submission; do not show an alarming error for a normal partly typed IPv6 literal.
IP entry uses a keyboard that permits hexadecimal characters, colons, dots and slash;
numeric-only input is reserved for prefix/host-count controls. Disable spelling correction
and automatic capitalisation for literals.
A complete CIDR paste fills the address and prefix together, with a brief visible
"Prefix taken from CIDR" notice. On committed valid input, show the address separately
from the numeric prefix; do not leave two independently conflicting prefix sources.
Manual typed CIDR follows the same rule on blur or Calculate, matching the Windows
CIDR-precedence contract. A pasted other-family literal can select its family with
a visible notice; invalid/incomplete input never guesses a family.
Preserve IPv4 and IPv6 drafts separately within the session. An IPv4-mapped IPv6
literal remains IPv6. Mask editing is an optional IPv4 section linked to the one
prefix value; reject a noncontiguous mask.
Show canonical network/prefix first, then mask, address count, appropriate host/endpoint
range, last address, and IPv4 broadcast where applicable.
Separate total address count from conventional host capacity and deployment suitability.
Do not subtract two for /31 point-to-point, /32, or IPv6. IPv6 has no broadcast.
Show the maths opens an explanation tied to the computed result, including role-specific
/31 and /32 handling. Save result creates an explicitly named calculation in Saved plans.
Clear distinguishes clearing the current fields from deleting a saved plan.
Copy and explicit Share
must not silently put private subnet inputs into a URL.
Edits mark old results as out of date and disable copying them as a current calculation
until recalculated; invalid input never displays a stale result as valid.

## Plan
Equal split and VLSM segmented modes. The parent network is always visible.
Equal split accepts a child prefix and offers bounded/indexed pages, never enumerating
a huge IPv6 space. Counts are exact. Proposed default page size: 50 rows on every device,
shown as compact cards on a phone. Provide First/Previous/Next/Last and a direct
zero-based index field under Jump to subnet; display one-based row positions with
explicit labels to avoid confusing them with the zero-based mathematical index.
Parent prefix equal to child prefix produces exactly one subnet. A shorter child
prefix is rejected. Store index/count values as decimal strings, never floating-point numbers.
VLSM uses named requirements and stable IDs/insertion order independent of names or
display sorting. IPv4 roles are explicit:
- LAN: requested hosts include router interfaces; one or two hosts still use /30.
- Point-to-point: one or two requested endpoints use /31.
- Host route: exactly one address uses /32.
- IPv6: request a prefix, not an ambiguous usable-host count.

Recommended first-release scope: Advanced exposes pinned allocations and reservations,
consistent with the Windows tool. This scope was approved with revision 02.
Pins, reservations and existing assignments must be aligned, inside the parent,
same-family and non-overlapping; invalid constraints reject the complete preview.

Allocation lifecycle:
1. Edit requirements; mark any previous allocation as out of date.
2. Allocate generates a proposal while preserving valid existing assignments.
3. Review the proposal, including per-row reason/status and requested versus provided capacity.
4. Apply allocation commits the proposal to the current in-memory draft.
5. Save plan writes the named plan to device storage and confirms only after success.

Reallocate unpinned is a separate Advanced action with a before/after movement preview.
It may move unpinned assignments only; it must never move pins. Reducing a requested
capacity must not silently shrink an existing valid assigned network.
For the first release, a capacity failure may show diagnostic proposed rows, but Apply
is disabled unless the complete request fits. No partial plan is silently committed.
Cancelling or receiving an obsolete worker result leaves the applied plan unchanged.

Show allocated, reserved and unallocated ADDRESS counts separately. Total free addresses
do not imply a contiguous subnet: include largest free aligned CIDR and optional Free
blocks detail. Do not label free address count as usable hosts.

Save draft is permitted for incomplete editable requirements, explicitly labelled Draft;
it never stores an unreviewed proposal as an applied allocation. Save plan after Apply
stores the applied assignments with their inputs. Both use the same named-plan library.
Opening/deleting/replacing a plan with unsaved edits offers Save draft, Discard or Cancel.
Within-session requirement deletion offers Undo. Destructive removal of a saved plan
requires confirmation. Unsaved edits are not automatically persisted across process exit.

Local persistence is not cloud backup. Export/import gives the user explicit backup.
A versioned typed PWA plan contains name/ID, family, canonical parent and original input,
mode, child prefix/index, stable requirements/order/roles/quantity, applied CIDRs/pins,
reservations and draft/applied state. Serialize all large integers as decimal strings.
Recompute derived totals on import and validate bounds/schema before changing the current
plan. Unknown fields affecting semantics or unsupported versions must not be discarded
silently. Failed import/save leaves the previous stored plan intact.
Initial export is explicitly labelled a PWA plan backup. Windows v1 files use a UI-field
map and require a deliberate converter; do not claim native interoperability until
conversion rules and round-trip tests are implemented and approved.

## Public IP
Separate IPv4 and IPv6 observations, each with Copy, checked time and provider.
Use api.ipify.org and api6.ipify.org over HTTPS, with strict response/address-family validation.
Use one Recheck button BELOW BOTH cards; it refreshes both observations independently.
Check now remains available without enabling launch checks. Launch opt-in is an inline
choice or labelled setting, not a modal blocking offline calculation on first visit.
Each request uses credential-free HTTPS/CORS JSON, a 10-second deadline and 1 KiB
response cap. Do not use JSONP or insert provider responses as HTML.
Show each family's result as soon as it completes; one failure must not hide the other
successful result. While pending, show Checking and a Cancel action; disable overlapping
Recheck requests. Every request has a revision so late responses cannot overwrite newer
observations. Neither abort nor failure replaces the last observation with a fake address.
If direct CORS access fails, report it; never substitute a server-side proxy that would
change whose public address is being measured.
First use explains that ipify sees the connection's public address and asks whether to
enable launch checks. Declining leaves manual checking available.
Opt-in is a remembered preference, not consent inferred from visiting the landing page.
No periodic/background polling. A launch is a new app session, not every tab switch.
Treat browser resume as potentially stale and offer Recheck. Show per-family observation
time, not an unqualified Current IP label. Browser online/offline hints do not prove
internet reachability. Turning launch checks off stops future automatic requests.
If the preference cannot be saved, explain that it was not remembered.
States: not checked, checking, verified at a stated time, failed/unverified, offline.
During refresh, previous observations are labelled previous; failure never promotes an old
address to current. Do not cache IP API responses in the service worker.
The address is the egress visible to the provider; VPNs/proxies/Private Relay may affect it.
Do not infer local adapter addresses, physical location, VPN presence, or absence of IPv6
from this result. This PWA is not the Windows local-adapter inventory.

## Website, installation and offline operation
PWA URL: https://velocityeu.github.io/NetTools/app/.
Landing page: separate Open web app action alongside Download ZIP / Download EXE.
The PWA links back to the learning centre, GitHub and corporate site in Learn/About.
Do not label a first visit Offline ready until shell, engine and relevant help are
successfully cached. Provide app version, cache readiness and manual update information
under About. Keep cached shell and arithmetic engine versions consistent.
iPhone/iPad installation guidance explains Share > Add to Home Screen where supported.
Do not promise a programmable one-click iOS install prompt.
After successful initial loading/caching, calculations, planning and bundled relevant
help work offline. The first visit requires internet; public-IP checks always do.
Scope the service worker to /NetTools/app/ so it does not control the learning centre
or intercept Windows release downloads.
Check for updates automatically on launch, on foreground return (throttled to 15 minutes), and every 15 minutes while visible. Download in the background. Activate automatically only when all open app tabs have no unsaved work or running checks; otherwise show Update now/Later. All clients vote before activation and freeze edits during the short handoff. Never reload an unsaved plan mid-edit. User added automatic updating during implementation.
Handle cache/storage eviction, denied persistence, full storage and private browsing.
No login, analytics, cloud plan storage, or external font dependency is proposed.

## Arithmetic architecture recommendation (design only)
Prefer one shared stateless C++ subnet/classification core compiled to WebAssembly in
a dedicated Web Worker. Inspection found the core uses standard C++; the current top-level
CMake build rejects non-Windows/MSVC and will need a separate target/adapter after approval.
Reuse does not mean a browser build already exists or has been tested.

Browser code owns UI, plans and public-IP HTTP. The narrow worker boundary exchanges
strict typed messages with decimal-string address/count/index values. UI job revisions
discard late responses. Long-running allocation can be cancelled by terminating and
recreating its stateless worker while retaining the draft in the UI; do not depend on
a cancel message being processed while synchronous WASM is busy.

Use a single worker without pthreads/SharedArrayBuffer or a requirement for COOP/COEP
headers. Independent Python/standard fixtures plus native/core comparisons remain
necessary; shared code alone is not an independent correctness test.
After approval, measure cold download, startup, memory, cancellation and responsive
editing on a representative iOS 17 device. Common calculations should feel immediate;
long work must remain cancellable and never block scrolling/input.
If the WASM build cannot meet agreed delivery/performance constraints, explicitly
review one JavaScript BigInt implementation using the same typed contract. Do not ship
silent runtime switching between two arithmetic engines.

## Design-level calculation checks
These are hand-reviewed expected outcomes, not executed PWA tests.

| Input | Expected outcome |
| --- | --- |
| 192.168.10.42/26 | Network 192.168.10.0/26; mask 255.255.255.192; 64 addresses; conventional hosts .1–.62; broadcast .63 |
| 192.0.2.10/31, point-to-point | Two endpoints .10 and .11; no conventional broadcast exclusion |
| 192.0.2.10/32 | One address |
| 0.0.0.0/0 | 4,294,967,296 total addresses; no claim all addresses are deployable hosts |
| 2001:db8::1/64 | Network 2001:db8::/64; 18,446,744,073,709,551,616 addresses; no broadcast |
| ::/0 | Exactly 340,282,366,920,938,463,463,374,607,431,768,211,456 addresses |
| 192.168.10.0/24 split to /26 | Four networks: .0/26, .64/26, .128/26, .192/26 |
| VLSM /24: Office 50 LAN, Voice 25 LAN, Link 2 point-to-point | .0/26, .64/27, .96/31; 98 addresses allocated, 158 remain |
| ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe/127 | Two addresses ending fffe and ffff; no broadcast |
| ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128 | One address at the maximum IPv6 value |
| ::/0 split to /128, index 340282366920938463463374607431768211455 | Last subnet is ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128 |
| 192.0.2.0/24 split to /24 | One unchanged subnet |
| ::ffff:192.0.2.1/128 | IPv6, one address; not reinterpreted as IPv4 |
| Two endpoints requested as LAN versus point-to-point | LAN /30; explicit point-to-point /31 |
| Sample VLSM free space | .98/31, .100/30, .104/29, .112/28, .128/25; 158 addresses; largest .128/25 |
| Noncontiguous mask, /33 IPv4, /129 IPv6, malformed literal | Explicit validation error; no misleading result |

## Functional acceptance plan after implementation
- Match known examples and every prefix boundary against independent arithmetic.
- CIDR paste/typed-prefix precedence, incomplete IPv6 edits, family switching and stale-copy prevention.
- UI walkthroughs: first calculation, edit/recalculate, /31 role change, last equal-split page,
  VLSM preview/apply/save, unsaved navigation, failed save/import and reopened draft.
- Large count copying must preserve exact digits, despite visual grouping or wrapping.
- Mobile requirement editing, keyboard visibility, focus return from sheets and error summaries.
- Real CORS access to both ipify endpoints from the published PWA origin, without credentials.
- VLSM: input reorder, stable IDs, deterministic ties, capacity exhaustion and overlap.
- IP: opt-in/decline, manual refresh, one family fails, stale response, invalid JSON,
  wrong-family response, CORS/TLS failure, timeout, cancellation and offline launch.
- Browser fixtures must prove that late responses cannot replace newer observations.
- Named plans: explicit save, export/import, corrupted/unsupported data and storage failure.
- Real iPhone/iPad Safari and installed Home Screen mode; touch, keyboard, orientation,
  safe areas, on-screen keyboard, text zoom, VoiceOver and dark mode.
- Desktop and Android browser coverage; tablet split view and narrow windows.
- Offline initial/returning visits, safe service-worker update, and no stale IP cache.
- Deploy only after tests; keep the website/Windows app pipeline functioning independently.

## Sources
- Apple UI guidance: https://developer.apple.com/design/tips/
- Home Screen web apps: https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/
- Labelled/adaptive navigation: https://developer.apple.com/design/human-interface-guidelines/tab-bars
- Data entry: https://developer.apple.com/design/human-interface-guidelines/entering-data
- Shared-core web boundary: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html
- Thread requirements: https://emscripten.org/docs/porting/pthreads.html
- Windows maths contract: [math-contract.md](math-contract.md)
- Browser storage and eviction: https://webkit.org/blog/14403/updates-to-storage-policy/
- IPv4/IPv6 public-IP endpoints: https://www.ipify.org/

## Final design approval
The recommendation is the refined adaptive UI, shared WASM core, and Advanced pins/
reservations in the first release. Initial JSON files are PWA backups; native-file
conversion is a separately reviewed enhancement.
The image is an illustrative proposal: avoid its duplicated Saved plans shortcut,
and include the explicit Apply stage and Draft state defined above even though
the image shows only the happy-path allocation summary.
The user approved the complete revised design for implementation.
Implementation verification is recorded in [the implementation plan](../superpowers/plans/2026-09-13-pwa.md). Physical iOS/iPadOS and accessibility validation remain preview limitations.

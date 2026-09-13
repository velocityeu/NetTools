# Adaptive desktop workspace upgrade

Approved task: design first, plan the upgrade, implement and test on this PC. Phone appearance already approved and must remain intact.

## Evidence
The live PC browser reports 1280 x 609 CSS pixels and devicePixelRatio 1.5. The old 76px header and 38px heading consume scarce vertical space; large card/row gaps extend results below the fold. The sidebar is fixed relative to the viewport while the header scrolls with the page, producing misalignment.

## Design
Use an application frame above 650px: header in a stable 52px grid row, navigation and workspace below. Main owns vertical scrolling; sidebar cannot drift relative to the header. Desktop body text 14px, heading24px, pointer controls34px; preserve 16px inputs and44px targets on coarse pointers. Never suppress zoom.

Wide windows (1000px+): 152px navigation rail; workspace padding16px; input panel280px, remaining width for results. Above1600px center content with maximum1440px for readable lines. Medium651–999px: navigation becomes horizontal beneath header, Saved plans moves to header, panels use available full width. Stack calculator panels if effective content width cannot hold both; narrow650px and below retain existing phone styles.

Calculator hierarchy: canonical network prominent, total/mask/host range/broadcast/capacity compact rows; wildcard,last address and offset plus explanatory guidance under More details on desktop. Keep all original results visible in the phone layout and copy/export output unchanged. Maths remains expandable. Both cards align along their top edge. Result actions remain accessible without expanding details.

Planner: compact desktop requirement rows, aligned parent/options, adaptive grids, scrolling long allocation lists within main. Public IP uses aligned two-column observation cards when space permits; single column at narrow widths. Learn/library/dialogs respect viewport height and long text.

## Upgrade plan
1. Add browser layout regressions that fail for current 1280x609 results/actions, header/rail drift and desktop heading size. Preserve existing phone geometry checks.
2. Add semantic result-details grouping and desktop-only frame/density rules. Avoid changes to arithmetic, persistence, IP requests or update protocol.
3. Test widths390,650,768,960,1024,1280,1366,1440,1920,2560 and short heights. Model high zoom through reduced CSS viewport dimensions and test real PC dimensions. Check populated IPv4/IPv6, VLSM, Public IP, Learn and dialogs.
4. Visually inspect screenshots and use the actual PC browser; run existing Chromium/WebKit workflows and module tests. Rebuild versioned offline package.
5. Publish as VEU via the organisation App, await Pages CI, then verify the live PC browser. Keep physical phone layout unchanged.

## Acceptance
No horizontal page overflow, clipped values, inaccessible controls or drifting navigation. At1280x609 and1366x650 the default IPv4 calculation's primary actions fit. Very short/tightly zoomed windows scroll naturally; fitting every long plan without scrolling is not a requirement. Phone controls retain existing dimensions. No screen-resolution/DPI assumptions or user-agent detection.

## Verification completed
- 92 Chromium and 92 WebKit browser assertions passed, including the existing functional/offline/update workflows and the new layout matrix.
- Populated VLSM and maximum-length IPv6 values checked across390–2560px widths and400–1200px heights.
- 40 arithmetic/service/offline module checks and3 packaging tests passed.
- Actual PC browser:1036x582 CSS viewport,1.5 device-pixel ratio; header0–52px, rail52px onwards, calculator action bottom507px, no horizontal overflow. Earlier PC reading1280x609 is included in automated checks.
- Touch PC/tablet check768x640: header ends60px, navigation60–116px, workspace starts116px, no horizontal overflow.
- Per-tool scroll restoration regression initially failed and passes after the fix. Phone inputs retain16px and existing mobile styles.

# Velocity NetTools — product design

Status: design in progress. This document records confirmed constraints and proposed scope; it is not authorisation to implement the Windows application.

## Confirmed

- Brand: **Velocity NetTools**. Publisher: **Velocity EU Inc**. Corporate link: https://www.velocity-eu.com/.
- Public repository: https://github.com/velocityeu/NetTools. MIT licence.
- Windows 10/11 and corresponding Windows Server families; x64 first release.
- One portable executable; no application runtime installation or external help files required.
- Stock native Windows controls. Native GDI/Direct2D drawing permitted for charts.
- Complete searchable help with examples and online resources, plus a professional About dialog.
- Companion webpage should be a training/help/refresher destination for engineers, explaining calculations simply and by hand.
- Design automatic releases from the outset; do not implement the application before final feature/UI approval.

## Recommended architecture

C++20, Win32 Unicode APIs, MSVC, Windows SDK and CMake. Use /MT for a statically linked C/C++ runtime; import APIs from system DLLs available on the chosen baseline. Embed manifest, icon, help and notices. Keep computation independent of UI and network probing. All network work must be asynchronous from the UI’s perspective, cancellable and bounded in memory/concurrency.

Exact oldest Windows builds and Server Core support need to be resolved before implementation and compatibility testing. Native controls must follow system settings, keyboard conventions, high contrast and per-monitor DPI. Avoid undocumented theme hacks.

## Proposed first workbench

| View | Features |
| --- | --- |
| Calculator | IPv4/IPv6, prefix or mask, network bounds, exact counts, classification, binary/hex, copy/export |
| Split / VLSM | Equal splits, named host requirements, reservations, pinned allocations, free space, undo/redo |
| Aggregate | Exact prefix union and range-to-CIDRs; covering-supernet operation labelled separately |
| Compare | Equality, containment, overlap and extra coverage |

Subnet arithmetic must use exact integers, validate masks, preserve the entered host while displaying the normalised network, and handle /0 and full-width prefixes without overflow. IPv6 /0 address counts require a representation that can express 2^128. Do not enumerate IPv6 hosts. Explain /31, /32, /127 and /128 explicitly.

Potential subsequent tools: visual ping, repeated traceroute, DNS lookup, interface/routing/neighbor inspection, TCP connectivity, HTTP/TLS inspection, Wake-on-LAN and MTU probing.

## Visual direction

![Initial subnet UI concept](subnet-concept-01.png)

This generated image predates the final brand name and is a design illustration, not a screenshot of a built application. Preserve the standard Win32 controls, update the title to Velocity NetTools, and consider collapsing binary details by default.

Brand palette proposal:

| Token | Colour | Purpose |
| --- | --- | --- |
| Network blue | #0A3972 | Web headings, product identity, primary action |
| Signal cyan | #16A6D5 | Mathematical emphasis and graph series |
| Velocity orange | #EF5937 | Small corporate accent and instructional notes |
| Pale blue | #EDF4FA | Web learning surfaces |
| Ink | #182D44 | Web body text |

Typography: Segoe UI family for the website and OS-selected UI font for the application; monospace only where it helps compare addresses or bits. The proposed N monogram uses a small orange accent. The actual corporate logo is not being replaced. Proposed tagline: **Clarity for every connection.** Brand name is confirmed; the icon, palette and tagline remain reviewable.

## Related designs

- [Help and About](help-and-about.md)
- [Release pipeline and download links](release-pipeline.md)
- [Engineer’s field guide](../help/README.md)

## Remaining product decisions

- Final first-release feature scope and IPv6 depth.
- Exact minimum Windows builds and Server Desktop Experience/Core behaviour.
- Settings persistence: session-only by default or optional portable sidecar file.
- Final icon, UI density and approved help navigation.
- Code-signing service/certificate and release-channel policy.

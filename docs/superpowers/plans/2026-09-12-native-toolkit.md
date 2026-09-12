# Native toolkit implementation plan

> **For agentic workers:** Use the implementation and verification skills; execute the tasks below with explicit file ownership and review.

**Goal:** Build and validate the approved portable native x64 Velocity NetTools application.
**Architecture:** Pure exact arithmetic, Windows service functions, native UI and resource-generated help are separately testable. Workers return immutable result data to the UI; no worker owns HWNDs.
**Tech Stack:** C++20, MSVC, Win32, CMake/CTest, Python for build-time resource generation only.
**Spec:** docs/design/product-design.md and its maths, diagnostic, network and Windows API contracts.

## Global constraints

- One release EXE, /MT, x64, Unicode, system DLLs only; Windows 10 build 10240+ and Server 2016+ Desktop Experience target.
- No probes or external IP lookup on startup. ipify only on explicit action.
- Official corporate assets; VEU author/committer. No personal-auth push or release.
- Use independent literal test expectations, exercise failures before implementation, and keep the app responsive during cancellation.
- User approved application development on 12 September 2026. Signing, old-OS VM evidence and GitHub App activation remain publication gates.

## Structure and ownership

- include/veu/subnet.hpp, src/core/*, tests/subnet_tests.cpp: pure exact subnetting component.
- include/veu/network.hpp, src/net/*, tests/network_tests.cpp: Windows networking and diagnostic statistics.
- include/veu/help.hpp, src/ui/help.cpp, scripts/build_native_resources.py, resources/*, tests/resources_tests.py: native embedded help and assets.
- src/ui/main.cpp, src/ui/* (except help.cpp), include/veu/storage.hpp, src/storage/*, tests/storage_tests.cpp: root owns UI, persistence and integration.
- CMakeLists.txt, CMakePresets.json, scripts/build.ps1, .github/workflows/build.yml: root owns build and validation.
- docs/implementation-progress.md: persistent execution ledger with completed tasks and rulings.

## Task 1: Exact subnet component

- [ ] Define the public interface in include/veu/subnet.hpp and send it before UI integration.
- [ ] Write and run independent tests for /0, /31, /32, /127, /128; strict parsing; maximum endpoints; exact union/difference; indexed splitting and pinned VLSM.
- [ ] Implement exact-width address and wider count arithmetic, canonical formatting and the reviewed operations.
- [ ] Run meaningful property/boundary tests and independent Python reference checks.
- [ ] Review API/overflow/limits/cancellation semantics.

Example oracle:
    calculate("192.168.10.42", "/26") => network 192.168.10.0/26, broadcast .63, total 64, usable 62
    count("::/0") => 340282366920938463463374607431768211456
    range("192.0.2.5", "192.0.2.14") => .5/32, .6/31, .8/30, .12/31, .14/32

## Task 2: Windows networking

- [ ] Publish include/veu/network.hpp with Request, result columns/rows, progress callback and cooperative cancellation.
- [ ] Write failing tests for validated targets/options, magic packet bytes, parser bounds and diagnostic statistics.
- [ ] Implement adapters, routes/neighbours, explicit ipify, timed worker ICMP/trace, DNS, TCP, HTTP/TLS, WoL and honest MTU bounds.
- [ ] Verify local fixtures and loopback integration without requiring public Internet success.
- [ ] Review all handles, result/error classification, finite deadlines and stopping behaviour.

Example oracle:
    successful RTT [10,14,22,18] => mean16, population variance20, p95=22
    timeout is absent RTT; cancelled is absent loss denominator
    magic packet MAC 00:11:22:33:44:55 => 102 bytes, 6 FF followed by 16 copies

## Task 3: Embedded native help and resources

- [ ] Generate RTF/plain searchable text from the canonical topic tree, embed the licence and official artwork.
- [ ] Test escaping/non-BMP Unicode and exact original icon packaging before integration.
- [ ] Implement a resizable native help window with topic tree, offline search, Rich Edit, navigation, copy and print; external links only on activation.
- [ ] Provide About with actual build metadata, notices, corporate links and Copy version.
- [ ] Verify resource generation and independent DLL loading/lifetime.

Interface: void veu::show_help(HWND owner, std::wstring_view context); void veu::show_about(HWND owner).
Root initializes COM and common controls; helper code owns its help windows and system Rich Edit module.

## Task 4: Native shell, files and integration

- [ ] Create native navigation and subnet tabs; form fields are per tool and preserve session state.
- [ ] Wire pure calculations first with invalid-result suppression and no-wrap navigation.
- [ ] Add explicit Start/Stop, bounded worker sessions and UI-owned snapshots for all networking tools.
- [ ] Add native graph plus equivalent result table, copy and bounded CSV.
- [ ] Test versioned JSON round-trip, malformed/unknown versions, escaping and recovery-preserving saves before implementing file commands.
- [ ] Save/Open and unsaved changes never auto-run a restored network target.
- [ ] Add DPI fallbacks, keyboard/F1, high-contrast use and useful failure states.

## Task 5: Build, verification and documentation

- [ ] Build Release x64 with CMake/CTest and the installed SDK.
- [ ] Run all component tests, native UI smoke/interaction checks and repeated start/stop/close checks.
- [ ] Inspect PE imports and embedded manifest/resources; launch from a path without build-tool sidecars.
- [ ] Add trusted build validation workflow and document application release gates; do not publish without the VEU App/signing policy.
- [ ] Review integrated changes, fix findings, update help/status and record exact limitations.

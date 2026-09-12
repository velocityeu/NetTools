# Embedded help, learning centre and About design

Status: proposed system design. Educational examples and the website preview exist; native application help and About have not been implemented.

## One manual, three destinations

Maintain canonical, versioned topic content in this repository. A future documentation build will emit:

1. GitHub-readable Markdown.
2. Static web lessons for the learning centre.
3. Embedded application resources and a local search index.

The current 15-lesson Markdown file is a snapshot of the web preview. It must not be treated as an independently maintained second source. Adopt a structured topic schema before adding the application renderer: stable topic ID, title, summary, prerequisites, content blocks, examples with expected answers, related topics, context IDs, references and last-reviewed version/date.

## Native help window

Use a resizable native window with Back, Forward, Contents and Search commands; a native tree for contents, an edit control for search, a list view for hits, and the OS Rich Edit control for text. No WebView2, CHM sidecar, JavaScript engine or help-server process. Rich Edit is provided by Windows; embed rich-text content and images as application resources.

```text
Velocity NetTools Help                         [min] [max] [close]
Back   Forward   Contents   Search [________________________]
---------------------------------------------------------------
Contents / Search results    | Subnetting by hand
  Getting started           | Plain-language explanation
  Subnet calculations       | Five numbered steps
  Planning                  | Example input + expected output
  Diagnostics               | Common mistakes
  Reference                 | Related topics + online references
---------------------------------------------------------------
Topic title / help version                           Copy   Close
```

- F1 opens the most specific topic for the focused field, then falls back to the active tool’s overview.
- Search works entirely offline and covers titles, synonyms, commands, formulas and error identifiers. Search results show a useful excerpt and topic context. Empty results suggest related terminology.
- Internal links and back/forward history work without network access. External HTTPS links open the default browser only on user action and are labelled online resources.
- Examples offer Copy input and Copy result. Any future “Open in calculator” action must populate fields without starting network probes.
- Keyboard navigation, screen-reader semantics, scalable text, high contrast, copy/select-all and printing/exporting readable topics are part of the design.
- Display the manual version alongside the app version. Version the hosted manual for historical releases; show which product version a topic describes.

## Complete manual coverage

| Chapter | Required content |
| --- | --- |
| Getting started | Supported OS/architecture, first launch, portability, keyboard navigation, finding help |
| Calculator | Every input/output field, validation, normalisation, masks, counts, classification, /0 and edge cases |
| Maths | Binary weights, powers of two, AND, host-capacity selection, octet boundaries, exact arithmetic |
| Split / VLSM | Equal splits, capacity policies, alignment, pinned allocations, conflicts, free space, undo |
| Aggregate / compare | Exact union, covering supernet, gaps, range conversion, intersection and containment |
| IPv6 | Text notation, prefixes, counts, no broadcast, link scope, /127 and /128 |
| Diagnostics | Each probe option, source interface, timeouts, cancellation, interpretation, limits |
| DNS | Record types, resolver selection, TTL/cache, negative responses, errors |
| Files / settings | CSV and plan formats, escaping, saved state, read-only media, export failures |
| Troubleshooting | Actionable error catalogue: cause, recovery and relevant context |
| Reference | Complete command/shortcut list, glossary, prefix tables, source links |
| Publisher / notices | About, version/build details, MIT text, third-party notices and corporate links |

Each feature must ship with its matching help topic and worked example. New or changed commands require a documentation review in the same pull request. Do not invent commands or error messages before their behaviour is agreed.

## Learning-centre design

An engineering reference surface, with a short introduction leading directly into the lesson browser. Fifteen current lessons cover manual subnetting, binary maths, masks, a /20, capacity planning, equal splitting, glossary, IPv4 results, VLSM, /31-/32, IPv6, aggregation, ping/traceroute, DNS and portability/help.

The page offers topic search, previous/next navigation, a prefix quick reference, a practice question with explanatory feedback, and authoritative online resources. Further lessons can add progressive exercises, copyable examples, print views and deep links. No live probes run in the page. Keep every claim about the future Windows app visibly separate from currently available teaching content.

## About dialog

Use a native dialog with embedded product icon, text controls, SysLink corporate hyperlink and stock buttons. The browser modal is a visual concept only.

Approved identity:

```text
Velocity NetTools
By Velocity EU Inc
Clarity for every connection.              [proposed tagline]

Version: [actual product version at build time]
Build: [actual commit/build identifier]
Built: [actual date]
Architecture: x64
© 2026 Velocity EU Inc
www.velocity-eu.com

[Copy version details] [Licence & notices] [Help] [OK]
```

Populate actual version data from the release metadata; do not hardcode a fictional version into a design preview. Licence & notices displays the full MIT licence and applicable third-party attributions offline. Corporate link target: https://www.velocity-eu.com/. A GitHub project link can be included alongside it.

Copy version details should copy only app/build/runtime-version information, not network inventory or personal details. Do not imply that an executable is signed unless signature verification confirms it. Product icon and wordmark do not substitute for a verified publisher signature.

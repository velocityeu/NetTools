# Velocity NetTools — proposed subnet mathematics contract

Status: design review proposal, 12 September 2026. This document closes mathematical and algorithmic ambiguities in [the product design](product-design.md); it does not authorise application development. The existing worked subnet calculations in [the lessons](../help/lessons.md) are correct. The behaviours and limits below are proposed acceptance requirements, not claims about implemented or tested software.

## 1. Input and canonical output

- An operation has exactly one address family. IPv4 addresses are 32-bit values; IPv6 addresses are 128-bit values. An IPv4-mapped IPv6 address remains IPv6. Reject mixed-family parents, pins, ranges, comparison operands and aggregation lists with a row-specific error; never convert families implicitly.
- Accept IPv4 as exactly four decimal octets, each 0–255. Reject leading zeros except the single digit `0`, abbreviated forms, integers, octal, hexadecimal and signs. This deliberately selects the unambiguous decimal-octet grammar in [RFC 3986 section 3.2.2](https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2); it is a product input policy, not a claim that every other IPv4 API uses this policy.
- Accept the IPv6 textual forms in [RFC 4291 section 2.2](https://www.rfc-editor.org/rfc/rfc4291.html#section-2.2): case-insensitive hexadecimal groups, one optional `::` that expands to at least one group, and an optional IPv4 tail occupying the final 32 bits. Apply the strict decimal-octet policy to that tail. Expanded length must be exactly 128 bits.
- Trim surrounding ASCII whitespace from each field/token; reject embedded whitespace, NULs, non-ASCII digits and trailing garbage. Calculator address input accepts a literal or `literal/prefix`; CIDR paste updates address and prefix together. A CIDR suffix is a decimal prefix, not a dotted mask. The separate prefix/mask field accepts `p`, `/p`, or, for IPv4 only, a dotted mask. Decimal prefix digits may include leading zeros and are always base 10; signs and fractions are invalid. Valid prefix lengths are 0–32 or 0–128 inclusive. A missing prefix remains incomplete, with derived results unavailable.
- Accept IPv4 masks only when their bits are a run of ones followed by zeros. Both all-zero and all-one masks are valid. Wildcards are output only; do not silently interpret a wildcard as a mask. IPv6 accepts prefix lengths only.
- First-release subnet arithmetic rejects scope suffixes such as `fe80::1%12`, URI brackets, ports and URLs. Explain that an unscoped literal is required. Do not silently remove a zone identifier: zones provide context beyond the 128 address bits. This is an explicit scope choice informed by [RFC 4007 section 11](https://www.rfc-editor.org/rfc/rfc4007.html#section-11), not a claim that scoped literals are invalid IPv6.
- Preserve the entered host value and original display input alongside the normalised network. For Calculator, parent and Aggregate/Compare CIDR inputs, normalise host bits visibly; derived operations use the displayed normalised prefix. A pinned allocation or reservation must already be aligned, and is rejected if normalisation would move its start. Imported plans must use canonical aligned network fields, with entered-host fields separate.
- Output IPv4 in ordinary decimal. Format IPv6 with lowercase digits, no unnecessary leading zeros, and `::` for the longest run of at least two zero groups, choosing the leftmost run on a tie. Input may compress a single zero group even though output does not. Render addresses in `::ffff:0:0/96` with an IPv4 tail; render other IPv6 values in hexadecimal. The formatting basis is [RFC 5952 sections 4–7](https://www.rfc-editor.org/rfc/rfc5952.html#section-4). Never compare or sort address strings as substitutes for numeric values.

## 2. Integer and interval model

Let `W` be 32 or 128, `M = 2^W`, address `A` satisfy `0 <= A < M`, and prefix `p` satisfy `0 <= p <= W`.

| Quantity | Exact definition |
| --- | --- |
| Block size `B` | `2^(W-p)` |
| Network start `N` | `floor(A/B) * B` |
| Exclusive end `E` | `N+B` |
| Last address `L` | `E-1` |
| Host offset | `A-N`, zero-based |
| Address interval | `[N,E)`; UI range is inclusive `N` through `L` |
| IPv4 mask | `M-B` |
| IPv4 wildcard | `B-1` |

Addresses need `W` bits, but counts and exclusive endpoints must also represent `M`. IPv6 `/0` contains exactly **340282366920938463463374607431768211456** addresses. An unsigned 128-bit address container alone cannot hold that count. Internal end-of-space `M` is an endpoint sentinel, never an address to format.

All operations, formatting, JSON and clipboard paths must retain exact integers. Persist counts, offsets and potentially large row indexes as decimal strings; no floating point, scientific notation or truncation. Intermediates also need care: `|A|+|B|` can exceed `M`, and requested allocation totals can exceed parent capacity. Either use a wider checked integer representation or arrange bounded subtraction/union before summation. A 129-bit final count type does not by itself make every intermediate safe. Bound numeric input length and value before allocation or conversion.

The implementation must define full-width shifts and zero explicitly; native C++ shifts by the operand width are not an acceptable implementation of `/0`, `/32` or `/128`. Avoid unchecked `last+1`, address wraparound and signed comparisons. Endianness must not change numeric ordering.

Previous/next moves by `B` only when the complete adjacent block exists: previous requires `N >= B`; next requires `E < M`. Both are disabled for `/0`. Preserve the host offset when navigating Calculator, so an entered `.42/26` advances to `.106/26` while the next network is `.64/26`.

## 3. Capacity is separate from membership

Every syntactically valid block is accepted for address-space mathematics, including special-purpose space. Classification and assignability do not alter its bounds, exact union, difference or address count.

| Family / prefix | Capacity display and endpoint treatment |
| --- | --- |
| IPv4 `/0`–`/30` | `B` total; `B-2` conventional host capacity; host interval `[N+1,E-1)`; block-end/broadcast arithmetic uses `L`. This is a convention, not a guarantee that those addresses can be deployed as a LAN. |
| IPv4 `/31` | `B=2`; explicitly labelled point-to-point; both addresses are endpoints; directed broadcast not applicable. Do not offer it as a conventional LAN allocation. |
| IPv4 `/32` | `B=1`; one address / host route; directed broadcast not applicable; no subtraction. |
| IPv6 any prefix | `B` total addresses; broadcast and generic usable-host count not applicable. `/127` is two addresses and `/128` one address. |

IPv4 point-to-point behaviour follows [RFC 3021 section 2.1](https://www.rfc-editor.org/rfc/rfc3021.html#section-2.1); `/32` host routes are identified in [RFC 4632 section 3.1](https://www.rfc-editor.org/rfc/rfc4632.html#section-3.1). A `/32` containing `255.255.255.255` still has the separate limited-broadcast classification; the absence of a subnet-directed-broadcast result does not remove that special role.

Help must explain IPv6 `/127` specifically: [RFC 6164](https://www.rfc-editor.org/rfc/rfc6164.html) applies to point-to-point links between routers, and requires disabling Subnet-Router anycast for that `/127`. It is not a blanket recommendation for host LANs. IPv6's absence of broadcast does not imply that every address is ordinary unicast; addressing roles are described in [RFC 4291](https://www.rfc-editor.org/rfc/rfc4291.html). Prefix arithmetic must accept all 0–128 lengths without implying that every length works with every link configuration.

Classification must use a versioned, embedded registry snapshot whose date and source URLs are visible in help. Use the [IANA IPv4 special-purpose registry](https://www.iana.org/assignments/iana-ipv4-special-registry/) and [IANA IPv6 special-purpose registry](https://www.iana.org/assignments/iana-ipv6-special-registry/), together with address-type rules for multicast and the [IPv4](https://www.iana.org/assignments/ipv4-address-space/) / [IPv6](https://www.iana.org/assignments/ipv6-address-space/) address-space registries. A missing special-purpose entry is insufficient evidence of global reachability. Preserve more-specific exceptions, registry footnotes and unknown/not-applicable values; do not reduce them to a single private/public Boolean. A block crossing differing classifications is mixed, with constituent ranges available. No runtime registry download is required.

## 4. Exact sets, ranges and covering prefixes

- Range endpoints are inclusive in input, same-family, and ordered `start <= last`. Convert to `[start,last+1)` with the wide endpoint type. Reject reversed ranges rather than swapping them silently.
- Canonicalise input into sorted, disjoint intervals, merging overlaps and adjacency. Duplicate addresses count once, including contained and duplicate CIDRs. Summing raw input sizes is incorrect.
- Convert a nonempty interval to CIDRs by repeatedly taking the largest aligned power-of-two block at its current start that fits before its exclusive end. At start zero, alignment can extend to all `W` bits. Emit in ascending network order. This produces an exact minimal CIDR cover of that interval without enumerating addresses.
- Exact aggregation returns the minimal canonical CIDR representation of the union, with no extra addresses. Adjacent equal-size prefixes merge only when they are aligned siblings; adjacency alone is insufficient.
- A covering supernet is the smallest *single* prefix containing a nonempty union: find the common leading bits of the union's minimum address and maximum included address. Equal endpoints produce `/W`; endpoints differing in their highest bit produce `/0`. Extra coverage is `cover \ union`, and its exact count is `cover size - union size`. Show the extra ranges/CIDRs as well as the count. Empty input has an empty exact union and no covering prefix.
- Compare uses normalised sets `A` and `B`. Equality means identical membership, containment includes equality, and overlap means a nonempty intersection. Return `A intersection B`, `A \ B` and `B \ A`, their counts, and their exact CIDRs. Differences are directional. A single pair of aligned CIDRs can only be disjoint or have one contain the other; partial overlap is meaningful for arbitrary ranges or sets.
- Empty-set semantics are explicit: two empty sets are equal, every set contains the empty set, and an empty set overlaps nothing. An incomplete UI field is an input error, not an intentional empty set.

These are mathematical operation definitions; the CIDR representation is grounded in [RFC 4632](https://www.rfc-editor.org/rfc/rfc4632.html). They describe address membership and do not assert that advertising an aggregate route is operationally appropriate.

## 5. Equal split and deterministic VLSM

Equal split accepts child prefix `q` with `p <= q <= W`, or an exact positive power-of-two part count `K` with `K <= 2^(W-p)`. Compute `K=2^(q-p)`; equal-prefix splitting into three parts is invalid. `q=p` returns one unchanged block. Child index `i`, starting at zero, yields start `N+i*2^(W-q)`, for `0 <= i < K`. Generate requested pages directly from indexes; do not build preceding pages or a native ListView with `2^128` items. No host enumeration is required.

VLSM has one parent, named requests, named reserved CIDRs and pinned allocations. Proposed request rules:

- IPv4 conventional LAN: require an integer `H >= 1`, including gateway/router interfaces. Choose the smallest block with `B-2 >= H`, constrained to `/0`–`/30`. One or two required hosts therefore need `/30`. Reject `H > 4294967294` before computing `H+2`; parent capacity can be smaller.
- IPv4 point-to-point: an explicitly selected row type with one or two required endpoints gets `/31`; a larger request is invalid. It never silently changes to LAN rules. A single host-route request is explicitly `/32`, rather than a one-host LAN.
- IPv6: request a child prefix, never an IPv4-style host count. Requested blocks must fit the parent prefix, irrespective of address classifications.

The allocation contract is:

1. Validate the complete proposal before allocating. Pins and already assigned networks must be aligned, within the parent, match the family, satisfy their request, and be mutually disjoint. Reservations must be aligned and within the parent. Overlapping reservations contribute their union to unavailable space; retain their labels and show that they overlap. No allocation may overlap any reservation. Invalid constraints stop the operation with the responsible rows identified.
2. Ordinary **Allocate** fills unassigned requests and preserves every existing assignment. Moving unpinned assignments requires an explicit reallocation action and a preview of the moves; pinned assignments never move. Changing a parent or request cannot silently invalidate an existing assignment.
3. Start with the parent minus reservations and preserved allocations, represented as canonical free CIDRs or intervals. Sort unassigned requests by required block size descending, then by a stable insertion sequence stored in the plan. Renaming rows, sorting the view or reopening the same plan must not change ties.
4. For each request, choose the lowest numeric aligned start in the free space at which its complete block fits. Remove exactly that block from free space. Continue after a request cannot fit, so smaller requests can still be proposed. Never split one request over several blocks.
5. Report each unallocated row and distinguish invalid input from insufficient total space and insufficient contiguous aligned space. Show both total free address count and the largest free aligned CIDR. Do not claim an optimal rearrangement of pins or a globally optimal subset of requests.
6. Present allocations, unchanged pins, remaining free CIDRs and all unallocated rows as one completed preview. Applying a partial allocation is explicit and keeps failed rows unassigned. Apply/undo/redo act on the complete plan transaction; a failed, cancelled or superseded calculation leaves the committed plan intact.

## 6. Proposed resource and cancellation limits

These are first-release design defaults to validate on the oldest supported machines. Adjustments require updating the visible limits and acceptance cases together; there must be no hidden or silent truncation.

| Resource | Proposed bound / behaviour |
| --- | --- |
| Explicit input rows | 10,000 combined rows per operation, including both Compare sides; VLSM requests, pins and reservations share this limit |
| Input file or bulk paste | 16 MiB UTF-8; reject before retaining an oversized complete payload |
| Numeric token | At most 39 decimal digits for IPv6 counts/indexes; then enforce the operation's exact value bound; IPv4 requests use the stricter host bound above |
| Result page | At most 200 prefix/range rows; exact total count and direct page/offset navigation |
| Working data | 128 MiB per calculation, tracked before growth; leave the previous plan intact on limit or allocation failure |
| Export | At most 1,000,000 rows and 256 MiB per export; preflight known row counts, stream bounded batches, and stop safely at the byte limit |
| Long computation | Worker operation; check cancellation and resource budgets between bounded chunks, at least every 1,024 records and before/after sort/format/export stages; no monolithic uninterruptible bulk operation |
| Responsiveness target | UI remains operable; target cancellation acknowledgement within 250 ms on the compatibility test machine; subdivide sorting and other expensive phases as needed |

An equal split with more than the export cap still has valid exact counts and browsable pages; exporting the complete set explains the limit and requires a smaller selected interval. Explicitly choosing to export `/32` or `/128` child prefixes is still bounded by the export limit; the tool must not enumerate the addresses within every child as an additional operation. Cancellation or an export limit must not replace a destination file with partial content. Use a temporary destination and commit only on complete success.

Each calculation carries an input revision. A worker may publish a result only if that revision is still current; editing while an operation runs cannot resurrect stale valid results. A cancelled preview can leave a previously completed preview visible only when clearly identified as belonging to older inputs, with its Apply/derived-export actions unavailable.

## 7. Acceptance examples

These are proposed test vectors for development, not application tests that have already passed. Expected counts below are exact; explanatory thousands separators must be removed in serialized decimal values.

### Parsing, bounds and capacity

| Input / operation | Expected result |
| --- | --- |
| `192.168.10.42/26` | Network `.0/26`, last `.63`, offset 42, 64 total, conventional hosts `.1`–`.62`, 62 capacity |
| `172.16.37.19` with mask `255.255.240.0` | `/20`, network `172.16.32.0`, last `172.16.47.255`, 4,096 total, 4,094 conventional capacity |
| Masks `0.0.0.0`, `255.255.255.255` | Accept as `/0`, `/32` respectively |
| Masks `255.0.255.0`, `255.255.255.1`, `0.0.0.255` | Reject as noncontiguous; do not reinterpret as wildcards |
| `192.168.001.1`, `127.1`, `0xc0000201`, `256.0.0.1`, `+1.2.3.4` | Reject |
| Address `192.0.2.1` with `/024` | Accept `/24`; address without any prefix remains incomplete |
| `2001:0DB8:0:0:1:0:0:1/64` | Host `2001:db8::1:0:0:1`; network `2001:db8::/64`; total 18,446,744,073,709,551,616 |
| `2001:db8::1:1:1:1:1/128` | Accept single-group compression; output `2001:db8:0:1:1:1:1:1/128` |
| `2001::db8::1`, `1:2:3:4:5:6:7:8::`, `::ffff:192.0.002.1` | Reject malformed/ambiguous text |
| `fe80::1%12/64`, `[2001:db8::1]:443`, `192.0.2.1:80` | Reject with the specific unsupported scope/port syntax reason |
| IPv4 `/33`, IPv6 `/129`, either family `/-1`, `/1.5` | Reject |
| `::ffff:192.0.2.1/120` | IPv6; network `::ffff:192.0.2.0/120`; 256 total; no IPv4 host rule |
| Compare `192.0.2.1/32` and `::ffff:192.0.2.1/128` | Family mismatch; do not report equality |
| `255.255.255.255/0` | Network `0.0.0.0`, last `255.255.255.255`, 4,294,967,296 total, offset 4,294,967,295, both navigation buttons disabled; mixed classification |
| `192.0.2.11/31` | Network `.10`, last `.11`, 2 point-to-point endpoints, offset 1, directed broadcast not applicable |
| `255.255.255.255/32` | One address, last equal to first, offset 0, next disabled, limited-broadcast classification retained |
| `ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/0` | Network `::`, last unchanged, total `340282366920938463463374607431768211456`, offset one less, navigation disabled |
| `2001:db8:0:0:8000::1/65` | Network `2001:db8:0:0:8000::/65`; last `2001:db8::ffff:ffff:ffff:ffff`; 9,223,372,036,854,775,808 total; offset 1 |
| `2001:db8::11/127` | Bounds `2001:db8::10`–`2001:db8::11`, 2 total, no broadcast or generic usable-host count |
| `ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128` | One address, offset 0, next disabled; previous is the address ending `fffe` |
| Classification `192.0.0.9/32` and `192.0.0.0/24` | More-specific PCP anycast registry entry for the host; mixed detailed treatment of the containing block, not inherited parent flags for every address |

### Exact aggregation and comparison

| Input / operation | Expected result |
| --- | --- |
| Union `192.168.10.0/25`, `.128/25`, `.0/25` | Exactly `192.168.10.0/24`, 256 addresses; duplicate ignored |
| Union `192.168.10.0/24`, `.64/26` | Exactly the `/24`, 256 addresses; contained prefix adds no count |
| Union `192.168.10.64/26`, `.128/26` | Two `/26`s; they are adjacent but not siblings; single covering `/24` adds 128 addresses |
| Union `192.168.10.0/26`, `.128/26` | Two `/26`s, 128 addresses; cover `/24`; extra `.64/26` and `.192/26`, 128 addresses |
| Range `192.0.2.5`–`192.0.2.14` | `.5/32`, `.6/31`, `.8/30`, `.12/31`, `.14/32`; 10 addresses |
| Full IPv6 range `::` through all-`ffff` | Exactly `::/0`; count `2^128`, no endpoint overflow |
| Union `::/1`, `8000::/1` | Exactly `::/0` |
| `A=192.0.2.0/24`, `B=192.0.2.64/26` | Intersection `B` (64); `A\B` is `.0/26` and `.128/25` (192); `B\A` empty |
| Ranges `A=.5`–`.14`, `B=.10`–`.20` in `192.0.2.0/24` | Intersection `.10`–`.14` (5); `A\B=.5`–`.9` (5); `B\A=.15`–`.20` (6); neither contains the other |
| `::/0` minus `::/128` | 128 CIDRs, first `::1/128`, last `8000::/1`; exact count `340282366920938463463374607431768211455` |
| `::/0` minus `::/0` | Empty, count zero; no negative or wrapped count |
| Reversed range `.14`–`.5` | Validation error; no output or mutation |

### Splits and allocations

| Input / operation | Expected result |
| --- | --- |
| `192.168.10.0/24` into four | `.0/26`, `.64/26`, `.128/26`, `.192/26`; total union equals parent |
| Same `/24` into three; child `/23` | Reject both |
| `2001:db8:1234::/48` into `/64` | 65,536 children; first `2001:db8:1234::/64`, last `2001:db8:1234:ffff::/64` |
| `::/0` into `/128` | `2^128` children; page of at most 200 produced directly; index `2^128-1` is all-`ffff`; index `2^128` rejected; full export rejected by row limit |
| LAN requirements 1, 2, 3, 50, 75, 100 | Prefixes `/30`, `/30`, `/29`, `/26`, `/25`, `/25` respectively |
| LAN H=0, negative, fractional, 4,294,967,295 | Reject; LAN H=4,294,967,294 needs `/0` before parent-fit checks |
| `/24` parent with Office=100, Voice=50, Lab=20 | `.0/25`, `.128/26`, `.192/27`; free `.224/27` as in the lesson |
| `192.0.2.0/24`; pin `.64/26`; requests Large=100, Small=50 | Large `.128/25`, Small `.0/26`, pin unchanged, no free space; repeating and renaming produce identical placement |
| `192.0.2.0/24`; pins `.64/26`, `.192/26`; LAN H=100 | Unallocated: 128 free addresses exist, but the two free `/26`s contain no aligned `/25` |
| `192.0.2.0/24`; reserved `.0/26`; two 50-host rows inserted A then B | A `.64/26`, B `.128/26`, free `.192/26`; sorting view B first does not change allocations |
| Same-size requests A, B, C; space for only two | A and B allocated by stored insertion order; C reported unallocated; partial Apply is explicit |
| Pin `192.0.2.65/26`; outside-parent pin; overlapping pins; pin inside reservation | Reject each complete proposal before allocating; no silent alignment or movement |
| Existing `.0/26` assignment followed by a new 100-host request | Existing assignment stays `.0/26`; new request goes `.128/25`; ordinary Allocate does not pack existing rows again |

### Required verification beyond examples

Use an independent exact reference implementation for boundary vectors and generated properties during development. Cover every prefix length, especially 0, 1, 31, 32, 63, 64, 65, 127 and 128, with starts/ends around carry boundaries. Do not use the application's own helpers as its only expected-value oracle.

- Parse/format/parse preserves address value and family; canonical output is idempotent. Fuzz invalid inputs, pasted Unicode lookalikes, excessive lengths and damaged saved plans.
- Every CIDR has aligned start, positive power-of-two size, exact bounds and contains its preserved host. IPv4 mask-to-prefix and prefix-to-mask round-trip for all 33 prefixes.
- Exact union is permutation-invariant and idempotent. Its output is disjoint, sorted, has no contained entries or remaining mergeable siblings, and covers precisely the original set. Sample small address domains exhaustively as well as random full-width intervals.
- `A = (A\B) union (A intersection B)` as a disjoint partition; corresponding counts add exactly. Test `A=B=::/0` to catch overflowing inclusion-exclusion intermediates.
- Equal children are aligned/disjoint, their exact union is the parent, and page boundaries introduce neither omissions nor duplicates. Test indexes near `2^64` and `2^128` without materialising the sequence.
- VLSM allocations satisfy their capacity, remain within parent, avoid reservations and each other, preserve pins, and partition parent with free/reserved space. Save/Open, undo/redo, rename and view-sort preserve stable tie order and assignments.
- Test exactly at and immediately above each input/export limit; cancellation during parsing, sorting, allocation, decomposition and export; out-of-memory handling; stale worker completion after input changes; destination write failure. None may commit a partial or obsolete plan/export.

The shared help source now includes worked lessons for IPv6 `/127` and `/128`, strict input errors, range-to-CIDRs, directional Compare, pinned/fragmented VLSM, and `/0`. Before calling native help complete, document the agreed paging/export limits and every shipped command/error, then validate examples against the implemented engine. The original arithmetic examples required no correction.

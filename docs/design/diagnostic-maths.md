# Velocity NetTools — diagnostic measurements and interpretation

Status: proposed design contract, 12 September 2026. This specifies visual Ping, repeated Traceroute and MTU measurements for the expanded toolkit scope. It contains no application implementation. Pair it with the Windows API contract for lifecycle, cancellation, limits and platform validation.

## 1. A probe is an observation with a terminal outcome

Keep an immutable probe identity, session/configuration revision, target address and family, source/interface context, scheduled and actual start times, sequence number, timeout, payload size, probe type and (for Traceroute) round and TTL/Hop Limit. Use a monotonic clock for elapsed time and deadlines; retain wall-clock time only for human timestamps. A changed target, family, source, size or measurement configuration starts a new series segment.

Resolve a hostname before starting that segment and show the selected literal address. Record resolution duration and resolver errors separately. Neither forward DNS time nor asynchronous reverse-DNS time contributes to RTT, timeout loss or hop delay. Keep addresses visible while PTR names arrive; names never replace address identities. A later address change starts a separate segment instead of mixing different destinations into one latency distribution.

Each attempted probe ends in exactly one of these states, or remains pending:

| Symbol | State | Meaning |
| --- | --- | --- |
| `R` | Expected reply | One matched successful Echo Reply for Ping; the expected matched hop/destination response for Traceroute |
| `T` | Timeout | No accepted result by the probe's actual measurement deadline |
| `E` | Network/protocol error | A matched network error outcome such as destination unreachable; retain its exact code and available provenance |
| `F0` | Immediate local send error | Submission rejected locally before the API accepted the operation |
| `F1` | Later local operation error | API accepted submission, but a later local error prevents a valid network outcome |
| `P` | Pending | Accepted operation whose measurement has not reached a terminal outcome |
| `C` | Cancelled | Accepted operation cancelled before a measurement result became terminal |

API/IP status mapping must be explicit and tested: resource exhaustion, invalid parameters and local setup failures are not evidence of network loss. Preserve the original status when provenance is uncertain; classify a generic local/API failure as an operation error rather than asserting a remote router sent it. A nonzero reply count does not make every returned status a successful Echo Reply. Windows exposes separate success, timeout, unreachable, packet-too-big and local failure statuses in [ICMP_ECHO_REPLY](https://learn.microsoft.com/en-us/windows/win32/api/ipexport/ns-ipexport-icmp_echo_reply).

Scheduled work never attempted because of pause, Stop or a concurrency limit is **not sent** and belongs in a separate skipped/not-started count. Skipped slots remain visible as a scheduling gap. Do not manufacture timeout samples for them or dispatch catch-up bursts after a pause.

An accepted asynchronous operation is not a measured reply and is not proof that a packet appeared on the wire. In particular, `ERROR_IO_PENDING` is a successful pending indication for [IcmpSendEcho2](https://learn.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmpsendecho2). The [Icmp6SendEcho2 documentation](https://learn.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmp6sendecho2) says its Timeout parameter is only used synchronously: asynchronous IPv6 needs an application measurement deadline, with underlying operation/buffer ownership maintained until safe cleanup. Measurement timeout and kernel-operation lifetime are separate concepts.

Use the API's documented result/deadline semantics and a deterministic race policy. A valid result already recorded before cancellation remains recorded; cancellation cannot overwrite it. Once a probe is terminal, late results or callbacks cannot resurrect it or change a subsequent session. If an application deadline is needed, resolve completion/deadline races on the worker with the available completion information, not when the UI happens to repaint. Duplicate and unmatched replies do not create extra successful probes; retain them as annotations/counters. An observed late reply leaves the original timeout classification unchanged and is labelled late.

## 2. Counts and denominators shown by visual Ping

For a specified session segment or rolling window, calculate:

| Display | Definition |
| --- | --- |
| Attempted (details) | `A = R + T + E + F0 + F1 + P + C` |
| Sent | `S = A - F0`; tooltip: requests accepted by the API, not a wire capture |
| Completed | `D = R + T + E`; these are the probes eligible for reply-loss measurement |
| Received | `R`; one expected Echo Reply per probe |
| Lost / no Echo Reply | `L = T + E`; show separate Timeout and Network error counts |
| Reply loss | `100 * L / D`, or **not available** if `D=0` |
| Pending / cancelled | `P` and `C`, shown separately |
| Send/operation errors | `F0 + F1`, shown separately and excluded from `D` |

The display label is **Echo reply loss**, not an unqualified claim of packet loss on a particular link. An ICMP error counts as failure to obtain the expected Echo Reply; it is not called a timeout. Show the numerator and completed denominator with the percentage, for example `2 / 6 completed (33.33%)`. Because accepted submissions may later fail locally, `Sent = Completed + Pending + Cancelled + F1`; do not force `Sent = Received + Lost` during a run.

Pending, cancellation and local errors contribute to neither numerator nor denominator. Their counts must remain visible so a display of 0% cannot conceal a run in which few or no probes completed. If all attempts are cancelled or fail locally, reply loss is unavailable, never 0% or 100%.

These are declared application metrics. ICMP does not guarantee either delivery of the request or generation/delivery of an error response, so a timeout alone cannot identify which direction failed or prove a host is offline. See [RFC 792](https://www.rfc-editor.org/rfc/rfc792.html) and [RFC 4443](https://www.rfc-editor.org/rfc/rfc4443.html).

## 3. RTT, variation and percentiles

Use only the `n=R` successful Ping RTT samples, in milliseconds. Never substitute zero or the configured timeout for a missing RTT. The baseline Windows Echo API returns integer-millisecond RTT; label that resolution and preserve its zero value as `0 ms (API resolution)` rather than inventing sub-millisecond precision. A local stopwatch around an API call includes scheduling and completion overhead, and must not silently replace the API RTT in the same series. The documented units are in [ICMP_ECHO_REPLY.RoundTripTime](https://learn.microsoft.com/en-us/windows/win32/api/ipexport/ns-ipexport-icmp_echo_reply).

For sorted successful samples `x[1] ... x[n]`:

- Minimum and maximum are the smallest and largest samples.
- Mean is `sum(x)/n`.
- **RTT standard deviation (population)** is `sqrt(sum((x-mean)^2)/n)`. This describes the observed collection; it is not the `n-1` sample estimator. For one observation it is zero, shown with `n=1`; for no observations all RTT statistics are unavailable. The distinction from sample standard deviation is deliberate; [NIST's measures of scale](https://www.itl.nist.gov/div898/handbook/eda/section3/eda356.htm) describes the sample convention.
- **p95 (nearest rank)** is `x[ceil(0.95*n)]`, using one-based indexing and no interpolation. For `n=1`, it is that sample. State the method and sample count; small samples do not imply a stable estimate of future performance.

Compute before display rounding. Use stable mean/variance accumulation or recomputation of the bounded retained window; do not use cancellation-prone subtraction of two large squared sums. Check counter overflow. A lifetime mean/min/max/standard deviation can be accumulated without keeping all raw probes. An exact lifetime percentile requires retained samples or an exact value-frequency representation; never silently turn a downsampled-chart percentile into the lifetime p95. Where exact lifetime p95 is unavailable, label it unavailable and offer the exact rolling-window p95.

**RTT variation (mean adjacent change)** is the mean of `abs(RTT[i] - RTT[i-1])` only for pairs whose original consecutive probe sequence numbers both have successful Echo Replies, lie inside the selected window, and belong to the same uninterrupted configuration segment. Publish the number of eligible pairs. Timeout, error, cancellation, skipped scheduling slot, pause and configuration change break adjacency. Completion arrival order must not reorder sequence adjacency. With zero eligible pairs, variation is unavailable, not zero.

Do not call this metric RTP jitter or use its value in RTP quality formulas. [RFC 3550 section 6.4.1](https://www.rfc-editor.org/rfc/rfc3550.html#section-6.4.1) defines RTP interarrival jitter from differences in packet transit times in RTP timestamp units and a `1/16` smoothing update, in arrival order. An Echo RTT sequence does not supply that RTP measurement. The short UI label can be **RTT variation**, with the full adjacent-pair definition in its tooltip/help.

## 4. Rolling windows and chart meaning

The default time-window cohort is probes whose **actual start time** is in `(now-windowDuration, now]`. The same cohort drives counts, RTT statistics and table rows. Do not select successes by reply arrival time while selecting timeouts by send time. Probes still pending inside the cohort are shown as pending. A completion whose start has fallen outside the rolling window affects the applicable session totals, but cannot be inserted as a fresh rolling-window sample.

Charts plot against start time and keep separate outcome markers. Successful RTTs occupy the RTT axis; timeout, network error, local error, cancellation and pending use distinguishable markers or a separate status strip, with text/shape meaning in addition to colour. A missing response is a **gap** in the RTT line. Do not connect a line through a timeout or carry the previous successful value over it. A timeout threshold can be shown as a labelled reference line; it is not the RTT of a lost probe.

When a time range contains more samples than screen pixels, aggregate into labelled time bins. Each bin retains counts `R,T,E,F0,F1,P,C`, success minimum/mean/maximum and an exact count of represented probes. Draw a success min–max envelope/mean with a visible status/loss indicator, and disclose bin duration in the tooltip. A bin containing one timeout cannot become an unqualified green success because its remaining probes replied. An empty bin is no data; an all-timeout bin has no RTT; neither has a zero mean.

Compute overall and rolling metrics from the underlying cohort, not from means or percentiles of bins. Combining bin means requires weighting by their successful sample counts; combining percentiles is not generally valid. Zooming, resizing and changing chart-bin width must not change the reported loss, RTT statistics or p95 for the same cohort. Show the applicable cohort/window and sample counts in exports as well as on screen.

## 5. Repeated Traceroute measures responses at hop limits

Retain `(round, target segment, TTL/Hop Limit, probe index)` for every probe, including timeouts. IPv4 uses TTL and ICMP Time Exceeded; IPv6 uses Hop Limit and ICMPv6 Time Exceeded. A matched Time Exceeded response is an expected hop observation for Traceroute, even though it would be a failed Echo outcome in ordinary Ping. A matched destination Echo Reply marks the destination reached for that round. An unreachable/prohibited response is shown with its specific error; it does not mean the destination was reached. Message formats and roles are described in [RFC 792](https://www.rfc-editor.org/rfc/rfc792.html) and [RFC 4443](https://www.rfc-editor.org/rfc/rfc4443.html).

- At each hop-limit row, show probes completed, expected hop/destination replies, timeouts, network errors, local errors, pending and cancelled. Use the Ping denominator rules with the Traceroute definition of `R`. Label failure percentages **missing expected hop responses**, not forwarding loss at that router. Display network errors separately from timeout counts.
- Maintain all distinct numeric responders at a hop limit, with their own RTT distributions and reply counts. Aggregate hop-row RTT combines a disclosed mixture of those responses. Do not merge different addresses because they share a PTR name.
- A hop response can have an unavailable RTT if the backend does not provide a meaningful timing value for that response status. Count the response, but leave its RTT absent; do not turn an unset timing field into zero. Show the timed-sample count separately when it is smaller than the response count. A backend using locally measured elapsed time must label that basis and resolution consistently rather than mix it silently with API-provided RTTs.
- Missing probes cannot be assigned to a particular responder when several paths are possible. For a responder, show replies and share of observed responses; do not invent per-router loss using all probes at that TTL as if each was routed to that router. Direct Ping to that responder would be a separate measurement.
- For destination statistics, count only probes actually intended to test/reach the destination in that mode. TTL-limited intermediate probes cannot be counted as destination Echo losses. A round stopped after reaching the destination has unattempted higher hops, not lost higher-hop probes. A round cancelled partway through remains visibly incomplete.
- Keep rounds distinct when comparing observations. Expose target, source, probe protocol, size and any available flow context. Load balancing can select different next hops from packet header fields; a set of responders collected across probes is not necessarily one path a single packet took. [RFC 2992](https://www.rfc-editor.org/rfc/rfc2992.html) describes an ECMP selection model. Do not claim flow-stable or Paris-traceroute behaviour unless the actual backend controls and validates the necessary packet fields.

Routers may limit ICMP response generation independently of forwarding. [RFC 1812 section 4.3.2.8](https://www.rfc-editor.org/rfc/rfc1812.html#section-4.3.2.8) discusses rate limiting for IPv4 errors and other responses. Thus a hop with high missing-response percentage followed by a responsive destination is not evidence that the displayed percentage of transit traffic is dropped there. Timeouts can also reflect filtering, return-path behaviour or a router that does not answer that probe type.

Hop RTT includes the outbound trip, responder processing and return path; it is not a one-way link delay. Subtracting adjacent hop RTTs does not measure that link's delay and can legitimately produce a negative number. Do not draw definitive failed links or add hop RTTs into an end-to-end latency. If responders change, label **observed responders changed**. Unknown hops and separate probe paths prevent a conclusive physical-route or link-failure claim.

## 6. MTU arithmetic and evidential limits

MTU is the IP packet size accepted without the relevant IP-layer fragmentation, including the IP header and excluding link-layer headers. Distinguish **Echo data bytes**, **calculated IP packet bytes**, **reported local/interface MTU**, **reported next-hop MTU**, and **tested path-size bounds**. They are different values.

| Probe layout | Calculated IP packet bytes `B` |
| --- | --- |
| IPv4 with standard 20-byte header, no options | Echo data `P + 20 + 8 = P + 28` |
| IPv4 with actual header length `H` | `P + H + 8`; IPv4 options change `H` |
| IPv6 with 40-byte base header, no extension headers | Echo data `P + 40 + 8 = P + 48` |
| IPv6 with extension headers of total size `X` | `P + 40 + X + 8` |

The Echo header is separate from its data. Packet formats come from [IPv4, RFC 791](https://www.rfc-editor.org/rfc/rfc791.html), [ICMPv4, RFC 792](https://www.rfc-editor.org/rfc/rfc792.html), [IPv6, RFC 8200](https://www.rfc-editor.org/rfc/rfc8200.html), and [ICMPv6, RFC 4443](https://www.rfc-editor.org/rfc/rfc4443.html). If the application embeds its own marker in Echo data, those marker bytes are part of `P`. Ethernet/VLAN framing is not added to this IP-MTU calculation. VPN/tunnel overhead may reduce the usable inner-packet size; do not subtract a guessed overhead without a known encapsulation.

For IPv4, require DF on the outgoing test packet and disclose it. Microsoft's [IP_OPTION_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/ipexport/ns-ipexport-ip_option_information) documents `IP_FLAG_DF` for IPv4. A successful fragmented exchange does not establish a lower bound for an unfragmented packet of its original size. A matched fragmentation-needed response may supply a next-hop MTU; an older response may leave that field zero, meaning no numeric MTU was supplied. [RFC 1191](https://www.rfc-editor.org/rfc/rfc1191.html) defines the MTU interpretation and this older-message case.

IPv6 routers do not fragment packets, but the **source can**. Therefore a successful large ICMPv6 Echo does not by itself prove the original datagram crossed the path unfragmented. [RFC 8200 sections 4.5 and 5](https://www.rfc-editor.org/rfc/rfc8200.html#section-4.5) define source fragmentation and the 1280-byte IPv6 minimum link MTU. An implementation must validate the chosen Windows API's actual source-fragmentation control, packet construction, cached PMTU behaviour and status reporting on supported systems. The documented IPv4 DF flag must not simply be assumed to enforce IPv6 behaviour. If the backend cannot establish unfragmented IPv6 transmission, present a payload reachability test or **MTU measurement unavailable for this backend**, not a confirmed PMTU.

Keep payload limits below both protocol limits and the validated API/buffer limits. A `WORD RequestSize` is a parameter type, not a promise that every value through 65535 is legal or supported. Without options/extension headers or jumbograms, the protocol arithmetic gives maximum Echo data of 65507 bytes for IPv4 and 65527 for IPv6; the latter's 16-bit IPv6 Payload Length excludes its 40-byte base header. The actual tool limit can be smaller. Reject out-of-range sizes before conversion; no integer truncation, silent clamping or buffer-size overflow. This feature does not require jumbograms.

### A bounded search reports evidence, not a guessed exact MTU

Maintain the greatest tested unfragmented successful IP size `L`, and an upper bound `U` only when supported by a matched packet-too-big/fragmentation-needed result or another explicitly identified constraint. Test sizes as integers with declared headers. A timeout is **inconclusive**, not an MTU failure and not a reason by itself to reduce `U`. Retrying a size is bounded by the configured attempts; small control probes can help show general reachability but cannot prove why a larger probe timed out.

A too-big result for size `B` implies an observed constraint below `B`; a validated advertised next-hop MTU can give a tighter bound. Do not treat a zero/absent field as MTU zero, or increase a bound in response to a too-big report. Keep local send/interface constraints distinguishable from a reported remote next hop, and identify unavailable API detail honestly. ICMP filtering can prevent PMTU discovery, as described in [RFC 8201](https://www.rfc-editor.org/rfc/rfc8201.html).

Binary refinement is appropriate only inside a bracket supported by actual successes and explicit size failures under a stable test context. All successes through the configured ceiling mean **at least the largest tested size**, not that the ceiling equals the PMTU. Success at size `B` followed by timeout at `B+1` also leaves the upper bound unknown. Contradictory results, changed routes/interfaces or different responding paths invalidate a single monotonic bracket; show the observations and mark the estimate inconclusive instead of forcing a number.

Even a bracket narrowed to one value is labelled **observed path-MTU estimate**, with source, destination, family, headers, fragmentation policy, time, successful-size evidence and failure/report evidence. A large Echo Reply might itself take another route or be fragmented; a successful unfragmented request supports the tested forward path, not an equal reverse-path MTU or every protocol/flow's path. Cached/local values are reported as such, not promoted to fresh measurements.

## 7. Worked acceptance vectors

### Ping state and RTT example

Use one unchanged configuration segment with the following attempted sequences:

| Sequence | Outcome |
| --- | --- |
| 1 | Reply, 10 ms |
| 2 | Reply, 14 ms |
| 3 | Timeout |
| 4 | Reply, 22 ms |
| 5 | Reply, 18 ms |
| 6 | Matched destination-unreachable error |
| 7 | Pending |
| 8 | Cancelled after API acceptance |
| 9 | Immediate local send rejection |
| 10 | Local operation failure after API acceptance |

Expected: Attempted 10; Sent 9; Completed 6; Received 4; Lost/no Echo Reply 2, broken into one timeout and one network error; Pending 1; Cancelled 1; Send/operation errors 2. Echo reply loss is `2/6 = 33.333...%`, displayed as 33.33%. It is not `2/9`, `6/10` or `6/9`.

Successful RTTs are `[10,14,22,18]`: minimum 10 ms; mean 16 ms; maximum 22 ms; population standard deviation `sqrt(20) = 4.47213595... ms`; p95 22 ms. The two eligible variation pairs are sequences 1–2 and 4–5, each 4 ms, so RTT variation is 4 ms over two pairs. Sequences 2–4 do not form a pair because sequence 3 timed out.

A window containing only sequences 3–5 has one timeout and two replies: loss `1/3 = 33.333...%`; RTT mean 20 ms, population standard deviation 2 ms, p95 22 ms; one variation pair, 4 ms. A chart bin containing sequences 1–5 shows four replies, one timeout, a 10–22 ms success envelope, mean 16 ms and loss `1/5 = 20%`; its timeout remains visible.

Additional exact cases: no completed probes gives unavailable loss; one 7 ms reply gives min/mean/max/p95 7 ms, population SD 0 ms and unavailable variation; sorted RTTs 1–20 ms give nearest-rank p95 **19 ms**. Replies arriving in order 2,1,4,5 retain the same sequence-based variation. A late reply to sequence 3 does not erase its timeout. DNS taking 100 ms followed by a 10 ms Echo produces separate resolution 100 ms and RTT 10 ms, not RTT 110 ms.

### Traceroute interpretation examples

- Hop 2 returns 2 expected replies out of 10 completed probes and times out 8; destination-directed probes return 10 of 10. Display hop 2's missing expected responses as 80%, destination failure as 0%, and no conclusion of 80% forwarding loss at hop 2.
- One TTL has responder A four times, responder B four times and two timeouts. Display eight observed responses and two missing responses (20% at that TTL); each responder's share of observed responses is 4/8 = 50%. Do not label each responder as having 60% packet loss.
- Hop 3 RTT is 30 ms and hop 4 RTT is 20 ms. Keep both observations; do not report a negative 10 ms physical link latency or subtract the value from a path total.
- A round reaches the destination at hop 5. Hops 6 and above are not attempted in that round and contribute no losses. Cancelling during hop 4 does not turn remaining hop slots into timeouts.

### MTU arithmetic and result examples

| Observation / input | Expected interpretation |
| --- | --- |
| IPv4 Echo data 1472, no options | IP packet 1500 bytes |
| IPv4 Echo data 1472 with a 24-byte IP header | IP packet 1504 bytes; a 1500-byte target permits data 1468 |
| IPv6 Echo data 1452, no extension headers | IP packet 1500 bytes |
| IPv6 Echo data 1232, no extension headers | IP packet 1280 bytes; arithmetic does not guarantee a reply |
| Target IP size 1492 | Data 1464 for plain IPv4 or 1444 for plain IPv6 |
| Verified IPv4 DF success at data 1472; matched too-big at data 1473, reported MTU 1500 | Supported observed estimate 1500, for this test context |
| Verified DF success at data 1472; timeout at 1473 | Lower bound 1500; upper bound unknown; no exact PMTU claim |
| Every tested packet succeeds through IP size 1500 | At least 1500 tested; search ceiling reached |
| IPv6 data 2000 succeeds, source fragmentation unverified | Echo data reachability observed; PMTU 2048 is not established |
| Too-big response with absent/zero MTU field | Size failure recorded; no MTU value of zero |
| Size rejected locally before sending | Local send error/API limit; not remote packet loss |

Before release, verify these examples independently and test all-terminal-state partitions; zero/single-sample windows; timestamp/deadline races; duplicate/late/out-of-order replies; pauses and skipped schedules; window boundary entry/expiry; mixed responders; chart downsampling; cancellation; header and buffer boundaries; confirmed too-big versus timeout; local versus remote errors; contradictory MTU results; and the actual emitted fragmentation policy on supported Windows versions. Statistical correctness tests must use saved probe fixtures and independent expectations, not live Internet timing as their sole oracle.

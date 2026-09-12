# Velocity NetTools — Worked lessons

By [Velocity EU Inc](https://www.velocity-eu.com/). [Help index](README.md).

Educational content from the webpage design preview. Application-specific behaviour is proposed where labelled.

## 1. Subnetting by hand: five steps

Start here · Worked example · IPv4

Let’s work out `192.168.10.42/26` without a calculator. We want the network address, broadcast address and usable host range.

Step 1

### Count the bits left for hosts

An IPv4 address has 32 bits. The prefix /26 uses 26 for the network. That leaves **32 − 26 = 6 host bits**.

Step 2

### Turn the bits into an address count

Each bit has two choices: 0 or 1. Six bits give 2 × 2 × 2 × 2 × 2 × 2 = **64 addresses**. You can count by doubling: 2, 4, 8, 16, 32, 64.

Step 3

### Find the block containing 42

For this /26, the first three octets stay fixed. The last octet is divided into blocks of 64: **0–63, 64–127, 128–191, 192–255**. The number 42 is in 0–63, so the network is **192.168.10.0**.

Step 4

### Find the broadcast address

The next network starts at .64. Step back one address: **192.168.10.63** is the broadcast address of this block.

Step 5

### Leave the endpoints out of the host range

For this conventional subnet, .0 identifies the network and .63 is broadcast. Hosts run from **192.168.10.1 to 192.168.10.62**. That gives **64 − 2 = 62 usable hosts**.

- Network: **.0**
- First host: **.1**
- Last host: **.62**
- Broadcast: **.63**

The block-size shortcut must follow the octet where the mask changes. A /20 changes in the third octet, not the fourth. And /31, /32 and IPv6 need their own host-count rules.

Standard behind the method: [RFC 4632 — CIDR](https://www.rfc-editor.org/info/rfc4632/).

---

## 2. Bits, bytes and powers of two

A **bit** is a single 0 or 1. Eight bits form a byte. In an IPv4 address, each of the four dotted numbers is an 8-bit group, also called an **octet**.

### Why does an octet stop at 255?

Eight bits give 2⁸ = 256 different combinations. Counting starts at zero, so the values are 0 through 255.

```text
Bit weights: 128  64  32  16   8   4   2   1
Binary:        0   0   1   0   1   0   1   0
Value:                   32 + 8 + 2 = 42
```

### Convert 42 to binary by hand

- 128 and 64 are too large: write 0, 0.

- Take 32, leaving 10: write 1.

- 16 is too large: write 0.

- Take 8, leaving 2: write 1.

- 4 is too large: write 0.

- Take 2, leaving 0: write 1.

- No 1 remains: write 0.

The result is `00101010`. To read binary, add the weights under the 1s. To count combinations, double once per bit.

---

## 3. From prefix to subnet mask

A subnet mask marks network bits with 1 and host bits with 0. A /26 means 26 leading 1s, followed by 6 zeros.

```text
11111111.11111111.11111111.11000000
255  .   255  .   255  .   192
```

The last octet is 128 + 64 = 192. The mask is **255.255.255.192**. Its wildcard is the per-octet complement: 255 minus each mask octet, giving **0.0.0.63**.

### The quick boundary trick

Subtract the changing mask octet from 256: **256 − 192 = 64**. Network boundaries occur every 64 in that octet.

### Why does address AND mask work?

AND returns 1 only when both input bits are 1. The mask keeps network bits and clears host bits. For .42 in this example:

```text
Address: 00101010  (42)
Mask:    11000000  (192)
AND:     00000000  (0)
```

A valid CIDR mask has a continuous run of 1s, then only 0s. You cannot put another network bit after the host bits start.

---

## 4. Cross an octet boundary: /20

Take `172.16.37.19/20`. The same method works, but the boundary is in the third octet.

- **Host bits:** 32 − 20 = 12.

- **Addresses:** 2¹² = 4,096.

- **Mask:** 255.255.240.0. In the third octet, four network bits give 128 + 64 + 32 + 16 = 240.

- **Block step:** 256 − 240 = 16 in the third octet.

- **Locate 37:** the third-octet blocks start at 0, 16, 32, 48… so 37 belongs to 32–47.

```text
Network:   172.16.32.0/20
Next block: 172.16.48.0/20
Broadcast: 172.16.47.255
Hosts:     172.16.32.1–172.16.47.254
Usable:    4,096 − 2 = 4,094
```

The last octet varies from 0 to 255 inside this block. An address ending in .0 or .255 is not automatically a network or broadcast address; the prefix determines its role.

---

## 5. Choose a prefix for host capacity

For a conventional IPv4 LAN, choose the smallest power of two that covers the requested hosts plus the network and broadcast addresses.

### Example: 50 host addresses

- Add the two reserved endpoints: 50 + 2 = 52.

- Count powers of two: 2, 4, 8, 16, 32, 64.

- 32 is too small; 64 fits. 64 = 2⁶, so you need 6 host bits.

- Prefix = 32 − 6 = **/26**.

```text
/27 → 32 total → 30 usable → too small
/26 → 64 total → 62 usable → fits 50
```

Include router interfaces, printers and other devices in the host requirement. If growth needs 75 hosts, 64 total is insufficient; choose /25 for 126 conventional usable hosts.

This is ordinary IPv4 LAN planning. /31 point-to-point links and cloud-provider reservation policies need different capacity rules.

---

## 6. Split a network into equal parts

Every extra prefix bit cuts a block in half. One extra bit gives two child blocks; two extra bits give four.

### Example: split 192.168.10.0/24 into four

Four = 2², so borrow 2 more bits for the network. The child prefix is **24 + 2 = /26**. Each child has 64 addresses.

```text
192.168.10.0/26   → .0–.63
192.168.10.64/26  → .64–.127
192.168.10.128/26 → .128–.191
192.168.10.192/26 → .192–.255
```

Each has 62 conventional usable host addresses. You keep the same total address space, but splitting introduces additional network/broadcast endpoints. Check that your applications have enough host capacity after the split.

---

## 7. Glossary and common mistakes

### Subnet / prefix

A contiguous, aligned address block. The number after / says how many leading bits identify it.

### Network address

The start of the block, found by setting all host bits to zero.

### Broadcast

For a conventional IPv4 subnet, the address with every host bit set to one. IPv6 has no broadcast.

### Gateway

A router used to reach destinations via another network. A subnet calculator cannot infer the configured gateway from the address and prefix alone. It is not necessarily the first host.

### Common mistakes

- Subtracting two from every address count, including IPv6 and /31.

- Treating every .0 as a network address and every .255 as broadcast.

- Guessing a prefix from an old class A/B/C label.

- Calling a covering supernet an exact aggregation.

- Assuming an ICMP timeout means the device is offline.

---

## 8. Understand an IPv4 subnet

A prefix tells you how many bits identify the network. An IPv4 address has 32 bits. With `/26`, 26 identify the network and 6 remain for addresses inside it.

### Example: 192.168.10.42/26

```text
Address count = 2⁶ = 64
Network = 192.168.10.0/26
Broadcast = 192.168.10.63
Host range = 192.168.10.1–192.168.10.62
Conventional usable hosts = 64 − 2 = 62
```

### Read the result

The entered address is 42 addresses after the network boundary. The next /26 begins at `192.168.10.64`. The /31 and /32 cases follow different rules.

Online reference: [RFC 4632 — CIDR](https://www.rfc-editor.org/info/rfc4632/)

---

## 9. Plan with VLSM

Variable Length Subnet Masking assigns differently sized blocks to networks with different capacity needs. Start with the largest requirement so that small allocations do not fragment the available space.

### Example: three networks inside 192.168.10.0/24

```text
Office: 100 hosts → 192.168.10.0/25 (126 usable)
Voice: 50 hosts → 192.168.10.128/26 (62 usable)
Lab: 20 hosts → 192.168.10.192/27 (30 usable)
Remaining: 192.168.10.224/27
```

### Check before allocating

These counts use conventional IPv4 network and broadcast reservations. Include gateways and any spare capacity in your requirement. A cloud provider may reserve additional addresses.

Background: [CIDR address allocation and aggregation](https://www.rfc-editor.org/info/rfc4632/)

---

## 10. Use /31 and /32 correctly

The usual “subtract two” formula is not universal. A /31 on a point-to-point link uses both addresses. A /32 identifies one address, commonly as a host route.

### Example: a point-to-point link

```text
192.0.2.10/31
Endpoint A = 192.0.2.10
Endpoint B = 192.0.2.11
Directed broadcast = Not applicable
```

Both ends must support /31 operation. A calculator describes the address block; it does not verify how the actual link is configured.

Online reference: [RFC 3021 — IPv4 point-to-point links](https://www.rfc-editor.org/info/rfc3021/)

---

## 11. Understand IPv6 prefixes

IPv6 addresses are 128 bits wide. IPv6 has no broadcast address, so its capacity display must not subtract two as though it were IPv4.

### Example: divide a /48 into /64s

```text
Parent = 2001:db8:1234::/48
Child prefix = /64
Number of child prefixes = 2¹⁶ = 65,536
Addresses in each /64 = 2⁶⁴
```

This is address-space capacity, not a count of discovered devices or a promise that every address has the same assignability. Link roles and reserved addresses matter. The 2001:db8::/32 prefix is used for documentation.

Online references: [IPv6 addressing](https://www.rfc-editor.org/rfc/rfc4291.html) and [IPv6 text formatting](https://www.rfc-editor.org/rfc/rfc5952.html).

---

## 12. Aggregate without extra coverage

Exact aggregation combines blocks without adding addresses. A covering supernet may also include addresses that were not in the original selection.

### Example: adjacent sibling networks

```text
192.168.10.0/25 + 192.168.10.128/25
= 192.168.10.0/24
Extra addresses = 0
```

By contrast, the smallest single prefix covering `192.168.10.0/26` and `192.168.10.128/26` is a /24. That covering block includes 128 additional addresses. Keep the two /26s for an exact result.

Online reference: [CIDR aggregation](https://www.rfc-editor.org/info/rfc4632/)

---

## 13. Read ping and traceroute results

Ping measures whether a target answers an ICMP echo request and how long the reply takes. A timeout means no reply arrived within the limit; it does not by itself prove that the device is offline.

### Example: a short observation

```text
Requests = 100
Replies = 98
Observed reply loss = 2%
Average latency = average of successful replies
```

Traceroute asks routers along a path to respond. Some routers limit these responses while forwarding traffic normally. Compare intermediate-hop results with the destination before drawing conclusions.

For the proposed jitter display, we will document the exact formula and how missing samples are handled.

Online reference: [Microsoft — ping](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/ping)

---

## 14. Interpret a DNS lookup

DNS translates names into records. A records contain IPv4 addresses; AAAA records contain IPv6 addresses. MX records identify mail exchangers, and PTR records are used for reverse lookup.

### Illustrative response

```text
Query: host.example — A
Answer: 192.0.2.20
TTL: 300 seconds
```

The TTL controls how long a response may be cached. Two resolvers can temporarily return different answers because their caches were filled at different times. Distinguish an empty answer, a nonexistent name, a server failure, and a timeout.

Online reference: [RFC 1035 — DNS records and messages](https://www.rfc-editor.org/info/rfc1035/)

---

## 15. Help, portability and shortcuts

The proposed manual is embedded in NetTools. Reading it will not require an internet connection, a separate help file, or a browser runtime.

### Proposed help controls

- **F1:** open the topic for the current tool or focused field.

- **Contents:** browse the complete manual.

- **Search:** find words, commands, examples and error messages.

- **Back / Forward:** retrace the topics you opened.

- **Copy:** copy example addresses and selected text.

Online reference links open in your default browser only when selected. Saved plans and exported results are user-created files; they are not runtime dependencies.

Core portability, keyboard behaviour and error recovery will each have dedicated chapters in the final manual.

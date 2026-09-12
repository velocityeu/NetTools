# Velocity NetTools — Engineer’s field guide

Learn the method, follow an example, then check the answer. You do not need the Windows application to use these lessons.

## Start here

Read [the worked lessons](lessons.md). Begin with **Subnetting by hand: five steps**. If bits or powers of two are unfamiliar, read the next lesson before continuing.

| Learning goal | Lesson |
| --- | --- |
| Work out a subnet manually | Subnetting by hand: five steps |
| Understand why the formula works | Bits, bytes and powers of two |
| Convert /26 into a dotted mask | From prefix to subnet mask |
| Handle larger networks | Cross an octet boundary: /20 |
| Choose a subnet for 50 hosts | Choose a prefix for host capacity |
| Divide a /24 into four networks | Split a network into equal parts |
| Allocate several different-sized networks | Plan with VLSM |
| Handle unusual prefixes | Use /31 and /32 correctly; Understand IPv6 prefixes |
| Avoid adding unintended addresses | Aggregate without extra coverage |
| Interpret diagnostic results | Read ping and traceroute results; Interpret a DNS lookup |

The [web preview](../../website-preview/dist/index.html) presents the same lessons with search, navigation, a prefix reference table and a practice question. For this design iteration, the Markdown lessons are an exported snapshot of the webpage content. The production documentation design calls for one canonical source shared by the web manual and embedded Windows help.

## The pocket method

For a conventional IPv4 subnet:

1. **Host bits:** 32 minus the prefix.
2. **Addresses:** double 1 once for every host bit.
3. **Network:** find the block containing the input address.
4. **Broadcast:** one address before the next block.
5. **Usable range:** exclude the network and broadcast endpoints.

For a non-full changing mask octet, the boundary step in that octet is 256 minus its mask value. Whole-octet boundaries need to be treated accordingly. /31, /32 and IPv6 are explicit exceptions to conventional host counting.

## Practice

Find the network containing **192.168.5.77/27** before opening the answer.

<details>
<summary>Show the method and answer</summary>

32 − 27 = 5 host bits. 2⁵ = 32 addresses. The final-octet block starts are 0, 32, 64, 96… The number 77 belongs to 64–95.

- Network: **192.168.5.64/27**
- Broadcast: **192.168.5.95**
- Host range: **192.168.5.65–192.168.5.94**
- Conventional usable hosts: **30**

</details>

## Documentation scope

The tutorials are usable educational content. Application-specific command descriptions, error catalogues and F1 context mappings remain a design until the application behaviour is finalised. See [the complete help-system design](../design/help-and-about.md).

## Primary references

- [RFC 4632 — CIDR](https://www.rfc-editor.org/info/rfc4632/)
- [RFC 3021 — IPv4 /31 point-to-point links](https://www.rfc-editor.org/info/rfc3021/)
- [RFC 4291 — IPv6 addressing](https://www.rfc-editor.org/rfc/rfc4291.html)
- [RFC 5952 — IPv6 text representation](https://www.rfc-editor.org/rfc/rfc5952.html)
- [RFC 6164 — IPv6 /127 point-to-point links](https://www.rfc-editor.org/rfc/rfc6164.html)
- [RFC 1035 — DNS](https://www.rfc-editor.org/info/rfc1035/)
- [Microsoft — ping](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/ping)

By [Velocity EU Inc](https://www.velocity-eu.com/). Licensed under the repository’s [MIT licence](../../LICENSE).

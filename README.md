# ntfw
ntfw is a light-weight, fixed-function L3 'firewall' for server deployments of Windows which provides:

- Host-based authorization to private services with support for remote authentication.
- Traffic policing to help protect public services from abuse via rate-limiting.
- Denying bots/proxies access to public services (made possible thanks to Luke at [getipintel](https://getipintel.com))

IPv6 is not currently supported and may only be unconditionally dropped or accepted.

<br>
<br>

## Overview

ntfw classifies all network traffic arriving at your machine as either public or private. *Public* traffic is that which is destined
to internet-facing services you provide such as web- and game servers, while *private* is implicitly defined as the inverse and
is assumed to constitute your internal systems (e.g, RDP)

Private traffic is subject to host-based authorization while public traffic is optionally subject to traffic policing.
<br>

### Important

Broadly, a *firewall* is not a substitute for a secure system; it should be considered an auxillary first line of defense and for
the purpose of security evaluation: assumed not exist at all.
<br>

In particular, ntfw's filtering functionality should (conservatively) be considered *best-effort* as by design there is no contractual
guarentee of strictly correct behavior in this regard. This relaxation of requirements permits decisions made in favor of performance in
critical areas.[^1]


### Package Contents

- `ntfw.exe` Control and configuration tool.
- `ntfwkm.sys` Kernel-mode driver that implements the core filtering functionality.
- `ntfwum.exe` User-mode driver that pairs with the latter to provide core features.
- `ntfwd.exe` (Optional) Provides the ntfw authorization client-side. Installed as an NT service.

<br>
<br>

### Notes

- Rules are compiled into a perfect hash-table, providing deterministic O(1) lookup per-packet, and thus performance is not dependent on the number of rules defined.

- The KMD (ntfwkm) inserts itself as low as is reasonably possible within the network data-path with the expectation it will be near-first to touch
  frames after they're DMA'd to the NIC rx ring. This is important as to avoid the relatively costly path up through the kernel as
  configured for most machines.

- ntfw engages *before* Windows Firewall and, currently, before Wireshark/npcap (see above)
  
- In the presence of a moderate ingress packet-rate (~12Mpps), it's recommended to configure the RSS hash type of your NIC to be over the IPv4 source address
  alone. This will, in the current implementation atleast, improve performance by virtue of reducing the amount of contention over some cache-lines.


[^1]: In practice, the worst-case scenario is limited to a handful packets errorneously being classed as authorized under very rare conditions.

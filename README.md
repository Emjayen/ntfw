# ntfw
ntfw is a light-weight, fixed-function L3 'firewall' for server deployments of Windows which provides:

- Host-based authorization to private services with support for remote authentication.
- Traffic policing to help protect public services from abuse via rate-limiting.


IPv6 is not currently supported and may only be unconditionally dropped or accepted.

<br>
<br>

## Overview

ntfw classifies all network traffic arriving at your machine as either public or private. *Public* traffic is that which is destined
to internet-facing services you provide such as web- and game servers, while *private* is implicitly defined as the inverse and
is assumed to constitute your internal systems (e.g, RDP)

Private traffic is subject to host-based authorization while public traffic is optionally subject to traffic policing.
<br>

## Configuration

### Users

*Users* for ntfw currently serve the sole purpose of facilitating remote authorization, granting them ability to access private services on
the machine where ntfw is running. Unless you have a reliable static IP assigned by your ISP, you will likely need to minimally configure
a user for yourself to permit remote access.
<br>
<br>
Each user is defined by a *username*, *password* and/or a *public key* depending on which protocol is made available, along with their pros and cons:

- HTTPS:
  web-based interface, enabling the familiar username/password scheme to be used by navigating to your server's domain with
  a browser.
    - &#128504; Simple and familiar
    - &#128504; Easy to setup
    - &#128504; Client accessible and compatible, requiring only a browser.
    - &#10799; Entails establishment of bidirectional channel (TCP), requiring atleast a response packet to both legitimate and illegitimate clients.
    - &#10799; Relatively expensive TLS session setup and application-layer processing.
    - &#10799; Mishaps possible; accidental blocking (AV, WFP) of the service may render the machine inaccessible.

- NTFP:
  A simple UDP-based protocol that uses a digital signature to authenticate authorization requests.
    - &#128504; Does not generate any network response except to authenticated clients.
    - &#128504; No expensive processing; performed in-kernel; ignores unauthorized requests with a single 64-bit comparison.
    - &#128504; Reliably available; cannot be interfered with by other firewall software, ect.
    - &#10799; Requires the additional step of key generation.
    - &#10799; Clients must run specialized software (ntfwd)
    - &#10799; Currently limited to a single keypair[^1]

### Important

Broadly, a *firewall* is not a substitute for a secure system; it should be considered an auxillary first line of defense and for
the purpose of software security evaluation: assumed not exist at all.
<br>

In particular, ntfw's filtering functionality should (conservatively) be considered *best-effort* as by design there is no contractual
guarentee of strictly correct behavior in this regard.[^2] This relaxation of requirements permits decisions made in favor of performance in
critical areas.


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


[^1]: Multiple users can ofcourse share the same private key.
[^2]: In practice, the worst-case scenario is limited to a handful of packets being errorneously classed as authorized under very rare conditions.

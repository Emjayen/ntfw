# ntfw
ntfw is a light-weight, fixed-function L3 'firewall' for server deployments of Windows which provides:

- Host-based authorization to private services (e.g, RDP) with support for remote configuration.
- Traffic policing to help protect public services from volumetric abuse.

<br>

### Limitations

- IPv6 is not currently supported and is implicitly dropped.
- Asymmetric traffic where-in ingress is relatively low under normal conditions is assumed.
- Effectiveness is limited in the presence of *spoofed* DDoS attacks. This is broadly beyond the scope of individual hosts to mitigate.

The latter two points are only relevant to the traffic-policing feature.

<br>

### Overview

ntfw classifies all network traffic arriving at your machine as either *public* or *private*. *Public* traffic is that which relates
to the internet-facing services your server hosts such as web- and game servers, while *private* is simply defined as the inverse and
constitutes your internal administration systems.

Private traffic is subject to host-based authorization 

<br>

### Notes

- Rules are compiled down into a perfect (but not minimal-) hash-table, providing deterministic O(1) lookup per-packet, and thus performance
  is not dependent on the number of rules defined.

- The KMD (ntfwkm) inserts itself as reasonably low as possible within the filter chain with the expectation it will be near-first to touch
  frames in the data-path after they're DMA'd to the NIC rx ring. This is important as to avoid the relatively costly path up through the kernel as
  configured for most machines.

- ntfw engages *before* Windows Firewall and, currently, before Wireshark/npcap (see above)
- 

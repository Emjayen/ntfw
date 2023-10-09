# ntfw
ntfw is a light-weight, fixed-function L3 'firewall' for server deployments of Windows which provides:

- Host-based authorization to private services with support for remote configuration.
- Traffic policing to help protect public services from volumetric abuse.


<br>

### Limitations

- IPv6 is not currently supported and is implicitly dropped.
- Asymmetric traffic where-in ingress is relatively low under normal conditions is assumed.
- Effectiveness is impaired in the presence of *spoofed* DDoS attacks.[^1]

The latter two points are only relevant to the traffic-policing feature.

<br>

### Overview

ntfw classifies all network traffic arriving at your machine as either public or private. *Public* traffic is that which is destined
to internet-facing services you provide such as web- and game servers, while *private* is implicitly defined as the inverse and
is assumed to constitute your internal systems (e.g, RDP)

Private traffic is subject to host-based authorization while public traffic is optionally subject to traffic policing.



<br>

### Configuration

```
;
; The 'general' section defines general configuration.
;
[General]


[Meter#1]
Peak=1000
Rate=1


;
; The 'rules' section defines public traffic. Following illustrates the basic syntax.
;
[Rules]
tcp/80
    @Meter1
       byte  _cost=1000
       packet_cost=10

    @Meter2
       syn_cost=1

arp
udp/444
icmp

;
; The 'static' section lists any statically authorized hosts.
;
[Static]
192.168.1.1
11.22.33.44

;
; The 'users' section defines authorized users, identified solely by their public key.
;
[Users]
A262FB0B70DED5DF7F4ABE62C181AF989E7161B1A76470D0B3E72049A625A3E2
B70A20610957DF7E72B6CF471FAD650AFDDCBCD4B486CE0CAE3E39F28106CB4C

```

<br>

### Notes

- Rules are compiled into a perfect hash-table, providing deterministic O(1) lookup per-packet, and thus performance is not dependent on the number of rules defined.

- The KMD (ntfwkm) inserts itself as low as is reasonably possible within the network data-path with the expectation it will be near-first to touch
  frames after they're DMA'd to the NIC rx ring. This is important as to avoid the relatively costly path up through the kernel as
  configured for most machines.

- ntfw engages *before* Windows Firewall and, currently, before Wireshark/npcap (see above)
  
- In the presence of a moderate ingress packet-rate (~12Mpps), it's recommended to configure the RSS hash type of your NIC to be over the IPv4 source address
  alone. This will, in the current implementation atleast, improve performance by virtue of reducing the amount of contention over some cache-lines.


[^1]: This class of attack is mostly beyond the scope of any end-host solution and generally requires calling out to services such as CloudFlare.

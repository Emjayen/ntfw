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

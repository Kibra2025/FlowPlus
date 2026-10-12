# Security policy

FlowPlus is a development fork. File transfer is experimental, and this repository does not promise an upstream Deskflow or LocalSend support policy.

## Reporting a vulnerability

Use this repository's GitHub private vulnerability reporting feature when it is enabled. Otherwise open a minimal [FlowPlus issue](https://github.com/Kibra2025/FlowPlus/issues) asking for a private reporting channel; do not publish exploit details, private files, transfer tokens or certificate keys in that issue.

## File transfer trust

Use authenticated TLS input connections and verify peer fingerprints. Direct LocalSend HTTP traffic is plaintext in this implementation, so use a trusted LAN or encrypted VPN. Multicast discovery and hostnames are not authentication. A receive rejection must never be bypassed through the fallback tunnel. Review [architecture](ARCHITECTURE.md) and [testing](TESTING.md) for current limits.

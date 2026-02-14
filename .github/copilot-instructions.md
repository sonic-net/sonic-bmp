# Copilot Instructions for sonic-bmp

## Project Overview

sonic-bmp implements BMP (BGP Monitoring Protocol, RFC 7854) support for SONiC. It is based on OpenBMP and provides BGP route monitoring and analytics capabilities. BMP enables real-time monitoring of BGP routing information by streaming BGP updates from routers to BMP collectors, useful for network visibility, troubleshooting, and security analysis.

## Architecture

```
sonic-bmp/
├── Server/              # BMP server (collector) implementation
├── deb_package/         # Debian packaging scripts
├── debian/              # Debian package configuration
├── rpm_package/         # RPM packaging scripts
├── database/            # Database schema and migrations
├── cron_scripts/        # Periodic maintenance scripts
├── docs/                # Documentation
├── Makefile             # Build entry point
└── README.md            # Project documentation
```

### Key Concepts
- **BMP collector**: Receives BMP messages from BGP routers
- **BGP monitoring**: Captures routing table state, updates, and peer state changes
- **OpenBMP**: Based on the OpenBMP project for BGP monitoring
- **Kafka integration**: Parsed BMP feeds can be published to Apache Kafka
- **Multi-protocol**: Supports IPv4, IPv6, VPNv4, bgp-ls, extended communities
- **RPKI/IRR**: Integration for routing security analysis

## Language & Style

- **Primary language**: C++ (server), SQL (database)
- **Indentation**: 4 spaces
- **Naming conventions**: Follow OpenBMP conventions
- **Comments**: Document protocol-specific logic thoroughly

## Build Instructions

```bash
# Build from source
make

# Build Debian package
cd deb_package
./build.sh

# Or from sonic-buildimage context
dpkg-buildpackage -us -uc -b
```

## Testing

- Protocol conformance testing with BMP-capable routers (FRR, GoBGP)
- Integration tests with sonic-mgmt test infrastructure
- Verify BMP message parsing against RFC 7854

## PR Guidelines

- **Commit format**: `[component]: Description`
- **Signed-off-by**: REQUIRED (`git commit -s`)
- **CLA**: Sign Linux Foundation EasyCLA
- **RFC compliance**: BMP implementation must follow RFC 7854
- **Protocol testing**: Test with multiple BGP implementations

## Common Patterns

### BMP Message Processing
```
Router sends BMP message → Parser decodes message type →
Route Monitoring / Peer Up/Down / Stats Report →
Store in database / Publish to Kafka
```

### BMP Message Types
- **Route Monitoring**: BGP UPDATE messages
- **Peer Up/Down**: BGP session state changes
- **Statistics Report**: Counters and statistics
- **Initiation/Termination**: Session lifecycle

## Dependencies

- **OpenBMP**: Base implementation
- **Apache Kafka**: Message streaming (optional)
- **Database**: MySQL/MariaDB for route storage
- **sonic-swss-common**: SONiC database integration

## Gotchas

- **High volume**: BGP full tables can generate millions of BMP messages — handle efficiently
- **Memory usage**: Storing full routing tables requires significant memory
- **Message ordering**: BMP messages must be processed in order per peer
- **Peer state**: Track peer UP/DOWN correctly to interpret route monitoring messages
- **RFC compliance**: Some routers send non-standard BMP extensions — handle gracefully
- **Kafka partitioning**: Use appropriate topic/partition strategies for scaling
- **Database growth**: Route tables grow large — implement retention policies

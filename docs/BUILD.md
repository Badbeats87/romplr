# Build Instructions

## Directory Structure

```
mini-jv880-restructured/
├── synth/                    # Shared synth engine code
│   ├── shared/              # Common DSP components
│   ├── baremetal/           # Bare-metal specific
│   └── linux/               # Linux specific
├── platforms/
│   ├── baremetal/           # Circle bare-metal build
│   └── linux/               # Buildroot Linux build
├── bootloaders/             # WiFi/Ethernet bootloaders
├── scripts/                 # Build and utility scripts
├── third_party/             # Submodule references
└── docs/                    # Documentation
```

## Quick Start

### Bare-Metal Build (Circle)

```bash
cd platforms/baremetal
make clean && make          # Build kernel (~500KB)
make deploy                 # TFTP upload to Pi at 192.168.0.92
./deploy-to-sd.sh           # Copy samples to SD card
```

### Linux Build (Buildroot)

```bash
cd platforms/linux
make                        # Build Linux distribution
```

## Setup

Initialize all git submodules:
```bash
make setup
```

Or manually:
```bash
git submodule update --init --recursive
```

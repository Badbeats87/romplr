# Repository Reorganization Guide

This is a restructured version of `mini-jv880-v2` with improved organization following best practices.

## What Changed

### Before (Flat structure)
```
mini-jv880-v2/
├── build.sh, build_pi4.sh, build_sd.sh     ← Scripts scattered at root
├── rompler/                                 ← Bare-metal implementation
├── rompler-linux/                           ← Linux implementation (duplication!)
├── src/                                     ← Abandoned JV-880 emulator
├── wifi-bootloader/, eth-bootloader/       ← Bootloaders mixed in
├── test-kernel/                             ← Test code
├── circle-stdlib/, CMSIS_5                  ← Submodules exposed
└── buildroot/, buildroot-build/             ← Build config scattered
```

### After (Organized structure)
```
mini-jv880-restructured/
├── Makefile                    ← Central build orchestration
├── README.md, STATUS.md        ← Documentation at root
├── docs/BUILD.md               ← Detailed build guide
│
├── synth/                       ← Shared synth engine (ready for deduplication)
│   ├── shared/                 ← Future: Common DSP components
│   ├── baremetal/              ← Bare-metal-specific code
│   └── linux/                  ← Linux-specific code
│
├── platforms/                  ← Build environments
│   ├── baremetal/              ← Circle bare-metal build
│   ├── linux/                  ← Buildroot Linux build
│
├── bootloaders/                ← WiFi & Ethernet bootloaders
├── scripts/                    ← All build/utility scripts
├── third_party/                ← Git submodule documentation
├── archive/                    ← Legacy code (original JV-880 emulator, test-kernel)
└── builds/                     ← Reserved for build artifacts
```

## Key Improvements

### 1. **Clear Separation of Concerns**
- `platforms/` isolates build targets (bare-metal vs Linux)
- `synth/` provides a clear location for shared engine code
- `archive/` separates legacy/experimental code from active work

### 2. **Centralized Build System**
- Top-level `Makefile` orchestrates all builds
- `docs/BUILD.md` documents build process
- `scripts/` contains reusable utilities

### 3. **Reduced Duplication**
- `synth/` sets up structure to extract and share engine code
- Currently copies bare-metal synth; next step would extract common C code into `synth/shared/`

### 4. **Better Dependency Management**
- `third_party/README.md` documents all Git submodules
- Makes it clear what requires `git submodule update --init --recursive`

### 5. **Cleaner Git Submodules**
- Submodules are documented but not exposed at root level
- `buildroot/` and `buildroot-build/` are now internally organized under `platforms/linux/`

## Next Steps (Future Improvements)

### Phase 1: Deduplicate Synth Engine
Extract common DSP code into `synth/shared/`:
- `effects.c/h` → shared (if identical)
- `envelope.c/h` → shared
- `lfo.c/h` → shared
- `tone.c/h` → shared
- `voice.c/h` → shared

Update both platform builds to link against `synth/shared/`.

### Phase 2: Document Platform Differences
Create `platforms/baremetal/NOTES.md` and `platforms/linux/NOTES.md`:
- Required dependencies
- Known issues (USB MIDI blocker for bare-metal)
- Feature comparison matrix

### Phase 3: Consolidate Configuration
- Move buildroot config from `buildroot/configs/` into `platforms/linux/config/`
- Create unified build artifact directory structure in `builds/`

### Phase 4: CI/CD Setup
- Update GitHub Actions to use new `Makefile` targets
- Build both targets in CI
- Archive artifacts to `builds/`

## Git Submodule Status

The following submodules are used (initialize with `make setup`):

- **circle-stdlib** — Bare-metal Raspberry Pi framework (Step 46)
  - Used by: `platforms/baremetal/`
  
- **CMSIS_5** — ARM Cortex MCU Software Interface Standard
  - Used by: `circle-stdlib`
  
- **buildroot** — Linux distribution builder
  - Used by: `platforms/linux/`

**Note:** This reorganization uses symlinks to the original repo's submodules to avoid duplication. If you want a fully independent clone, copy the large directories instead of symlinking.

## Migration Checklist

- [ ] Test bare-metal build: `cd platforms/baremetal && make clean && make`
- [ ] Test Linux build: `cd platforms/linux && make`
- [ ] Verify deployment scripts work: `platforms/baremetal/deploy-to-sd.sh`
- [ ] Extract shared synth code to `synth/shared/`
- [ ] Update platform Makefiles to reference shared code
- [ ] Test both builds again
- [ ] Update GitHub Actions workflows
- [ ] Archive original `mini-jv880-v2` repository

## Questions?

Refer to:
- `docs/BUILD.md` — Build instructions
- `STATUS.md` — Current project status and blockers
- `README.md` — Project overview
- `archive/README.md` — Information on legacy code

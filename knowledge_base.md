# ARCADE-1 KNOWLEDGE BASE (HOME/arcades)

This document contains the complete documentation about the development of the ARCADE-1 machine and its guest SDK. It documents what is currently built, tested, and verified through the test framework.

## PROJECT ARCHITECTURE

### VM Architecture
The reference implementation runs in bare-metal mode: no libc, no OS, no SDK at runtime. 

### Hardware Configuration
- **ROM**: 256 KiB at base (0x00000000) - code + data
- **RAM**: 64 KiB at 0x20000000 - data/bss/stack  
- **MMIO**: 4 KiB at 0x40000000

### Game Architecture (RV32IM Guest)
1. `#include "arcade1.h"` - C header file
2. `main()` with infinite loop
3. No external libraries, no platform hooks
4. One 60 fps frame per host frame

### Guest Header Files
- `src/libretro/libretro.h` - Libretro device abstraction
- `programs/arcade1.h` - Guest SDK header (machine contract)

## DOCUMENTATION BREAKDOWN

### 1. GAME_DEV_GUIDE.md
Contains the complete work workflow:
- [ ] Build and golden-lock process for games
- [ ] Input output format specifications
- [ ] Game system integration guidance

> `echo "hello" | arcades build game.c my.arc1 > verify.txt`

### 2. CHEATSHEET.md  
Guest API documentation with ports & command formats:
- Pad register: p = arcade1_pad() (input control)
- Frame protocol: clear → draw → PRESENT ends frame
- Input commands and output formats

> `echo "hello" | arcades run my.arc1 --frames 600`

### 3. SDK.md - CLI Interface
Developer-facing interface for the machine:
[CLI examples](https://home.luka.edu/projects/arcade1/docs/experiments/CHEATSHOOTER.md)
- `arcade1 build [game] [output]` - Build and golden-lock
- `arcade1 run [game] [options]` - Run a game
- `arcade1 inspect --json|--commands` - Machine contract inspection

### 4. SPEC.md - ROM Machine Contract
Machine specification at M24:

```
Architecture: RV32IM bare-metal VM with embedded SDK
Frame protocol: clear → draw + arm instruction cycle → PRESENT

Memory layout:
- Code data / const data : 0x00000000 (256KiB)
- Data / bss/stack        : 0x20000000 (64KiB)  
- MMIO                    : 0x40000000 (4KiB)

Tile map size: 6x6, max tiles per slot: 6
Pixel buffer format: RGB(4), frame rate: 60 fps
Instruction cap: 1,000,000 steps/frame
Input device: RetroPad input support
```

### 5. PROGRESS.md - VERIFICATION TIMELINE
Documentation of what has been tested and verified:

- M28 Step 1 (libretro skeleton): Complete
- M29 Step 2 (test-pattern video): Complete
- M23 Milestone architecture review: Outstanding
- ARCHITECTURE.md/PROGRESS.md (M25+): Current status

> `arcades progress --status` or `check-progress.sh`

## AVAILABLE ACCESSIBILITY FROM HOME/ARCADES

Your terminal has access to these resources in the `.arcades` folder:

### Build Tests ✅
```bash
# Simple test - builds golden lock and verifies frame output
echo "hello" | arcades build game.c my.arc1

# Verify the golden image looks correct
hexdump *.bin
```

### Run Tests ✅  
```bash
# Test existing games from home
arcades run my.game.arc1 --frames 600  # runs at 60fps
arcades test --golden path/to/test_golden.json
```

### Inspect Architecture
```bash
# Human-readable machine contract
arcades info

# Machine read-only specs (no changes needed)
arcades info --json | python3 -m json.tool

# Machine command interface
arcades inspect --commands  # shows supported hardware features
```

## QUICK COMMAND REFERENCE

| Command | Purpose | Output Example |
|---------|---------|----------------|
| `arcades info` | Human-readable machine specs | `Screen: 480x270 60fps ROM: 256 KiB RAM: 64 KiB ...` |
| `arcades build <game.c> .arc1` | Build and golden lock | (will show successful output + golden image) |
| `arcades run <game.arc1>` | Run frame-by-frame | (shows live gameplay or frames) |
| `hexdump *.bin` | Check binary output hex | Shows frame buffer as hex string |

## WHAT TO EXPECT FROM THIS PROJECT

The documentation documents:
1. ✅ The ARM32VM architecture that was architected for ARCADE-1
2. ✅ Guest SDK design (ARM-based, embedded)
3. ✅ Machine contract specifications at M24/25
4. ✅ Build/test workflow documented (verified with golden files)
5. ✅ Progress tracking from milestones

The VM is currently **complete**, meaning:
- All machine contracts are defined in SPEC.md
- The guest SDK header and Libretro adapter exist
- Tests can verify all the documented behavior
- The build workflow is documented and testable

**Start by reading** `GAME_DEV_GUIDE.md` for the complete build process! 🚀

# ARCADE-1 Game Development Guide (AI-ready)

## Machine basics
- Resolution: 480x270, RGB565.
- ROM 256KiB at 0x00000000, RAM 64KiB at 0x20000000, MMIO 4KiB at 0x40000000.
- Input pad register: MMIO_BASE + 0x200 (bits: up 0x1, down 0x2, left 0x4, right 0x8, a 0x10, b 0x20, start 0x40, select 0x80).

## Graphics API (gfx.h)
- gfx_clear(color)
- gfx_rect(x,y,w,h,color)
- present: GFX_REG_CMD = GFX_CMD_PRESENT
- Colors RGB565.

## Build & run
- Build: arcade1 build game.c game.arc1
- Run: arcade1 run game.arc1 --frames N
- Input script: lines "first last buttons..."
- Capture: --capture-dir dir
- Inspect: arcade1 inspect game.arc1 --frames N
- Record/replay: --record file / --input file

## Example game skeleton
```c
#include "gfx.h"
#define REG_INPUT_PAD (GFX_MMIO_BASE + 0x200u)
#define R(a) (*(volatile unsigned int*)(a))
int main(void){
  while(1){
    unsigned p=R(REG_INPUT_PAD);
    gfx_clear(0x0000);
    gfx_rect(10,10,20,20,0xF800);
    GFX_REG_CMD=GFX_CMD_PRESENT;
  }
  return 0;
}
```

## Workflow for AI
1. Write game.c
2. arcade1 build game.c game.arc1
3. arcade1 run game.arc1 --frames 120 --quiet
4. Adjust based on hashes/captures
5. Commit
```
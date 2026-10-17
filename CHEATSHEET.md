# ARCADE-1 Cheat Sheet

## Build & run
- Build core: `make`
- Build guest: `arcade1 build program.c out.arc1`
- Run: `arcade1 run game.arc1 --frames 600`
- Capture frames: `arcade1 run game.arc1 --capture-dir frames/ --capture-every 1`
- Record input: `arcade1 run game.arc1 --input script.txt --record rec.txt`
- Replay: `arcade1 run game.arc1 --input rec.txt`
- Full check: `make check`

## Graphics (gfx.h)
- `gfx_clear(color)` RGB565
- `gfx_rect(x,y,w,h,color)`
- `GFX_REG_CMD = GFX_CMD_PRESENT` to present

## Input script format
```
0 59 none
60 119 right a
```
Buttons: up down left right a b start select

## Named variables
`arcade1 run game.arc1 --syms game.syms --vars all`
`arcade1 run game.arc1 --syms game.syms --vars score,lives --var-every 60`

## Exit codes
0 success, 1 runtime error, 2 usage error.

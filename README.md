# Game Boy emulator (Cinoop-style) + a homebrew shooter

A small, readable Game Boy (DMG) emulator in C, laid out like
[Cinoop](https://cturt.github.io/cinoop.html), plus a space shooter ROM
written in Game Boy assembly to play on it.

```sh
make            # builds ./gbemu   (needs gcc + libx11-dev)
make run        # builds roms/shooter.gb and plays it
./gbemu some_other_game.gb --scale 3
```

## Controls

| Key            | Game Boy        |
|----------------|-----------------|
| Arrow keys     | D-pad           |
| Z / Space      | A (fire)        |
| X              | B               |
| Enter          | Start (restart) |
| Backspace      | Select          |
| Tab (hold)     | fast-forward    |
| P / R          | pause / reset   |
| F1             | print CPU registers to the terminal |
| F12            | save `screenshot.ppm` |
| Esc            | quit            |

## Suggested reading order

| File | What you learn |
|------|----------------|
| `src/registers.h` | The 8 registers, register pairs (a C `union`), the Z N H C flags |
| `src/memory.c` | The memory map: ROM, VRAM, WRAM, OAM, I/O, HRAM. How ROM bank switching (MBC) works, OAM DMA, the joypad register |
| `src/cpu.c` | Fetch → decode → execute. Opcodes are decoded from their bit pattern (`x y z`), so ~500 instructions fit in ~400 lines |
| `src/interrupts.c` | IE / IF / IME and how the CPU jumps to 0x40, 0x48... |
| `src/timer.c` | DIV and TIMA |
| `src/gpu.c` | Scanlines, the PPU modes, tiles, background / window / sprites |
| `src/keys.c` | The joypad register at 0xFF00 (active-low, 2 groups of 4) |
| `src/main.c` | The main loop: run 70224 cycles, draw a frame, sleep |
| `tools/shooter.py` | The game in assembly: what a real ROM does with all that hardware |

## Debugging tools built in

```sh
./gbemu rom.gb --trace | head -50        # every instruction + registers
./gbemu rom.gb --headless 300 --screenshot out.ppm
./gbemu rom.gb --headless 300 --input "10:a,left 60:start 62:" --screenshot out.ppm
./gbemu cpu_instrs.gb --serial           # Blargg test ROMs print results here
```

## Ideas for what to build next

1. Run [Blargg's `cpu_instrs`](https://github.com/retrio/gb-test-roms) with `--serial` to check every instruction.
2. Add a disassembler to `--trace` (Cinoop's instruction table has the mnemonics).
3. Save battery RAM (`cartRam`) to a `.sav` file.
4. Change the shooter: faster enemies as the score rises, lives, a boss, sound (`0xFF10`–`0xFF26`).
5. Swap `platform_x11.c` for an SDL2 version to get gamepad support.

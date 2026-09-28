/*
 * memory.h - the memory map (MMU).
 *
 * The CPU sees one flat 16-bit address space (0x0000-0xFFFF). Different
 * ranges are wired to different hardware:
 *
 *   0000-3FFF  ROM bank 0          (fixed, from the cartridge)
 *   4000-7FFF  ROM bank 1..N       (switchable by the cartridge's MBC chip)
 *   8000-9FFF  VRAM                (tile graphics + background maps)
 *   A000-BFFF  External RAM        (battery save RAM on the cartridge)
 *   C000-DFFF  Work RAM (WRAM)
 *   E000-FDFF  Echo RAM            (mirror of C000-DDFF)
 *   FE00-FE9F  OAM                 (sprite attribute table, 40 x 4 bytes)
 *   FEA0-FEFF  Unusable
 *   FF00-FF7F  I/O registers       (joypad, timer, LCD, sound...)
 *   FF80-FFFE  High RAM (HRAM)
 *   FFFF       IE                  (interrupt enable register)
 */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

/* Common I/O register addresses */
#define REG_JOYP 0xFF00
#define REG_SB   0xFF01
#define REG_SC   0xFF02
#define REG_DIV  0xFF04
#define REG_TIMA 0xFF05
#define REG_TMA  0xFF06
#define REG_TAC  0xFF07
#define REG_IF   0xFF0F
#define REG_LCDC 0xFF40
#define REG_STAT 0xFF41
#define REG_SCY  0xFF42
#define REG_SCX  0xFF43
#define REG_LY   0xFF44
#define REG_LYC  0xFF45
#define REG_DMA  0xFF46
#define REG_BGP  0xFF47
#define REG_OBP0 0xFF48
#define REG_OBP1 0xFF49
#define REG_WY   0xFF4A
#define REG_WX   0xFF4B
#define REG_IE   0xFFFF

extern uint8_t vram[0x2000];
extern uint8_t oam[0xA0];
extern uint8_t io[0x80];

int  loadROM(const char *path);
void resetMemory(void);
void freeMemory(void);

uint8_t  readByte(uint16_t address);
void     writeByte(uint16_t address, uint8_t value);
uint16_t readShort(uint16_t address);
void     writeShort(uint16_t address, uint16_t value);

/* Print cartridge title / type info */
void printCartInfo(void);

/* Set to 1 to echo bytes sent over the serial port to stdout
 * (test ROMs such as Blargg's cpu_instrs report results this way). */
extern int serialToStdout;

#endif

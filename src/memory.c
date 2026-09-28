/*
 * memory.c - routes every CPU read/write to the right piece of hardware.
 *
 * readByte()/writeByte() are the only way the CPU touches memory, which makes
 * this file the "motherboard" of the emulator: writing to 0xFF46 starts a DMA,
 * writing to 0x2000 switches ROM banks, reading 0xFF00 reads the joypad, etc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "interrupts.h"
#include "keys.h"
#include "timer.h"

/* ---- Cartridge ---- */
static uint8_t *rom;          /* whole ROM file */
static size_t   romSize;
static int      romBanks;     /* number of 16 KiB banks */
static uint8_t  cartRam[0x20000]; /* up to 128 KiB external RAM */

enum { MBC_NONE, MBC_1, MBC_3, MBC_5 };
static int mbcType;
static int romBank = 1;       /* bank mapped at 0x4000-0x7FFF */
static int ramBank = 0;
static int ramEnabled = 0;
static int mbc1Upper = 0;     /* MBC1: 2 extra bits (ROM bank bits 5-6 or RAM bank) */
static int mbc1Mode = 0;

/* ---- Internal memory ---- */
uint8_t vram[0x2000];
static uint8_t wram[0x2000];
uint8_t oam[0xA0];
uint8_t io[0x80];
static uint8_t hram[0x7F];
static uint8_t ie;

int serialToStdout = 0;

int loadROM(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }

    fseek(f, 0, SEEK_END);
    romSize = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (romSize < 0x8000) romSize = 0x8000; /* always at least 2 banks */
    rom = calloc(1, romSize);
    if (fread(rom, 1, romSize, f) == 0) {
        fprintf(stderr, "%s: could not read ROM\n", path);
        fclose(f);
        return 0;
    }
    fclose(f);
    romBanks = (int)(romSize / 0x4000);

    /* 0x0147 in the cartridge header tells us which MBC chip it has */
    uint8_t type = rom[0x147];
    switch (type) {
    case 0x00: case 0x08: case 0x09:                         mbcType = MBC_NONE; break;
    case 0x01: case 0x02: case 0x03:                         mbcType = MBC_1;    break;
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:   mbcType = MBC_3;    break;
    case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: mbcType = MBC_5; break;
    default:
        fprintf(stderr, "Warning: unsupported cartridge type 0x%02X, treating as MBC1\n", type);
        mbcType = MBC_1;
    }
    return 1;
}

void printCartInfo(void) {
    char title[17] = {0};
    memcpy(title, &rom[0x134], 16);
    for (int i = 0; i < 16; i++) if (title[i] && (title[i] < 32 || title[i] > 126)) title[i] = 0;
    const char *mbcName[] = { "ROM only", "MBC1", "MBC3", "MBC5" };
    printf("Title: %s\nCartridge: %s (type 0x%02X), %d ROM banks (%zu KiB)\n",
           title, mbcName[mbcType], rom[0x147], romBanks, romSize / 1024);
}

void freeMemory(void) {
    free(rom);
    rom = NULL;
}

/* Put the I/O registers in the state the boot ROM leaves them in */
void resetMemory(void) {
    memset(vram, 0, sizeof vram);
    memset(wram, 0, sizeof wram);
    memset(oam, 0, sizeof oam);
    memset(io, 0, sizeof io);
    memset(hram, 0, sizeof hram);
    ie = 0;

    io[0x00] = 0xCF; /* JOYP */
    io[0x05] = 0x00; /* TIMA */
    io[0x06] = 0x00; /* TMA  */
    io[0x07] = 0xF8; /* TAC  */
    io[0x0F] = 0xE1; /* IF   */
    io[0x40] = 0x91; /* LCDC: LCD on, BG on, tiles at 0x8000 */
    io[0x41] = 0x85; /* STAT */
    io[0x47] = 0xFC; /* BGP  */
    io[0x48] = 0xFF; /* OBP0 */
    io[0x49] = 0xFF; /* OBP1 */
}

/* Map an address in 0x4000-0x7FFF to an offset in the ROM file */
static uint8_t readBankedRom(uint16_t address) {
    int bank = romBank % romBanks;
    return rom[bank * 0x4000 + (address - 0x4000)];
}

/* Writes to the ROM area don't change the ROM - they talk to the MBC chip */
static void writeMBC(uint16_t address, uint8_t value) {
    switch (mbcType) {
    case MBC_NONE:
        break;

    case MBC_1:
        if (address < 0x2000) {
            ramEnabled = (value & 0x0F) == 0x0A;
        } else if (address < 0x4000) {
            int low = value & 0x1F;
            if (low == 0) low = 1;         /* bank 0 can't be selected here */
            romBank = (mbc1Upper << 5) | low;
        } else if (address < 0x6000) {
            mbc1Upper = value & 0x03;
            if (mbc1Mode == 0) romBank = (mbc1Upper << 5) | (romBank & 0x1F);
            else               ramBank = mbc1Upper;
        } else {
            mbc1Mode = value & 1;
        }
        break;

    case MBC_3:
        if (address < 0x2000) {
            ramEnabled = (value & 0x0F) == 0x0A;
        } else if (address < 0x4000) {
            romBank = value & 0x7F;
            if (romBank == 0) romBank = 1;
        } else if (address < 0x6000) {
            ramBank = value;               /* 0-3 = RAM, 8-C = RTC (not emulated) */
        }
        break;

    case MBC_5:
        if (address < 0x2000)      ramEnabled = (value & 0x0F) == 0x0A;
        else if (address < 0x3000) romBank = (romBank & 0x100) | value;
        else if (address < 0x4000) romBank = (romBank & 0xFF) | ((value & 1) << 8);
        else if (address < 0x6000) ramBank = value & 0x0F;
        break;
    }
}

uint8_t readByte(uint16_t address) {
    if (address < 0x4000) return rom[address];
    if (address < 0x8000) return readBankedRom(address);
    if (address < 0xA000) return vram[address - 0x8000];
    if (address < 0xC000) {
        if (!ramEnabled || ramBank > 0x0F) return 0xFF;
        return cartRam[(ramBank * 0x2000 + (address - 0xA000)) % sizeof cartRam];
    }
    if (address < 0xE000) return wram[address - 0xC000];
    if (address < 0xFE00) return wram[address - 0xE000];   /* echo RAM */
    if (address < 0xFEA0) return oam[address - 0xFE00];
    if (address < 0xFF00) return 0xFF;                     /* unusable */
    if (address < 0xFF80) {
        switch (address) {
        case REG_JOYP: return keysRead(io[0x00]);
        case REG_DIV:  return timerGetDiv();
        case REG_IF:   return io[0x0F] | 0xE0;
        default:       return io[address - 0xFF00];
        }
    }
    if (address < 0xFFFF) return hram[address - 0xFF80];
    return ie;
}

void writeByte(uint16_t address, uint8_t value) {
    if (address < 0x8000) { writeMBC(address, value); return; }
    if (address < 0xA000) { vram[address - 0x8000] = value; return; }
    if (address < 0xC000) {
        if (ramEnabled && ramBank <= 0x0F)
            cartRam[(ramBank * 0x2000 + (address - 0xA000)) % sizeof cartRam] = value;
        return;
    }
    if (address < 0xE000) { wram[address - 0xC000] = value; return; }
    if (address < 0xFE00) { wram[address - 0xE000] = value; return; }
    if (address < 0xFEA0) { oam[address - 0xFE00] = value; return; }
    if (address < 0xFF00) return;

    if (address < 0xFF80) {
        switch (address) {
        case REG_JOYP:
            /* only bits 4-5 (which button group to read) are writable */
            io[0x00] = (io[0x00] & 0xCF) | (value & 0x30);
            return;
        case REG_SC:
            /* start a serial transfer; there is no link cable, so finish at once */
            if (value & 0x80) {
                if (serialToStdout) { putchar(io[0x01]); fflush(stdout); }
                io[0x01] = 0xFF;
                io[0x02] = value & 0x7F;
                requestInterrupt(INT_SERIAL);
            } else {
                io[0x02] = value;
            }
            return;
        case REG_DIV:
            timerResetDiv();                     /* any write resets DIV to 0 */
            return;
        case REG_STAT:
            io[0x41] = (io[0x41] & 0x07) | (value & 0x78); /* low 3 bits are read-only */
            return;
        case REG_LY:
            return;                              /* read-only */
        case REG_DMA: {
            /* OAM DMA: copy 160 bytes from value*0x100 into sprite memory.
             * Real hardware takes 160 us; we do it instantly. */
            uint16_t src = (uint16_t)value << 8;
            for (int i = 0; i < 0xA0; i++) oam[i] = readByte(src + i);
            io[0x46] = value;
            return;
        }
        case REG_IF:
            io[0x0F] = value & 0x1F;
            return;
        default:
            io[address - 0xFF00] = value;
            return;
        }
    }
    if (address < 0xFFFF) { hram[address - 0xFF80] = value; return; }
    ie = value;
}

/* The Game Boy is little-endian: low byte first */
uint16_t readShort(uint16_t address) {
    return readByte(address) | (readByte(address + 1) << 8);
}

void writeShort(uint16_t address, uint16_t value) {
    writeByte(address, value & 0xFF);
    writeByte(address + 1, value >> 8);
}

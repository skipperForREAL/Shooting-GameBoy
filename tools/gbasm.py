"""
gbasm.py - a tiny two-pass Game Boy (SM83) assembler, just enough to build
the demo game without installing RGBDS.

Syntax is close to RGBDS:

    NAME = $FF40            ; constants ($hex, %binary, decimal, expressions)
    label:                  ; global label
    .loop:                  ; local label (scoped to the previous global label)
        ld a, [hl+]
        ldh [$FF40], a      ; ldh takes the full $FFxx address
        jr nz, .loop
        db 1, 2, $FF        ; raw bytes
        dw label            ; 16-bit little-endian words
        org $150            ; move the output position

Only the ROM-only (32 KiB, no MBC) layout is supported.
"""
import re

R8 = {'b': 0, 'c': 1, 'd': 2, 'e': 3, 'h': 4, 'l': 5, '[hl]': 6, 'a': 7}
R16 = {'bc': 0, 'de': 1, 'hl': 2, 'sp': 3}
R16_STACK = {'bc': 0, 'de': 1, 'hl': 2, 'af': 3}
CC = {'nz': 0, 'z': 1, 'nc': 2, 'c': 3}
ALU = {'add': 0, 'adc': 1, 'sub': 2, 'sbc': 3, 'and': 4, 'xor': 5, 'or': 6, 'cp': 7}
ROT = {'rlc': 0, 'rrc': 1, 'rl': 2, 'rr': 3, 'sla': 4, 'sra': 5, 'swap': 6, 'srl': 7}
BITOP = {'bit': 0x40, 'res': 0x80, 'set': 0xC0}
SIMPLE = {'nop': 0x00, 'halt': 0x76, 'di': 0xF3, 'ei': 0xFB, 'ret': 0xC9, 'reti': 0xD9,
          'rlca': 0x07, 'rrca': 0x0F, 'rla': 0x17, 'rra': 0x1F, 'daa': 0x27,
          'cpl': 0x2F, 'scf': 0x37, 'ccf': 0x3F}
INDIRECT_A = {'[bc]': 0x02, '[de]': 0x12, '[hl+]': 0x22, '[hli]': 0x22, '[hl-]': 0x32, '[hld]': 0x32}

# Nintendo logo: the boot ROM refuses to start a cartridge without it
LOGO = bytes.fromhex(
    'CEED6666CC0D000B03730083000C000D0008111F8889000EDCCC6EE6DDDDD999'
    'BBBB67636E0EECCCDDDC999FBBB9333E')


class AsmError(Exception):
    pass


class Symbols(dict):
    """Unknown names evaluate to 0 in pass 1 (labels defined later)."""
    strict = False

    def __missing__(self, key):
        if key in ('HIGH', 'LOW'):
            raise KeyError(key)          # fall through to the built-in functions
        if self.strict:
            raise AsmError(f"undefined symbol '{key}'")
        return 0


def split_operands(text):
    return [o.strip().lower() if is_register_like(o.strip()) else o.strip()
            for o in text.split(',')] if text.strip() else []


def is_register_like(o):
    o = o.lower().replace(' ', '')
    return o in R8 or o in R16 or o in R16_STACK or o in CC or o in INDIRECT_A or o == '[c]'


def is_mem(o):
    return o.startswith('[') and o.endswith(']')


class Assembler:
    def __init__(self, title='GAME'):
        self.title = title

    # ---------------------------------------------------------------- helpers
    def value(self, expr):
        expr = expr.strip()
        expr = re.sub(r'\$([0-9A-Fa-f]+)', r'0x\1', expr)
        expr = re.sub(r'%([01]+)', r'0b\1', expr)
        expr = re.sub(r'(?<![\w])\.([A-Za-z_]\w*)', lambda m: self.local(m.group(1)), expr)
        env = {'HIGH': lambda v: (v >> 8) & 0xFF, 'LOW': lambda v: v & 0xFF, '__builtins__': {}}
        try:
            return int(eval(expr, env, self.symbols))
        except AsmError:
            raise
        except Exception as e:
            raise AsmError(f"bad expression '{expr}': {e}")

    def local(self, name):
        return f'{self.scope}__{name}'

    def byte(self, expr):
        v = self.value(expr)
        if self.symbols.strict and not -128 <= v <= 255:
            raise AsmError(f'byte value out of range: {expr} = {v}')
        return v & 0xFF

    def word(self, expr):
        v = self.value(expr) & 0xFFFF
        return [v & 0xFF, v >> 8]

    # -------------------------------------------------------------- encoding
    def encode(self, mn, ops, pc):
        n = len(ops)
        if mn in SIMPLE and n == 0:
            return [SIMPLE[mn]]
        if mn == 'stop':
            return [0x10, 0x00]

        if mn == 'ld':
            d, s = ops
            if d in R8 and s in R8 and not (d == '[hl]' and s == '[hl]'):
                return [0x40 | R8[d] << 3 | R8[s]]
            if d == 'a' and s in INDIRECT_A:
                return [INDIRECT_A[s] | 0x08]
            if s == 'a' and d in INDIRECT_A:
                return [INDIRECT_A[d]]
            if d == '[c]' and s == 'a':
                return [0xE2]
            if d == 'a' and s == '[c]':
                return [0xF2]
            if d == 'sp' and s == 'hl':
                return [0xF9]
            if is_mem(d) and d not in R8 and s == 'a':
                return [0xEA] + self.word(d[1:-1])
            if d == 'a' and is_mem(s):
                return [0xFA] + self.word(s[1:-1])
            if is_mem(d) and d not in R8 and s == 'sp':
                return [0x08] + self.word(d[1:-1])
            if d in R16:
                return [0x01 | R16[d] << 4] + self.word(s)
            if d in R8:
                return [0x06 | R8[d] << 3, self.byte(s)]

        if mn == 'ldh':
            d, s = ops
            if s == 'a' and is_mem(d):
                return [0xE0, self.value(d[1:-1]) & 0xFF]
            if d == 'a' and is_mem(s):
                return [0xF0, self.value(s[1:-1]) & 0xFF]

        if mn in ('inc', 'dec') and n == 1:
            r = ops[0]
            if r in R8:
                return [(0x04 if mn == 'inc' else 0x05) | R8[r] << 3]
            if r in R16:
                return [(0x03 if mn == 'inc' else 0x0B) | R16[r] << 4]

        if mn == 'add' and n == 2 and ops[0] == 'hl':
            return [0x09 | R16[ops[1]] << 4]
        if mn == 'add' and n == 2 and ops[0] == 'sp':
            return [0xE8, self.byte(ops[1])]

        if mn in ALU:
            if n == 2 and ops[0] == 'a':
                ops = ops[1:]
            s = ops[0]
            if s in R8:
                return [0x80 | ALU[mn] << 3 | R8[s]]
            return [0xC6 | ALU[mn] << 3, self.byte(s)]

        if mn == 'jp':
            if ops == ['hl']:
                return [0xE9]
            if n == 2:
                return [0xC2 | CC[ops[0]] << 3] + self.word(ops[1])
            return [0xC3] + self.word(ops[0])

        if mn == 'jr':
            target = ops[-1]
            op = 0x18 if n == 1 else 0x20 | CC[ops[0]] << 3
            offset = self.value(target) - (pc + 2)
            if self.symbols.strict and not -128 <= offset <= 127:
                raise AsmError(f'jr target too far ({offset} bytes), use jp')
            return [op, offset & 0xFF]

        if mn == 'call':
            if n == 2:
                return [0xC4 | CC[ops[0]] << 3] + self.word(ops[1])
            return [0xCD] + self.word(ops[0])

        if mn == 'ret' and n == 1:
            return [0xC0 | CC[ops[0]] << 3]

        if mn == 'push':
            return [0xC5 | R16_STACK[ops[0]] << 4]
        if mn == 'pop':
            return [0xC1 | R16_STACK[ops[0]] << 4]
        if mn == 'rst':
            return [0xC7 | self.value(ops[0])]

        if mn in ROT and n == 1:
            return [0xCB, ROT[mn] << 3 | R8[ops[0]]]
        if mn in BITOP:
            bit = self.value(ops[0])
            return [0xCB, BITOP[mn] | bit << 3 | R8[ops[1]]]

        raise AsmError(f"can't encode: {mn} {', '.join(ops)}")

    # -------------------------------------------------------------- assembly
    def run_pass(self, source, strict):
        self.symbols.strict = strict
        self.rom = bytearray(0x8000)
        self.scope = ''
        pc = 0

        for lineno, raw in enumerate(source.splitlines(), 1):
            line = raw.split(';', 1)[0].strip()
            if not line:
                continue
            try:
                # labels (possibly followed by an instruction)
                m = re.match(r'^(\.?[A-Za-z_]\w*):(.*)$', line)
                if m:
                    name = m.group(1)
                    if name.startswith('.'):
                        name = self.local(name[1:])
                    else:
                        self.scope = name
                    self.symbols[name] = pc
                    line = m.group(2).strip()
                    if not line:
                        continue

                m = re.match(r'^([A-Za-z_]\w*)\s*=\s*(.+)$', line)
                if m:
                    self.symbols[m.group(1)] = self.value(m.group(2))
                    continue

                parts = line.split(None, 1)
                mn = parts[0].lower()
                ops = split_operands(parts[1]) if len(parts) > 1 else []
                ops = [o.replace(' ', '') if is_register_like(o) else o for o in ops]

                if mn == 'org':
                    pc = self.value(ops[0])
                    continue
                if mn == 'db':
                    data = [self.byte(o) for o in ops]
                elif mn == 'dw':
                    data = [b for o in ops for b in self.word(o)]
                elif mn == 'ds':
                    data = [0] * self.value(ops[0])
                else:
                    data = self.encode(mn, ops, pc)

                if pc + len(data) > len(self.rom):
                    raise AsmError('ROM overflow')
                self.rom[pc:pc + len(data)] = bytes(data)
                pc += len(data)
            except AsmError as e:
                raise AsmError(f'line {lineno}: {raw.strip()}\n  {e}') from None
            except (KeyError, ValueError) as e:
                raise AsmError(f'line {lineno}: {raw.strip()}\n  bad operand {e}') from None

    def assemble(self, source):
        self.symbols = Symbols()
        self.run_pass(source, strict=False)   # pass 1: find label addresses
        self.run_pass(source, strict=True)    # pass 2: emit real bytes
        self.fix_header()
        return bytes(self.rom)

    def fix_header(self):
        rom = self.rom
        rom[0x104:0x134] = LOGO
        title = self.title.upper().encode()[:15]
        rom[0x134:0x134 + len(title)] = title
        rom[0x147] = 0x00   # ROM only
        rom[0x148] = 0x00   # 32 KiB
        rom[0x149] = 0x00   # no RAM
        rom[0x14A] = 0x01   # non-Japanese
        x = 0
        for i in range(0x134, 0x14D):
            x = (x - rom[i] - 1) & 0xFF
        rom[0x14D] = x      # header checksum (checked by the boot ROM)
        total = sum(rom) - rom[0x14E] - rom[0x14F]
        rom[0x14E] = (total >> 8) & 0xFF
        rom[0x14F] = total & 0xFF

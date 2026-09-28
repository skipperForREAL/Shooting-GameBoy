"""
shooter.py - builds roms/shooter.gb, a small vertical space shooter.

    python3 tools/shooter.py roms/shooter.gb

Controls (in the emulator):
    arrows  move the ship       Z / Space  fire (hold for auto-fire)
    Enter   restart after game over

What the game shows off, hardware-wise:
    - tiles copied into VRAM while the LCD is off
    - the background map (a starfield) scrolled every frame with SCY
    - the window layer used as a fixed status bar at the bottom
    - sprites driven through a shadow OAM + OAM DMA from HRAM
    - the V-Blank interrupt + HALT to run the game at 60 fps
    - reading the joypad through 0xFF00
    - BCD score with DAA
"""
import random
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from gbasm import Assembler  # noqa: E402

# --------------------------------------------------------------------------
# Graphics. Each tile is 8x8, one character per pixel:
#   '.' = colour 0 (transparent for sprites), '1' '2' '3' = colours 1..3.
# The palettes make colour 0 black and colour 3 bright (space!).
# --------------------------------------------------------------------------
TILES = {
    'BLANK': ['........'] * 8,
    'STAR1': ['........', '........', '........', '...3....',
              '........', '........', '........', '........'],
    'STAR2': ['........', '........', '.....1..', '....121.',
              '.....1..', '........', '........', '........'],
    'SHIP': ['...33...',
             '...33...',
             '..3223..',
             '..3113..',
             '.332233.',
             '33322333',
             '3.3333.3',
             '..1..1..'],
    'BULLET': ['...33...',
               '...33...',
               '...22...',
               '...11...',
               '........', '........', '........', '........'],
    'ENEMY': ['3......3',
              '.3.33.3.',
              '..3223..',
              '.331133.',
              '33333333',
              '3.3..3.3',
              '...33...',
              '........'],
}

FONT = {
    '0': ['.333.', '3...3', '3..33', '3.3.3', '33..3', '3...3', '.333.'],
    '1': ['..3..', '.33..', '..3..', '..3..', '..3..', '..3..', '.333.'],
    '2': ['.333.', '3...3', '....3', '...3.', '..3..', '.3...', '33333'],
    '3': ['33333', '...3.', '..3..', '...3.', '....3', '3...3', '.333.'],
    '4': ['...3.', '..33.', '.3.3.', '3..3.', '33333', '...3.', '...3.'],
    '5': ['33333', '3....', '3333.', '....3', '....3', '3...3', '.333.'],
    '6': ['..33.', '.3...', '3....', '3333.', '3...3', '3...3', '.333.'],
    '7': ['33333', '....3', '...3.', '..3..', '.3...', '.3...', '.3...'],
    '8': ['.333.', '3...3', '3...3', '.333.', '3...3', '3...3', '.333.'],
    '9': ['.333.', '3...3', '3...3', '.3333', '....3', '...3.', '.33..'],
    'A': ['.333.', '3...3', '3...3', '33333', '3...3', '3...3', '3...3'],
    'C': ['.333.', '3...3', '3....', '3....', '3....', '3...3', '.333.'],
    'E': ['33333', '3....', '3....', '3333.', '3....', '3....', '33333'],
    'G': ['.333.', '3...3', '3....', '3.333', '3...3', '3...3', '.3333'],
    'M': ['3...3', '33.33', '3.3.3', '3.3.3', '3...3', '3...3', '3...3'],
    'O': ['.333.', '3...3', '3...3', '3...3', '3...3', '3...3', '.333.'],
    'R': ['3333.', '3...3', '3...3', '3333.', '3.3..', '3..3.', '3...3'],
    'S': ['.3333', '3....', '3....', '.333.', '....3', '....3', '3333.'],
    'V': ['3...3', '3...3', '3...3', '3...3', '3...3', '.3.3.', '..3..'],
}
for ch, rows in FONT.items():
    TILES['CH_' + ch] = ['.' + r.ljust(7, '.') for r in rows] + ['........']

TILE_NAMES = list(TILES)
TILE_INDEX = {name: i for i, name in enumerate(TILE_NAMES)}


def encode_tile(rows):
    """8 rows of 8 pixels -> 16 bytes (low bit-plane byte, high bit-plane byte per row)."""
    out = []
    for row in rows:
        lo = hi = 0
        for x, ch in enumerate(row):
            c = 0 if ch == '.' else int(ch)
            lo |= (c & 1) << (7 - x)
            hi |= (c >> 1) << (7 - x)
        out += [lo, hi]
    return out


def text_tiles(s):
    return [TILE_INDEX['BLANK'] if ch == ' ' else TILE_INDEX['CH_' + ch] for ch in s]


def db_lines(data, per_line=16):
    return '\n'.join('    db ' + ', '.join(f'${b:02X}' for b in data[i:i + per_line])
                     for i in range(0, len(data), per_line))


# Starfield: a 32x32 background map, mostly empty with random stars
rng = random.Random(1989)
bg_map = []
for _ in range(32 * 32):
    r = rng.random()
    bg_map.append(TILE_INDEX['STAR2'] if r < 0.03 else TILE_INDEX['STAR1'] if r < 0.10 else TILE_INDEX['BLANK'])

status_row = text_tiles('SCORE 0000') + [0] * 22

tile_bytes = [b for name in TILE_NAMES for b in encode_tile(TILES[name])]

SOURCE = f"""
; ======================= Hardware registers =======================
rJOYP = $FF00
rDIV  = $FF04
rIF   = $FF0F
rLCDC = $FF40
rSCY  = $FF42
rLY   = $FF44
rDMA  = $FF46
rBGP  = $FF47
rOBP0 = $FF48
rWY   = $FF4A
rWX   = $FF4B
rIE   = $FFFF

; ======================= Game variables (WRAM) ====================
vblank_flag = $C000
joy_cur     = $C001   ; buttons held this frame
joy_prev    = $C002
joy_new     = $C003   ; buttons that went down this frame
player_x    = $C004   ; sprite coordinates: screen x + 8
player_y    = $C005   ;                     screen y + 16
score_lo    = $C006   ; BCD: $42 means "42"
score_hi    = $C007
game_over   = $C008
fire_cd     = $C009   ; frames until we may fire again
rng_state   = $C00A
frame       = $C00B
bullets     = $C010   ; NUM_BULLETS x (y, x), y = 0 means unused
enemies     = $C020   ; NUM_ENEMIES x (y, x)
SHADOW_OAM  = $C100   ; copied to OAM by DMA each V-Blank (must be $xx00)

NUM_BULLETS = 4
NUM_ENEMIES = 5

; joypad bits in joy_cur / joy_new
PAD_A = 0
PAD_B = 1
PAD_SELECT = 2
PAD_START = 3
PAD_RIGHT = 4
PAD_LEFT = 5
PAD_UP = 6
PAD_DOWN = 7

TILE_SHIP   = {TILE_INDEX['SHIP']}
TILE_BULLET = {TILE_INDEX['BULLET']}
TILE_ENEMY  = {TILE_INDEX['ENEMY']}
DIGIT0      = {TILE_INDEX['CH_0']}

; ======================= Interrupt vectors ========================
    org $40                 ; V-Blank interrupt
    jp vblank_handler

; ======================= Cartridge entry point ====================
    org $100
    nop
    jp start                ; $104-$14F is the header (filled in by gbasm)

    org $150
start:
    di
    ld sp, $FFFE

    ; The LCD may only be turned off during V-Blank
.wait_vblank:
    ldh a, [rLY]
    cp 144
    jr c, .wait_vblank
    xor a
    ldh [rLCDC], a

    ; With the LCD off we can freely write to VRAM
    ld hl, $8000            ; tile data
    ld de, tiles
    ld bc, tiles_end - tiles
    call memcpy

    ld hl, $9800            ; background map: starfield
    ld de, bg_map
    ld bc, 1024
    call memcpy

    ld hl, $9C00            ; window map: status bar
    ld de, status_row
    ld bc, 32
    call memcpy

    ld hl, $C000            ; clear game variables + shadow OAM
    ld bc, $200
    call memclear

    ld hl, $FF80            ; the DMA routine must run from HRAM
    ld de, dma_routine
    ld bc, dma_routine_end - dma_routine
    call memcpy

    ld a, %00011011         ; colour 0 -> black ... colour 3 -> white
    ldh [rBGP], a
    ldh [rOBP0], a
    ld a, 136               ; window covers the last 8 lines
    ldh [rWY], a
    ld a, 7
    ldh [rWX], a

    ld a, $5A
    ld [rng_state], a
    call init_game

    ; LCD on | window map $9C00 | window on | tiles $8000 | sprites on | BG on
    ld a, %11110011
    ldh [rLCDC], a

    ld a, 1                 ; enable the V-Blank interrupt only
    ldh [rIE], a
    xor a
    ldh [rIF], a
    ei

; ======================= Main loop (once per frame) ===============
main_loop:
    call wait_vblank
    call draw_status        ; VRAM writes must happen in V-Blank

    ld hl, frame
    inc [hl]

    ldh a, [rSCY]           ; scroll the stars downwards
    dec a
    ldh [rSCY], a

    call read_joypad

    ld a, [game_over]
    and a
    jr z, .playing
    ld a, [joy_new]         ; game over: wait for Start
    bit PAD_START, a
    jr z, main_loop
    call init_game
    jr main_loop

.playing:
    call move_player
    call fire
    call update_bullets
    call update_enemies
    call check_collisions
    call build_oam
    jr main_loop

; ======================= Interrupt handler ========================
vblank_handler:
    push af
    call $FF80              ; OAM DMA: shadow OAM -> real OAM
    ld a, 1
    ld [vblank_flag], a
    pop af
    reti

; Copied to HRAM at $FF80. During DMA the CPU can only access HRAM.
dma_routine:
    ld a, HIGH(SHADOW_OAM)
    ldh [rDMA], a
    ld a, 40                ; wait 160 microseconds
.wait:
    dec a
    jr nz, .wait
    ret
dma_routine_end:

wait_vblank:
    xor a
    ld [vblank_flag], a
.loop:
    halt                    ; sleep until an interrupt
    ld a, [vblank_flag]
    and a
    jr z, .loop
    ret

; ======================= Input ====================================
; joy_cur = %DULRSsBA (Down Up Left Right Start select B A), 1 = held
read_joypad:
    ld a, $20               ; select the direction keys
    ldh [rJOYP], a
    ldh a, [rJOYP]
    ldh a, [rJOYP]          ; read twice to let the lines settle
    cpl                     ; hardware uses 0 = pressed
    and $0F
    swap a
    ld b, a
    ld a, $10               ; select the buttons
    ldh [rJOYP], a
    ldh a, [rJOYP]
    ldh a, [rJOYP]
    ldh a, [rJOYP]
    cpl
    and $0F
    or b
    ld b, a
    ld a, $30               ; deselect both
    ldh [rJOYP], a

    ld a, [joy_cur]
    ld [joy_prev], a
    ld a, b
    ld [joy_cur], a
    ld a, [joy_prev]
    cpl
    and b                   ; new = held now AND NOT held before
    ld [joy_new], a
    ret

; ======================= Player ===================================
move_player:
    ld a, [joy_cur]
    ld b, a
    ld a, [player_x]
    bit PAD_LEFT, b
    jr z, .no_left
    cp 10
    jr c, .no_left
    sub 2
.no_left:
    bit PAD_RIGHT, b
    jr z, .no_right
    cp 159
    jr nc, .no_right
    add a, 2
.no_right:
    ld [player_x], a

    ld a, [player_y]
    bit PAD_UP, b
    jr z, .no_up
    cp 88
    jr c, .no_up
    sub 2
.no_up:
    bit PAD_DOWN, b
    jr z, .no_down
    cp 143
    jr nc, .no_down
    add a, 2
.no_down:
    ld [player_y], a
    ret

fire:
    ld a, [fire_cd]
    and a
    jr z, .ready
    dec a
    ld [fire_cd], a
    ret
.ready:
    ld a, [joy_cur]
    bit PAD_A, a
    ret z
    ld hl, bullets          ; find a free bullet slot
    ld b, NUM_BULLETS
.find:
    ld a, [hl]
    and a
    jr z, .found
    inc hl
    inc hl
    dec b
    jr nz, .find
    ret                     ; all bullets in flight
.found:
    ld a, [player_y]
    sub 6
    ld [hl+], a
    ld a, [player_x]
    ld [hl], a
    ld a, 8
    ld [fire_cd], a
    ret

update_bullets:
    ld hl, bullets
    ld b, NUM_BULLETS
.loop:
    ld a, [hl]
    and a
    jr z, .next
    sub 4                   ; bullets fly up 4 px per frame
    cp 8
    jr nc, .store
    xor a                   ; off the top: free the slot
.store:
    ld [hl], a
.next:
    inc hl
    inc hl
    dec b
    jr nz, .loop
    ret

; ======================= Enemies ==================================
update_enemies:
    ld hl, enemies
    ld b, NUM_ENEMIES
.loop:
    ld a, b                 ; speed = 1 or 2 depending on the slot
    and 1
    inc a
    add a, [hl]
    cp 145
    jr c, .store
    call respawn_enemy      ; reached the bottom
    jr .next
.store:
    ld [hl], a
.next:
    inc hl
    inc hl
    dec b
    jr nz, .loop
    ret

; hl = pointer to enemy (y, x). Puts it at a random spot above the screen.
respawn_enemy:
    call random
    and $7F
    add a, 20
    ld c, a                 ; x = 20..147
    call random
    and $1F                 ; y = 0..31 (above / at the top edge)
    ld [hl+], a
    ld a, c
    ld [hl-], a
    ret

; returns a pseudo-random number in a (only a is changed)
random:
    push bc
    ld a, [rng_state]
    ld b, a
    add a, a
    add a, a
    add a, b                ; a * 5
    inc a                   ; + 1
    ld b, a
    ldh a, [rDIV]           ; mix in the hardware divider
    xor b
    ld [rng_state], a
    pop bc
    ret

; ======================= Collisions ===============================
; a = |a - c|
absdiff:
    sub c
    ret nc
    cpl
    inc a
    ret

check_collisions:
    ld hl, enemies
    ld b, NUM_ENEMIES
.enemy_loop:
    ; --- enemy vs player ---
    ld a, [player_y]
    ld c, a
    ld a, [hl]
    call absdiff
    cp 7
    jr nc, .check_bullets
    ld a, [player_x]
    ld c, a
    inc hl
    ld a, [hl-]
    call absdiff
    cp 7
    jr nc, .check_bullets
    ld a, 1                 ; crash!
    ld [game_over], a
    ret

.check_bullets:
    push hl
    push bc
    ld a, [hl+]             ; d = enemy y, e = enemy x
    ld d, a
    ld a, [hl-]
    ld e, a
    ld hl, bullets
    ld b, NUM_BULLETS
.bullet_loop:
    ld a, [hl]
    and a
    jr z, .next_bullet
    ld c, d
    call absdiff
    cp 8
    jr nc, .next_bullet
    inc hl
    ld a, [hl-]
    ld c, e
    call absdiff
    cp 7
    jr nc, .next_bullet
    xor a                   ; hit: remove the bullet...
    ld [hl], a
    call add_score          ; ...score a point...
    pop bc
    pop hl
    call respawn_enemy      ; ...and send the enemy back to the top
    jr .next_enemy
.next_bullet:
    inc hl
    inc hl
    dec b
    jr nz, .bullet_loop
    pop bc
    pop hl
.next_enemy:
    inc hl
    inc hl
    dec b
    jr nz, .enemy_loop
    ret

add_score:
    ld a, [score_lo]
    add a, 1
    daa                     ; keep it a valid BCD number
    ld [score_lo], a
    ret nc
    ld a, [score_hi]
    add a, 1
    daa
    ld [score_hi], a
    ret

; ======================= Drawing ==================================
; Fill the shadow OAM: sprite 0 = player, 1-4 = bullets, 5-9 = enemies
build_oam:
    ld hl, SHADOW_OAM
    ld a, [player_y]
    ld [hl+], a
    ld a, [player_x]
    ld [hl+], a
    ld a, TILE_SHIP
    ld [hl+], a
    xor a
    ld [hl+], a

    ld de, bullets
    ld b, NUM_BULLETS
.bullet:
    ld a, [de]
    ld [hl+], a
    inc de
    ld a, [de]
    ld [hl+], a
    inc de
    ld a, TILE_BULLET
    ld [hl+], a
    xor a
    ld [hl+], a
    dec b
    jr nz, .bullet

    ld a, [frame]           ; flip the enemy sprite every 8 frames = animation
    and 8
    add a, a
    add a, a                ; 8 * 4 = $20 = X-flip attribute bit
    ld c, a
    ld de, enemies
    ld b, NUM_ENEMIES
.enemy:
    ld a, [de]
    ld [hl+], a
    inc de
    ld a, [de]
    ld [hl+], a
    inc de
    ld a, TILE_ENEMY
    ld [hl+], a
    ld a, c
    ld [hl+], a
    dec b
    jr nz, .enemy
    ret

draw_status:
    ld hl, $9C06
    ld a, [score_hi]
    call draw_bcd
    ld a, [score_lo]
    call draw_bcd
    ld hl, $9C0B
    ld de, text_game_over
    ld a, [game_over]
    and a
    jr nz, .copy
    ld de, text_blank
.copy:
    ld b, 9
.loop:
    ld a, [de]
    ld [hl+], a
    inc de
    dec b
    jr nz, .loop
    ret

; a = BCD byte -> two digit tiles at hl
draw_bcd:
    ld c, a
    swap a
    and $0F
    add a, DIGIT0
    ld [hl+], a
    ld a, c
    and $0F
    add a, DIGIT0
    ld [hl+], a
    ret

init_game:
    xor a
    ld [score_lo], a
    ld [score_hi], a
    ld [game_over], a
    ld [fire_cd], a
    ld a, 84
    ld [player_x], a
    ld a, 136
    ld [player_y], a

    ld hl, bullets
    ld bc, NUM_BULLETS * 2
    call memclear

    ld hl, enemies
    ld b, NUM_ENEMIES
.enemy:
    call respawn_enemy
    inc hl
    inc hl
    dec b
    jr nz, .enemy
    ret

; ======================= Utilities ================================
; copy bc bytes from de to hl
memcpy:
    ld a, [de]
    ld [hl+], a
    inc de
    dec bc
    ld a, b
    or c
    jr nz, memcpy
    ret

; zero bc bytes at hl
memclear:
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, memclear
    ret

; ======================= Data =====================================
text_game_over:
    db {', '.join(map(str, text_tiles('GAME OVER')))}
text_blank:
    db 0, 0, 0, 0, 0, 0, 0, 0, 0

status_row:
{db_lines(status_row)}

tiles:
{db_lines(tile_bytes)}
tiles_end:

bg_map:
{db_lines(bg_map)}
"""

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'shooter.gb'
    rom = Assembler(title='SHOOTER').assemble(SOURCE)
    with open(out, 'wb') as f:
        f.write(rom)
    print(f'Wrote {out} ({len(rom)} bytes)')

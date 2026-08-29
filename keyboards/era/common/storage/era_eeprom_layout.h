// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* The ERA EEPROM config block, 256 bytes, in two halves.
 *
 * **256 is a hard ceiling and not a budget.** ERA_EEPROM_CONFIG_END anchors
 * both VIA_EEPROM_MAGIC_ADDR and DYNAMIC_KEYMAP_EEPROM_ADDR
 * (era_storage_adoption.h). Rearranging inside it costs a user their ERA
 * settings; growing past it moves the VIA magic and the dynamic keymap with it,
 * so it costs them their keymaps as well. The assert in era_eeprom_config_io.c
 * is a different and looser bound -- it checks the logical EEPROM size, which
 * does not protect the keymap.
 *
 * The syncable half is the ERA_CONFIG storage domain exactly, byte for byte
 * (era_host_peer_storage_contract.md). The protected half is sync-excluded and
 * never crosses the wire.
 *
 * Regions are grouped by meaning -- input behaviour, then lighting, then the
 * board's own, then one reserve at the end of each half -- rather than by the
 * order they were added, and each name covers what it says. There is
 * deliberately no parent region over the input-behaviour group: a name like
 * COMMON_FEATURE_CONFIG stopped describing its contents the first time a common
 * feature was carved out of the hole beside it, and a parent that has to be
 * re-argued at every claim is a worse boundary than the claims themselves.
 *
 * A reserve is not slack. era_host_peer_storage.c walks SYNCABLE_RESERVED and
 * refuses the whole ERA_CONFIG domain whose bytes there are not zero, so a new
 * claim **shrinks the reserve** rather than sitting inside it -- a claim left
 * inside would make every board that also ran storage fail its own integrity
 * check the first time a user touched the new feature. PROTECTED_RESERVED is
 * growth room and is walked by nothing; the reason it cannot be is written at
 * that claim below. */

#define ERA_EEPROM_CONFIG_SIZE 256
/* ERA storage adoption freezes stock QMK's no-KB/no-user eeconfig prefix at
 * 37 bytes. Keep this header assembler-safe because era_storage_adoption.h is
 * force-included as CONFIG_H in every translation unit, including .S files.
 * A C-only static assertion in era_host_peer_storage.c binds this literal back
 * to QMK's private EECONFIG_SIZE definition so upstream drift still fails the
 * build instead of moving the portable schema silently. */
#define ERA_EEPROM_QMK_CONFIG_SIZE 37
#define ERA_EEPROM_CONFIG_ADDR ERA_EEPROM_QMK_CONFIG_SIZE
#define ERA_EEPROM_CONFIG_END (ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_CONFIG_SIZE)

#define ERA_EEPROM_SYNCABLE_CONFIG_OFFSET 0
#define ERA_EEPROM_SYNCABLE_CONFIG_SIZE 176

/* -- syncable: input behaviour -------------------------------------------- */
#define ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET 0
#define ERA_EEPROM_TAP_DANCE_CONFIG_SIZE 88

#define ERA_EEPROM_TAPPING_CONFIG_OFFSET 88
#define ERA_EEPROM_TAPPING_CONFIG_SIZE 12

#define ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET 100
#define ERA_EEPROM_MOUSEKEY_CONFIG_SIZE 16

#define ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET 116
#define ERA_EEPROM_DEBOUNCE_CONFIG_SIZE 8

/* One parent over the two pairs, because they are one feature with one VIA
 * page per pair and a single size the pair struct is asserted against. */
#define ERA_EEPROM_SOCD_CONFIG_OFFSET 124
#define ERA_EEPROM_SOCD_CONFIG_SIZE 16
#define ERA_EEPROM_SOCD_LR_CONFIG_OFFSET 124
#define ERA_EEPROM_SOCD_LR_CONFIG_SIZE 8
#define ERA_EEPROM_SOCD_UD_CONFIG_OFFSET 132
#define ERA_EEPROM_SOCD_UD_CONFIG_SIZE 8

#define ERA_EEPROM_KKUK_CONFIG_OFFSET 140
#define ERA_EEPROM_KKUK_CONFIG_SIZE 4

/* -- syncable: lighting ---------------------------------------------------- */
#define ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET 144
#define ERA_EEPROM_BACKLIGHT_CONFIG_SIZE 4
#define ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET 148
#define ERA_EEPROM_RGB_INDICATOR_CONFIG_SIZE 8

/* -- syncable: the board's own --------------------------------------------- */
#define ERA_EEPROM_KEYBOARD_CONFIG_OFFSET 156
#define ERA_EEPROM_KEYBOARD_CONFIG_SIZE 8

/* -- syncable: the reserve ------------------------------------------------- */
#define ERA_EEPROM_SYNCABLE_RESERVED_OFFSET 164
#define ERA_EEPROM_SYNCABLE_RESERVED_SIZE 12

/* -- protected: sync-excluded ---------------------------------------------- */
#define ERA_EEPROM_PROTECTED_CONFIG_OFFSET 176
#define ERA_EEPROM_PROTECTED_CONFIG_SIZE 80

#define ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET 176
#define ERA_EEPROM_LOCAL_POLICY_CONFIG_SIZE 24

/* The split link's runtime-selected rate. Sync-excluded on purpose and not for
 * tidiness: the two halves agree on a rate over the wire, and syncing the value
 * would let one half's copy overwrite the other's and reintroduce exactly the
 * divergence that agreement prevents.
 *
 * One level byte, one flags byte, two reserved. Every way the block can fail
 * to say something -- zeroed, out of range, an unknown flag, a nonzero
 * reserved byte -- reads as High,
 * the compiled default every image already runs at and the zero value, so a
 * fresh EEPROM and a CLEANed one are both already at the default with no
 * initialiser having to run. That is why the region was allocated a session
 * before its reader existed and owed no second key bump when the reader
 * arrived. The reader and the writer are split/era_split_link.c. */
#define ERA_EEPROM_LINK_CONFIG_OFFSET 200
#define ERA_EEPROM_LINK_CONFIG_SIZE 4
#define ERA_SPLIT_LINK_LEVEL_HIGH 0
#define ERA_SPLIT_LINK_LEVEL_MEDIUM 1
#define ERA_SPLIT_LINK_LEVEL_LOW 2

#define ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET 204
#define ERA_EEPROM_RESET_GUARD_CONFIG_SIZE 16
/* Bumped 0x45524102 -> 0x45524103 at Slice 9.5: the local-policy flags byte
 * and the VIA sync value ids renumbered with the DUAL-HOST parent removal,
 * and the key change is the whole migration.
 *
 * Bumped 0x45524103 -> 0x45524104 with the 2026-08-18 regrouping: every region
 * except tap dance moved, so a block written by an older image means something
 * different at every offset above 87.
 *
 * **The guard this key drives is the tomak family's** (sirind/common/tomak_common.c)
 * and no other board implements one, so on a non-split board the bump is inert
 * and each feature's own validity check is what catches the move. That is why
 * the regrouping put a signature at the end of the new MOUSE block and added
 * the reserved-zero test to era_socd.c: with those two, every moved region's
 * fallback is a clean reset rather than a misread
 * (era_source_map.md, Stored-Data Compatibility). */
#define ERA_EEPROM_RESET_KEY 0x45524104UL

/* Recency baseline record (Slice 10): seven per-domain sync-baseline CRC32
 * values plus one guard word, written only at storage convergence closes.
 * Layout, guard, and update rules are canonical in
 * era_host_peer_storage_contract.md (Recency Layer); this claim is that
 * contract's own entry for the former future-metadata reserved hole. The
 * range stays sync-excluded and zero after a strict reset, which reads as an
 * invalid guard and degrades every domain conservatively. */
#define ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET 220
#define ERA_EEPROM_SYNC_BASELINE_CONFIG_SIZE 32

/* Growth room, and **not** a tripwire -- stated because the syncable reserve
 * beside it is one and the asymmetry otherwise reads as an omission. The
 * zero-walk that arms the other reserve is the ERA_CONFIG domain's own
 * prevalidation: it indexes the staged 176-byte domain image before an apply,
 * and this range is outside that image because it is outside the domain. There
 * is nothing here to prevalidate -- protected bytes never travel, so no peer
 * can present them and no apply can write them -- so a walk would have to read
 * local EEPROM at a moment that has nothing to do with it. A claim taken from
 * here shrinks this range the same way. */
#define ERA_EEPROM_PROTECTED_RESERVED_OFFSET 252
#define ERA_EEPROM_PROTECTED_RESERVED_SIZE 4

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Each region ends where the next begins, so a size change fails the build at
 * the first boundary it breaks instead of silently overlapping its neighbour. */
_Static_assert(ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET + ERA_EEPROM_TAP_DANCE_CONFIG_SIZE == ERA_EEPROM_TAPPING_CONFIG_OFFSET, "ERA tap dance config must end where the tapping config starts.");
_Static_assert(ERA_EEPROM_TAPPING_CONFIG_OFFSET + ERA_EEPROM_TAPPING_CONFIG_SIZE == ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, "ERA tapping config must end where the mousekey config starts.");
_Static_assert(ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET + ERA_EEPROM_MOUSEKEY_CONFIG_SIZE == ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET, "ERA mousekey config must end where the debounce config starts.");
_Static_assert(ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET + ERA_EEPROM_DEBOUNCE_CONFIG_SIZE == ERA_EEPROM_SOCD_CONFIG_OFFSET, "ERA debounce config must end where the SOCD config starts.");
_Static_assert(ERA_EEPROM_SOCD_CONFIG_OFFSET == ERA_EEPROM_SOCD_LR_CONFIG_OFFSET, "ERA SOCD config must start at its Left/Right pair.");
_Static_assert(ERA_EEPROM_SOCD_LR_CONFIG_OFFSET + ERA_EEPROM_SOCD_LR_CONFIG_SIZE == ERA_EEPROM_SOCD_UD_CONFIG_OFFSET, "ERA SOCD Left/Right pair must end where the Up/Down pair starts.");
_Static_assert(ERA_EEPROM_SOCD_LR_CONFIG_SIZE + ERA_EEPROM_SOCD_UD_CONFIG_SIZE == ERA_EEPROM_SOCD_CONFIG_SIZE, "ERA SOCD config must hold exactly its two pairs.");
_Static_assert(ERA_EEPROM_SOCD_CONFIG_OFFSET + ERA_EEPROM_SOCD_CONFIG_SIZE == ERA_EEPROM_KKUK_CONFIG_OFFSET, "ERA SOCD config must end where the KKUK config starts.");
_Static_assert(ERA_EEPROM_KKUK_CONFIG_OFFSET + ERA_EEPROM_KKUK_CONFIG_SIZE == ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, "ERA KKUK config must end where the backlight config starts.");
_Static_assert(ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET + ERA_EEPROM_BACKLIGHT_CONFIG_SIZE == ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET, "ERA backlight config must end where the RGB indicator config starts.");
_Static_assert(ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET + ERA_EEPROM_RGB_INDICATOR_CONFIG_SIZE == ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, "ERA RGB indicator config must end where the keyboard config starts.");
_Static_assert(ERA_EEPROM_KEYBOARD_CONFIG_OFFSET + ERA_EEPROM_KEYBOARD_CONFIG_SIZE == ERA_EEPROM_SYNCABLE_RESERVED_OFFSET, "ERA keyboard config must end where the syncable reserve starts.");
_Static_assert(ERA_EEPROM_SYNCABLE_RESERVED_OFFSET + ERA_EEPROM_SYNCABLE_RESERVED_SIZE == ERA_EEPROM_PROTECTED_CONFIG_OFFSET, "ERA syncable reserve must end where the protected range starts.");
_Static_assert(ERA_EEPROM_SYNCABLE_CONFIG_OFFSET + ERA_EEPROM_SYNCABLE_CONFIG_SIZE == ERA_EEPROM_PROTECTED_CONFIG_OFFSET, "ERA syncable EEPROM range must end where protected range starts.");
_Static_assert(ERA_EEPROM_PROTECTED_CONFIG_OFFSET + ERA_EEPROM_PROTECTED_CONFIG_SIZE == ERA_EEPROM_CONFIG_SIZE, "ERA protected EEPROM range must end at the config block boundary.");
_Static_assert(ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET == ERA_EEPROM_PROTECTED_CONFIG_OFFSET, "ERA local policy range must start the protected range.");
_Static_assert(ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET + ERA_EEPROM_LOCAL_POLICY_CONFIG_SIZE == ERA_EEPROM_LINK_CONFIG_OFFSET, "ERA local policy range must end where the link config starts.");
_Static_assert(ERA_EEPROM_LINK_CONFIG_OFFSET + ERA_EEPROM_LINK_CONFIG_SIZE == ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET, "ERA link config must end where the reset guard starts.");
_Static_assert(ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET + ERA_EEPROM_RESET_GUARD_CONFIG_SIZE == ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET, "ERA reset guard must end where the sync baseline record starts.");
_Static_assert(ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET + ERA_EEPROM_SYNC_BASELINE_CONFIG_SIZE == ERA_EEPROM_PROTECTED_RESERVED_OFFSET, "ERA sync baseline record must end where the protected reserve starts.");
_Static_assert(ERA_EEPROM_PROTECTED_RESERVED_OFFSET + ERA_EEPROM_PROTECTED_RESERVED_SIZE == ERA_EEPROM_CONFIG_SIZE, "ERA protected reserve must end at the config block boundary.");
/* High is the compiled default and must stay the zero value: the link region
 * carries no initialiser, so a zeroed or fresh block is at the default only
 * while this holds. */
_Static_assert(ERA_SPLIT_LINK_LEVEL_HIGH == 0, "The default split link level must be the zero value; the link region has no initialiser.");
#endif

#define ERA_VIA_SOCD_LR_CHANNEL 10
#define ERA_VIA_SOCD_UD_CHANNEL 11
#define ERA_VIA_KKUK_CHANNEL 12
#define ERA_VIA_MOUSEKEY_CHANNEL 13
#define ERA_VIA_DEBOUNCE_CHANNEL 14
#define ERA_VIA_TAPPING_CHANNEL 15

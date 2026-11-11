#pragma once
/* Game flow module (game_flow.md): segments 0000, 01f4, 0792 except the per-frame HUD — main state
 * machine, start-up/shut-down, files and archives, messages, menus, race_run, results.
 * Functions other modules call. */
#include "mem.h"

/* 0000:0000 main — game_flow.md §4.1 (FN_main; state machine on DS:0086; ends through exit_to_dos /
 * fatal_exit -> crt_exit, so it does not return in practice; main.c returns its value) */
int game_main(void);

/* 0000:0d62 pal_load — game_flow.md §4.3 (112 colours -> DS:0B6A at 16 + DS:90F0; pal_set if VGA and game_state != 5) */
void pal_load(u16 name_ds);
/* 0000:0ee0 file_load_far — game_flow.md §4.3 (whole file / archive entry into dst; DS:E86A = bytes read, DS:E86C = 0) */
void file_load_far(u16 name_ds, FarPtr dst);
/* 0000:0f58 random — game_flow.md §4.4 (seed DS:00D2 = seed*41C64E6Dh + 3039h; returns (seed >> 16) & 7FFFh) */
u16 td3_random(void);
/* 0000:0f80 key_poll — game_flow.md §4.4 (key from DS:915B or the joystick, hotkeys, DS:E08C; host pointer,
 * e.g. key_poll(&DSW(0x90DA)) from dissolve_poll) */
void key_poll(u16 *key);
/* 0000:1518 nop_1518 — game_flow.md §2a (empty) */
void nop_1518(void);
/* 0000:151a nop_151a — game_flow.md §2a (empty) */
void nop_151a(void);
/* 0000:179c message_box — game_flow.md §4.5 (message DS:16DA[id] in a grey box, per-id wait/prompt; answer in DS globals) */
void message_box(s16 id);
/* 0000:1e44 joystick_keys — game_flow.md §4.4 (if DS:0092: button -> 0Dh, else direction -> DS:00AA[..]; *key = result) */
u16 joystick_keys(u16 *key);
/* 01f4:1864 gfx_frame — game_flow.md §4.6 (rectangle outline with gfx_move_to / gfx_line_to) */
void gfx_frame(s16 x0, s16 x1, s16 y0, s16 y1);
/* 01f4:1a82 print_records — game_flow.md §4.6 ({col,row,text} records at s_ds+off, 80h next / AAh end; returns the offset after AAh) */
s16 print_records(u16 s_ds, s16 off);
/* 01f4:1c00 ega_pal_black — game_flow.md §4.6 (clears DS:E788[32]; EGA palette only outside VGA) */
void ega_pal_black(void);
/* 01f4:1c34 ega_pal_restore — game_flow.md §4.6 (EGA palette from DS:0E6A outside VGA; VGA: nothing) */
void ega_pal_restore(void);
/* 01f4:1c48 pal_apply — game_flow.md §4.6 (VGA: pal_set) */
void pal_apply(void);
/* 01f4:59ac pic_draw — game_flow.md §4.6 (VGA: rle_draw_page(pairs, npairs, width, x, y_bottom); flag unused) */
void pic_draw(u16 pairs_ds, u16 npairs, u16 width, s16 x, s16 y_bottom, s16 flag);

/* ======================================================================================================
 * Internal to the game_flow module (flow*.c). Other modules should not need these.
 * Conventions: `name_ds` / `s_ds` are DGROUP string offsets; MSC `int` = s16.
 * ====================================================================================================== */

/* ---- flow.c: main, start-up, shut-down, input ---- */
_Noreturn void exit_to_dos(void);                 /* 0000:07e4 — §4.2 */
void harderr_handler(u16 deverror, u8 errcode);   /* 0000:081c — §4.2 (never installed in the port) */
_Noreturn void fatal_exit(s16 code);              /* 0000:0874 — §4.2 */
void config_load(void);                           /* 0000:092a — §4.2 */
void cfg_show_video_choice(u8 n);                 /* 0000:0ce6 — §4.2 */
void cfg_show_sound_choice(u8 n);                 /* 0000:0d0e — §4.2 */
void cfg_show_yes_no(s16 yes);                    /* 0000:0d36 — §4.2 */
void mem_alloc_all(void);                         /* 0000:11d2 — §4.2 */
void mem_free_all(void);                          /* 0000:1442 — §4.2 */
void joystick_direction(void);                    /* 0000:1ea4 — §4.4 */
void player_rec_load(void);                       /* 01f4:000e — §3 */
void player_rec_save(void);                       /* 01f4:003e — §3 */

/* ---- flow_files.c: archive lookup, loaders, description files ---- */
void pal_load_32(u16 name_ds);                    /* 0000:0df6 — §4.3 */
void file_load_near(u16 name_ds, u16 dst_ds);     /* 0000:0e74 — §4.3 */
s16  archive_open(u16 name_ds);                   /* 0000:151c — §4.3 (handle, 0 = not archived) */
u16  name_hash_h1(u16 s_ds, u16 mul);             /* 0000:16d0 — FORMATS.md */
u16  name_hash_h2(u16 s_ds);                      /* 0000:171a — FORMATS.md */
u32  name_hash(u16 s_ds);                         /* 0000:1760 — (h1 << 16) | h2 */
void playdisk_write(void);                        /* 01f4:2736 — descriptions.md */
void playdisk_verify(void);                       /* 01f4:2812 — descriptions.md */
s16  playdisk_load(void);                         /* 01f4:28ae — descriptions.md (0 ok) */
s16  car_lst_load(s16 slot, u16 path_ds, u16 name_ds, u16 pics_ds);   /* 01f4:29f0 (0 ok) */
s16  scene_lst_load(s16 slot, u16 path_ds, u16 name_ds);             /* 01f4:2bc6 (0 ok, also .HI) */
void hi_write(void);                              /* 01f4:2e66 — descriptions.md */
s16  shared_scene_load(void);                     /* 01f4:53c4 — §4.12 */

/* ---- flow_screens.c: messages, text, waits, palette wrappers, title, PLAY DISK ---- */
s16  print_text(u16 s_ds, s16 off);               /* 01f4:1ad6 — §4.6 */
s16  print_record(u16 s_ds, s16 off);             /* 01f4:1b0a — §4.6 */
void print_chars(u16 s_ds, s16 n);                /* 01f4:1b58 — §4.6 */
s16  wait_key(s16 n);                             /* 01f4:1b8e — §4.6 */
void screen_present(void);                        /* 01f4:1be2 — §4.6 */
void pal_black_01F4_1C56(void);                   /* 01f4:1c56 pal_black — §4.6 */
void pal_fade_in_01F4_1C64(void);                 /* 01f4:1c64 pal_fade_in — §4.6 */
void pal_fade_out_01F4_1C72(void);                /* 01f4:1c72 pal_fade_out — §4.6 */
void pal_fade_out_low_01F4_1C80(void);            /* 01f4:1c80 pal_fade_out_low — §4.6 */
void page_keep_high(void);                        /* 01f4:1c8e — §4.6 */
void ega_pal_init(void);                          /* 01f4:1c9c — §4.6 */
s16  title_sequence(void);                        /* 01f4:0070 — §4.7 */
s16  credits_text(u16 s_ds);                      /* 01f4:19f4 — §4.7 */
s16  copy_protection_check(void);                 /* 01f4:1ec2 — §4.19 (returns 0) */
void protection_picture_draw(s16 n, s16 x);       /* 01f4:262a — §4.19 (dead code) */
void playdisk_prompt(void);                       /* 01f4:26fe — §4.12 */
void playdisk_screen(void);                       /* 01f4:2f0e — §4.12 */

/* ---- flow_menus.c: main menu, options, car and course select ---- */
s16  main_menu(void);                             /* 01f4:144c — §4.8 */
void menu_box_draw(s16 item, s16 c, s16 c2);      /* 01f4:18b0 — §4.8 */
void options_screen(void);                        /* 01f4:3b2e — §4.9 */
void car_select(void);                            /* 01f4:310c — §4.10 */
s16  car_next(s16 s);                             /* 01f4:3332 — §4.10 */
s16  car_prev(s16 s);                             /* 01f4:3396 — §4.10 */
void car_select_draw(void);                       /* 01f4:33f8 — §4.10 */
void scene_select(void);                          /* 01f4:3636 — §4.11 */
s16  scene_next(s16 s);                           /* 01f4:36de — §4.11 */
s16  scene_prev(s16 s);                           /* 01f4:373e — §4.11 */
void scene_select_draw(void);                     /* 01f4:379c — §4.11 */

/* ---- flow_scores.c: results pages, top scores ---- */
void top_score_enter(void);                       /* 01f4:0a96 — §4.18 */
s16  top_scores_screen(s16 unused);               /* 01f4:0f4e — §4.18 */
void print_score(u32 v);                          /* 01f4:12d4 — §4.6 */
void leg_banner_draw(void);                       /* 01f4:3f8e — §4.17 */
void race_status_screen(void);                    /* 01f4:40ce — §4.17 */
void results_frame_draw(s16 status);              /* 01f4:470e — §4.17 */
void leg_results(void);                           /* 01f4:4954 — §4.17 */
void draw_leg_record(s16 x, s16 y, u16 rec_ds, u8 hl);   /* 01f4:4d7e — §4.17 */
s16  leg_record_merge(u16 new_ds, u16 best_ds);   /* 01f4:51e2 — §4.17 */
void opponent_results(void);                      /* 01f4:55f0 — §4.16 */

/* ---- flow_race.c: one leg ---- */
void race_run(void);                              /* 0792:000c — §4.13 */
void car_leg_reset(void);                         /* 0792:083c — §4.14 */
void stage_load_objects(void);                    /* 0792:0d2e — §4.14 */
void leg_map_load(void);                          /* 0792:0fce — §4.14 */

/* ---- Port-side helpers of the game_flow files (no original address) ---- */
#include <string.h>
#include "symbols.h"
#include "platform/platform.h"
/* `page(p)` of game_flow.md §4: DS:009A = p; gfx_set_draw_page(p) (the game always pairs them). */
static inline void flow_page(u16 p) { DSW(DS_page_cur) = p; gfx_set_draw_page((s16)p); }
/* `VGA` of game_flow.md §4: video_mode == 13h. */
static inline bool flow_vga(void) { return DSW(DS_video_mode) == 0x13; }
/* strcpy between DGROUP strings (1940:0746). */
static inline void flow_strcpy(u16 dst_ds, u16 src_ds) { strcpy(ds_str(dst_ds), ds_str(src_ds)); }
/* file_load_far(name, view_block) + lzw_decode(view_block, DS:2500): every "draw_pic" of §4. */
static inline void flow_load_decode(u16 name_ds)
{
    file_load_far(name_ds, ds_far(DS_view_block));
    lzw_decode(ds_far(DS_view_block), ds_ptr(DS_sprite_set));
}

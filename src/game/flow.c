/* Game flow (game_flow.md): main (the state machine with the legs x players loop and the results
 * arithmetic), start-up / shut-down, configuration, memory, random, key poll and joystick keys,
 * player records. Segment 0000 / 01f4. */
#include "game/game.h"
#include "portcfg.h"

#include <SDL3/SDL.h>

/* 0000:0000 main — game_flow.md §4.1 / §4.1.1 (verified against 0000:0000-07e3) */
static u32 flow_hundredths(u8 min, u8 sec, u8 frac)    /* inline in main: min*6000 + sec*100 + frac*10 */
{
    return (u32)min * 6000u + (u32)sec * 100u + (u32)frac * 10u;
}

int game_main(void)
{
    u16 last_leg_done = 0;                                  /* [bp-0Eh] */
    char d;

    DSW(DS_game_state) = 0;
    DSB(DS_protection_ok) = 0;
    DSB(DS_skip_flag) = 0;
    DSB(DS_playdisk_ok) = 0;
    DSB(DS_text_transparent) = 0;
    crt_getcwd(DS_scratch_buf, 0x4f);
    d = (char)(DSB(DS_scratch_buf) & 0xdf);                 /* current drive, upper case */
    DSB(DS_archive_path) = (u8)d;
    DSW(DS_game_state) = 0xff;
    /* PORT: the drive letters are patched in as the original does; the file layer ignores them. */
    DSB(DS_path_playdisk) = (u8)d;
    DSB(DS_path_program) = (u8)d;
    DSB(DS_path_playdisk_dat) = (u8)d;
    DSB(DS_path_car) = (u8)d;
    DSB(DS_path_opponent) = (u8)d;
    DSB(DS_path_scene) = (u8)d;
    DSB(DS_playdisk_drive) = (u8)d;
    DSB(0x1921) = (u8)d;                                    /* message 14h drive */
    DSB(0x1A01) = (u8)d;                                    /* message 1Eh drive */
    DSB(0x1FB8) = (u8)d;                                    /* PLAY DISK screen drive record */
    config_load();
    ega_pal_init();
    kbd_install();
    snd_init();
    mem_alloc_all();
    frame_buffers_init();
    /* PORT: _harderr(harderr_handler) dropped — no INT 24h (game_flow.md §6). */
    flow_strcpy(DS_path_program + 2, 0x0042);               /* "COMPASS.LZ" */
    file_load_far(DS_path_program, ds_far(DS_compass_pic));
    flow_strcpy(DS_path_program + 2, flow_vga() ? 0x004D : 0x0056);   /* "WATER.LZ" / "WATEREGA.LZ" */
    file_load_far(DS_path_program, ds_far(DS_water_pic));
    flow_strcpy(DS_path_program + 2, 0x0062);               /* "CHASE.LZ" */
    file_load_far(DS_path_program, ds_far(DS_chase_pic));
    flow_strcpy(DS_path_program + 2, flow_vga() ? 0x006B : 0x0074);   /* "BROKE.LZ" / "BROKEGA.LZ" */
    file_load_far(DS_path_program, ds_far(DS_broke_pic));
    DSW(DS_game_state) = 1;
    mouse_init();
    mouse_show(0);
    mouse_set_range(2, 0x13d, 2, 0xc6);

    for (;;) {
        switch (DSW(DS_game_state)) {
        case 1:
            title_sequence();
            shared_scene_load();
            if (playdisk_load() != 0) playdisk_prompt();
            DSW(DS_game_state) = 4;
            break;

        case 2:
            if (copy_protection_check() == 0) DSB(DS_protection_ok) = 1;
            DSW(DS_game_state) = 5;
            break;

        case 3:
            if (DSL(DS_hi_scores) != 0) top_scores_screen(0);
            DSW(DS_game_state) = 4;
            break;

        case 4:
            shared_scene_load();
            music_for_state(DSB(DS_radio_station));
            while (DSW(DS_game_state) == 4) {
                DSB(DS_leg_index) = 0;
                DSW(DS_menu_choice) = 0;
                main_menu();
                if (DSW(DS_menu_choice) == 4) playdisk_prompt();
                if (DSW(DS_menu_choice) == 3) scene_select();
                if (DSW(DS_menu_choice) == 2) car_select();
                if (DSW(DS_menu_choice) == 1) options_screen();
                if (DSW(DS_menu_choice) == 0) DSW(DS_game_state) = DSB(DS_protection_ok) ? 5 : 2;
            }
            break;

        case 5: {
            /* ---- §4.1.1: legs x players and the results arithmetic (inline in main) ---- */
            playdisk_verify();
            DSB(DS_leg_index) = 0;
            {   /* ENH developer aid: TD3_START_LEG=n starts the race at leg n (0-based) */
                const char *e = SDL_getenv("TD3_START_LEG");
                if (e && SDL_atoi(e) > 0 && SDL_atoi(e) < DSB(DS_leg_count)) DSB(DS_leg_index) = (u8)SDL_atoi(e);
            }
            last_leg_done = 0;
            DSB(DS_auto_gearbox) = (s16)DSW(DS_skill_level) < 3;
            for (s16 p = 0; p < 4; p++) {
                for (s16 i = 0; i < 0x1b; i++) DSB(DS_player_recs + p * 0x1b + i) = 0;
                DSB(DS_player_recs + p * 0x1b + 0x18) = (DSB(DS_protection_ok) == 1) ? 5 : 1;   /* lives */
            }
            for (; DSB(DS_leg_index) < DSB(DS_leg_count); DSB(DS_leg_index)++) {
                for (s16 p = 0; p < 4; p++)
                    for (s16 i = 0; i < 10; i++) DSB(DS_player_recs + p * 0x1b + i) = 0;
                for (DSB(DS_cur_player) = 1; DSB(DS_cur_player) <= DSB(DS_race_players); DSB(DS_cur_player)++) {
                    player_rec_load();
                    if (DSB(DS_lives) == 0) continue;
                    DSL(DS_leg_score) = 0;
                    for (s16 i = 0; i < 10; i++) DSB(DS_leg_result + i) = 0;
                    DSW(DS_game_state) = 5;
                    DSB(DS_crash_count) = 0;
                    DSB(DS_leg_0a76) = 0;
                    race_run();
                    opponent_results();

                    /* leg time and distance */
                    DSB(DS_leg_result + 0) = DSB(DS_clock_min);
                    DSB(DS_leg_result + 1) = DSB(DS_clock_sec);
                    DSB(DS_leg_result + 2) = DSB(DS_clock_sub);
                    if (DSB(DS_car_imperial) == 0) {          /* km -> miles, 8.8 fixed point */
                        u16 v = (u16)(div32_16((u32)DSB(DS_odo_sixtieths) << 8, 60, NULL)
                                      + ((u16)DSB(DS_odo_units) << 8));
                        u32 m = ((u32)v * 5u) >> 3;
                        DSB(DS_leg_dist_units) = (u8)(m >> 8);
                        DSB(DS_leg_dist_sixtieths) = (u8)(((u32)(u8)m * 60u) >> 8);
                    } else {
                        DSB(DS_leg_dist_units) = DSB(DS_odo_units);
                        DSB(DS_leg_dist_sixtieths) = DSB(DS_odo_sixtieths);
                    }
                    u32 dist = (u32)DSB(DS_leg_dist_units) * 60u + DSB(DS_leg_dist_sixtieths);
                    u32 t = flow_hundredths(DSB(DS_leg_result), DSB(DS_leg_result + 1), DSB(DS_leg_result + 2));
                    DSB(DS_leg_result + 3) = t ? (u8)((s32)(dist * 6000u) / (s32)t) : 0;   /* _aFldiv, low byte */

                    /* leg score */
                    u32 k = (u16)((u16)(DSW(DS_skill_level) + 4) * (u16)DSB(DS_leg_result + 3));   /* 16-bit mul */
                    u32 s;
                    u16 ref = DSW(DS_leg_ref_time);
                    if (dist != 0 && DSB(DS_lives) != 0) {
                        s = (k * ref * 0x14u) / dist;                      /* u32 wrap, _aFuldiv */
                    } else {
                        u32 dd = (dist > ref) ? ref : dist;
                        s = (dd * k) / 0x60u;
                    }
                    if (s > 1000000u) s = 1000000u;
                    DSL(DS_leg_score) = s;
                    DSL(DS_leg_result + 4) = s;                       /* 09CA..09CD */
                    DSW(DS_game_state) = 6;

                    /* cumulative record */
                    DSL(DS_race_score) += s;
                    DSB(DS_race_result + 2) = (u8)(DSB(DS_race_result + 2) + DSB(DS_clock_sub));
                    if (DSB(DS_race_result + 2) > 9) {
                        DSB(DS_race_result + 2) = (u8)(DSB(DS_race_result + 2) - 10);
                        DSB(DS_race_result + 1)++;
                    }
                    DSB(DS_race_result + 1) = (u8)(DSB(DS_race_result + 1) + DSB(DS_clock_sec));
                    if (DSB(DS_race_result + 1) > 0x3b) {
                        DSB(DS_race_result + 1) = (u8)(DSB(DS_race_result + 1) - 0x3c);
                        if (DSB(DS_race_result) != 0xff) DSB(DS_race_result)++;
                    }
                    if ((u16)DSB(DS_race_result) + DSB(DS_clock_min) < 0x100)
                        DSB(DS_race_result) = (u8)(DSB(DS_race_result) + DSB(DS_clock_min));
                    else
                        DSB(DS_race_result) = 0xff;
                    DSB(DS_race_dist_sixtieths) = (u8)(DSB(DS_race_dist_sixtieths) + DSB(DS_leg_dist_sixtieths));
                    if (DSB(DS_race_dist_sixtieths) > 0x3b) {
                        DSB(DS_race_dist_sixtieths) = (u8)(DSB(DS_race_dist_sixtieths) - 0x3c);
                        DSB(DS_race_dist_units)++;
                    }
                    DSB(DS_race_dist_units) = (u8)(DSB(DS_race_dist_units) + DSB(DS_leg_dist_units));
                    u32 cd = (u32)DSB(DS_race_dist_units) * 60u + DSB(DS_race_dist_sixtieths);
                    u32 ct = flow_hundredths(DSB(DS_race_result), DSB(DS_race_result + 1), DSB(DS_race_result + 2));
                    DSB(DS_race_result + 3) = ct ? (u8)((s32)(cd * 6000u) / (s32)ct) : 0;
                    DSL(DS_race_result + 4) = DSL(DS_race_score);    /* 09D4..09D7 */

                    if (DSB(DS_protection_ok) && DSB(DS_leg_index) != DSB(DS_leg_count))
                        music_for_state(DSB(DS_radio_station));
                    flow_page(0);
                    gfx_set_colour(0);
                    gfx_fill_rect(0, 0x13f, 0, 199);
                    pal_apply();
                    if (DSB(DS_protection_ok) == 0 || DSB(DS_leg_index) == DSB(DS_leg_count)) {
                        DSL(DS_race_score) = 0;                       /* demo mode, or Esc + Y in the race */
                        goto race_end;
                    }
                    if (DSB(DS_lives) != 0) {
                        last_leg_done = DSB(DS_leg_index);
                        DSB(DS_lives) = (u8)(DSB(DS_lives) + 2);
                    }
                    leg_banner_draw();
                    leg_results();
                    player_rec_save();
                    if (DSB(DS_race_players) != 1 || DSB(DS_race_computer_cars) != 0) race_status_screen();
                }
            }
            DSW(DS_game_state) = 3;
            if (last_leg_done == (u16)(DSB(DS_leg_count) - 1) && DSB(DS_protection_ok)) {
                for (DSB(DS_cur_player) = 1; DSB(DS_cur_player) <= DSB(DS_race_players); DSB(DS_cur_player)++) {
                    player_rec_load();
                    if (DSB(DS_lives) != 0 && DSL(DS_race_score) >= DSL(DS_hi_scores + 6 * 4))   /* DS:1FDA */
                        top_score_enter();
                }
            }
        race_end:
            if (DSB(DS_protection_ok) == 0) {
                DSW(DS_game_state) = 4;
                DSL(DS_race_score) = 0;
            } else {
                DSW(DS_menu_choice) = 0;
            }
            break;
        }

        case 6:
            DSW(DS_game_state) = 4;
            break;

        default:
            break;                                          /* FFh never reached here */
        }
    }
}

/* 0000:07e4 exit_to_dos — game_flow.md §4.2 */
_Noreturn void exit_to_dos(void)
{
    snd_shutdown();
    mem_free_all();
    gfx_free_page(1);
    gfx_set_mode((s16)DSW(DS_bios_mode_at_start));
    text_exit_clear();
    kbd_restore();
    crt_exit(0);
}

/* 0000:081c harderr_handler — game_flow.md §4.2 (PORT: never installed, _harderr is dropped) */
void harderr_handler(u16 deverror, u8 errcode)
{
    (void)deverror;
    if (errcode != 0 && errcode != 2) {
        fatal_exit(5);
    }
    enable_interrupts();
    if (errcode == 0) message_box(0x23);        /* "Write Protect?" */
    if (errcode == 2) message_box(0x24);        /* "Drive Not Ready?" */
    /* PORT: _hardresume(1) (retry) dropped with the handler. */
}

/* 0000:0874 fatal_exit — game_flow.md §4.2 (PORT: print_text_bios logs the message, crt_exit shuts down) */
_Noreturn void fatal_exit(s16 code)
{
    snd_shutdown();
    if (code != 4) {
        mem_free_all();
        if (code != 1) gfx_free_page(1);
    }
    gfx_set_mode((s16)DSW(DS_bios_mode_at_start));
    text_exit_clear();
    kbd_restore();
    switch (code) {
    case 1:
        print_text_bios(0x0106, 1, 0);          /* "Not enough RAM, 556K (VGA & Tandy) or 526K (EGA)" */
        print_text_bios(0x0137, 1, 1);          /* "free RAM needed for TEST DRIVE III." */
        break;
    case 2: print_text_bios(0x015B, 1, 0); break;   /* "Important file open failed in TEST DRIVE III." */
    case 3: print_text_bios(0x0189, 1, 0); break;   /* "High score file update failed. Is your disk full?" */
    case 5: print_text_bios(0x01BB, 1, 0); break;   /* "AAAHHHH!! Unknown horrible hardware failure!!" */
    default: print_text_bios(0x01E9, 1, 0); break;  /* "Unknown failure mode!" */
    }
    crt_exit(code);
}

/* 0000:092a config_load — game_flow.md §4.2, descriptions.md TD3.CFG */
void config_load(void)
{
    u16 f;

    DSW(DS_bios_mode_at_start) = (u16)gfx_get_mode();
    DSW(DS_adapter) = (u16)gfx_detect();
    DSW(DS_cfg_tmp) = DSB((u16)(DSW(DS_adapter) + 0xee));  /* default video choice by adapter */
    gfx_set_mode((s16)DSW(DS_bios_mode_at_start));
    f = crt_fopen(0x0202, 0x01FF);                          /* "TD3.CFG", "rb" */
    DSW(DS_file_handle) = f;
    if (f == 0) {
        /* PORT: the text-mode prompts (getch) are dropped: defaults VGA/MCGA and AdLib/Sound Blaster, and
         * the file is not written (game_flow.md §4.2 "Port"). */
        DSW(DS_cfg_tmp) = 0;
        DSW(DS_video_mode) = DSW(DS_cfg_tmp);
        DSW(DS_snd_device_cfg) = 4;
        DSW(DS_midi_flag) = 0;
        DSW(DS_video_mode) = DSB((u16)(DSW(DS_video_mode) + 0xea));
    } else {
        crt_fread(ds_ptr(DS_cfg_tmp), 2, 1, f);
        /* PORT: only VGA (video 0) is ported: any other choice is forced to 0. */
        if (DSW(DS_cfg_tmp) != 0) DSW(DS_cfg_tmp) = 0;
        DSW(DS_video_mode) = DSB((u16)(DSW(DS_cfg_tmp) + 0xea));
        crt_fread(ds_ptr(DS_snd_device_cfg), 2, 1, f);
        crt_fread(ds_ptr(DS_midi_flag), 2, 1, f);
        crt_fclose(f);
    }
    /* PORT: audio 1-3 (Tandy, CMS, MT-32) are not ported and fall back to AdLib (4); 0 stays the PC speaker. */
    if (DSW(DS_snd_device_cfg) != 0) DSW(DS_snd_device_cfg) = 4;
    portcfg_apply_config();                 /* PORT: --sound (launcher option), portcfg.c */
    if (DSW(DS_snd_device_cfg) == 3) {
        DSB(DS_sfx_off) = 1;
        DSW(DS_snd_device_cfg) = 0x81;
    }
    DSB(DS_cfg_0090) = 0;
    gfx_set_mode((s16)DSW(DS_video_mode));
    gfx_set_visible_page(0);
    DSW(DS_cfg_tmp) = (u16)gfx_alloc_page(1);
    if (DSW(DS_cfg_tmp) == 8) fatal_exit(1);
    gfx_set_copy_page(1);
    gfx_set_draw_page(1);
    DSW(DS_page1_seg) = gfx_get_draw_seg();
    gfx_set_draw_page(0);
    DSW(DS_page_seg) = gfx_get_draw_seg();
}

/* 0000:0ce6 cfg_show_video_choice — game_flow.md §4.2 (unused in the port: prompts dropped) */
void cfg_show_video_choice(u8 n)
{
    DSB(DS_scratch_buf + 0x40) = (u8)(n + '0');             /* PORT: the original uses a stack buffer */
    DSB(DS_scratch_buf + 0x41) = 0;
    print_text_bios(DS_scratch_buf + 0x40, 0x20, 8);
}

/* 0000:0d0e cfg_show_sound_choice — game_flow.md §4.2 (unused in the port: prompts dropped) */
void cfg_show_sound_choice(u8 n)
{
    DSB(DS_scratch_buf + 0x40) = (u8)(n + '0');             /* PORT: the original uses a stack buffer */
    DSB(DS_scratch_buf + 0x41) = 0;
    print_text_bios(DS_scratch_buf + 0x40, 0x20, 0x11);
}

/* 0000:0d36 cfg_show_yes_no — game_flow.md §4.2 (unused in the port: prompts dropped) */
void cfg_show_yes_no(s16 yes)
{
    print_text_bios(yes == 0 ? 0x0442 : 0x0444, 0x2c, 0x15);   /* "N" / "Y" */
}

/* 0000:11d2 mem_alloc_all — game_flow.md §4.2 (far buffers from crt_fmalloc inside mem[]) */
void mem_alloc_all(void)
{
    FarPtr p;

    p = crt_fmalloc(0x57b0);
    ds_far_wr(DS_face_block, p);
    if (far_is_null(p)) fatal_exit(1);
    ds_far_wr(DS_order_ofs, far_add(p, 16000));             /* DS:CEB8 = +3E80h */
    ds_far_wr(DS_key_ofs, far_add(p, 0x4b00));              /* DS:CC8E */
    ds_far_wr(DS_remap_ptr, far_add(p, 0x5780));            /* DS:E5B0 */
    p = crt_fmalloc(0xd010);
    ds_far_wr(DS_sprite_cache, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(0xf7e4);
    ds_far_wr(DS_tiles_shared, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(65000);
    ds_far_wr(DS_tiles_scene, p);
    if (far_is_null(p)) fatal_exit(1);
    ds_far_wr(DS_diffleva_pic, p);
    ds_far_wr(DS_difflevb_pic, far_add(p, 0x3520));
    ds_far_wr(DS_difflevc_pic, far_add(p, 27000));
    ds_far_wr(DS_music_buf, far_add(p, 0x9bdc));
    ds_far_wr(DS_select_pic, far_add(p, 0xb34c));
    ds_far_wr(0xE870, far_add(p, 0xc864));                  /* no reader known */
    ds_far_wr(DS_ssbj_pic, far_add(p, 0xef74));
    p = crt_fmalloc(0x7810);
    ds_far_wr(DS_view_block, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(0x7df0);
    ds_far_wr(DS_objects_set, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(0x1e6e);
    ds_far_wr(DS_lanes_set, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(flow_vga() ? 0x12d4 : 0xf46);
    ds_far_wr(DS_water_pic, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(flow_vga() ? 0x1d06 : 0x1374);
    ds_far_wr(DS_broke_pic, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(0xe2e);
    ds_far_wr(DS_scene_music_buf, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(0x8e8);
    ds_far_wr(DS_car_pob, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(0x37a);
    ds_far_wr(DS_chase_pic, p);
    if (far_is_null(p)) fatal_exit(1);
    p = crt_fmalloc(0x113);
    ds_far_wr(DS_compass_pic, p);
    if (far_is_null(p)) fatal_exit(1);
    if (lzw_alloc() == 0) fatal_exit(1);
}

/* 0000:1442 mem_free_all — game_flow.md §4.2 */
void mem_free_all(void)
{
    crt_ffree(ds_far(DS_face_block));
    crt_ffree(ds_far(DS_sprite_cache));
    crt_ffree(ds_far(DS_tiles_shared));
    crt_ffree(ds_far(DS_tiles_scene));
    crt_ffree(ds_far(DS_view_block));
    crt_ffree(ds_far(DS_objects_set));
    crt_ffree(ds_far(DS_lanes_set));
    crt_ffree(ds_far(DS_water_pic));
    crt_ffree(ds_far(DS_broke_pic));
    crt_ffree(ds_far(DS_scene_music_buf));
    crt_ffree(ds_far(DS_car_pob));
    crt_ffree(ds_far(DS_chase_pic));
    crt_ffree(ds_far(DS_compass_pic));
    lzw_free();
}

/* 0000:1518 nop_1518 — game_flow.md §2a */
void nop_1518(void) {}

/* 0000:151a nop_151a — game_flow.md §2a */
void nop_151a(void) {}

/* 0000:0f58 random — game_flow.md §4.4 (verified) */
u16 td3_random(void)
{
    u32 seed = (u32)DSW(DS_rand_hi) << 16 | DSW(DS_rand_lo);
    seed = seed * 0x41C64E6Du + 0x3039u;                   /* u32 wrap */
    DSW(DS_rand_lo) = (u16)seed;
    DSW(DS_rand_hi) = (u16)(seed >> 16);
    return (u16)(DSW(DS_rand_hi) & 0x7fff);
}

/* 0000:0f80 key_poll — game_flow.md §4.4 (verified) */
void key_poll(u16 *key)
{
    *key = 0;
    if (DSB(DS_demo_input) == 0) {
        s8 k;
        if (DSB(DS_kbd_key) != 0) {
            *key = DSB(DS_kbd_key);
            DSB(DS_kbd_key) = 0;
        }
        k = (s8)*key;
        if (k == 0) {
            joystick_keys(key);
        } else {
            if (k == -0x80) {                               /* Esc */
                if (DSW(DS_game_state) == 5) {
                    if (DSB(DS_replay_state) == 0) {
                        message_box(0x26);                  /* "Exit to MAIN SELECT SCREEN (Y/N)?" */
                        if (DSB(DS_msg_answer) != 0) {
                            DSW(DS_race_state) = 3;
                            DSB(DS_leg_index) = DSB(DS_leg_count);
                        }
                    }
                } else {
                    message_box(0x12);                      /* "Exit to DOS (Y/N)?" */
                }
                k = 0; *key = 0;
            }
            if (k == 0x14) {                                /* Ctrl-J */
                DSW(DS_joystick_on) = 1;
                message_box(5);
                k = 0; *key = 0;
            }
            if (k == 0x15) {                                /* Ctrl-K */
                DSB(DS_joy_bits) = 0;
                DSW(DS_joystick_on) = 0;
                message_box(6);
                k = 0; *key = 0;
            }
            if (k == 0x17) {                                /* Ctrl-A */
                DSB(DS_joy_analog) = 1;
                message_box(0x2f);
                k = 0; *key = 0;
            }
            if (k == 0x18) {                                /* Ctrl-D */
                DSB(DS_joy_analog) = 0;
                message_box(0x30);
                k = 0; *key = 0;
            }
            if (k == 0x11) {                                /* Ctrl-Q */
                DSB(DS_music_off) ^= 1;
                if (DSB(DS_music_off) == 1) music_stop(0);
                else music_for_state(DSB(DS_radio_station));
                message_box((s16)(DSB(DS_music_off) + 1));
                k = 0; *key = 0;
            }
            if (k == 0x16) {                                /* Ctrl-E */
                DSB(DS_engine_off) ^= 1;
                message_box((s16)(DSB(DS_engine_off) + 0x29));
                k = 0; *key = 0;
            }
            if (k == 0x13) {                                /* Ctrl-S */
                DSB(DS_sfx_off) ^= 1;
                if (DSB(DS_sfx_off) == 1) {
                    sfx_play(0);
                    if (DSW(DS_snd_device_cfg) == 0 && DSW(DS_game_state) == 5)
                        music_for_state(DSB(DS_radio_station));
                } else if (DSW(DS_snd_device_cfg) == 0 && DSW(DS_game_state) == 5 && DSB(DS_music_off) == 0) {
                    music_stop(0);
                }
                message_box((s16)(DSB(DS_sfx_off) + 3));
                k = 0; *key = 0;
            }
            if (k == 0x12) {                                /* Ctrl-P */
                *key = 0;
                message_box(0x11);                          /* "PAUSE - Press space to resume" */
                music_for_state(DSB(DS_radio_station));
            }
        }
    } else {
        nop_far();                                          /* demo_keys: never (DS:008D is never set) */
    }
    *key &= 0x00ff;
    DSB(DS_key_code) = (u8)*key;
    if ((u8)*key != 0) DSB(DS_skip_flag) = 1;
}

/* 0000:1e44 joystick_keys — game_flow.md §4.4 */
u16 joystick_keys(u16 *key)
{
    u16 r = 0;
    DSB(DS_joy_bits) = 0;
    if (DSW(DS_joystick_on) != 0) {
        DSW(DS_joy_x_prev) = DSW(DS_joy_x);
        DSW(DS_joy_y_prev) = DSW(DS_joy_y);
        if (joy_read() != 0) {
            DSB(DS_joy_bits) = (u8)(DSB(DS_joy_bits) + 0x10);
            r = 0x0d;                                       /* button = Enter */
        }
        joystick_direction();
        if (r == 0) r = DSB((u16)(DS_joy_dir_keys + (DSB(DS_joy_bits) & 0x0f)));
    }
    *key = r;
    return r;
}

/* 0000:1ea4 joystick_direction — game_flow.md §4.4 (verified; all u16 unsigned) */
void joystick_direction(void)
{
    u16 x = DSW(DS_joy_x), y = DSW(DS_joy_y);
    if (x < DSW(DS_joy_xmin)) DSW(DS_joy_xmin) = x;
    if (DSW(DS_joy_xmax) < x) DSW(DS_joy_xmax) = x;
    if (y < DSW(DS_joy_y_min)) DSW(DS_joy_y_min) = y;
    if (DSW(DS_joy_y_max) < y) DSW(DS_joy_y_max) = y;
    if ((u16)((u16)(DSW(DS_joy_xmax) - DSW(DS_joy_xcentre)) >> 3) + DSW(DS_joy_xcentre) < x)
        DSB(DS_joy_bits) = (u8)(DSB(DS_joy_bits) + 8);                         /* right */
    else if (x < (u16)(DSW(DS_joy_xcentre) - (u16)((u16)(DSW(DS_joy_xcentre) - DSW(DS_joy_xmin)) >> 2)))
        DSB(DS_joy_bits) = (u8)(DSB(DS_joy_bits) + 4);                         /* left */
    if ((u16)((u16)(DSW(DS_joy_y_max) - DSW(DS_joy_y_ctr)) / 6) + DSW(DS_joy_y_ctr) < y)
        DSB(DS_joy_bits) = (u8)(DSB(DS_joy_bits) + 2);                         /* down */
    else if (y < (u16)(DSW(DS_joy_y_ctr) - (u16)((u16)(DSW(DS_joy_y_ctr) - DSW(DS_joy_y_min)) >> 2)))
        DSB(DS_joy_bits) = (u8)(DSB(DS_joy_bits) + 1);                         /* up */
}

/* 01f4:000e player_rec_load — game_flow.md §3 (record cur_player-1, bytes 0..18h -> DS:09C6) */
void player_rec_load(void)
{
    for (s16 i = 0; i < 0x19; i++)
        DSB(DS_leg_result + i) = DSB((u16)(DSB(DS_cur_player) * 0x1b + i - 0x1827));
}

/* 01f4:003e player_rec_save — game_flow.md §3 */
void player_rec_save(void)
{
    for (s16 i = 0; i < 0x19; i++)
        DSB((u16)(DSB(DS_cur_player) * 0x1b + i - 0x1827)) = DSB(DS_leg_result + i);
}

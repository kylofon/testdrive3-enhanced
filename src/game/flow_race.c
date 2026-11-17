/* Game flow (game_flow.md §4.13-4.14): race_run (one leg: stage load, the frame loop, surface events, the
 * frame pacing), the car reset at a leg (re)start and the stage / leg map loaders. Segment 0792. */
#include "game/game.h"
#include "enhanced/enhanced.h"

#include <SDL3/SDL.h>

/* 0792:000c race_run — game_flow.md §4.13 (verified) */
void race_run(void)
{
    u16 frame_start;

    DSB(DS_frozen) = 0;
    enh_keys_race(true);                                   /* ENH: key bindings while racing */
    /* ENH developer aid: TD3_DEBUG_KEYS=1 turns on the original's dormant debug keys (T rain, S snow, N night;
     * they also make the car invulnerable), for checking the renderer in weather and at night. */
    bool debug_keys = SDL_getenv("TD3_DEBUG_KEYS") != NULL;
    playdisk_verify();
    DSB(DS_prev_external_view) = 0;
    DSB(DS_external_panel_on) = 0;
    DSB(DS_ext_view) = 0;
    DSW(DS_view_top_row) = 0x10;
    sfx_play(0);
    music_stop(0);
    stage_load_objects();
    car_leg_reset();
    hud_reset_topbar();
    cockpit_setup();
    shifter_update();
    wheel_update();
    tach_draw();
    odometer_draw();
    DSB(DS_kbd_key) = 0;
    if (DSB(DS_race_players) != 1) {
        DSB(0x17E9) = (u8)(DSB(DS_cur_player) + '0');         /* "Player n, ready to drive...." */
        message_box(9);
    }
    /* PORT: [bp-4] is uninitialised on the first frame in the original (game_flow.md §7): start at now. */
    frame_start = DSW(DS_tick_count);

    for (;;) {
        u16 now = DSW(DS_tick_count);                          /* the pacing reference (iVar2) */
        if (debug_keys) DSB(DS_debug_keys) = 1;                /* ENH developer aid (see above) */
        if (DSW(DS_race_state) == 3) {                         /* leave */
            enh_stop();                                        /* ENH */
            enh_keys_race(false);                              /* ENH */
            if (DSB(DS_race_computer_cars) != 0) opponent_times_finalize();
            DSB(DS_shake) = 1;
            screen_shake_step();
            sfx_play(0);
            pal_black_01F4_1C56();
            flow_page(0);
            gfx_set_colour(0);
            gfx_fill_rect(0, 0x13f, 0, 199);
            return;
        }
        /* PORT: the original reads DS:00A0 twice here (u8 difference, then the new frame start); no tick can
         * arrive in between in the port, so both reads are `now`. */
        DSB(DS_frame_ticks) = (u8)((u8)now - (u8)frame_start);
        frame_start = now;

        if (DSB(DS_prev_external_view) != DSB(DS_ext_view) || DSW(DS_race_state) != DSW(DS_prev_race_state)) {
            if (DSB(DS_look_rect_dirty) != 0) {
                gfx_copy_rect(0xd0, 0x107, 0x68, 0x97, 0xf8, 0xab, 1, 0);
                DSB(DS_look_rect_dirty) = 0;
            }
            if (DSB(DS_need_place) != 0) car_leg_reset();
            hud_reset_topbar();
            if (DSW(DS_race_state) == DSW(DS_prev_race_state)) {
                if (DSB(DS_crashed) != 0) {                    /* crash sequence */
                    if (DSB(DS_crash_water) == 0) broken_glass_overlay();
                    else {
                        enh_stop();                            /* ENH: the water roll scrolls the screen */
                        water_overlay_start();
                    }
                    for (s16 i = 1; i < 0x50; i += 2) {        /* 40 steps */
                        if (DSB(DS_crash_water) == 0 || DSB(DS_water_anim) != 1) bios_wait_ticks(1);
                        else water_roll_step();
                        screen_shake_step();
                    }
                    if (DSB(DS_crash_water) != 0 && DSB(DS_water_anim) == 1) {
                        flow_page(1);
                        cockpit_pictures_draw(0x67);
                        flow_page(0);
                        DSB(DS_colour_offset) = 0;
                    }
                    DSB(DS_water_anim) = 0;
                    DSB(DS_cam_moved) = 0;
                    life_lost(0);                              /* 0e12:044e: lives, restart or leave */
                }
            } else {                                           /* state change (restart ...) */
                DSB(DS_look_rect_dirty) = 0;
                DSB(DS_unused_e330) = 0;
                DSB(DS_lever_hold) = 0;
                DSB(DS_lever_target) = DSB((u16)(DS_car_lever_step + DSB(DS_gear)));
                DSB(DS_lever_step) = (u8)(DSB(DS_lever_target) + 1);   /* force the lever redraw */
            }
            if (DSB(DS_prev_external_view) != DSB(DS_ext_view) && DSB(DS_ext_view) == 0) chase_view_exit();
            if (DSB(DS_prev_external_view) != DSB(DS_ext_view) && DSB(DS_ext_view) != 0) chase_view_enter();
            DSB(DS_prev_external_view) = DSB(DS_ext_view);
            if (DSB(DS_keep_prev_state) == 0) DSW(DS_prev_race_state) = DSW(DS_race_state);
            else DSB(DS_keep_prev_state) = 0;
            if (DSW(DS_race_state) == 2) {                     /* restart: release the clock */
                DSB(DS_clock_running) = 1;
                DSW(DS_race_state) = 1;
                continue;                                      /* no frame, no wait */
            }
            flow_page(0);
        }
        if (DSW(DS_pending_msg) != 0) {
            s16 m = (s16)DSW(DS_pending_msg);
            DSW(DS_pending_msg) = 0;
            message_box(m);
        }

        /* ENH: the first of the three control reads runs before the simulation instead of after it, so the car
         * reacts to the keys held now instead of those held up to a frame earlier (the same three reads a frame:
         * steering and throttle change at the same rate). */
        race_input(0);
        frame_update();
        frame_draw();
        race_input(1);
        if (DSB(DS_crashed) == 0 && DSB(DS_ext_view) == 0) {
            shifter_update();
            tach_draw();
            odometer_draw();
            odometer_update();
        }
        if (DSW(DS_race_state) != 3 && DSW(DS_race_state) != 2) view_present();
        race_input(2);
        td3_random();

        if (DSB(DS_last_surface) != DSB(DS_surface_under_car)) {
            if (DSB(DS_ext_view) == 0) {
                u8 surf = DSB(DS_surface_under_car);
                if (surf == 0x1c) {                            /* weather zone, step 2, 0..4 (no remap) */
                    if (DSB(DS_wind_level) == 0) DSB(DS_zone_dir) = 1;
                    if (DSB(DS_wind_level) == 4) DSB(DS_zone_dir) = 0;
                    if (DSB(DS_zone_dir) == 1) DSB(DS_wind_level) = (u8)(DSB(DS_wind_level) + 2);
                    else DSB(DS_wind_level) = (u8)(DSB(DS_wind_level) - 2);
                } else if (surf == 0x1d) {
                    if (DSB(DS_rain_level) == 0) DSB(DS_zone_dir) = 1;
                    if (DSB(DS_rain_level) == 8) DSB(DS_zone_dir) = 0;
                    if (DSB(DS_zone_dir) == 1) {
                        DSB(DS_rain_level) = (u8)(DSB(DS_rain_level) + 4);
                        DSB(DS_sky_alt) = 1;
                    } else {
                        DSB(DS_rain_level) = (u8)(DSB(DS_rain_level) - 4);
                        if (DSB(DS_rain_level) == 0) DSB(DS_sky_alt) = 0;
                    }
                    build_colour_remap();
                } else if (surf == 0x1e) {
                    DSB(DS_rain_level) = 0;
                    if (DSB(DS_snow_level) == 0) DSB(DS_zone_dir) = 1;
                    if (DSB(DS_snow_level) == 8) DSB(DS_zone_dir) = 0;
                    if (DSB(DS_zone_dir) == 1) {
                        DSB(DS_snow_level) = (u8)(DSB(DS_snow_level) + 4);
                        DSB(DS_sky_alt) = 1;
                    } else {
                        DSB(DS_snow_level) = (u8)(DSB(DS_snow_level) - 4);
                        if (DSB(DS_snow_level) == 0) DSB(DS_sky_alt) = 0;
                    }
                    build_colour_remap();
                } else if (surf == 0x1f) {
                    DSW(DS_race_state) = 3;                    /* finish (gas station tile) */
                }
            }
            DSB(DS_last_surface) = DSB(DS_surface_under_car);
        }
        if (DSB(DS_protection_ok) == 0 && DSB(DS_clock_min) == 2) {   /* demo limit */
            message_box(0x1f);                                 /* "Your time is up..." */
            DSW(DS_race_state) = 3;
        }
        /* frame pacing. PORT (PLAN.md decision 4): the original waits for 5 ticks; the port waits for
         * host_frame_ticks() ticks (default 29, --frame-ticks), DS:B70E keeps the measured ticks. */
        while ((u16)(DSW(DS_tick_count) - now) < (u16)host_frame_ticks()) {
            td3_random();
            host_pump();
        }
    }
}

/* 0792:083c car_leg_reset — game_flow.md §4.14 (checked against the disassembly: word stores, the start
 * position << 7 zero-extended for x/z and sign-extended for y) */
void car_leg_reset(void)
{
    u16 heading;
    s16 gh;

    DSB(DS_mirror_dirty) = 1;
    DSB(DS_on_ground) = 0;
    DSW(DS_odo_accum) = 0;
    DSW(DS_odo_accum + 2) = 0;
    replay_clear();
    DSB(DS_shake) = 1;
    screen_shake_step();
    if (DSB(DS_menu_preview) == 0 && DSB(DS_lives) != 0) music_for_state(DSB(DS_radio_station));
    life_reset();
    DSW(DS_rpm_prev) = 0;
    DSW(DS_engine_rpm) = 0;
    DSL(DS_vel_y) = 0;                                         /* DS:125A/125C */
    DSL(DS_speed_long) = 0;                                    /* DS:124E/1250 */
    DSW(DS_pitch) = 0;
    DSB(DS_need_place) = 0;
    DSW(DS_offroad_frames) = 0;
    DSL(DS_rpm_inertia) = 0;                                   /* DS:12A4/12A6 */
    DSB(DS_steer_wheel) = 0x10;
    heading = (u16)((u8)(DSB(DS_obj_heading) + 0x40) << 8);    /* heading of object 0 */
    DSW(DS_view_heading) = heading;
    DSW(DS_course) = heading;
    DSW(DS_body_heading) = heading;
    DSL(DS_pos_x) = (u32)DSW(DS_sprite_x) << 7;                /* zero-extended */
    DSL(DS_pos_z) = (u32)DSW(DS_sprite_z) << 7;
    gh = (s16)(DSW(DS_sprite_y) - DSW(DS_car_eye_height));
    DSW(DS_ground_height) = (u16)gh;
    DSL(DS_pos_y) = (u32)(s32)gh << 7;                         /* sign-extended */
    DSB(DS_compass_drawn) = 0xff;
    mouse_set_pos(0xa0, 100);
}

/* 0792:0d2e stage_load_objects — game_flow.md §4.14 (leg sprites, map, tiles, object sets, car and opponents) */
void stage_load_objects(void)
{
    view_setup();
    DSB(DS_tach_step) = 0;
    DSB(DS_speedo_step) = 0;
    DSB(DS_odo_units) = 0;
    DSB(DS_odo_sixtieths) = 0;
    DSW(DS_pending_msg) = 0;
    DSW(DS_race_state) = 1;
    DSW(DS_prev_race_state) = 1;
    DSB(DS_crashed) = 0;
    if (DSB(DS_menu_preview) == 0) {                           /* <scene><d>.DAT sprites */
        DSB(DS_path_scene + 9) = DSB((u16)(DSB(DS_leg_index) + DS_leg_data_digit));
        flow_strcpy(DS_path_scene + 10, 0x11AE);               /* ".DAT" */
        file_load_near(DS_path_scene, DS_sprite_set);
    }
    leg_map_load();
    if (DSB(DS_menu_preview) == 0) {
        sprites_prescale();
        enh_sprites_loaded();                                  /* ENH: native sprite images */
    }
    if (DSB(DS_menu_preview) == 0) {
        flow_strcpy(DS_path_scene + 9, 0x11B3);                /* "T.BIN" */
        file_load_far(DS_path_scene, ds_far(DS_tiles_scene));
        DSB(DS_shared_data_loaded) = 0;                        /* the menu pictures are destroyed */
        DSB(DS_path_scene + 9) = 0;
        if (DSB(0x0B15) == 0) {                                /* the scene has its own O/P sets */
            flow_strcpy(DS_path_scene + 9, 0x11B9);            /* "O.BIN" */
            file_load_far(DS_path_scene, ds_far(DS_objects_set));
            flow_strcpy(DS_path_scene + 9, 0x11BF);            /* "P.BIN" */
            file_load_far(DS_path_scene, ds_far(DS_lanes_set));
        }
    }
    flow_strcpy(DS_path_car + 7, 0x11C5);                      /* ".POB" */
    file_load_far(DS_path_car, ds_far(DS_car_pob));
    DSB(DS_opp2_slot) = 0;
    DSB(DS_opp1_slot) = 0;
    if (DSB(DS_protection_ok) != 0 && DSB(DS_race_computer_cars) != 0 && DSB(DS_menu_preview) == 0) {
        u16 f;
        if (DSB(DS_cars_on_disk) > 1) {
            if (DSB(DS_cars_on_disk) == 2) {
                DSB(DS_opp1_slot) = (u8)(DSB(DS_car_index) ^ 1);
                DSB(DS_opp2_slot) = DSB(DS_opp1_slot);
            } else {
                DSB(DS_opp1_slot) = 0;
                DSB(DS_opp2_slot) = 1;
                if (DSW(DS_car_index) == 0) DSB(DS_opp1_slot) = 2;
                if (DSW(DS_car_index) == 1) DSB(DS_opp2_slot) = 2;
            }
        }
        flow_strcpy(DS_path_opponent + 2, (u16)(DSB(DS_opp1_slot) * 6 + DS_car_slots));
        flow_strcpy(DS_path_opponent + 7, 0x11CA);             /* ".LST" */
        f = crt_fopen(DS_path_opponent, 0x11CF);               /* "rb" */
        DSW(DS_file_handle) = f;
        if (f != 0) {
            crt_fread(ds_ptr(DS_opp1_name), 1, 0x13, f);
            crt_fclose(f);
        }
        flow_strcpy(DS_path_opponent + 7, 0x11D2);             /* ".POB" */
        file_load_near(DS_path_opponent, 0xCEBC);
        flow_strcpy(DS_path_opponent + 2, (u16)(DSB(DS_opp2_slot) * 6 + DS_car_slots));
        flow_strcpy(DS_path_opponent + 7, 0x11D7);             /* ".LST" */
        f = crt_fopen(DS_path_opponent, 0x11DC);               /* "rb" */
        DSW(DS_file_handle) = f;
        if (f != 0) {
            crt_fread(ds_ptr(DS_opp2_name), 1, 0x13, f);
            crt_fclose(f);
        }
        flow_strcpy(DS_path_opponent + 7, 0x11DF);             /* ".POB" */
        file_load_near(DS_path_opponent, 0xD7A4);
    }
    leg_state_reset();
    if (DSB(DS_menu_preview) == 0) pal_fade_out_01F4_1C72();
}

/* 0792:0fce leg_map_load — game_flow.md §4.14 (<scene><'A'+leg>.DAT into DS:9592, opponents off, colour pairs) */
void leg_map_load(void)
{
    if (DSB(DS_menu_preview) == 0) {
        DSB(DS_path_scene + 9) = (u8)(DSB(DS_leg_index) + 'A');
        flow_strcpy(DS_path_scene + 10, 0x11E4);               /* ".DAT" */
        file_load_near(DS_path_scene, DS_leg_file);
    }
    if (DSB(DS_protection_ok) == 0 || DSB(DS_race_computer_cars) == 0 || DSB(DS_menu_preview) != 0) {
        DSW(DS_obj_flags + 2) = 0x2f;                          /* DS:A47B: opponents not racing */
        DSW(DS_obj_flags + 4) = 0x22af;                        /* DS:A47D */
    }
    if (DSW(DS_video_mode) == 9 || DSW(DS_video_mode) == 0x0d) {   /* EGA / Tandy colour pairs (not reached) */
        u16 n = 0, hi = 0;
        while (hi < 0x1000) {
            for (u16 i = 0; i < 0x10; i++) DSW((u16)(DS_colour_pairs + (n++) * 2)) = (u16)(hi + i);
            hi = (u16)((u8)((hi >> 8) + 1) << 8);
        }
    }
    if (flow_vga() && DSB(DS_colour_mode) == 0) {
        for (u16 i = 0; i < 0x100; i++) {
            u16 a = (u16)(DS_colour_pairs + i * 2);
            if (DSW(a) > 0x0f0f) DSW(a) = (u16)(DSW(a) - 0x202);
        }
    }
}

/* Game flow (game_flow.md §4.13-4.14): race_run (one leg: stage load, the frame loop, surface events, the
 * frame pacing), the car reset at a leg (re)start and the stage / leg map loaders. Segment 0792. */
#include "game/game.h"
#include "enhanced/enhanced.h"
#include "portcfg.h"

#include <SDL3/SDL.h>
#include <string.h>

/* ENH --weather-fix: the original uses one DS_zone_dir for wind, rain and snow, so a crossing of one weather
 * type can flip the direction of another. Keep a direction per weather type instead. */
static u8 weather_wind_dir;
static u8 weather_rain_dir;
static u8 weather_snow_dir;

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
                    u8 *dir = portcfg.weather_fix ? &weather_wind_dir : &DSB(DS_zone_dir);
                    if (DSB(DS_wind_level) == 0) *dir = 1;
                    if (DSB(DS_wind_level) == 4) *dir = 0;
                    if (*dir == 1) DSB(DS_wind_level) = (u8)(DSB(DS_wind_level) + 2);
                    else DSB(DS_wind_level) = (u8)(DSB(DS_wind_level) - 2);
                } else if (surf == 0x1d) {
                    u8 *dir = portcfg.weather_fix ? &weather_rain_dir : &DSB(DS_zone_dir);
                    if (DSB(DS_rain_level) == 0) *dir = 1;
                    if (DSB(DS_rain_level) == 8) *dir = 0;
                    if (*dir == 1) {
                        DSB(DS_rain_level) = (u8)(DSB(DS_rain_level) + 4);
                        DSB(DS_sky_alt) = 1;
                    } else {
                        DSB(DS_rain_level) = (u8)(DSB(DS_rain_level) - 4);
                        if (DSB(DS_rain_level) == 0) DSB(DS_sky_alt) = 0;
                    }
                    build_colour_remap();
                } else if (surf == 0x1e) {
                    DSB(DS_rain_level) = 0;
                    u8 *dir = portcfg.weather_fix ? &weather_snow_dir : &DSB(DS_zone_dir);
                    if (DSB(DS_snow_level) == 0) *dir = 1;
                    if (DSB(DS_snow_level) == 8) *dir = 0;
                    if (*dir == 1) {
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
                DSB(DS_last_surface) = DSB(DS_surface_under_car);
            } else if (!portcfg.weather_fix) {
                DSB(DS_last_surface) = DSB(DS_surface_under_car);
            }
            /* ENH --weather-fix: when in external view, keep the transition pending so it fires on return. */
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

/* ENH: choose the computer cars for the whole course. The original derived both opponents from the
 * player's slot on every leg (DS_opp*_slot = player ^ 1, or slots 0/1/2), so the three-car game never
 * used the two expansion cars against you. Here the slots are drawn once from the other cars on the
 * play disk and kept in DS_opp1_slot / DS_opp2_slot; stage_load_objects() reloads those slots on each
 * leg, so a leg change (or a restart within a leg) keeps the same two rivals. */
void race_opponents_pick(void)
{
    u8 others[14];                                     /* DS_car_slots holds at most 14 cars */
    u8 n = 0;
    u8 player = (u8)DSW(DS_car_index);
    u8 installed = DSB(DS_cars_on_disk);

    if (!portcfg.random_rivals) {
        /* ENH option off: the original's three-car selection, made here once for the whole course.
         * DSW(DS_car_index) cannot change while a course runs, so this is the same on every leg. */
        DSB(DS_opp1_slot) = 0;
        DSB(DS_opp2_slot) = 0;
        if (installed > 1) {
            if (installed == 2) {
                DSB(DS_opp1_slot) = (u8)(player ^ 1);
                DSB(DS_opp2_slot) = DSB(DS_opp1_slot);
            } else {
                DSB(DS_opp1_slot) = 0;
                DSB(DS_opp2_slot) = 1;
                if (player == 0) DSB(DS_opp1_slot) = 2;
                if (player == 1) DSB(DS_opp2_slot) = 2;
            }
        }
        return;
    }
    if (DSB(DS_protection_ok) == 0 || DSB(DS_race_computer_cars) == 0) {
        DSB(DS_opp1_slot) = 0;
        DSB(DS_opp2_slot) = 0;
        return;
    }
    if (installed > 14) installed = 14;
    for (u8 slot = 0; slot < installed; slot++) {
        if (slot != player && DSB((u16)(slot * 6 + DS_car_slots)) == 'C') others[n++] = slot;
    }
    if (n == 0) {                                      /* no other car installed: keep the player's */
        DSB(DS_opp1_slot) = player;
        DSB(DS_opp2_slot) = player;
        return;
    }
    u8 first = (u8)(td3_random() % n);
    DSB(DS_opp1_slot) = others[first];
    others[first] = others[--n];                       /* do not give both rivals the same car */
    DSB(DS_opp2_slot) = (n == 0) ? DSB(DS_opp1_slot) : others[td3_random() % n];
}

static void ai_opponent_routing_generate(void);

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
    /* ENH: the slots were picked once for the course by race_opponents_pick(); the original recomputed
     * them from DS_car_index here on every leg, which confined the rivals to the three original cars. */
    if (DSB(DS_protection_ok) != 0 && DSB(DS_race_computer_cars) != 0 && DSB(DS_menu_preview) == 0) {
        u16 f;
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
    ai_opponent_routing_generate();                            /* ENH: --ai-opponent-routing */
    if (DSB(DS_menu_preview) == 0) pal_fade_out_01F4_1C72();
}

/* ENH --ai-opponent-routing: generate opponent AI branch scripts at leg load.
 * Legacy keeps the scene's original scripts. Modern modes probe valid lane routes to the
 * resolved finish cell using the same control-point/branch semantics as vehicle_follow_lane,
 * then write a script to DS_opp_scripts (same on-disk/runtime format).
 * Finish resolution: SCENE02 uses the unique 0x51 finish-line cell when present; otherwise
 * trace original opponent scripts and require agreement. Deterministic picks stable routes and
 * gives opponents separate routes when possible; Random picks an independent random valid route. */
#define AI_ROUTE_MAX_STATES 262144
#define AI_ROUTE_MAX_RESULTS 128
#define AI_ROUTE_STEP_LIMIT 1024
#define AI_ROUTE_MAX_BRANCHES 9

typedef struct {
    u8 x, y, wp, sp, beh, initial;
    u8 nbranch;
    u8 branch[AI_ROUTE_MAX_BRANCHES];
    u16 steps;
} AiRouteState;

typedef struct {
    u8 initial;
    u8 nbranch;
    u8 branch[AI_ROUTE_MAX_BRANCHES];
    u16 steps;
    u8 fx, fy;
} AiRouteResult;

/* Finish resolution for AI route generation.
 * - SCENE02: shipped branch scripts are unreliable; use the unique 0x51 finish-line cell.
 * - Other scenes: trace original opponent branch scripts through the lane graph and use
 *   their termination cell. If scripts disagree or cannot be traced, keep original scripts. */
static u8 ai_map_tile(u8 x, u8 y) { return (u8)DSW(DS_leg_map + 2 * (y * 32 + x)); }
static u8 ai_map_rot(u8 x, u8 y)  { return (u8)((DSW(DS_leg_map + 2 * (y * 32 + x)) >> 14) & 3); }

static int ai_s6(int v) { v &= 0x3F; return v & 0x20 ? v - 0x40 : v; }
static u16 ai_swapb(u16 v) { return (u16)(v << 8 | v >> 8); }

static int ai_branch_offset(u16 flags, u8 behavior)
{
    if (flags & 0x8000) {
        u16 sel = flags;
        if ((behavior & 7) == 6) sel = ai_swapb(sel);
        if ((sel & 0x3F) == 0) sel = ai_swapb(sel);
        return ai_s6(sel & 0x3F);
    }
    {
        int low = ai_s6(flags & 0x3F);
        int high = ai_s6((flags >> 8) & 0x3F);
        int bm = (behavior < 3) ? 0 : (behavior == 3) ? 1 : (behavior == 4) ? 2 : -1;
        if (bm == 1 && low != 0) return low;
        if (bm > 1 && high != 0) return high;
        if (bm < 0) return 0x7FFF; /* nondeterministic for simple modes; avoid */
    }
    return 1;
}

static bool ai_lane_wp(FarPtr pbin, u8 tile, u8 wp, u16 *flags, u8 *count)
{
    u16 off = rd16(pbin.seg, (u16)(pbin.off + tile * 2));
    u16 base = (u16)(pbin.off + off);
    u8 hdr = rd8(pbin.seg, base);
    u8 c = (u8)(hdr & 0x7F);
    if (count) *count = c;
    if (wp >= c) return false;
    if (flags) *flags = rd16(pbin.seg, (u16)(base + 1 + wp * 8));
    return true;
}

static bool ai_find_entry(FarPtr pbin, u8 tile, u8 entry_flags, u8 *out_wp)
{
    u16 off = rd16(pbin.seg, (u16)(pbin.off + tile * 2));
    u16 base = (u16)(pbin.off + off);
    u8 c = (u8)(rd8(pbin.seg, base) & 0x7F);
    for (u8 i = 0; i < c; i++) {
        u16 f = rd16(pbin.seg, (u16)(base + 1 + i * 8));
        if ((u8)f == entry_flags && (f & 0xC0) == 0xC0) { *out_wp = i; return true; }
    }
    return false;
}

static int ai_trace_script_finish(FarPtr pbin, u8 opp, u8 start_x, u8 start_y, u8 start_wp);

static int ai_find_unique_tile_cell(u8 tile)
{
    int found = -1;
    for (int i = 0; i < 512; i++) {
        if ((u8)DSW(DS_leg_map + 2 * i) != tile) continue;
        if (found >= 0) return -1;
        found = i;
    }
    return found;
}

/* Resolve the intended finish cell for AI route generation.
 * SCENE02: shipped branch scripts are unreliable, use the known 0x51 finish-line tile.
 * Others: trace original opponent scripts through the lane graph to their termination cell.
 * Returns 0..511, or -1 if unresolved (caller should keep original scripts). */
static int ai_resolve_finish_cell(FarPtr pbin, const u8 *opp_id, const u8 *opp_x, const u8 *opp_y, const u8 *opp_wp, int nopp)
{
    /* SCENE02: shipped branch scripts are unreliable; prefer the known 0x51 finish-line tile. */
    if (DSW(DS_scene_index) == 1) {
        int cell = ai_find_unique_tile_cell(0x51);
        if (cell >= 0) {
            u16 flags; u8 lc;
            if (ai_lane_wp(pbin, 0x51, 0, &flags, &lc) && lc > 0) return cell;
        }
        /* Fall through to script trace if 0x51 is absent/unexpected. */
    }

    /* Trace original opponent scripts to their termination cell. */
    {
        int cell = -1;
        for (int i = 0; i < nopp; i++) {
            int c = ai_trace_script_finish(pbin, opp_id[i], opp_x[i], opp_y[i], opp_wp[i]);
            if (c < 0) continue;
            if (cell < 0) cell = c;
            else if (cell != c) return -1; /* disagree: keep original scripts */
        }
        if (cell >= 0) {
            u16 flags; u8 lc;
            u8 t = ai_map_tile((u8)(cell & 0x1F), (u8)((cell >> 5) & 0x0F));
            if (ai_lane_wp(pbin, t, 0, &flags, &lc) && lc > 0) return cell;
        }
    }
    return -1;
}

static void ai_write_script(u8 opponent, u8 initial, const u8 *branch, u8 nbranch)
{
    u16 base = (u16)(DS_opp_scripts + 10 * opponent);
    DSB(base) = initial;
    for (u8 i = 1; i < 10; i++) DSB((u16)(base + i)) = 0;
    for (u8 i = 0; i < nbranch && i < 9; i++) DSB((u16)(base + 1 + i)) = branch[i];
    if (nbranch < 9) DSB((u16)(base + 1 + nbranch)) = 0;
}

/* Trace an opponent's original branch script through the lane graph to find the cell where
 * it terminates (script byte == 0 at a control point). Returns cell index 0..511, or -1. */
static int ai_trace_script_finish(FarPtr pbin, u8 opp, u8 start_x, u8 start_y, u8 start_wp)
{
    u8 x = start_x, y = start_y, wp = start_wp;
    u8 beh = DSB((u16)(DS_opp_scripts + 10 * opp)); /* initial behavior */
    u8 spos = 0; /* DS_script_pos */
    for (int steps = 0; steps < 4096; steps++) {
        u8 tile = ai_map_tile(x, y);
        u16 flags; u8 count;
        if (!ai_lane_wp(pbin, tile, wp, &flags, &count)) return -1;
        if ((flags & 0xC0) == 0x80) {
            u8 rot = ai_map_rot(x, y);
            u8 worldSide = (u8)((rot + (flags & 3)) & 3);
            int dx[4] = {0,1,0,-1}, dy[4] = {-1,0,1,0};
            u8 nx = (u8)((x + dx[worldSide] + 32) % 32);
            u8 ny = (u8)((y + dy[worldSide] + 16) % 16);
            u8 nrot = ai_map_rot(nx, ny);
            u8 entryFlags = (u8)(0xC0 | (((worldSide + 2 - nrot) & 3)));
            u8 entryWp;
            if (ai_find_entry(pbin, ai_map_tile(nx, ny), entryFlags, &entryWp)) { x = nx; y = ny; wp = entryWp; continue; }
            {
                u8 revFlags = (u8)(0xC0 | (flags & 3));
                if (ai_find_entry(pbin, tile, revFlags, &entryWp)) { wp = entryWp; continue; }
            }
            return -1;
        }
        {
            u16 controlFlags = (flags & 0x80) ? 0 : flags;
            bool consumes = (controlFlags & 0xBF3F) != 0;
            int off = ai_branch_offset(controlFlags, beh);
            int nextWp;
            if (off == 0x7FFF) off = 1;
            nextWp = (int)wp + off;
            if (nextWp < 0 || nextWp >= count) return -1;
            if (consumes) {
                u8 pos = (u8)(spos + 1);
                u8 s;
                spos = pos;
                s = DSB((u16)(DS_opp_scripts + 10 * opp + pos));
                if (s == 0) return (int)(y * 32 + x);
                beh = s;
            }
            wp = (u8)nextWp;
        }
    }
    return -1;
}

static int ai_probe_routes(FarPtr pbin, int finish_cell, u8 start_x, u8 start_y, u8 start_wp,
                            AiRouteResult *out, int max_out)
{
    static AiRouteState q[AI_ROUTE_MAX_STATES];
    static AiRouteResult res[AI_ROUTE_MAX_RESULTS];
    static u16 seen[512 * 128];
    int qh = 0, qt = 0, nres = 0, states = 0;
    static const u8 modes[3] = { 2, 3, 4 };

    if (finish_cell < 0 || max_out <= 0) return 0;
    memset(seen, 0, sizeof(seen));

    for (int m = 0; m < 3; m++) {
        AiRouteState *s = &q[qt++];
        u16 key = (u16)((start_y * 32 + start_x) * 128 + (start_wp & 0x7F));
        u16 bit = (u16)(1u << ((modes[m] - 2) * 3 + (modes[m] - 2)));
        memset(s, 0, sizeof *s);
        s->x = start_x; s->y = start_y; s->wp = start_wp; s->sp = 0; s->beh = modes[m]; s->initial = modes[m];
        if (seen[key] & bit) { qt--; continue; }
        seen[key] |= bit;
    }

    while (qh < qt && states < AI_ROUTE_MAX_STATES) {
        AiRouteState cur = q[qh++];
        states++;
        if (cur.steps > AI_ROUTE_STEP_LIMIT) continue;
        {
            u8 tile = ai_map_tile(cur.x, cur.y);
            u16 flags; u8 count;
            if ((int)(cur.y * 32 + cur.x) == finish_cell) {
                if (nres < AI_ROUTE_MAX_RESULTS) {
                    AiRouteResult *r = &res[nres++]; r->initial = cur.initial; r->nbranch = cur.nbranch; r->steps = cur.steps; r->fx = cur.x; r->fy = cur.y;
                    for (u8 i = 0; i < cur.nbranch; i++) r->branch[i] = cur.branch[i];
                }
                continue;
            }
            if (!ai_lane_wp(pbin, tile, cur.wp, &flags, &count)) continue;
            if ((flags & 0xC0) == 0x80) { /* exit */
                u8 rot = ai_map_rot(cur.x, cur.y);
                u8 worldSide = (u8)((rot + (flags & 3)) & 3);
                int dx[4] = {0,1,0,-1}, dy[4] = {-1,0,1,0};
                u8 nx = (u8)((cur.x + dx[worldSide] + 32) % 32);
                u8 ny = (u8)((cur.y + dy[worldSide] + 16) % 16);
                u8 nrot = ai_map_rot(nx, ny);
                u8 entryFlags = (u8)(0xC0 | (((worldSide + 2 - nrot) & 3)));
                u8 entryWp;
                if (ai_find_entry(pbin, ai_map_tile(nx, ny), entryFlags, &entryWp)) {
                    if (qt < AI_ROUTE_MAX_STATES) {
                        u16 key = (u16)((ny * 32 + nx) * 128 + (entryWp & 0x7F));
                        u16 bit = (u16)(1u << ((cur.beh - 2) * 3 + (cur.initial - 2)));
                        if (!(seen[key] & bit)) { seen[key] |= bit;
                            AiRouteState *ns = &q[qt++]; *ns = cur; ns->x = nx; ns->y = ny; ns->wp = entryWp; ns->steps++; }
                    }
                } else {
                    u8 revFlags = (u8)(0xC0 | (flags & 3));
                    if (ai_find_entry(pbin, tile, revFlags, &entryWp) && qt < AI_ROUTE_MAX_STATES) {
                        u16 key = (u16)((cur.y * 32 + cur.x) * 128 + (entryWp & 0x7F));
                        u16 bit = (u16)(1u << ((cur.beh - 2) * 3 + (cur.initial - 2)));
                        if (!(seen[key] & bit)) { seen[key] |= bit;
                            AiRouteState *ns = &q[qt++]; *ns = cur; ns->wp = entryWp; ns->steps++; }
                    }
                }
                continue;
            }
            {
                u16 controlFlags = (flags & 0x80) ? 0 : flags;
                bool consumes = (controlFlags & 0xBF3F) != 0;
                int off = ai_branch_offset(controlFlags, cur.beh);
                int nextWp;
                if (off == 0x7FFF) continue;
                nextWp = (int)cur.wp + off;
                if (nextWp < 0 || nextWp >= count) continue;
                if (consumes) {
                    if (cur.nbranch >= AI_ROUTE_MAX_BRANCHES) continue;
                    for (int m = 0; m < 3; m++) {
                        if (qt >= AI_ROUTE_MAX_STATES) break;
                        {
                            u16 key = (u16)((cur.y * 32 + cur.x) * 128 + ((u8)nextWp & 0x7F));
                            u16 bit = (u16)(1u << ((modes[m] - 2) * 3 + (cur.initial - 2)));
                            if (seen[key] & bit) continue;
                            seen[key] |= bit;
                        }
                        {
                            AiRouteState *ns = &q[qt++]; *ns = cur;
                            ns->wp = (u8)nextWp; ns->sp++; ns->beh = modes[m];
                            ns->branch[ns->nbranch++] = modes[m];
                            ns->steps++;
                        }
                    }
                } else if (qt < AI_ROUTE_MAX_STATES) {
                    u16 key = (u16)((cur.y * 32 + cur.x) * 128 + ((u8)nextWp & 0x7F));
                    u16 bit = (u16)(1u << ((cur.beh - 2) * 3 + (cur.initial - 2)));
                    if (!(seen[key] & bit)) { seen[key] |= bit;
                        AiRouteState *ns = &q[qt++]; *ns = cur; ns->wp = (u8)nextWp; ns->steps++; }
                }
            }
        }
    }

    if (nres > max_out) nres = max_out;
    for (int i = 0; i < nres; i++) out[i] = res[i];
    return nres;
}

static void ai_apply_route(u8 opp, u16 bx, const AiRouteResult *r)
{
    ai_write_script(opp, r->initial, r->branch, r->nbranch);
    /* Keep entity behavior nibble in sync with script initial mode (same as course-editor). */
    DSW((u16)(DS_obj_flags + bx)) = (u16)((DSW((u16)(DS_obj_flags + bx)) & 0xF0FF) | ((u16)(r->initial & 0x0F) << 8));
}

static void ai_opponent_routing_generate(void)
{
    FarPtr pbin;
    int finish_cell;
    u8 opp_id[2], opp_x[2], opp_y[2], opp_wp[2];
    u16 opp_bx[2];
    int nopp = 0;

    if (portcfg.ai_opponent_routing == 0) return; /* legacy */
    if (DSB(DS_menu_preview) != 0) return;
    pbin = ds_far(DS_lanes_set);
    if (pbin.seg == 0 && pbin.off == 0) return;

    for (u16 slot = 0; slot < 160 && nopp < 2; slot++) {
        u16 bx = (u16)(slot * 2);
        u16 flags = DSW((u16)(DS_obj_flags + bx));
        u16 model = flags & 0x3F;
        u16 cell, wp;
        if (model != 2 && model != 3) continue;
        cell = DSW((u16)(DS_obj_cell + bx));
        wp = DSW((u16)(DS_obj_waypoint + bx)) & 0x3F;
        opp_id[nopp] = (u8)(model - 2);
        opp_bx[nopp] = bx;
        opp_x[nopp] = (u8)(cell & 0x1F);
        opp_y[nopp] = (u8)((cell >> 5) & 0x0F);
        opp_wp[nopp] = (u8)wp;
        nopp++;
    }
    if (nopp == 0) return;

    finish_cell = ai_resolve_finish_cell(pbin, opp_id, opp_x, opp_y, opp_wp, nopp);
    if (finish_cell < 0) return; /* unresolved: keep original scripts */

    {
        static AiRouteResult routes[2][AI_ROUTE_MAX_RESULTS];
        int nres[2] = { 0, 0 };
        for (int i = 0; i < nopp; i++)
            nres[i] = ai_probe_routes(pbin, finish_cell, opp_x[i], opp_y[i], opp_wp[i], routes[i], AI_ROUTE_MAX_RESULTS);

        if (portcfg.ai_opponent_routing == 2) {
            /* Modern: Random - each opponent picks an independent random valid route. */
            for (int i = 0; i < nopp; i++) {
                if (nres[i] <= 0) continue;
                u32 r = (u32)td3_random() ^ (u32)SDL_GetTicks();
                ai_apply_route(opp_id[i], opp_bx[i], &routes[i][r % (u32)nres[i]]);
            }
        } else {
            /* Modern: Deterministic - stable routes; opponents take separate routes when possible.
             * Prefer earliest physical divergence, then routes matching original scripts, then lower steps. */
            int pick[2] = { 0, 0 };
            if (nopp == 2 && nres[0] > 0 && nres[1] > 0) {
                /* Check which probed routes match the original opponent script exactly. */
                bool origMatch[2][AI_ROUTE_MAX_RESULTS];
                for (int i = 0; i < 2; i++) {
                    u8 oinit = DSB((u16)(DS_opp_scripts + 10 * opp_id[i]));
                    u8 obr[9], onb = 0;
                    for (int k = 0; k < 9; k++) { u8 b = DSB((u16)(DS_opp_scripts + 10 * opp_id[i] + 1 + k)); if (b == 0) break; obr[onb++] = b; }
                    for (int r = 0; r < nres[i]; r++) {
                        bool m = (routes[i][r].initial == oinit && routes[i][r].nbranch == onb);
                        for (u8 k = 0; m && k < onb; k++) if (routes[i][r].branch[k] != obr[k]) m = false;
                        origMatch[i][r] = m;
                    }
                }

                int bestDiv = 1000000, bestI = 0, bestJ = 0, bestOrig = -1;
                bool found = false;
                for (int i = 0; i < nres[0]; i++) {
                    for (int j = 0; j < nres[1]; j++) {
                        const AiRouteResult *ra = &routes[0][i], *rb = &routes[1][j];
                        u8 ax = opp_x[0], ay = opp_y[0], awp = opp_wp[0], abeh = ra->initial, abi = 0;
                        u8 bx2 = opp_x[1], by = opp_y[1], bwp = opp_wp[1], bbeh = rb->initial, bbi = 0;
                        int div = 1000000;
                        for (int step = 0; step < 4096; step++) {
                            if (ax != bx2 || ay != by || awp != bwp) { div = step; break; }
                            {
                                u8 ta = ai_map_tile(ax, ay), tb = ai_map_tile(bx2, by);
                                u16 fa, fb; u8 ca, cb;
                                bool oka = ai_lane_wp(pbin, ta, awp, &fa, &ca);
                                bool okb = ai_lane_wp(pbin, tb, bwp, &fb, &cb);
                                if (!oka || !okb) { if (!(oka && okb)) div = step; break; }
                                if ((fa & 0xC0) == 0x80 || (fb & 0xC0) == 0x80) {
                                    if ((fa & 0xC0) == 0x80) {
                                        u8 rot = ai_map_rot(ax, ay), ws = (u8)((rot + (fa & 3)) & 3);
                                        int ddx[4]={0,1,0,-1}, ddy[4]={-1,0,1,0};
                                        u8 nx=(u8)((ax+ddx[ws]+32)%32), ny2=(u8)((ay+ddy[ws]+16)%16);
                                        u8 nrot=ai_map_rot(nx,ny2), ef=(u8)(0xC0|(((ws+2-nrot)&3))); u8 ewp;
                                        if (ai_find_entry(pbin, ai_map_tile(nx,ny2), ef, &ewp)) { ax=nx; ay=ny2; awp=ewp; }
                                        else { u8 rf=(u8)(0xC0|(fa&3)); if (ai_find_entry(pbin, ta, rf, &ewp)) awp=ewp; else { div=step; break; } }
                                    }
                                    if ((fb & 0xC0) == 0x80) {
                                        u8 rot = ai_map_rot(bx2, by), ws = (u8)((rot + (fb & 3)) & 3);
                                        int ddx[4]={0,1,0,-1}, ddy[4]={-1,0,1,0};
                                        u8 nx=(u8)((bx2+ddx[ws]+32)%32), ny2=(u8)((by+ddy[ws]+16)%16);
                                        u8 nrot=ai_map_rot(nx,ny2), ef=(u8)(0xC0|(((ws+2-nrot)&3))); u8 ewp;
                                        if (ai_find_entry(pbin, ai_map_tile(nx,ny2), ef, &ewp)) { bx2=nx; by=ny2; bwp=ewp; }
                                        else { u8 rf=(u8)(0xC0|(fb&3)); if (ai_find_entry(pbin, tb, rf, &ewp)) bwp=ewp; else { div=step; break; } }
                                    }
                                    continue;
                                }
                                {
                                    u16 cfa = (fa & 0x80) ? 0 : fa, cfb = (fb & 0x80) ? 0 : fb;
                                    bool cona = (cfa & 0xBF3F) != 0, conb = (cfb & 0xBF3F) != 0;
                                    int oa = ai_branch_offset(cfa, abeh), ob = ai_branch_offset(cfb, bbeh);
                                    if (oa == 0x7FFF || ob == 0x7FFF) { div = step; break; }
                                    int na = (int)awp + oa, nb2 = (int)bwp + ob;
                                    if (na < 0 || na >= ca || nb2 < 0 || nb2 >= cb) { div = step; break; }
                                    if (cona) { if (abi < ra->nbranch) abeh = ra->branch[abi++]; }
                                    if (conb) { if (bbi < rb->nbranch) bbeh = rb->branch[bbi++]; }
                                    awp = (u8)na; bwp = (u8)nb2;
                                }
                            }
                        }
                        if (div < 1000000) {
                            int origScore = (origMatch[0][i] ? 1 : 0) + (origMatch[1][j] ? 1 : 0);
                            int cost = (int)ra->steps + (int)rb->steps;
                            int bestCost = found ? ((int)routes[0][bestI].steps + (int)routes[1][bestJ].steps) : 0x7fffffff;
                            if (!found || div < bestDiv ||
                                (div == bestDiv && origScore > bestOrig) ||
                                (div == bestDiv && origScore == bestOrig && cost < bestCost)) {
                                bestDiv = div; bestI = i; bestJ = j; bestOrig = origScore; found = true;
                            }
                        }
                    }
                }
                if (found) { pick[0] = bestI; pick[1] = bestJ; }
            }
            for (int i = 0; i < nopp; i++) {
                if (nres[i] <= 0) continue;
                if (pick[i] >= nres[i]) pick[i] = 0;
                ai_apply_route(opp_id[i], opp_bx[i], &routes[i][pick[i]]);
            }
        }
    }
}


/* 0792:0fce leg_map_load — game_flow.md §4.14 (<scene><'A'+leg>.DAT into DS:9592, opponents off, colour pairs) */
void leg_map_load(void)
{
    if (DSB(DS_menu_preview) == 0) {
        DSB(DS_path_scene + 9) = (u8)(DSB(DS_leg_index) + 'A');
        flow_strcpy(DS_path_scene + 10, 0x11E4);               /* ".DAT" */
        file_load_near(DS_path_scene, DS_leg_file);
        if (portcfg.weight > 1)
            DSW(DS_leg_gravity) = (u16)(DSW(DS_leg_gravity) * (u16)portcfg.weight);  /* ENH: --weight */
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

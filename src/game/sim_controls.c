/* Simulation module: held controls, gearbox, key dispatch and every key handler, life lost (segment 0e12,
 * simulation.md §4.4, §4.5, §4.7, §4.10).
 *
 * The key handler tables stay in mem[]: 64 near pointers at 0e12:0000 (codes 40h-7Fh) and 10 at DS:B6EF
 * (F1-F10). key_dispatch reads the stored offset and calls the registered C function through
 * codeptr_lookup_near(0x0E12, off). Register-argument routines take the register as a parameter. */
#include "game/game.h"
#include <math.h>

static void controls_poll(void);
static u8   mouse_controls(u8 cl);
static void steer_throttle(u8 cl);
static void shift_keys(u8 cl);
static void gear_up(void);
static void gear_down(void);
static void gear_up_gate(void);
static void gear_down_gate(void);

/* ---- far entry points ---- */

/* 0e12:0080 controls_poll_far — simulation.md §4.4 (call 0751; retf) */
void controls_poll_far(void)
{
    controls_poll();
}

/* 0e12:0084 key_dispatch — simulation.md §4.7 */
void key_dispatch(void)
{
    u8 k;
    DSW(DS_page_cur) = 0;
    gfx_set_draw_page(0);
    k = DSB(DS_key_code);
    DSB(DS_key_code) = 0;
    if (DSB(DS_debug_keys) != 0 && k == 0x88) { DSW(DS_race_state) = 3; return; } /* ENH debug: F8 ends leg */
    if (k >= 0x40 && k < 0x80) {
        u16 off = SEGW(0x0E12, 0x0000 + 2 * (k - 0x40));         /* table CS:0000 */
        codeptr_lookup_near(0x0E12, off)();
        return;
    }
    if (k >= 0x81 && k <= 0x8A && DSW(DS_race_state) < 2) {
        u16 off = DSW(DS_key_ext_handlers + 2 * (k - 0x81));     /* DS:B6EF */
        codeptr_lookup_near(0x0E12, off)();
        return;
    }
    controls_poll();                                            /* 0, < 40h, 80h, > 8Ah, or F-keys in race_state >= 2 */
}

/* ---- key handlers (near, reached only through the tables) ---- */

/* 0e12:00d5 key_none — simulation.md §4.7 (unassigned keys: the controls poll) */
static void key_none(void)
{
    controls_poll();
}

/* 0e12:00d9 key_mouse_toggle — simulation.md §4.7 (F7) */
static void key_mouse_toggle(void)
{
    if (DSB(DS_menu_preview) != 0 || DSW(DS_race_state) != 1 || DSB(DS_ext_view) != 0) return;
    DSB(DS_mouse_on) ^= 1;
    if (DSB(DS_mouse_on)) {
        mouse_set_pos(0xA0, 0x64);
        message_box(0x32);                                    /* "Mouse steering sensitivity desired (1-9)" */
        DSB(DS_mouse_div) = (u8)((u8)-(u8)(DSB(DS_msg_answer) - 0x39) + 1 + 1);
    } else {
        message_box(0x33);                                    /* "Mouse steering off" */
    }
}

/* 0e12:012d key_mirror — simulation.md §4.7 (R) */
static void key_mirror(void)
{
    DSB(DS_mirror_on) ^= 1;
}

/* 0e12:0133 key_wheel_centring — simulation.md §4.7 (C) */
static void key_wheel_centring(void)
{
    DSB(DS_wheel_centring) ^= 1;
    message_box((s16)(u8)(DSB(DS_wheel_centring) + 0x0E));
}

/* 0e12:0149 key_version — simulation.md §4.7 (V) */
static void key_version(void)
{
    message_box(0x20);                                        /* "TEST DRIVE III Version 3.0" */
}

/* 0e12:0269 key_knob_toggle — simulation.md §4.7 (D) */
static void key_knob_toggle(void)
{
    DSB(DS_knob_hidden) ^= 1;
}

/* 0e12:026f key_chase_view — simulation.md §4.7 (F5) */
static void key_chase_view(void)
{
    if (DSB(DS_replay_state) != 0) {
        if (DSB(DS_crashed)) life_lost(1);
        return;
    }
    DSW(DS_top_row_prev) = 0;
    DSB(DS_ext_view) ^= 1;
    if (DSB(DS_ext_view)) {
        sfx_play_ax(0);
        DSB(DS_world_rebuilt) = 1;
        DSB(DS_cam_yaw) = DSB(DS_cam_heading + 1);            /* high byte of DS:9498 */
        DSB(DS_cam_moved) = 1;
        DSB(DS_cam_roll_94AD) = 0;
        DSW(DS_cam_pitch) = 0;
        DSW(DS_cam_y_94A7) = DSW(DS_sprite_y);
        DSB(DS_cam_elev) = 0x40;
        DSW(0xB6E4) = DSW(DS_ground_height);                  /* saved ground state */
        DSW(0xB6E6) = DSW(DS_ground_pitch);
        DSW(DS_cam_dist) = DSW(DS_car_eye_height);
        DSB(0xB6E8) = DSB(DS_ground_roll);
        DSB(0xB6E9) = DSB(DS_surface_under_car);
    } else {
        DSW(DS_ground_height) = DSW(0xB6E4);
        DSW(DS_ground_pitch) = DSW(0xB6E6);
        DSB(DS_ground_roll) = DSB(0xB6E8);
        DSB(DS_surface_under_car) = DSB(0xB6E9);
    }
}

/* 0e12:0302 key_wipers — simulation.md §4.7 (W) */
static void key_wipers(void)
{
    if (DSB(DS_ext_view) == 0) DSB(DS_wipers_on) ^= 1;
}

/* 0e12:030f key_headlights — simulation.md §4.7 (H) */
static void key_headlights(void)
{
    if (DSB(DS_ext_view) == 0) DSB(DS_headlights) ^= 1;
}

/* 0e12:031c key_window_size — simulation.md §4.7 (F1) */
static void key_window_size(void)
{
    DSB(DS_half_window) ^= 1;
    DSW(DS_top_row_prev) = 0;
    message_box((s16)(u8)(DSB(DS_half_window) + 0x0A));      /* "Window size full" / "half" */
    view_setup();
    if (DSW(DS_view_rows) != 0x60) {                          /* redraw the borders of the half window */
        gfx_set_draw_page(0);
        gfx_set_colour(0);
        gfx_fill_rect(0, 0x27, 0x10, 0x6F);
        gfx_fill_rect(0x118, 0x13F, 0x10, 0x6F);
        gfx_fill_rect(0x28, 0x117, 0x50, 0x6F);
    }
    gfx_set_draw_page((s16)DSW(DS_page_cur));
    DSW(DS_last_cell) = 0xFFFF;
}

/* 0e12:03ab key_detail — simulation.md §4.7 (F2) */
static void key_detail(void)
{
    u8 d = (u8)(DSB(DS_detail) + 1);
    if (d >= 3) d = 0;
    DSB(DS_detail) = d;
    detail_apply();
    message_box((s16)(DSB(DS_detail) + 0x1B));               /* "Detail level low/medium/high" */
    DSW(DS_last_cell) = 0xFFFF;
}

/* 0e12:0534 key_instant_replay — simulation.md §4.7 (F10) */
static void key_instant_replay(void)
{
    if (DSB(DS_replay_state) != 0) {
        sfx_play_ax(0);
        DSB(DS_replay_request) = 1;
        message_box(0x19);                                    /* "Replay Requested" */
        return;
    }
    replay_start();
    DSB(DS_cam_moved) = 0;
    sfx_play_ax(0);
}

/* 0e12:056a key_return_to_road — simulation.md §4.7 (F6) */
static void key_return_to_road(void)
{
    if (DSB(DS_ext_view) != 0 || DSB(DS_f6_allowed) == 0) return;
    DSB(DS_return_to_road) = 1;
    DSB(DS_need_place) = 1;
    DSW(DS_prev_race_state) = 2;
    DSW(DS_offroad_frames) = 0;
    message_box(7);                                           /* "Returning to road..." */
}

/* 0e12:059c key_replay_pause — simulation.md §4.7 (F9) */
static void key_replay_pause(void)
{
    if (DSB(DS_replay_state) == 0) return;
    text_set_colours(0x0F, 8);
    DSB(DS_replay_paused) ^= 1;
    print_records(DSB(DS_replay_paused) ? 0x1ECC /* "Paused" */ : 0x1ED5 /* blanks */, 0);
}

/* 0e12:05cd key_steer_sensitivity — simulation.md §4.7 (F3; the answer is handled in message_box) */
static void key_steer_sensitivity(void)
{
    DSB(0x1C18) = (u8)(DSB(DS_steering_response) + 0x30);   /* digit patched into message 31h */
    message_box(0x31);
}

/* 0e12:05e3 key_debug_rain — simulation.md §4.7 (T; dead: DS:B6EA is never set) */
static void key_debug_rain(void)
{
    u16 v;
    if (DSB(DS_debug_keys) == 0) return;
    v = DSW(DS_rain_level);
    DSB(DS_sky_alt) = 1;
    v++;
    if (v > 8) { v = 0; DSB(DS_sky_alt) = 0; }
    DSW(DS_rain_level) = v;
    build_colour_remap();
}

/* 0e12:0606 key_debug_snow — simulation.md §4.7 (S; dead: DS:B6EA is never set) */
static void key_debug_snow(void)
{
    u16 v;
    if (DSB(DS_debug_keys) == 0) return;
    v = DSW(DS_snow_level);
    DSB(DS_sky_alt) = 1;
    v++;
    if (v > 8) { v = 0; DSB(DS_sky_alt) = 0; }
    DSW(DS_snow_level) = v;
    build_colour_remap();                                     /* (the original tests DS:95C7 first; both paths call it) */
}

/* 0e12:0630 key_radio — simulation.md §4.7 (M) */
static void key_radio(void)
{
    u8 st = (u8)(DSB(DS_radio_station) + 1);
    s16 msg;
    if (st >= 3) st = 0;
    DSB(DS_radio_station) = st;
    if (music_for_state(st) != 0) msg = 0x22;                 /* "Change radio station" */
    else if (DSB(DS_music_off) == 1) msg = 0x28;              /* "Music must be on to hear radio" */
    else msg = 0x27;                                          /* "Sounds must be off to hear radio" */
    message_box(msg);
}

/* 0e12:0667 key_debug_night — simulation.md §4.7 (N; dead: DS:B6EA is never set) */
static void key_debug_night(void)
{
    if (DSB(DS_debug_keys) == 0) return;
    DSB(DS_colour_mode) ^= 1;
    if (DSB(DS_colour_mode) == 0) {
        DSB(DS_sky_colours + 0) = 9;    DSB(DS_sky_colours + 1) = 8;
        DSB(DS_sky_colours + 2) = 0x72; DSB(DS_sky_colours + 3) = 0x13;
    } else {
        DSB(DS_sky_colours + 0) = 0x0B; DSB(DS_sky_colours + 1) = 7;
        DSB(DS_sky_colours + 2) = 0x79; DSB(DS_sky_colours + 3) = 0x19;
    }
    build_colour_remap();
}

/* 0e12:06b6 key_debug_flip — simulation.md §4.7 (O; only while frozen, which never happens) */
static void key_debug_flip(void)
{
    if (DSB(DS_frozen) != 0) DSB(DS_debug_flip) ^= 1;
}

/* ---- gearbox (simulation.md §4.5) ---- */

/* 0e12:0156 gear_up — simulation.md §4.5 */
static void gear_up(void)
{
    if (DSB(DS_ext_view) != 0) return;
    DSB(DS_clock_running) = 1;
    if (DSB(DS_gear) >= DSB(DS_car_top_gear)) return;
    DSB(DS_gear)++;
    if (DSB(DS_gear) >= 2) {
        u16 dx = DSW(0x1232);
        if (DSB(DS_gear) == 2) dx = (u16)(dx << 1);          /* 1st gear gets twice the preset */
        DSB(DS_throttle) = (u8)dx;
    }
    gear_lever_target();
}

/* 0e12:0188 gear_up_gate — simulation.md §4.5 (key A, Enter+Up) */
static void gear_up_gate(void)
{
    u8 cl = DSB(DS_car_gears_per_range), ch, ah, al;
    if (cl == 1) { gear_up(); return; }
    ch = DSB(DS_gear);
    ah = DSB(DS_car_top_gear);
    if (ch < 2) { gear_up(); return; }
    al = (u8)(cl + 2);
    if (ch >= al) {
        al = (u8)(al + cl);
        if (al > ah) return;
        if (ch >= al) return;
    }
    DSB(DS_gear) = al;
    DSB(DS_throttle) = DSB(0x1232);
    gear_lever_target();
}

/* 0e12:01c5 gear_lever_target — simulation.md §4.5 */
void gear_lever_target(void)
{
    DSB(DS_lever_hold) = 0;
    DSB(DS_lever_target) = DSB(DS_car_lever_step + DSB(DS_gear));
}

/* 0e12:01d8 gear_down — simulation.md §4.5 */
static void gear_down(void)
{
    if (DSB(DS_ext_view) != 0) return;
    DSB(DS_clock_running) = 1;
    if (DSB(DS_gear) == 0) return;
    DSB(DS_gear)--;
    if (DSB(DS_gear) == 0 && !(DSW(DS_speed_long + 2) & 0x8000) && DSW(DS_car_speed) > 0x14) {
        DSW(DS_damage) |= 0x100;                              /* reverse gear broken */
        sfx_play_ax(0x0B);
    }
    if (DSB(DS_gear) != 1) {
        u16 dx = DSW(0x1234);
        if (DSB(DS_gear) == 0) dx = (u16)(dx << 1);          /* reverse gets twice the preset */
        DSB(DS_throttle) = (u8)dx;
    }
    gear_lever_target();
}

/* 0e12:0229 gear_down_gate — simulation.md §4.5 (key Z, Enter+Down) */
static void gear_down_gate(void)
{
    u8 cl = DSB(DS_car_gears_per_range), ch, al, ah;
    if (cl == 1) { gear_down(); return; }
    ch = DSB(DS_gear);
    if (ch <= 2) { gear_down(); return; }
    al = (u8)(1 + cl);
    if (ch <= al) {                                           /* low range -> N */
        DSB(DS_gear) = 2;
        gear_down();
        return;
    }
    ah = (u8)(al + cl);
    if (ch > ah) al = (u8)(al + cl);
    DSB(DS_gear) = al;
    DSB(DS_throttle) = DSB(0x1234);
    gear_lever_target();
}

/* 0e12:095a auto_shift — simulation.md §4.5 (the CERV in-range up-shift falls through to the down-shift
 * checks at the top of a range; checked against the disassembly) */
void auto_shift(void)
{
    u16 rpm = DSW(DS_engine_rpm);
    u8 bl = DSB(DS_gear), cl, ch;

    if (rpm >= DSW(0x120A) && bl >= 2) {
        if (DSB(DS_auto_gearbox)) { gear_up(); return; }
        cl = DSB(DS_car_gears_per_range);
        if (cl == 1) return;
        ch = (u8)(1 + cl);
        if (ch != bl) {
            ch = (u8)(ch + cl);
            if (ch != bl) {
                ch = (u8)(ch + cl);
                if (ch > bl) { gear_up(); return; }
            }
        }
    }
    if (DSB(DS_throttle) != 0) return;
    if (rpm >= DSW(0x120C)) return;
    if (bl == 1) return;
    if (bl < 1) {                                             /* R -> N when coasting (automatic only) */
        if (DSB(DS_auto_gearbox)) gear_up();
        return;
    }
    if (DSB(DS_auto_gearbox) == 0) {
        cl = DSB(DS_car_gears_per_range);
        if (cl == 1) return;
        ch = 2;
        if (ch == bl) return;
        ch = (u8)(ch + cl);
        if (ch == bl) return;
        ch = (u8)(ch + cl);
        if (ch == bl) return;
    }
    gear_down();
}

/* ---- held controls (simulation.md §4.4) ---- */

/* 0e12:0751 controls_poll — simulation.md §4.4 */
static void controls_poll(void)
{
    u8 cl, ch;
    if (DSB(DS_mouse_on)) mouse_get(&DSS(DS_mouse_x), &DSS(DS_mouse_y), &DSS(DS_mouse_buttons));
    DSB(DS_brake) = 0;
    cl = DSB(DS_kbd_bits);
    ch = DSB(DS_joy_bits);
    cl |= ch;
    ch &= 2;
    ch = (u8)(ch << 4);
    cl |= ch;
    cl &= 0x3F;
    if (DSB(DS_mouse_on) && DSB(DS_ext_view) == 0) cl = mouse_controls(cl);
    if (DSB(DS_ext_view) == 0) {
        ch = DSB(DS_prev_bits);
        DSB(DS_prev_bits) = cl;
        if ((ch & 3) == 1 && !(cl & 1)) DSB(DS_throttle_released) = 1;   /* Up let go */
    }
    if (cl == 0) {
        DSB(DS_steer_hold) = cl;
        if (DSB(DS_mouse_on) == 0 && DSW(DS_joystick_on) != 0 && DSB(DS_joy_analog) != 0)
            DSB(DS_steer_wheel) = 0x10;
        return;
    }
    if (DSB(0xB6CD) != 0) {                                   /* debug camera height (B6CD never set) */
        cl &= 3;
        if (cl == 0) return;
        if (cl & 1) DSW(0xB6CE) += 8;
        else        DSW(0xB6CE) -= 8;
        return;
    }
    if (DSB(DS_ext_view) == 0) {
        if (cl & 0x20) {                                      /* Space: hard brake */
            DSB(DS_brake) = 4;
            ch = (u8)(DSB(DS_throttle) - 2);
            if (ch & 0x80) ch = 0;
            DSB(DS_throttle) = ch;
        }
        if ((cl & 0x10) && DSB(DS_lever_target) == DSB(DS_lever_step)) shift_keys(cl);
        steer_throttle(cl);
        return;
    }
    /* replay / chase camera: cursor keys move the camera */
    DSB(DS_steer_hold) = 0;
    cl &= 0x0F;
    if (cl == 0) return;
    DSB(DS_cam_moved) = 1;
    DSB(DS_cam_elev) &= 0xF8;
    if (cl < 3) {
        u8 al = DSB(DS_cam_elev);
        if (cl & 1) {
            if (al > 0x10) DSB(DS_cam_elev) = (u8)(al - 8);
        } else {
            al = (u8)(al + 8);
            if (al != 0) DSB(DS_cam_elev) = al;
        }
    }
    if (cl == 4) DSB(DS_cam_yaw) += 4;
    if (cl == 8) DSB(DS_cam_yaw) -= 4;
    {
        s16 ax = DSS(DS_cam_pitch);
        if (cl == 9 && ax < 0x30) { ax = (s16)(ax + 8); DSS(DS_cam_pitch) = ax; }
        if (cl == 0x0A && ax > -0x18) { ax = (s16)(ax - 8); DSS(DS_cam_pitch) = ax; }
    }
    if (DSB(DS_frozen) == 0) {
        if (cl == 5 && DSW(DS_cam_dist) < 0x140) DSW(DS_cam_dist) += 4;
        if (cl == 6 && DSW(DS_cam_dist) > 0x2E) DSW(DS_cam_dist) -= 4;
    } else {
        if (cl == 5) DSW(DS_cam_y_94A7) += 4;
        if (cl == 6) DSW(DS_cam_y_94A7) -= 4;
    }
}

/* 0e12:08d9 mouse_controls — simulation.md §4.4 (CL in/out) */
static u8 mouse_controls(u8 cl)
{
    u8 neg = 0, al;
    u16 ax = (u16)(DSW(DS_mouse_x) - 0xA0);
    if (ax & 0x8000) { neg = 1; ax = (u16)-ax; }
    al = (u8)div16_8(ax, DSB(DS_mouse_div));
    if (neg) al = (u8)-al;
    al = (u8)(al + 0x10);
    if (al & 0x80) al = 0;
    if (al > 0x1F) al = 0x1F;
    DSB(DS_steer_wheel) = al;

    cl &= 0xF0;
    ax = DSW(DS_mouse_buttons);
    if (ax & 1) cl |= 2;                                      /* left = brake */
    else if (ax & 2) cl |= 1;                                 /* right = gas */
    if (cl & 3) return cl;

    neg = 0;
    ax = (u16)(DSW(DS_mouse_y) - 0x64);
    if (ax & 0x8000) { neg = 1; ax = (u16)-ax; }
    al = (u8)div16_8(ax, (u8)(DSB(DS_mouse_div) + 5));
    if (neg) al = (u8)-al;
    al = (u8)(al + 8);
    if (al & 0x80) al = 0;
    if (al > 0x10) al = 0x10;
    if (al <= 2) return (u8)(cl | 1);                         /* pushed forward = gas */
    if (al >= 0x0E) cl |= 2;
    return cl;
}

/* ENH: keyboard steering rewritten for the smooth view (ENHANCED.md "Steering"). The original steps the wheel
 * (0..20h, centre 10h) by 1..3 per control read, three reads a frame, and the turn follows those steps; in a view
 * interpolated between frames every step shows. Here the wheel is a continuous position (steer_pos, -1..1),
 * moved once per game frame by the frame's length in seconds: a key swings it towards its lock at a rate that
 * falls with speed and eases in over the first quarter second (a tap gives a small angle, a hold a hard turn),
 * turning back through the centre goes 2.5 times as fast, and without a key (both keys, or with wheel centring
 * on and the car moving) it returns to the centre with a 0.15 s time constant. DS_steer_wheel follows it (the
 * HUD, the mouse, the analog stick and the rest of the game read or set that). The mouse and the analog stick
 * set the wheel themselves; car_physics then takes it as it is. */
double steer_pos;                                     /* the continuous wheel, -1 (lock 0) .. 1 (lock 20h) */
static double steer_held;                             /* seconds the current steering key has been held */

#define STEER_TICK_S ((double)PIT_DIV_GAME / PIT_HZ)

static bool steer_analog(void)
{
    return DSB(DS_mouse_on) != 0 || (DSW(DS_joystick_on) != 0 && DSB(DS_joy_analog) != 0);
}

/* once per game frame, before car_physics' steering (sim.h) */
void steer_frame(void)
{
    u8 w = DSB(DS_steer_wheel);
    if (steer_analog() || (u8)lround(0x10 + 0x10 * steer_pos) != w) {   /* set elsewhere: take its value */
        steer_pos = (w - 0x10) / 16.0;
        if (steer_pos > 1) steer_pos = 1;
        if (steer_pos < -1) steer_pos = -1;
        if (steer_analog()) return;
    }
    double dt = DSB(DS_frame_ticks) * STEER_TICK_S;
    if (dt < 0.01) dt = 0.01;
    if (dt > 0.25) dt = 0.25;
    double spd = DSW(DS_car_speed) / 96.0;
    if (spd > 1) spd = 1;
    double rate = 1.0 / (0.28 + 0.62 * spd);          /* centre to lock: 0.28 s standing, 0.9 s flat out */
    u8 k = (u8)(DSB(DS_prev_bits) & 0x0C);
    if (k == 4 || k == 8) {
        double dir = k == 8 ? 1.0 : -1.0;
        steer_held += dt;
        double ease = steer_held >= 0.25 ? 1.0 : 0.45 + 0.55 * steer_held / 0.25;
        double r = rate * ease;
        if (steer_pos * dir < 0) r = rate * 2.5;      /* turning back through the centre */
        steer_pos += dir * r * dt;
        if (steer_pos > 1) steer_pos = 1;
        if (steer_pos < -1) steer_pos = -1;
    } else {
        steer_held = 0;
        if (k == 0x0C || (DSB(DS_wheel_centring) != 0 && DSSL(DS_speed_long) != 0)) {
            steer_pos *= exp(-dt / 0.15);
            if (fabs(steer_pos) < 0.01) steer_pos = 0;
        }
    }
    DSB(DS_steer_wheel) = (u8)lround(0x10 + 0x10 * steer_pos);
}

/* 0e12:09d6 steer_throttle — simulation.md §4.4 (CL = bits) */
static void steer_throttle(u8 cl)
{
    if (cl & 3) {
        if (cl & 1) {
            if (DSB(DS_throttle) < 0x1E) {
                DSB(DS_throttle) += 2;
                if (DSB(DS_auto_gearbox) && DSB(DS_gear) == 1) gear_up();   /* automatic: N -> 1st on gas */
            }
        } else {
            u8 ch;
            DSB(DS_brake) |= 1;
            ch = (u8)(DSB(DS_throttle) - 2);
            if (ch & 0x80) ch = 0;
            DSB(DS_throttle) = ch;
        }
    }
    if (!(cl & 0x0C)) {
        DSB(DS_steer_hold) = 0;
        if (DSW(DS_joystick_on) != 0 && DSB(DS_joy_analog) != 0 && DSB(DS_mouse_on) == 0)
            DSB(DS_steer_wheel) = 0x10;
        return;
    }
    DSB(DS_steer_hold) = 1;                           /* ENH: the wheel itself moves in steer_frame */
    if (DSW(DS_joystick_on) == 0 || DSB(DS_joy_analog) == 0) return;
    {                                                         /* analog stick overrides */
        u16 bx = DSW(DS_joy_xcentre), ax = (u16)(DSW(DS_joy_x) - bx);
        if (!(ax & 0x8000)) {
            ax = div32_16((u32)ax * 0x0F, (u16)(DSW(DS_joy_xmax) - bx), NULL);
            ax = (u16)(ax + 0x10);
        } else {
            ax = (u16)-ax;
            ax = div32_16((u32)ax * 0x0F, (u16)(bx - DSW(DS_joy_xmin)), NULL);
            ax = (u16)((u16)-ax + 0x0F);
        }
        DSB(DS_steer_wheel) = (u8)ax;
    }
}

/* 0e12:0b02 shift_keys — simulation.md §4.4 (CL; Enter+Up / Enter+Down; CX preserved) */
static void shift_keys(u8 cl)
{
    if (cl & 1) gear_up_gate();
    else if (cl & 2) gear_down_gate();
}

/* ---- life lost (simulation.md §4.10) ---- */

/* 0e12:044e life_lost — simulation.md §4.10 */
void life_lost(s16 skip)
{
    u8 bl = DSB(DS_lives), bh;
    u16 msg = 0x25;                                           /* GAME OVER */
    bl--;
    if (bl != 0) {
        msg = 0x15;                                           /* "This is your last life..." */
        if (bl != 1) {
            msg = (u16)((msg & 0xFF00) | 0x16);               /* "You have nn lives left" (mov al,16h) */
            bh = 0x20;
            if (bl >= 0x0A) {
                bh = 0x30;
                do { bh++; bl = (u8)(bl - 0x0A); } while (bl >= 0x0A);
            }
            bl = (u8)(bl + 0x30);
            DSB(0x194B) = bh;
            DSB(0x194C) = bl;
        }
    }
    if (DSB(DS_external_panel_on) == 1) panel_erase((u16)skip);
    DSB(DS_kbd_key) = 0;
    message_box((s16)msg);
    if (skip == 0 && DSB(DS_replay_request) != 0) {
        if (DSB(DS_external_panel_on) == 1) { replay_panel_draw(); return; }
        DSB(DS_replay_request) = 0;
        DSB(DS_cam_moved) = 0;
        return;
    }
    DSB(DS_crash_count)++;
    DSB(DS_replay_state) = 0;
    DSB(DS_ext_view) = 0;
    if (--DSB(DS_lives) == 0) {
        DSW(DS_race_state) = 3;
        return;
    }
    DSW(DS_race_state) = 2;
    DSB(DS_need_place) = 1;
}

/* 0e12:04f1 panel_erase — simulation.md §4.10 (DX: 0 = narrower lower strip) */
void panel_erase(u16 dx)
{
    gfx_set_colour(8);
    gfx_fill_rect(0x38, 0x12F, 0x84, 0xB3);
    gfx_fill_rect(0x38, (u8)dx ? 0x12F : 0xC7, 0xB4, 0xC3);
}

/* ---- code pointers ---- */

/* PORT: registers every handler stored as a near pointer in 0e12:0000 and DS:B6EF (simulation.md §4.7). */
void sim_register_codeptrs(void)
{
    codeptr_register(FN_key_none, key_none);
    codeptr_register(FN_key_mouse_toggle, key_mouse_toggle);
    codeptr_register(FN_key_mirror, key_mirror);
    codeptr_register(FN_key_wheel_centring, key_wheel_centring);
    codeptr_register(FN_key_version, key_version);
    codeptr_register(FN_gear_up_gate, gear_up_gate);
    codeptr_register(FN_gear_down_gate, gear_down_gate);
    codeptr_register(FN_key_knob_toggle, key_knob_toggle);
    codeptr_register(FN_key_chase_view, key_chase_view);
    codeptr_register(FN_key_wipers, key_wipers);
    codeptr_register(FN_key_headlights, key_headlights);
    codeptr_register(FN_key_window_size, key_window_size);
    codeptr_register(FN_key_detail, key_detail);
    codeptr_register(FN_key_instant_replay, key_instant_replay);
    codeptr_register(FN_key_return_to_road, key_return_to_road);
    codeptr_register(FN_key_replay_pause, key_replay_pause);
    codeptr_register(FN_key_steer_sensitivity, key_steer_sensitivity);
    codeptr_register(FN_key_debug_rain, key_debug_rain);
    codeptr_register(FN_key_debug_snow, key_debug_snow);
    codeptr_register(FN_key_radio, key_radio);
    codeptr_register(FN_key_debug_night, key_debug_night);
    codeptr_register(FN_key_debug_flip, key_debug_flip);
}

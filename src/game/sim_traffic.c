/* Simulation module: traffic, opponents, vehicle collisions, crossing gate, police and the opponents'
 * leg times (segment 0e12, simulation.md §4.12, §4.13; render3d.md §4.14 for the same functions seen
 * from the drawing side).
 *
 * Object arrays are u16[160] at DS:A479.. indexed by the slot bx = 2*i, as in the original. The
 * scratch globals the original reuses (DS:9460/9462 polar outputs, DS:946A-946F placement bytes) are
 * read and written in mem[] exactly where the original does, because other routines see them. */
#include "game/game.h"

#include <math.h>

/* Object array element at slot bx (byte offset 2*i). */
#define OW(arr, bx) DSW((u16)((arr) + (bx)))
#define OB(arr, bx) DSB((u16)((arr) + (bx)))

static void   moving_objects_update(void);
static void   vehicle_follow_lane(u16 slot2_bx);
static FarPtr lane_list_for_cell(u16 cell_si);
static void   vehicle_collisions(void);
static void   vehicle_collision_boxes(u16 slot2_bx);
static void   opponent_time_estimate(u16 slot2_bx, u16 ofs_si);

static inline u8 rol8_2(u8 v) { return (u8)(v << 2 | v >> 6); }
static inline u16 swapb(u16 v) { return (u16)(v << 8 | v >> 8); }

/* 0e12:4d60 objects_update — simulation.md §4.12, render3d.md §4.14 */
void objects_update(void)
{
    parked_vehicles_emit();                     /* 4fc7 */
    moving_objects_update();                    /* 509b */
    vehicle_collisions();                       /* 5ad4 */
    lighthouse_rotate();                        /* 5df9 */
}

/* 0e12:509b moving_objects_update — simulation.md §4.12 (traffic_update), render3d.md §4.14 */
static void moving_objects_update(void)
{
    u16 ax, old, bx;
    u8 lo, hi;

    /* timers BCAB..BCAF (not BCAA), u8 saturating */
    ax = DSW(0xBCAB);                           /* opp_recover[1], opp_recover[2] (= opp_overtake[0]) */
    lo = (u8)ax; hi = (u8)(ax >> 8);
    if (lo) lo--;
    if (hi) hi--;
    DSW(0xBCAB) = (u16)(hi << 8 | lo);
    ax = DSW(0xBCAD);                           /* opp_overtake[1], opp_overtake[2] */
    lo = (u8)ax; hi = (u8)(ax >> 8);
    if (lo) lo--;
    if (hi) hi--;
    DSW(0xBCAD) = (u16)(hi << 8 | lo);
    if (DSB(DS_traffic_overtake)) DSB(DS_traffic_overtake)--;

    DSW(DS_vert_count) = DSW(DS_parked_vert_end);
    old = DSW(DS_face_count);
    DSW(DS_face_count) = DSW(DS_parked_face_end);

    bx = 1;
    if (DSB(DS_menu_preview) == 0) bx = DSW(DS_obj_a475);
    bx = (u16)(bx << 1);
    for (;;) {
        u16 dist, a, nf;
        FarPtr faces;

        bx = (u16)(bx - 2);
        if ((s16)bx < 0) break;                 /* js */

        DSB(DS_obj_full_transform) = 1;
        if (DSB(DS_ext_view) == (u8)bx) {       /* sic: compares ext_view with the low byte of 2i */
            DSB(DS_obj_full_transform) = 0;
            if (bx == 0) continue;
        }
        if (!(OW(DS_obj_flags, bx) & 0x2000)) continue;

        DSW(DS_obj_move_dx) = 0;
        DSW(DS_dz) = 0;                         /* BCC6 obj_move_dz */
        DSW(DS_dy) = 0;                         /* BCC8 obj_move_dy */
        if (DSB(DS_replay_state) == 0
            && (DSB(DS_menu_preview) != 0 || (DSB(DS_ext_view) == 0 && DSB(DS_clock_running) != 0))) {
            vehicle_follow_lane(bx);            /* 5466 */
            if (!(OW(DS_obj_flags, bx) & 0x2000)) continue;   /* NB: ranges not cleared */
            if (DSB(DS_menu_preview) == 0) {
                u16 vel = OW(DS_obj_vel, bx);
                s16 dx = (s8)(u8)vel;
                s16 cx = (s8)(u8)(vel >> 8);
                s16 ay = (s16)(u16)((u16)(s16)(s8)OB(DS_obj_pitch, bx) << 2);
                int move = 1;
                if (OW(DS_obj_flags, bx) & 0x1000) {
                    move = 0;
                    if (bx <= 4 && bx != 0 && DSB(DS_race_computer_cars) != 0) {
                        u16 i = (u16)(bx >> 1);
                        if (DSB(DS_opp_recover + i) != 0) move = 1;       /* stuck opponent may move */
                        else {
                            u8 c = (u8)(DSB(DS_opp_stuck + i) + 1);
                            if (c >= 0x1E) {
                                DSB(DS_opp_recover + i) = (u8)((u8)(i << 4) + 0x1E);
                                c = 0;
                            }
                            DSB(DS_opp_stuck + i) = c;
                        }
                    }
                }
                if (move) {
                    if (OW(DS_obj_flags, bx) & 0x8000) { dx >>= 1; cx >>= 1; ay >>= 1; }   /* sar */
                    DSW(DS_obj_move_dx) = (u16)(DSW(DS_obj_move_dx) + dx);
                    OW(DS_obj_x, bx) = (u16)((u16)(dx + OW(DS_obj_x, bx)) & 0x7FFF);
                    DSW(DS_dz) = (u16)(DSW(DS_dz) + cx);
                    OW(DS_obj_z, bx) = (u16)((u16)(cx + OW(DS_obj_z, bx)) & 0x3FFF);
                    OW(DS_obj_y8, bx) = (u16)(OW(DS_obj_y8, bx) + ay);
                    DSW(DS_dy) = (u16)(DSW(DS_dy) + (u16)(ay + (OW(DS_obj_y8, bx) & 7)));
                }
            }
        }

        /* visibility window around the car (949A/949C), Manhattan distance for the LOD */
        a = (u16)(OW(DS_obj_x, bx) - DSW(DS_cam_x));
        a = (u16)((s16)(u16)(a << 1) >> 1);
        a = (u16)(a + DSW(DS_view_dist));
        if (a > DSW(DS__2) /* B6E0 view_x2 */) goto clear_all;
        a = (u16)(a - DSW(DS_view_dist));
        if ((s16)a < 0) a = (u16)-a;
        dist = a;
        a = (u16)(OW(DS_obj_z, bx) - DSW(DS_cam_z));
        a = (u16)((s16)(u16)(a << 2) >> 2);
        a = (u16)(a + DSW(DS_view_dist));
        if (a > DSW(DS__2)) goto clear_all;
        a = (u16)(a - DSW(DS_view_dist));
        if ((s16)a < 0) a = (u16)-a;
        dist = (u16)(dist + a);

        DSW(DS_saved_vert_base) = OW(DS_obj_vert_base, bx);   /* BCCA */
        DSW(DS_count) = OW(DS_obj_vert_count, bx);            /* BCCC saved vertex count */
        nf = obj_emit_vehicle_moving(bx, dist, &faces);       /* 52a3 -> CX, ES:SI */
        if (DSB(DS_vert_overflow) != 0) goto clear_faces;
        if (DSW(DS_count) == OW(DS_obj_vert_count, bx)
            && DSW(DS_saved_vert_base) == OW(DS_obj_vert_base, bx)
            && DSW(DS_face_count) == OW(DS_obj_face_base, bx)) {
            DSW(DS_face_count) = (u16)(DSW(DS_face_count) + OW(DS_obj_face_count, bx));  /* reuse face records */
            continue;
        }
        DSW(DS_model_face_cut) = 0;
        model_emit_faces(faces, nf);                          /* 7487 */
        continue;

    clear_all:
        OW(DS_obj_vert_base, bx) = 0;
        OW(DS_obj_vert_count, bx) = 0;
    clear_faces:
        OW(DS_obj_face_base, bx) = 0;
        OW(DS_obj_face_count, bx) = 0;
    }
    if (old != DSW(DS_face_count)) DSB(DS_faces_resort) = 1;
}

/* 0e12:5aa4 lane_list_for_cell — simulation.md §4.12 (SI = cell -> ES:SI = first waypoint {flags, h, x, z};
 * DS:946A = rotation byte of the cell, DS:BCB8 = lane header, DS:946D = waypoint count) */
static FarPtr lane_list_for_cell(u16 cell_si)
{
    FarPtr pbin = ds_far(DS_lanes_set);                     /* P.BIN, far DS:CE9E */
    u16 m = DSW((u16)(DS_leg_map + (u16)(cell_si << 1)));
    u16 si;
    u8 ch;

    DSB(DS_place_rot) = (u8)(m >> 8);
    si = (u16)((u16)((m & 0xFF) << 1) + pbin.off);
    si = rd16(pbin.seg, si);
    si = (u16)(si + pbin.off);
    ch = rd8(pbin.seg, si);
    DSB(0xBCB8) = ch;                                       /* lane_hdr */
    DSB(0x946D) = (u8)(ch & 0x7F);                          /* lane_count */
    /* PORT: the original also leaves CH = lane_count; no caller uses it. */
    return far_make(pbin.seg, (u16)(si + 1));
}

/* Velocity component of vehicle_follow_lane (5466, 58bb-58e4 / 58eb-5914): (|d| << 16) / D, times the speed
 * (halved after a branch), high word, negated for d < 0. Returns DX; *abs_d = |d|. */
static u16 lane_vel_component(u16 d, u16 dd, u16 *abs_d)
{
    int neg = 0;
    u16 q = 0xFFFF, spd;
    if ((s16)d < 0) { neg = 1; d = (u16)-d; }
    *abs_d = d;
    if (dd > d) q = div32_16((u32)d << 16, dd, NULL);
    spd = DSW(DS_dxl2);                                     /* 9460: speed of this leg */
    if (DSB(0xBCC2) == 1) spd >>= 1;                        /* branch taken */
    spd = (u16)(((u32)q * spd) >> 16);
    if (neg) spd = (u16)-spd;
    return spd;
}

/* 0e12:5466 vehicle_follow_lane — simulation.md §4.12 (BX = 2*slot) */
static void vehicle_follow_lane(u16 bx)
{
    u16 ax, f, cx, sp, di, wf, dX, dZ, a, mn, mx, dd, adx, adz, big, n, res;
    u8 al, ah, dl, beh, bm, vx, vz, vb;
    FarPtr w;

    /* heading slews towards the target by 8/256 per frame */
    ax = OW(DS_obj_heading, bx);
    al = (u8)ax; ah = (u8)(ax >> 8);
    dl = (u8)(al - ah);
    if (dl) DSB(DS_obj_full_transform) = 1;
    dl = (u8)((u8)(dl + 4) & 0xF8);
    if (dl == 0) ah = al;
    else if (dl & 0x80) ah = (u8)(ah - 8);
    else ah = (u8)(ah + 8);
    OW(DS_obj_heading, bx) = (u16)(ah << 8 | al);
    if (DSB(DS_menu_preview) != 0) return;

    f = OW(DS_obj_flags, bx);
    if (!(f & 0x0F00)) { OB(DS_obj_pitch, bx) = 0; return; }
    if (!(bx <= 4 && DSB(DS_opp_recover + (bx >> 1)) != 0)) {   /* i <= 2 incl. the player slot */
        if (f & 0x1000) return;                                 /* halted */
    }
    if ((f & 0x8000) && OW(DS_obj_vel, bx) != 0 && (DSB(DS_frame8) & 1)) return;  /* braking: every 2nd frame */
    cx = OW(DS_obj_waypoint, bx);
    if (!(cx & 0xFFC0)) return;                                 /* parked until woken */
    cx = (u16)(cx - 0x40);
    OW(DS_obj_waypoint, bx) = cx;
    if (cx & 0xFFC0) return;

    /* waypoint reached */
    beh = (u8)((f >> 8) & 0x0F);
    cx = OW(DS_obj_waypoint, bx);
    if (beh == 9 || beh < 2) {                                  /* shuttle */
        u16 v;
        OW(DS_obj_waypoint, bx) = (u16)(DSB(DS_shuttle_period) << 8 | (u8)cx);
        v = OW(DS_obj_vel, bx);
        OW(DS_obj_vel, bx) = (u16)((u8)-(u8)(v >> 8) << 8 | (u8)-(u8)v);
        v = OW(DS_obj_pitch, bx);
        OW(DS_obj_pitch, bx) = (u16)((v & 0xFF00) | (u8)-(u8)v);
        OW(DS_obj_heading, bx) ^= 0x8080;
        return;
    }

    /* leg speed */
    al = rol8_2((u8)f);
    sp = DSW((u16)(DS_traffic_speeds + ((al & 3) << 1)));
    DSB(0x946F) = beh;                                          /* beh_lo3 */
    DSB(0xBCB7) = beh;                                          /* beh (full) */
    if (beh == 8) sp >>= 1;                                     /* train: half speed, no scaling */
    else {
        DSB(0x946F) = (u8)(beh & 7);
        if (DSB(DS_race_computer_cars) != 0 && bx <= 4) {
            /* BUG (faithful): highway test reads byte DS:9671 + cell, not the map word at 9671 + 2*cell */
            if (DSB((u16)(DS_leg_map + OW(DS_obj_cell, bx))) >= 0x74) sp = (u16)(sp + (sp >> 2));
            if (DSB(DS_opp_overtake + (bx >> 1)) != 0) sp = (u16)(sp + (sp >> 2));
        }
        a = (u16)((DSW(DS_skill_level) >> 2) + 6);
        a = (u16)((u32)a * sp);                                 /* mul si: low word */
        a >>= 4;
        if (DSB(DS_rain_level) != 0) a = (u16)(a - 2);
        if (DSB(DS_snow_level) != 0) a = (u16)(a - 4);
        sp = a;
    }
    DSW(DS_dxl2) = sp;                                          /* 9460: speed of this leg */

    /* branch mode by behaviour */
    DSB(0xBCC2) = 0;                                            /* branch_taken */
    if (beh < 3) bm = 0;
    else if (beh == 3) bm = 1;
    else if (beh == 4) bm = 2;
    else if (beh == 5) bm = (u8)(DSB(DS_rand_hi) & 1);
    else if (beh == 6) bm = (u8)((DSB(DS_rand_hi) & 1) << 1);
    else { bm = (u8)(DSB(DS_rand_hi) & 3); if (bm >= 3) bm = 0; }
    DSB(DS_branch_mode) = bm;
    DSB(DS_place_nverts) = (u8)cx;                              /* 946B: waypoint index */

    w = lane_list_for_cell(OW(DS_obj_cell, bx));
    w.off = (u16)(w.off + (u16)(DSB(DS_place_nverts) << 3));
    wf = rd16(w.seg, w.off);
    di = OW(DS_obj_cell, bx);

    if ((wf & 0xC0) == 0x80) {                                  /* lane exit by local edge wf & 3 */
        u16 key;
        u8 k;
        if (DSW(DS_uturn_obj) == bx) {                          /* police U-turn in the same cell */
            DSW(DS_uturn_obj) = 0;
            key = (u16)((u8)wf ^ 0x40);
        } else {
            u8 dir = rol8_2(DSB(DS_place_rot));
            s16 step;
            DSB(DS_coll_bits) = (u8)wf;                         /* 946C: exit flags */
            dir = (u8)((u8)(dir + (u8)wf) & 3);
            step = dir == 0 ? -32 : dir == 2 ? 32 : dir == 1 ? 1 : -1;
            di = (u16)(((u16)(di + step) & 0x1E0) | ((u16)(di + step) & 0x1F));
            al = rol8_2(DSB((u16)(DS_leg_map + 1 + (u16)(di << 1))));
            key = (u16)(0xC0 | ((u8)((dir ^ 2) - al) & 3));
        }
        /* search the lane entry; dead end: U-turn in the same cell. Faithful: a waypoint count of 0
         * loops 256 times. ENH: the original can loop forever when bad/curvy lane data has no matching
         * entry even after the fallback; bail out with an in-place U-turn instead of freezing. */
        {
            int tried_fallback = 0;
            for (;;) {
                w = lane_list_for_cell(di);
                k = 0;
                do {
                    if (key == rd16(w.seg, w.off)) goto found;
                    w.off = (u16)(w.off + 8);
                    k++;
                    DSB(0x946D) = (u8)(DSB(0x946D) - 1);
                } while (DSB(0x946D) != 0);
                if (tried_fallback) {
                    /* No matching entry found; reverse direction to avoid an infinite loop. */
                    u16 v;
                    OW(DS_obj_waypoint, bx) = (u16)(DSB(DS_shuttle_period) << 8 | (u8)OW(DS_obj_waypoint, bx));
                    v = OW(DS_obj_vel, bx);
                    OW(DS_obj_vel, bx) = (u16)((u8)-(u8)(v >> 8) << 8 | (u8)-(u8)v);
                    v = OW(DS_obj_pitch, bx);
                    OW(DS_obj_pitch, bx) = (u16)((v & 0xFF00) | (u8)-(u8)v);
                    OW(DS_obj_heading, bx) ^= 0x8080;
                    return;
                }
                key = (u16)(0xC0 | (DSB(DS_coll_bits) & 3));
                di = OW(DS_obj_cell, bx);
                tried_fallback = 1;
            }
        }
    found:
        DSB(DS_place_nverts) = k;
        goto target;
    }

    if (wf & 0x80) wf = 0;                                      /* entry waypoint */
    {
        u16 stop = (u16)(wf & 0x4040);
        if (stop) {                                             /* 59c7 */
            u16 v = OW(DS_obj_vel, bx);
            u16 wait = 0;
            int halt = 0;
            if (stop < 0x4000) {                                /* 0040h stop sign */
                if (v != 0 && !(OW(DS_obj_flags, bx) & 0x4000)) {
                    u8 q = 0;
                    halt = 1;
                    if ((s8)(u8)v <= 0) q = 1;
                    if ((s8)(u8)(v >> 8) <= 0) q = (u8)(q + 2);
                    q = (u8)(q << 3);
                    q = (u8)(q - DSB(DS_frame8));
                    q = (u8)((q & 0x1F) + 0x10);
                    wait = (u16)(q << 6);
                }
            } else if (stop > 0x4000) {                         /* 4040h level crossing: nobody exempt */
                if (DSB(DS_gate_pos) >= 0x10 && v != 0) { halt = 1; wait = 0x140; }
            } else {                                            /* 4000h traffic light */
                if (v != 0 && !(OW(DS_obj_flags, bx) & 0x4000)) {
                    s8 lx = (s8)(u8)v, lz = (s8)(u8)(v >> 8);
                    u8 ch = 0;
                    if (lx == 0) ch = 2;
                    else if (lx < 0) ch = (lz > 0) ? 2 : 0;
                    else ch = (lz < 0) ? 2 : 0;
                    if (((DSB(DS_light_phase) & 2) ^ ch) == 0) {
                        u8 q = DSB(DS_light_timer);
                        halt = 1;
                        if (!(DSB(DS_light_phase) & 1)) q = (u8)(q + 9);
                        q = (u8)(q + 4);
                        wait = (u16)(q << 6);
                    }
                }
            }
            if (halt) {
                OW(DS_obj_flags, bx) |= 0x8000;
                OW(DS_obj_vel, bx) = 0;
                OB(DS_obj_pitch, bx) = 0;
                OW(DS_obj_waypoint, bx) |= wait;
                return;
            }
            OW(DS_obj_flags, bx) &= 0x7FFF;
        }
    }

    if (wf & 0xBF3F) {                                          /* 56ab: opponent route script DS:9597 */
        u16 g = OW(DS_obj_flags, bx);
        u16 k = (u16)((g & 0x3F) - 2);
        if ((s16)k >= 0 && (u8)k < 2) {
            u8 pos = (u8)(DSB(DS_script_pos + k) + 1);          /* pre-increment */
            u8 s;
            u16 ng = (u16)(g & 0xF8FF);
            DSB(DS_script_pos + k) = pos;
            s = DSB((u16)(DS_opp_scripts + 10 * k + pos));
            if (s == 0) {                                       /* finished: time minus 20 s per player ticket */
                u8 tk = DSB(DS_clock_sub), sec = DSB(DS_clock_sec), mn2 = DSB(DS_clock_min);
                u8 t = DSB(DS_tickets);
                while (t) {
                    t--;
                    sec = (u8)(sec - 0x14);
                    if ((s8)sec < 0) { sec = (u8)(sec + 0x3C); mn2--; }
                }
                DSW((u16)(DS_opp_time + 3 * k)) = (u16)(sec << 8 | tk);
                DSB((u16)(DS_opp_time + 3 * k + 2)) = mn2;
                ng = 0;                                         /* removed */
            }
            OW(DS_obj_flags, bx) = (u16)(ng | (u16)(s << 8));
        }
    }

    /* 5722: branch */
    {
        u16 sel = wf;
        int take = 0;
        bm = DSB(DS_branch_mode);
        if (bm == 1 && (sel & 0x3F)) take = 1;
        else if (bm > 1 && (sel & 0x3F00)) { sel = swapb(sel); take = 1; }
        else if (sel & 0x8000) {
            if (DSB(0x946F) == 6) sel = swapb(sel);
            if (!(sel & 0x3F)) sel = swapb(sel);
            take = 1;
        }
        if (take) {
            s8 o = (s8)(u8)((u8)sel << 2) >> 2;                 /* 6-bit signed offset */
            DSB(0xBCC2) = 1;
            DSB(DS_place_nverts) = (u8)(DSB(DS_place_nverts) + (u8)o);
            w.off = (u16)(w.off + (u16)((u16)(s16)o << 3));
        } else {
            DSB(DS_place_nverts)++;
            w.off = (u16)(w.off + 8);
        }
    }

target:                                                         /* 576d */
    OW(DS_obj_cell, bx) = di;
    OB(DS_obj_waypoint, bx) = DSB(DS_place_nverts);
    {
        u16 h = rd16(w.seg, (u16)(w.off + 2));
        s16 x, z, t;
        h = (u16)(h + (DSW((u16)(DS_leg_map + (u16)(di << 1))) & 0x3F00));
        DSW(DS_dxr2) = (u16)(h << 3);                           /* 9462: target height * 8 */
        x = (s16)rd16(w.seg, (u16)(w.off + 4));
        z = (s16)rd16(w.seg, (u16)(w.off + 6));
        al = (u8)(DSB(DS_place_rot) & 0xC0);
        if (al == 0x40) { t = x; x = z; z = (s16)-t; }
        else if (al == 0x80) { x = (s16)-x; z = (s16)-z; }
        else if (al == 0xC0) { t = x; x = (s16)-z; z = t; }

        dX = (u16)((u16)x + (u16)((di & 0x1F) << 10) + 0x200 - OW(DS_obj_x, bx));
        a = ((s16)dX < 0) ? (u16)-dX : dX;
        if ((a >> 8) >= 0x40) dX = (u16)(dX + 0x8000);
        dZ = (u16)((u16)z + (u16)((u16)(0x1E0 - (di & 0x1E0)) << 5) + 0x200 - OW(DS_obj_z, bx));
        {
            u16 bp = 0xC000;
            a = dZ;
            if ((s16)dZ < 0) { a = (u16)-dZ; bp = 0x4000; }
            if ((a >> 8) >= 0x20) dZ = (u16)(dZ + bp);
        }
    }

    if (OW(DS_obj_flags, bx) & 0x4000) {                        /* overtake: aim 64 units to the left */
        u8 ovt = DSB(DS_traffic_overtake), rec = 0;
        if (bx <= 4) { ovt = DSB(DS_opp_overtake + (bx >> 1)); rec = DSB(DS_opp_recover + (bx >> 1)); }
        if (rec == 0 && (ovt != 0 || (DSB(0xBCB7) & 8))) {
            OW(DS_obj_flags, bx) &= 0xF7FF;
            if (!(DSB(0xBCB8) & 0x80)) {
                u16 s0, s1, ang;
                if (bx <= 4) DSB(DS_opp_overtake + (bx >> 1)) = 0x28;
                else DSB(DS_traffic_overtake) = 0x28;
                ang = atan2_xz((s16)dX, (s16)dZ, NULL, NULL);
                s0 = DSW(DS_dxl2);
                s1 = DSW(DS_dxr2);
                polar((u16)(0x4000 - ang), 0x40);
                dX = (u16)(dX + DSW(DS_dxr2));                  /* r*cos */
                dZ = (u16)(dZ + DSW(DS_dxl2));                  /* r*sin */
                DSW(DS_dxl2) = s0;
                DSW(DS_dxr2) = s1;
            }
        }
    }

    ax = atan2_xz((s16)dX, (s16)dZ, &mn, &mx);                  /* also CX = min, DX = max */
    OW(DS_obj_heading, bx) = (u16)((OW(DS_obj_heading, bx) & 0xFF00) | (ax >> 8));
    if ((s16)mn < 0) mn = (u16)-mn;
    {
        u16 s = (u16)(mn + mx);                                 /* carry lost */
        u32 t = (u32)s + (u16)(mx << 1);                        /* carry kept by rcr */
        dd = (u16)(t >> 1);
    }
    vz = (u8)lane_vel_component(dZ, dd, &adz);
    DSB(DS_place_rot) = vz;                                     /* 946A scratch */
    vx = (u8)lane_vel_component(dX, dd, &adx);
    OW(DS_obj_vel, bx) = (u16)(DSB(DS_place_rot) << 8 | vx);

    /* frames to the next waypoint */
    big = adx; vb = vx;
    if (adx < adz) { big = adz; vb = vz; }
    {
        u16 avb = (u16)(s16)(s8)vb;
        if ((s16)avb < 0) avb = (u16)-avb;
        n = 0;
        if (avb) n = div32_16(big, avb, NULL);
        if (n == 0) n = 1;
    }
    OW(DS_obj_waypoint, bx) |= (u16)(n << 6);                   /* 10-bit counter, truncates */

    /* vertical speed and the pitch shear code */
    {
        s16 vy = idiv32_16((s32)(s16)(u16)(DSW(DS_dxr2) - OW(DS_obj_y8, bx)), (s16)n, NULL);
        u8 vyb, sum, c1, c2;
        u16 v;
        vy = (s16)(vy >> 2);
        vyb = (u8)vy;
        res = vyb;
        v = OW(DS_obj_vel, bx);
        c1 = (u8)v; if (c1 & 0x80) c1 = (u8)-c1;
        c2 = (u8)(v >> 8); if (c2 & 0x80) c2 = (u8)-c2;
        sum = (u8)(c1 + c2);
        if (sum) {
            u8 c = 3, m = vyb;
            u16 q16;
            if (m & 0x80) m = (u8)-m;
            q16 = (u16)(m << 4);
            if ((u8)(q16 >> 8) < sum) {
                u8 q = (u8)div16_8(q16, sum);
                if (q < 0x18) { c = 4;
                    if (q < 0x0C) { c = 5;
                        if (q < 6) { c = 6;
                            if (q < 3) { c = 7;
                                if (q < 1) c = 0; } } } }
            }
            if (!(vyb & 0x80)) c = (u8)-c;
            res = (u16)(c << 8 | vyb);
        }
        if (OB(DS_obj_pitch, (u16)(bx + 1)) != (u8)(res >> 8)) DSB(DS_obj_full_transform) = 1;
        OW(DS_obj_pitch, bx) = res;
    }
}

/* Model data of object slot bx (inline in 52a3, 4db6, 5ad4, 5cf2; render3d.md §4.14 "model lookup"). */
static FarPtr obj_model_ptr(u16 bx)
{
    u16 m = (u16)(OW(DS_obj_flags, bx) & 0x3F);
    if (m > 3) {
        FarPtr o = ds_far(DS_objects_set);                      /* O.BIN, far DS:E54C */
        u16 si = (u16)((u16)(m << 1) + o.off);
        si = rd16(o.seg, si);
        return far_make(o.seg, (u16)(si + o.off));
    }
    if (m == 3) return ds_ptr(0xD7A4);                          /* opponent 2 model */
    if (m == 2) return ds_ptr(0xCEBC);                          /* opponent 1 model */
    return ds_far(DS_car_pob);                                  /* player POB (models 0, 1) */
}

/* First collision box of a vehicle model: p + 8 + 6*nv + 8*(u8)(nf + nc); *nk = box count. */
static u16 obj_model_boxes(FarPtr p, u8 *nk)
{
    u8 nf = rd8(p.seg, p.off), nv = rd8(p.seg, (u16)(p.off + 1));
    u8 nc = rd8(p.seg, (u16)(p.off + 2));
    u16 si;
    *nk = rd8(p.seg, (u16)(p.off + 3));
    si = (u16)(p.off + 2);
    si = (u16)(si + (u16)(3 * (u16)(nv << 1)));
    si = (u16)(si + 6 + (u16)((u8)(nf + nc) << 3));
    return si;
}

/* 0e12:5ad4 vehicle_collisions — simulation.md §4.12, render3d.md §4.14 */
static void vehicle_collisions(void)
{
    u16 bx, si;

    /* pass 1: a halted car that still has a speed swerves (0800h); others lose halt/brake */
    bx = (u16)(DSW(DS_obj_a475) << 1);
    while ((bx = (u16)(bx - 2)) != 0) {
        u16 f = OW(DS_obj_flags, bx);
        if (!(f & 0x2000)) continue;
        if (OW(DS_obj_vel, bx) == 0) continue;
        if ((f & 0x9000) == 0x9000) {
            OW(DS_obj_flags, bx) &= 0xEFFF;
            OW(DS_obj_flags, bx) |= 0x0800;
        } else OW(DS_obj_flags, bx) &= 0x6FFF;
    }

    /* pass 2 */
    bx = (u16)(DSW(DS_obj_a475) << 1);
    while ((bx = (u16)(bx - 2)) != 0) {
        u16 f = OW(DS_obj_flags, bx);
        u8 m;
        if (!(f & 0x2000)) continue;
        m = (u8)(f & 0x3F);
        if (m >= 0x12 && m <= 0x14) continue;                   /* trains are never the subject */
        si = bx;
        while ((s16)(si = (u16)(si - 2)) >= 0) {
            u16 xi = OW(DS_obj_x, bx), zi = OW(DS_obj_z, bx), yi = (u16)(OW(DS_obj_y8, bx) >> 3);
            u16 d;
            u8 nk, b;
            FarPtr p;
            if (si != 0 && OW(DS_obj_face_count, si) == 0) continue;   /* j must be drawn (player always) */
            d = (u16)(OW(DS_obj_x, si) - xi); if ((s16)d < 0) d = (u16)-d;
            if (d >= DSW(DS_coll_window)) continue;
            d = (u16)(OW(DS_obj_z, si) - zi); if ((s16)d < 0) d = (u16)-d;
            if (d >= DSW(DS_coll_window)) continue;
            d = (u16)((u16)(OW(DS_obj_y8, si) >> 3) - yi); if ((s16)d < 0) d = (u16)-d;
            if (d >= DSW(DS_coll_window)) continue;

            DSB(DS_coll_bits) = 0;
            DSW(DS_quad_temps) = si;                            /* 9464: slot j */
            p = obj_model_ptr(bx);
            {
                u16 box = obj_model_boxes(p, &nk);
                u16 cnt = nk;
                if (nk != 0) {
                    do {
                        s16 A, B, C;
                        DSB(DS_place_shear) = (u8)(OW(DS_obj_pitch, bx) >> 8);
                        DSB(DS_place_rot) = (u8)(OW(DS_obj_heading, bx) >> 8);
                        vertex_rotate(far_make(p.seg, box), 2, &A, &B, &C);   /* 7653: DX, AX, CX */
                        DSW(DS_dxr2) = rd16(p.seg, (u16)(box + 6));  /* 9462 = r | h << 8 */
                        box = (u16)(box + 8);
                        DSW(DS_obj_move_dx) = (u16)((u16)B + OW(DS_obj_x, bx));   /* BCC4 box centre */
                        DSW(DS_dz) = (u16)((u16)C + OW(DS_obj_z, bx));            /* BCC6 */
                        DSW(DS_dy) = (u16)((u16)A + (u16)(OW(DS_obj_y8, bx) >> 3)); /* BCC8 */
                        vehicle_collision_boxes(DSW(DS_quad_temps));           /* 5cf2 */
                    } while (--cnt != 0);
                    b = DSB(DS_coll_bits);
                } else b = 0;
            }
            si = DSW(DS_quad_temps);
            if (!b) continue;
            if (b & 1) {                                        /* contact: both halt, or the player crashes */
                OW(DS_obj_flags, bx) |= 0x9000;
                if (si) OW(DS_obj_flags, si) |= 0x9000;
                else DSB(DS_crash_flag) |= 1;
                continue;
            }
            DSW(DS_dxl2) = (b & 2) ? 0x9000 : 0x8000;           /* near -> halt, close -> brake */
            {
                u16 a, v, aa;
                a = atan2_xz((s16)(u16)(OW(DS_obj_x, si) - OW(DS_obj_x, bx)),
                             (s16)(u16)(OW(DS_obj_z, si) - OW(DS_obj_z, bx)), NULL, NULL);
                DSW(DS_dxr2) = a;
                v = OW(DS_obj_vel, bx);
                aa = atan2_xz((s8)(u8)v, (s8)(u8)(v >> 8), NULL, NULL);
                if ((u8)((u8)((u16)(aa - DSW(DS_dxr2)) >> 8) + 8) < 0x10) {
                    OW(DS_obj_flags, bx) |= DSW(DS_dxl2);       /* i heads at j */
                } else {
                    v = OW(DS_obj_vel, si);
                    aa = atan2_xz((s8)(u8)v, (s8)(u8)(v >> 8), NULL, NULL);
                    if ((u8)((u8)((u16)(aa - DSW(DS_dxr2)) >> 8) + 0x88) < 0x10 && si != 0)
                        OW(DS_obj_flags, si) |= DSW(DS_dxl2);   /* j heads at i */
                }
            }
        }
    }
}

/* 0e12:5cf2 vehicle_collision_boxes — simulation.md §4.12, render3d.md §4.14 (BX = 2*j; point BCC4/BCC6/BCC8,
 * DS:9462 = r_i | h_i << 8) */
static void vehicle_collision_boxes(u16 bx)
{
    FarPtr p;
    u16 si, cnt;
    u8 nk;

    DSB(DS_place_shear) = (u8)(OW(DS_obj_pitch, bx) >> 8);
    DSB(DS_place_rot) = (u8)(OW(DS_obj_heading, bx) >> 8);
    p = obj_model_ptr(bx);
    si = obj_model_boxes(p, &nk);
    if (nk == 0) return;
    cnt = nk;
    do {
        s16 A, B, C;
        u16 ax, cx, dx, rh;
        u8 al, dl, cl, ch, dh;
        vertex_rotate(far_make(p.seg, si), 2, &A, &B, &C);    /* 7653: DX = A', AX = B', CX = C' */
        si = (u16)(si + 6);                                     /* at r|h */
        ax = (u16)((u16)B + OW(DS_obj_x, bx));
        cx = (u16)((u16)C + OW(DS_obj_z, bx));
        dx = (u16)((u16)A + (u16)(OW(DS_obj_y8, bx) >> 3));
        ax = (u16)(ax - DSW(DS_obj_move_dx)); if ((s16)ax < 0) ax = (u16)-ax;
        cx = (u16)(cx - DSW(DS_dz));          if ((s16)cx < 0) cx = (u16)-cx;
        if (ax > cx) { u16 t = ax; ax = cx; cx = t; }
        {
            u16 s = (u16)(ax + cx);                             /* carry lost */
            u32 t = (u32)s + (u16)(cx << 1);                    /* carry kept by rcr */
            ax = (u16)((u16)(t >> 1) >> 1);                     /* ~(min + 3 max) / 4 */
        }
        /* BUG (faithful): the two early exits below skip the "add si, 2" past r|h, so the next box is read
         * 2 bytes too early (from this box's r|h word on). */
        if (ax >> 8) continue;
        dx = (u16)(dx - DSW(DS_dy)); if ((s16)dx < 0) dx = (u16)-dx;
        dx >>= 1;
        if (dx >> 8) continue;
        rh = (u16)(rd16(p.seg, si) + DSW(DS_dxr2));             /* r carries into h */
        si = (u16)(si + 2);
        al = (u8)ax; dl = (u8)dx;
        cl = (u8)rh; ch = (u8)(rh >> 8);
        dh = 7;
        if (!(al > cl) && dl < ch) goto hit;
        rh = (u16)(rh << 3);
        cl = (u8)rh; ch = (u8)(rh >> 8);
        dh = 6;
        if (al < cl && dl < ch) goto hit;
        ch = (u8)(ch + ch);
        dh = 4;
        {
            int carry = (cl & 0x80) != 0;
            cl = (u8)(cl + cl);
            if (!carry && al >= cl) continue;
        }
        if (dl >= ch) continue;
    hit:
        DSB(DS_coll_bits) |= dh;
    } while (--cnt != 0);
}

/* 0e12:5e70 crossing_gate_update — simulation.md §4.13, render3d.md §4.14 */
void crossing_gate_update(void)
{
    u16 g = DSW(DS_gate_slot1), bx;
    u8 near_, p;

    if (g == 0xFFFF) {
        DSB(DS_train_near) = 0;
        DSB(DS_gate_pos) = 0;
        DSW(DS_train_min_d) = 0xFFFF;
        return;
    }
    DSB(DS_train_near) = 0;
    bx = (u16)(DSW(DS_obj_a475) << 1);
    while ((bx = (u16)(bx - 2)) != 0) {
        u16 f = OW(DS_obj_flags, bx), a, d;
        u8 m;
        if (!(f & 0x2000)) continue;
        if (OW(DS_obj_face_count, bx) == 0) continue;
        m = (u8)(f & 0x3F);
        if (m < 0x12 || m > 0x14) continue;                     /* trains */
        a = (u16)((u16)((u16)(OW(DS_obj_x, g) + 0x100) & 0xFE00) - OW(DS_obj_x, bx));
        if ((s16)a < 0) a = (u16)-a;
        d = (u16)((u16)((u16)(OW(DS_obj_z, g) + 0x100) & 0xFE00) - OW(DS_obj_z, bx));
        if ((s16)d < 0) d = (u16)-d;
        a = (u16)(a + d);
        if (a >= 0x240) continue;
        if (DSW(DS_train_min_d) >= a) DSW(DS_train_min_d) = a;  /* never reset while a gate exists */
        DSB(DS_train_near) = 1;
        break;
    }
    near_ = DSB(DS_train_near);
    p = DSB(DS_gate_pos);
    if (p) sfx_play_ax(7);                                      /* bell */
    if (near_) {
        if (p < 0x40) {
            if (p < 0x3C && DSW(DS_train_min_d) <= 0x60) p = (u8)(p + 3);
            p++;
            if (p == 0x3E) sfx_play_ax(8);
        }
    } else if (p) {
        p--;
        p &= 0x3E;
    }
    DSB(DS_gate_pos) = p;
    crossing_gate_arm_place(DSW(DS_gate_slot1));                /* 5f57 */
    if (DSW(DS_gate_slot2) != 0xFFFF) crossing_gate_arm_place(DSW(DS_gate_slot2));
}

/* ENH: the police car in slot bx is ahead of the car in slot si (in front of the line across that car).
 * Positions wrap at 8000h (x) and 4000h (z); a heading's forward direction is (sin, cos) in (x, z) in the view's
 * convention, which is the objects' heading + 4000h (sim_physics.c: obj_heading = view_heading - 4000h). */
static bool police_ahead(u16 bx, u16 si)
{
    double dx = (s16)(u16)((u16)(OW(DS_obj_x, bx) - OW(DS_obj_x, si)) << 1) >> 1;
    double dz = (s16)(u16)((u16)(OW(DS_obj_z, bx) - OW(DS_obj_z, si)) << 2) >> 2;
    u16 hv = si == 0 ? DSW(DS_view_heading) : (u16)(OW(DS_obj_heading, si) + 0x4000);
    double h = hv * (2.0 * M_PI / 65536.0);
    return dx * sin(h) + dz * cos(h) > 0;
}

/* 0e12:5ffe police_update — simulation.md §4.13, render3d.md §4.14 */
void police_update(void)
{
    u16 bx;

    if (DSB(DS_ext_view) != 0) return;
    if (DSB(DS_ticket_cooldown)) DSB(DS_ticket_cooldown)--;
    DSB(DS_radar_level) = 0;
    DSB(DS_place_rot) = 0;                                      /* 946A: police close enough to pull over */
    DSB(DS_place_nverts) = 0;                                   /* 946B: chasing police (siren) */
    bx = (u16)(DSW(DS_obj_a475) << 1);
    while ((bx = (u16)(bx - 2)) != 0) {
        u16 f = OW(DS_obj_flags, bx), ax;
        u8 m;
        if (!(f & 0x2000)) continue;
        m = (u8)(f & 0x3F);
        if (m != 0x11 && m != 0x30) continue;                   /* police models */
        if (OW(DS_obj_face_count, bx) == 0) {                   /* not drawn: detector only */
            u16 a = (u16)(OW(DS_obj_x, bx) - DSW(DS_obj_x));
            u16 d = (u16)(OW(DS_obj_z, bx) - DSW(DS_obj_z));
            if ((s16)a < 0) a = (u16)-a;
            if ((s16)d < 0) d = (u16)-d;
            d = (u16)(d + a);
            if (d < 0x800) ax = 0x21;
            else if (d < 0xC00) ax = 0x11;
            else continue;
        } else {
            if (DSB(DS_race_computer_cars) != 0 && (OW(DS_obj_flags, bx) & 0x4000)) {
                u16 si = 2;
                u8 k;
                for (k = 2; k != 0; k--, si = (u16)(si + 2)) {  /* opponent ticket */
                    u16 a = (u16)(OW(DS_obj_x, bx) - OW(DS_obj_x, si));
                    u16 d = (u16)(OW(DS_obj_z, bx) - OW(DS_obj_z, si));
                    if ((s16)a < 0) a = (u16)-a;
                    if ((s16)d < 0) d = (u16)-d;
                    a = (u16)(a + d);
                    if (a >= 0xD8) continue;
                    if ((s8)(u8)((u8)(OB(DS_obj_heading, (u16)(bx + 1)) - OB(DS_obj_heading, (u16)(si + 1))) + 0x40) < 0)
                        continue;
                    if (police_ahead(bx, si)) continue;             /* ENH: only once it has overtaken the police */
                    DSB((u16)(DS_tickets + (si >> 1)))++;
                    DSB(DS_sky_flash) = 3;
                    DSB(DS_flash_msg) = 0x2C;                   /* "Your opponent got a ticket" */
                    DSB(DS_ticket_cooldown) = 0x96;
                    police_pursuit_clear();
                    return;                                     /* NB: no siren sfx this frame */
                }
            }
            ax = (u16)(0x6F - (u16)(DSW((u16)(DS_vert_depth + (u16)(OW(DS_obj_vert_base, bx) << 1))) >> 7));
            if ((s16)ax < 0) continue;
        }
        if (ax >= 0x20) {
            if (OW(DS_obj_flags, bx) & 0x4000) DSB(DS_place_nverts)++;
            if (ax >= 0x50 && DSB(DS_ticket_cooldown) == 0) {
                u16 dx = OW(DS_obj_flags, bx);
                int close = 1;
                if (!(dx & 0x4000)) {
                    if (DSB(DS_speedo_step) < 0x0C) close = 0;  /* car speed < 48 */
                    /* ENH: the original starts a chase for any police car this near while it is still in front
                     * of the player: one driving the same way races off ahead, an oncoming one turns round
                     * (uturn_obj below) and blocks the road with its siren on, a parked one pulls out ahead. A
                     * police car now gives chase only once the player has passed it (it is behind the line
                     * across the player's car); then as the original, the oncoming one turning round. */
                    else if (police_ahead(bx, 0))
                        close = 0;
                    else {
                        OW(DS_obj_flags, bx) = (u16)(dx | 0x40C0);   /* chase at speed class 3 */
                        DSB(DS_clocked_speed) = DSB(DS_speedo_step);
                        dx = OW(DS_obj_waypoint, bx);
                        if (!(dx & 0xFFC0)) OW(DS_obj_waypoint, bx) = (u16)(dx + 0x40);   /* wake a parked cop */
                        else {
                            dx = (u16)(OW(DS_obj_heading, bx) - DSW(DS_obj_heading));
                            if ((s8)(u8)((u8)(dx >> 8) - 0x40) >= 0) {
                                DSW(DS_uturn_obj) = bx;
                                close = 0;
                            }
                        }
                    }
                }
                if (close && ax >= 0x65) {
                    u8 c;
                    DSB(DS_place_rot)++;
                    c = (u8)(DSB(DS_pullover_count) + 1);
                    DSB(DS_pullover_count) = c;
                    if (c >= 0x1E) {                            /* caught after 30 frames */
                        u8 s;
                        DSB(DS_sky_flash) = 3;
                        DSB(DS_flash_msg) = 0x21;               /* "You just got a ticket" */
                        DSB(DS_ticket_cooldown) = 0x96;
                        police_pursuit_clear();
                        DSB(DS_tickets)++;
                        s = (u8)(DSB(DS_clock_sec) + 0x14);
                        if (s >= 0x3C) { DSB(DS_clock_min)++; s = (u8)(s - 0x3C); }
                        DSB(DS_clock_sec) = s;
                        return;
                    }
                }
            }
        }
        if (DSB(DS_radar_level) < (u8)ax) DSB(DS_radar_level) = (u8)ax;
    }
    if (DSB(DS_place_rot) == 0) DSB(DS_pullover_count) = 0;
    sfx_play_ax(DSB(DS_place_nverts) ? 0x14 : 0x94);            /* siren on / off */
}

/* 0e12:61d2 police_pursuit_clear — simulation.md §4.13 */
void police_pursuit_clear(void)
{
    u16 bx;
    DSB(DS_pullover_count) = 0;
    bx = (u16)(DSW(DS_obj_a475) << 1);
    while ((bx = (u16)(bx - 2)) != 0) {
        u16 f = OW(DS_obj_flags, bx);
        u8 m = (u8)(f & 0x3F);
        if (m == 0x11 || m == 0x30) OW(DS_obj_flags, bx) = (u16)(f & 0xBFBF);
    }
}

/* 0e12:61fd opponent_times_finalize — render3d.md §4.14 (far) */
void opponent_times_finalize(void)
{
    u16 ax;
    u8 cl;

    opponent_time_estimate(2, 0);
    opponent_time_estimate(4, 3);
    ax = DSW(DS_opp_time);
    cl = DSB(DS_opp_time + 2);
    DSB(0xE80F) = cl;                                           /* results record of opponent 1 */
    DSB(0xE810) = (u8)(ax >> 8);
    DSB(0xE811) = (u8)ax;
    ax = DSW(0x95AB);
    DSB(0xE817) = (u8)(ax >> 8);
    DSB(0xE818) = (u8)ax;
    ax = DSW(DS_opp_time + 3);
    cl = DSB(DS_opp_time + 5);
    DSB(0xE82A) = cl;                                           /* results record of opponent 2 */
    DSB(0xE82B) = (u8)(ax >> 8);
    DSB(0xE82C) = (u8)ax;
    ax = DSW(0x95AD);
    DSB(0xE832) = (u8)(ax >> 8);
    DSB(0xE833) = (u8)ax;
}

/* 0e12:624a opponent_time_estimate — render3d.md §4.14 (BX = opponent slot 2/4, SI = 0/3 into DS:BCBB) */
static void opponent_time_estimate(u16 bx, u16 si)
{
    u8 cl = DSB((u16)(DS_opp_time + si));                       /* frac */
    u8 dh = DSB((u16)(DS_opp_time + si + 1));                   /* sec */
    u8 dl = DSB((u16)(DS_opp_time + si + 2));                   /* min */
    u8 t;

    if ((cl | dh | dl) == 0) {                                  /* not finished: estimate from the distance */
        u16 ax, cx, n;
        ax = (u16)(DSW(DS_obj_x) - OW(DS_obj_x, bx)); if ((s16)ax < 0) ax = (u16)-ax;
        cx = (u16)(DSW(DS_obj_z) - OW(DS_obj_z, bx)); if ((s16)cx < 0) cx = (u16)-cx;
        cx = (u16)(cx + ax);                                    /* distance */
        ax = (u16)((u32)(u16)((DSW(DS_skill_level) >> 1) + 4) * DSW(0x95B5));   /* traffic_speeds[3] */
        ax >>= 4;
        { u16 t2 = ax; ax = cx; cx = t2; }                      /* xchg: AX = distance, CX = speed */
        n = 0;
        if (ax > cx) n = div32_16(ax, cx, NULL);
        n++;
        dh = 0xFF;
        if (n < 0x700) {
            u16 r = div16_8(n, 7);
            cl = (u8)(r >> 8);
            dh = (u8)r;
        } else cl = (u8)cx;                                     /* sic: CL keeps the low byte of the speed */
        dl = 0;
        if (dh & 0x80) dl = (u8)(dl + 4);
        dh = (u8)(dh << 1);
        while (dh >= 0x3C) { dl++; dh = (u8)(dh - 0x3C); }
        dh = (u8)(dh + DSB(DS_clock_sec));
        if (dh >= 0x3C) { dl++; dh = (u8)(dh - 0x3C); }
        dl = (u8)(dl + DSB(DS_clock_min));
        cl &= 7;
        t = DSB(DS_tickets);                                    /* the player's tickets */
        while (t) {
            t--;
            dh = (u8)(dh - 0x14);
            if ((s8)dh < 0) { dh = (u8)(dh + 0x3C); dl--; }
        }
    }
    t = DSB((u16)(DS_tickets + (bx >> 1)));                     /* this opponent's tickets */
    while (t) {
        t--;
        dh = (u8)(dh + 0x14);
        if (dh >= 0x3C) { dh = (u8)(dh - 0x3C); dl++; }
    }
    DSB((u16)(DS_opp_time + si)) = cl;
    DSB((u16)(DS_opp_time + si + 1)) = dh;
    DSB((u16)(DS_opp_time + si + 2)) = dl;
}

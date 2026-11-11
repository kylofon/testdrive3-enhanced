/* render3d (render3d.md): frame roots, view / buffer set-up, per-leg state, trigonometry helpers, and the
 * terrain under the car.
 * 0e12:03d4 detail_apply, 0fa1 screen_shake_step, 22fe atan2_xz, 234b polar, 2537 frame_buffers_init,
 * 255e leg_state_reset, 25f4 view_row_tables_init, 261c build_colour_remap, 34c3 scene_prepare,
 * 409c view_setup, 6316 plane_height_at_camera, 6421 ground_slope_update, 6604 rotate_by_heading,
 * 6666 slope_to_roll_code, 76ec frame_update, 77d5 frame_draw. */
#define RENDER3D_INTERNAL
#include "game/game.h"
#include "enhanced/enhanced.h"

/* 0e12:409c view_setup — render3d.md §4.2 (view size from DS:09C4 / B6DC; patches the 24 clip immediates
 * listed at DS:BAE4 — PORT: they stay in mem[] at their code addresses and the rasterizers read them there) */
void view_setup(void)
{
    u16 ax = 0x1FE0, bx = 0x10, cx = 0x56, dx = 0x38;
    if (DSB(DS_menu_preview) == 0) {
        ax = 0x27E0; bx = 0x14; cx = 0x60; dx = 0x38;
        if (DSB(DS_half_window) != 0) { ax = 0x1DE0; bx = 0x0F; cx = 0x40; dx = 0x28; }
    }
    DSW(DS_horizon_base) = dx;
    DSW(DS_view_rows) = cx;
    cx = (u16)(cx << 8);
    DSW(DS_view_bytes) = (u16)(cx + (cx >> 2));
    DSB(DS_view_cx_hi) = (u8)bx;
    DSW(DS_mirror_base) = (u16)(0x7500 + (bx << 8));
    DSW(DS_view_w32m) = ax;
    u16 w32 = (u16)(ax + 0x20), w32x = (u16)(w32 + 0x8000);
    const u16 t = DS_clip_patch_ptrs;
#define PATCH(k, v) SEGW(R3_CS, DSW(t + (k))) = (v)          /* mov cs:[di], reg */
    PATCH(0x00, w32); PATCH(0x1C, w32); PATCH(0x26, w32);
    PATCH(0x02, w32x); PATCH(0x1E, w32x); PATCH(0x28, w32x);
    PATCH(0x04, ax); PATCH(0x20, ax); PATCH(0x2A, ax);
    PATCH(0x06, w32); PATCH(0x22, w32); PATCH(0x2C, w32);
    PATCH(0x08, ax); PATCH(0x24, ax); PATCH(0x2E, ax);
    PATCH(0x0A, ax);
    PATCH(0x0C, (u16)(w32x - 0x20));
    PATCH(0x0E, ax); PATCH(0x10, ax); PATCH(0x12, ax);
    PATCH(0x14, w32); PATCH(0x16, w32);
    DSW(DS_view_cx32) = (u16)(w32 >> 1);
    PATCH(0x18, (u16)(w32 >> 5)); PATCH(0x1A, (u16)(w32 >> 5));
#undef PATCH
    u16 c = (u16)(bx << 8);                               /* octant base angles for 394c / 316c */
    DSW(DS_oct_base + 0x0) = c;
    DSW(DS_oct_base + 0x2) = (u16)(c + 0x8000);
    DSW(DS_oct_base + 0x4) = (u16)(c + 0x8000);
    DSW(DS_oct_base + 0x6) = c;
    DSW(DS_oct_base + 0x8) = (u16)(c + 0x4000);
    DSW(DS_oct_base + 0xA) = (u16)(c + 0x4000);
    DSW(DS_oct_base + 0xC) = (u16)(c + 0xC000);
    DSW(DS_oct_base + 0xE) = (u16)(c + 0xC000);
}

/* 0e12:2537 frame_buffers_init — render3d.md §4.2 (the far blocks normalised to offset 0) */
void frame_buffers_init(void)
{
    DSW(DS_viewbuf_seg) = (u16)((DSW(DS_view_block) >> 4) + 1 + DSW(DS_view_block + 2));
    DSW(DS_mirrorbuf_seg) = (u16)((DSW(DS_mirror_block) >> 4) + 1 + DSW(DS_mirror_block + 2));
}

/* 0e12:25f4 view_row_tables_init — render3d.md §4.2 */
void view_row_tables_init(void)
{
    for (u16 r = 0; r < 0x60; r++) DSW(DS_row_ofs + 2 * r) = (u16)(r * 0x140);
    for (u16 r = 0; r < 0x13; r++) DSW(DS_mrow_ofs + 2 * r) = (u16)(r * 0x58);
}

/* 0e12:261c build_colour_remap — render3d.md §4.2 (32-byte remap at far DS:E5B0) */
void build_colour_remap(void)
{
    u16 es = DSW(DS_remap_ptr + 2), bx = DSW(DS_remap_ptr);
    u8 cl = 0;
    do {
        u8 al = cl, dl;
        if (DSB(DS_colour_mode) != 0 && DSB(DS_rain_level) != 0) {
            dl = 0;
            if (cl == 8) { al = dl; goto store; }
        }
        dl = 0x0F;
        if (DSB(DS_colour_mode) == 0) {                   /* dark legs / night */
            dl = 7;
            if (cl == 8) al = 0;
            else if (cl == 7) al = 8;
        }
        if (DSB(DS_snow_level) != 0 && (cl == 3 || cl == 0x0A || cl == 2)) al = dl;
    store:
        wr8(es, bx, al);
        bx++;
        cl = (u8)((cl + 1) & 0x1F);
    } while (cl);
}

/* 0e12:03d4 detail_apply — render3d.md §4.14 (view window by detail, sprite seed mask, traffic thinning) */
void detail_apply(void)
{
    u8 al = DSB(DS_detail);
    u16 cx = DSW(0xB6ED) /* seed */, bx = 0x700;
    if (al <= 1) {
        cx &= 0x5555; bx = 0x440;
        if (al != 1) { cx = 0; bx = 0x300; }
    }
    DSW(DS_view_dist) = bx;
    DSW(DS_veg_seed) = cx;
    DSW(DS__2) = (u16)(bx << 1);
    DSW(DS__4) = (u16)(bx << 2);
    u8 dl = DSB(DS_skill_level), keep = 2;
    if (dl >= 3) { keep++; if (dl >= 6) return; }
    u8 ch = 5;
    dl = keep;
    u16 o = (u16)(DSW(DS_obj_a475) << 1);
    goto dec_ch;
    for (;;) {                                            /* 0426: keep the first `keep` of every 4 objects */
        dl--;
        if ((s8)dl < 0) {
            u16 f = (u16)(DSW(DS_obj_flags + o) & 0x3F);
            if ((u8)f < 0x12 || (u8)f > 0x14) DSW(DS_obj_flags + o) = f;   /* removed (not trains) */
        }
    dec_ch:
        if (--ch == 0) { ch = 4; dl = keep; }
        o = (u16)(o - 2);
        if (!(o > 4)) return;
    }
}

/* 0e12:255e leg_state_reset — render3d.md §4.14 */
void leg_state_reset(void)
{
    u16 s;
    for (;;) {
        s = td3_random();
        u8 n = 0;
        for (u16 b = s; b; b = (u16)(b << 1)) if (b & 0x8000) n++;
        if (n >= 10 && n <= 12) break;
    }
    DSW(DS_veg_seed) = s;
    DSW(0xB6ED) = s;                                      /* seed */
    view_row_tables_init();
    life_reset();
    DSB(DS_route_index) = 0;
    for (u16 k = 0; k < 10; k++) DSB(DS_tickets + k) = 0; /* BCA6..BCAF */
    DSB(DS_clock_min) = 0;
    DSB(DS_clock_sec) = 0;
    DSB(DS_headlights) = 0;
    DSB(DS_wipers_on) = 0;
    DSB(DS_drop_ring_idx) = 0;
    DSB(DS_ticket_cooldown) = 0;
    DSW(DS_uturn_obj) = 0;
    DSB(DS_script_pos) = 0;
    DSB(DS_script_pos + 1) = 0;
    DSW(DS_opp_time) = 0;
    DSB(DS_opp_time + 2) = 0;
    DSW(DS_opp_time + 3) = 0;
    DSB(DS_opp_time + 5) = 0;
    DSW(DS_rain_level) = 0;
    DSW(DS_snow_level) = 0;
    DSW(DS_wind_level) = 0;
    DSW(DS_cam_x) = DSW(DS_sprite_x);
    DSW(DS_cam_z) = DSW(DS_sprite_z);
}

/* 0e12:0fa1 screen_shake_step — render3d.md §4.12 */
void screen_shake_step(void)
{
    u8 bl = DSB(DS_shake);
    if (bl == 0) return;
    bl--;
    DSB(DS_shake) = bl;
    if (DSB(DS_adapter) == 0x13) return;
    u16 bx = (u16)(0x0F81 + 2 * bl);
    u8 y = r3_cs8(bx), x = r3_cs8((u16)(bx + 1));
    gfx_set_display_offset(x, y);
}

/* 0e12:22fe atan2_xz — render3d.md §4.14 (CX = x, DX = z -> AX; also CX = min, DX = max) */
u16 atan2_xz(s16 x_cx, s16 z_dx, u16 *cx_min, u16 *dx_max)
{
    u16 bp = 0, cx = (u16)x_cx, dx = (u16)z_dx;
    if (r3_neg(cx)) { cx = (u16)-cx; bp = 6; }
    if (r3_neg(dx)) { dx = (u16)-dx; bp ^= 2; }
    if (!(cx < dx)) { u16 t = dx; dx = cx; cx = t; bp ^= 8; }
    u16 ax = 0x1FF;
    if (dx != cx) {
        ax = 0;
        if (dx != 0) {
            u16 q = (u16)(((u32)cx << 16) / dx);          /* cx < dx: no overflow */
            ax = (u16)((q >> 7) & 0x1FF);
        }
    }
    u16 r = (u16)(r3_cs16((u16)(0x12F6 + 2 * ax + DSW(DS_oct_half + bp))) + DSW(0xB7BF + bp) /* octant base */);
    if (cx_min) *cx_min = cx;
    if (dx_max) *dx_max = dx;
    return r;
}

/* 0e12:234b polar — render3d.md §4.14 (DS:9460 = len*sin(angle), DS:9462 = len*cos(angle)) */
void polar(u16 angle, u16 len)
{
    u16 a = (u16)((angle >> 7) & 0x1FF);
    u16 k = (u16)(a & 0x7F);
    if (a & 0x80) k = (u16)(0x80 - k);
    u16 s = (u16)(((u32)r3_cs16((u16)(0x0FF2 + 2 * k)) * len) >> 16);
    if (a & 0x100) s = (u16)-s;
    DSW(DS_dxl2) = s;                                     /* 9460 */
    u16 c = (u16)(((u32)r3_cs16((u16)(0x0FF2 + 2 * (u16)(0x80 - k))) * len) >> 16);
    u8 q = (u8)((a << 1) >> 8);
    if (q != 0 && q != 3) c = (u16)-c;
    DSW(DS_dxr2) = c;                                     /* 9462 */
}

/* 0e12:34c3 scene_prepare — render3d.md §4.14 */
void scene_prepare(void)
{
    DSW(DS_top_row) = 0x60;
    objects_update();                                     /* 4d60 (simulation) */
    crossing_gate_update();                               /* 5e70 */
    vertices_project();
    faces_sort_keys();
    if (DSB(DS_ext_view) == 0) near_face_hits();          /* 34f1 */
    sky_ground_draw();
    obj_runtime_colours_all();
    police_update();                                      /* 5ffe */
}

/* IMULDIV of 6316 / 6421: 16x16 -> 32 imul, 32/16 idiv. PORT: the original traps on overflow
 * (render3d.md §4.9 / §9.7); clamp to the 16-bit range instead. */
static s16 imuldiv(s16 a, s16 b, s16 c)
{
    if (c == 0) return 0;                                 /* (callers skip a zero divisor) */
    s32 q = ((s32)a * b) / c;
    if (q > 32767) q = 32767;                             /* PORT: clamp */
    if (q < -32768) q = -32768;
    return (s16)q;
}

#define SWAPU(a, b) do { u16 t_ = (a); (a) = (b); (b) = t_; } while (0)

/* 0e12:6316 plane_height_at_camera — render3d.md §4.9 (height of the triangle's plane at the camera X/Z) */
u16 plane_height_at_camera(u16 b_bx, u16 s_si, u16 d_di)
{
    u16 bx = b_bx, si = s_si, di = d_di;
    s16 h;
    if (R3_VY(bx) == R3_VY(si) && R3_VY(bx) == R3_VY(di)) { h = R3_VY(bx); goto out; }
    {
        u16 cx = DSW(DS_cam_x4);
        if ((u16)R3_VX(bx) > cx) {
            SWAPU(si, bx);
            if ((u16)R3_VX(bx) > cx) { SWAPU(di, bx); goto l6350; }
        }
        if (!((u16)R3_VX(di) > cx)) SWAPU(di, si);
    l6350:
        if (!((u16)R3_VX(si) > cx)) SWAPU(di, bx);
        s16 bp = R3_VX(bx);
        s16 camx = (s16)DSW(DS_cam_x4);
        s16 z1 = 0, z2 = 0, y1 = 0, y2 = 0, d;
        d = (s16)(R3_VX(si) - bp);
        if (d) z1 = imuldiv((s16)(R3_VZ(si) - R3_VZ(bx)), (s16)(camx - bp), d);
        z1 = (s16)(z1 + R3_VZ(bx)); DSS(DS_vert_base) = z1;
        d = (s16)(R3_VX(di) - bp);
        if (d) z2 = imuldiv((s16)(R3_VZ(di) - R3_VZ(bx)), (s16)(camx - bp), d);
        z2 = (s16)(z2 + R3_VZ(bx)); DSS(DS_dxl2) = z2;
        d = (s16)(R3_VX(si) - bp);
        if (d) y1 = imuldiv((s16)(camx - bp), (s16)(R3_VY(si) - R3_VY(bx)), d);
        y1 = (s16)(y1 + R3_VY(bx)); DSS(DS_dxr2) = y1;
        d = (s16)(R3_VX(di) - bp);
        if (d) y2 = imuldiv((s16)(camx - bp), (s16)(R3_VY(di) - R3_VY(bx)), d);
        y2 = (s16)(y2 + R3_VY(bx)); DSS(DS_quad_temps) = y2;
        h = 0;
        d = (s16)(z2 - z1);
        if (d) h = imuldiv((s16)(DSW(DS_cam_z4) - (u16)z1), (s16)(y2 - y1), d);
        h = (s16)(h + y1);
    }
out:
    DSS(DS_plane_height) = h;
    DSB(DS_plane_above) = 0;
    if (h > DSS(DS_cam_y_949E)) DSB(DS_plane_above)++;
    return (u16)h;
}

/* slope of 6421 (64cf / 6586): dy/dz as 16.16, saturated */
static u16 slope(s16 dy, s16 dz)
{
    if (dy == 0) return 0;
    u16 ax = (u16)(dy < 0 ? -dy : dy);
    if (dz == 0) return 0x7FFF;
    u16 cx = (u16)((u16)(dz < 0 ? -dz : dz) >> 1);
    if (ax >= cx) return ((dy ^ dz) < 0) ? 0x8001 : 0x7FFF;
    return (u16)(s16)(((s32)dy * 65536) / dz);            /* |dy| < |dz|/2: fits */
}

/* 0e12:6421 ground_slope_update — render3d.md §4.12 (terrain triangle under the car -> BCDC, BCDD) */
void ground_slope_update(void)
{
    u16 cx0 = DSW(DS_ground_height);
    if (cx0 == 0) { DSW(DS_ground_height) = DSW(DS_ground_y_prev); return; }
    DSW(DS_ground_y_prev) = cx0;
    u16 bx = DSW(DS_ground_tri), si = DSW(DS_ground_tri + 2), di = DSW(DS_ground_tri + 4);
    s16 ax = 0, cx = 0;
    if (R3_VY(bx) == R3_VY(si) && R3_VY(bx) == R3_VY(di)) goto tail;
    /* pass 1: sort by X (unsigned), interpolate Y and Z at X[si] on the edge bx-di */
    {
        u16 x = (u16)R3_VX(si);
        if (x == (u16)R3_VX(bx)) goto p1_eq;
        if (!(x > (u16)R3_VX(bx))) SWAPU(di, bx);
        if (x == (u16)R3_VX(di)) { SWAPU(bx, di); goto p1_eq; }
        if (!(x < (u16)R3_VX(di))) SWAPU(di, si);
        x = (u16)R3_VX(si);
        if (x == (u16)R3_VX(bx)) goto p1_eq;
        if (!(x > (u16)R3_VX(bx))) SWAPU(si, bx);
        s16 bp = (s16)(R3_VX(di) - R3_VX(bx));
        s16 t = (s16)(R3_VX(si) - R3_VX(bx));
        DSS(DS_vert_base) = (s16)(imuldiv(t, (s16)(R3_VY(di) - R3_VY(bx)), bp) + R3_VY(bx));
        DSS(DS_dxl2) = (s16)(imuldiv(t, (s16)(R3_VZ(di) - R3_VZ(bx)), bp) + R3_VZ(bx));
        goto p1_done;
    p1_eq:
        DSS(DS_vert_base) = R3_VY(bx);
        DSS(DS_dxl2) = R3_VZ(bx);
    p1_done:
        DSW(DS_dxr2) = slope((s16)(R3_VY(si) - DSS(DS_vert_base)), (s16)(R3_VZ(si) - DSS(DS_dxl2)));
    }
    /* pass 2: the same with X and Z exchanged (keeps the vertex order left by pass 1) */
    {
        u16 z = (u16)R3_VZ(si);
        if (z == (u16)R3_VZ(bx)) goto p2_eq;
        if (!(z > (u16)R3_VZ(bx))) SWAPU(di, bx);
        if (z == (u16)R3_VZ(di)) { SWAPU(bx, di); goto p2_eq; }
        if (!(z < (u16)R3_VZ(di))) SWAPU(di, si);
        z = (u16)R3_VZ(si);
        if (z == (u16)R3_VZ(bx)) goto p2_eq;
        if (!(z > (u16)R3_VZ(bx))) SWAPU(si, bx);
        s16 bp = (s16)(R3_VZ(di) - R3_VZ(bx));
        s16 t = (s16)(R3_VZ(si) - R3_VZ(bx));
        DSS(DS_vert_base) = (s16)(imuldiv(t, (s16)(R3_VY(di) - R3_VY(bx)), bp) + R3_VY(bx));
        DSS(DS_dxl2) = (s16)(imuldiv(t, (s16)(R3_VX(di) - R3_VX(bx)), bp) + R3_VX(bx));
        goto p2_done;
    p2_eq:
        DSS(DS_vert_base) = R3_VY(bx);
        DSS(DS_dxl2) = R3_VX(bx);
    p2_done:
        cx = (s16)slope((s16)(R3_VY(si) - DSS(DS_vert_base)), (s16)(R3_VX(si) - DSS(DS_dxl2)));
    }
    {
        s16 c = (s8)(u8)((u16)(DSS(DS_dxr2) >> 1) >> 8);  /* sar 1, high byte, cbw */
        s16 a = (s16)-(s16)(s8)(u8)((u16)(cx >> 1) >> 8);
        DSB(DS_place_rot) = DSB(DS_view_heading + 1);
        rotate_by_heading(&a, &c);
        ax = a; cx = c;
    }
tail:
    DSB(DS_ground_roll) = slope_to_roll_code((u8)ax);
    {
        s16 d = (s16)(cx - DSS(DS_pitch));
        s16 r = 0;
        if (d != 0) {
            if (d < 0) r = (d <= -0x18) ? -0x18 : d;
            else r = ((u16)d >= 0x18) ? 0x18 : d;
        }
        DSS(DS_ground_pitch) = r;
    }
}

/* 0e12:6604 rotate_by_heading — render3d.md §4.12 (AX, CX by the angle byte DS:946A, Q15 tables) */
void rotate_by_heading(s16 *ax, s16 *cx)
{
    u8 r = DSB(DS_place_rot);
    s16 x = *ax, y = *cx;
    if (r & 0x3F) {
        u16 i2 = (u16)((r & 0x3F) * 2);
        s16 sn = DSS(DS_sin64 + i2), cs = DSS(DS_cos64 + i2);
        s16 nx = (s16)((s16)(((s32)x * cs) >> 15) + (s16)(((s32)y * sn) >> 15));
        s16 ny = (s16)((s16)-(s16)(((s32)x * sn) >> 15) + (s16)(((s32)y * cs) >> 15));
        x = nx; y = ny;
    }
    r &= 0xC0;
    if (r >= 0x40) {
        if (r == 0x40)      { s16 t = x; x = y; y = (s16)-t; }
        else if (r == 0x80) { x = (s16)-x; y = (s16)-y; }
        else                { s16 t = x; x = (s16)-y; y = t; }
    }
    *ax = x; *cx = y;
}

/* 0e12:6666 slope_to_roll_code — render3d.md §4.12 (sign(al) * DS:BD01[(|al| & 7Fh) >> 2]) */
u8 slope_to_roll_code(u8 al)
{
    u8 ah = al;
    if ((s8)al < 0) al = (u8)-al;
    u8 dl = DSB(0xBD01 + ((al & 0x7F) >> 2));             /* roll code table */
    if ((s8)ah < 0) dl = (u8)-dl;
    return dl;
}

/* 0e12:76ec frame_update — render3d.md §4.3 (per frame: shake, simulation, camera, world build, 3D build) */
void frame_update(void)
{
    DSW(DS_frame_counter)++;
    DSB(DS_crash_flag) = 0;
    DSW(DS_crash_face) = 0xFFFF;
    screen_shake_step();
    auto_shift();                                         /* 095a */
    engine_sound();                                       /* 23df */
    if (DSB(DS_menu_preview) == 0) race_clock_hud();      /* 0b1d */
    DSB(0x9484)++;
    if (DSB(DS_frozen) == 0) {
        if (DSB(DS_ext_view) == 0) car_physics();         /* 0977:0008 */
        replay_update();                                  /* 0ab4:1220 */
    }
    camera_pos_update();                                  /* 6e92 */
    /* 7733: camera select */
    u16 ax = DSW(DS_view_heading), bx = DSW(DS_pitch), cx = DSW(DS_sprite_y);
    u8 dl = DSB(DS_view_roll);
    if (DSB(DS_ext_view) != 0) {
        u8 ah = DSB(DS_cam_yaw);
        if (DSB(DS_cam_moved) == 0) {                     /* chase the car heading */
            u8 bh = (u8)(DSB(DS_view_heading + 1) - ah);
            u8 step = 0;
            if ((s8)bh < 0) { if (bh < 0xF0) step = 0xFE; }
            else if (bh > 0x10) step = 2;
            if (step) { DSB(DS_cam_yaw) = (u8)(DSB(DS_cam_yaw) + step); ah = (u8)(ah + step); }
        }
        ax = (u16)(ah << 8);
        bx = DSW(DS_cam_pitch);
        cx = DSW(DS_cam_y_94A7);
        dl = DSB(DS_cam_roll_94AD);
    }
    DSW(DS_cam_heading) = ax;
    DSW(DS_cam_row) = (u16)(bx + DSW(DS_horizon_base));
    DSB(DS_cam_roll_94A0) = dl;
    DSW(DS_cam_y_949E) = cx;
    {
        u16 b = DSW(DS_sprite_drift_z);
        if (r3_neg(b)) b = (u16)-b;
        if (r3_neg(ax)) b = (u16)-b;
        DSW(DS_sprite_drift_z) = b;
        b = DSW(DS_sprite_drift_x);
        if (r3_neg(b)) b = (u16)-b;
        if (r3_neg((u16)(ax - 0x4000))) b = (u16)-b;
        DSW(DS_sprite_drift_x) = b;
    }
    DSW(DS_page_cur) = 1;
    gfx_set_draw_page(1);
    world_build_visible();                                /* 70cd */
    scene_prepare();                                      /* 34c3 */
}

/* 0e12:77d5 frame_draw — render3d.md §4.12 */
void frame_draw(void)
{
    DSW(DS_page_cur) = 1;
    gfx_set_draw_page(1);
    draw_faces_and_sprites();                             /* 323e */
    if (DSB(DS_frozen) == 0) {
        ground_slope_update();                            /* 6421 */
        if (DSB(DS_surface_under_car) == 0x0E &&
            !((s16)(DSW(DS_sprite_y) - DSW(DS_car_eye_height)) > DSS(DS_ground_height)))
            DSB(DS_crash_flag) = 1;                       /* in water */
        if (DSB(DS_ext_view) != 0 && DSB(DS_frozen) == 0)
            DSW(DS_cam_y_94A7) = (u16)(DSW(DS_ground_height) + DSW(DS_cam_dist));
        if (DSB(DS_ext_view) == 0) {
            if (DSB(DS_debug_keys) != 0) DSB(DS_invulnerable) = 2;
            u8 al = DSB(DS_invulnerable);
            if (al) {
                al--;
                if (DSB(DS_surface_under_car) == 0x0E || DSB(DS_clock_running) != 0) DSB(DS_invulnerable) = al;
                DSB(DS_crash_flag) = 0;
            }
        }
        if (DSB(DS_crash_flag) != 0 && DSB(DS_ext_view) == 0) {
            DSB(DS_crash_water) = 0;
            if (DSB(DS_surface_under_car) == 0x0E) { DSB(DS_crash_water) = 1; DSB(DS_water_anim) = 1; }
            crash_start();                                /* 0f31 */
        }
        enh_ov_begin();                                   /* ENH: overlays recorded for the enhanced view */
        cockpit_overlays();                               /* 46c0 */
        if (DSB(DS_crashed) != 0) replay_start();         /* 4c51 */
        mirror_frame_draw();                              /* 7c21 */
        enh_ov_pixels();                                  /* ENH: the mirror rim in V */
    }
    DSW(DS_page_cur) = 0;
    gfx_set_draw_page(0);
    enh_frame_drawn();                                    /* ENH: the frame becomes the newest snapshot */
}

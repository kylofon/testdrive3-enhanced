#define RENDER3D_INTERNAL
#include "game/game.h"
#include "enhanced/enhanced.h"

/* render3d: objects and vehicles (render3d.md §4.14): runtime face colours, parked / moving vehicle
 * emission, lighthouse beam, crossing-gate arms, the folding plate. Object arrays are u16[160] indexed by
 * the slot byte offset bx = 2*i, as in the original. All state is in mem[] at its original address. */

/* ---- file-internal functions (register routines: parameters named after the registers) ---- */
/* 0e12:4db6 obj_face_runtime_colours (BX = 2*slot; the original's CX loop limit is replaced, see there) */
void obj_face_runtime_colours(u16 slot2_bx);
/* 0e12:539d obj_emit_vehicle (BX = 2*slot, DX = Manhattan distance, ES = objects set segment) */
void obj_emit_vehicle(u16 slot2_bx, u16 dist_dx, u16 oset_es);

/* ENH: the far LOD (vehicles, some buildings) only in the original picture (--classic) or when --far-lod asks
 * for it: the enhanced view is drawn at a much higher resolution, where the far models' missing detail shows,
 * and the cost is no concern. */
static bool far_lod(u16 dist_dx) { return dist_dx > 0x200 && (!enh_enabled() || enh_far_lod_enabled()); }

/* Model lookup, inline in 52a3 / 4db6 (5ad4 / 5cf2 too): flags & 3Fh > 3 -> objects set (far DS:E54C, table of
 * offsets relative to its start); 3 -> DS:D7A4, 2 -> DS:CEBC (opponent POBs); 0, 1 -> far DS:CEA4 (player). */
static FarPtr obj_model_ptr(u16 slot2_bx)
{
    u16 m = DSW(DS_obj_flags + slot2_bx) & 0x3F;
    if (m > 3) {
        u16 es = DSW(DS_objects_set + 2);
        u16 base = DSW(DS_objects_set);
        u16 si = rd16(es, (u16)(base + (u16)(m << 1)));
        return far_make(es, (u16)(si + base));
    }
    if (m == 3) return ds_ptr(0xD7A4);
    if (m == 2) return ds_ptr(0xCEBC);
    return ds_far(DS_car_pob);
}

/* 0e12:4590 plate_fold_down — render3d.md §4.14 (ES:BP = &face.w3 of a type-1Ah face; keeps BX, CX) */
void plate_fold_down(FarPtr w3_es_bp)
{
    u16 es = w3_es_bp.seg, bp = w3_es_bp.off;
    sfx_play_ax(4);
    DSB(DS_shake) = 5;
    DSB(DS_bump_frames) = 3;
    bp = (u16)(bp + 2);                                  /* colour word */
    u16 dx = rd16(es, bp);
    if ((u8)(dx >> 8) != 6) bp = (u16)(bp - 10);         /* the pair's first record has colour hi 6 */
    bp = (u16)(bp - 8);                                  /* R.w0 */
    u16 bx = rd16(es, bp);
    wr16(es, bp, (u16)(rd16(es, bp) & 0xE7FF));
    bx = (u16)((bx & 0x7FF) << 1);
    u16 ax = (u16)(R3_VY(bx) + 6);
    bp = (u16)(bp + 6);                                  /* R.w3: type 3 */
    bx = (u16)((rd16(es, bp) & 0x7FF) | 0x1800);
    wr16(es, bp, bx);
    bp = (u16)(bp + 4);                                  /* S.w0 */
    wr16(es, bp, (u16)(rd16(es, bp) & 0xE7FF));
    bx = (u16)((rd16(es, bp) & 0x7FF) << 1);
    dx = (u16)(R3_VY(bx) - ax);
    R3_VY(bx) = (s16)ax;
    R3_VZ(bx) = (s16)(R3_VZ(bx) + dx);
    bp = (u16)(bp + 2);                                  /* S.v1 */
    bx = (u16)((rd16(es, bp) & 0x7FF) << 1);
    dx = (u16)R3_VY(bx);
    ax++;
    R3_VY(bx) = (s16)ax;
    dx = (u16)(dx - ax);
    R3_VZ(bx) = (s16)(R3_VZ(bx) + dx);
    bp = (u16)(bp + 4);                                  /* S.w3: type 3 */
    bx = (u16)((rd16(es, bp) & 0x7FF) | 0x1800);
    wr16(es, bp, bx);
}

/* 0e12:4d6d obj_runtime_colours_all — render3d.md §4.14 */
void obj_runtime_colours_all(void)
{
    DSB(DS_frame8)++;
    DSB(DS_tlight_lamp) = 0;                             /* CH = 0 (CL left over: see 4db6) */
    if (DSB(DS_ext_view) == 0) {
        if (--DSB(DS_light_timer) == 0) {
            u8 al = (u8)((DSB(DS_light_phase) + 1) & 3);
            DSB(DS_light_phase) = al;
            DSB(DS_light_timer) = (al & 1) ? 9 : 0x5A;
        }
    }
    DSB(DS_wiper_frame)++;
    u16 bx = (u16)(DSW(DS_obj_count) << 1);
    for (;;) {
        bx = (u16)(bx - 2);
        if (r3_neg(bx)) break;
        if (DSW(DS_obj_face_count + bx) != 0) obj_face_runtime_colours(bx);
    }
}

/* 0e12:4db6 obj_face_runtime_colours — render3d.md §4.14
 * Overrides the colour word of the object's leading faces of types 4..13 (type 13 animates the model's
 * wiper vertices and ends the scan). */
void obj_face_runtime_colours(u16 slot2_bx)
{
    u16 bx = slot2_bx;
    u16 es = r3_face_seg();
    u16 di = (u16)(DSW(DS_obj_face_base + bx) * 10 + DSW(DS_face_block));
    /* PORT: the original's `loop` limit is CX, never initialised (CL left over from 373c, then carried from
     * one object to the next; 2*nv after a wiper face). The port scans at most the object's own faces
     * (B379[i]); the scan still ends at the first face whose type is not 4..13. */
    u16 n = DSW(DS_obj_face_count + bx);
    for (;;) {
        u16 ax = rd16(es, (u16)(di + 6));
        u8 ah = (u8)((ax >> 8) & 0xF8);                  /* type << 3 */
        if (ah > 0x68) return;
        if (ah == 0x68) {                                /* 13: wipers (model data, all instances) */
            FarPtr p = obj_model_ptr(bx);
            u16 mes = p.seg, si = p.off;
            u16 wd = (u16)(p.off + 8);
            u16 w = rd16(mes, si);
            u8 nf = (u8)w, nv = (u8)(w >> 8);
            si = (u16)(si + 2);
            w = rd16(mes, si);
            u8 al = (u8)((u8)w + (u8)(w >> 8) + nf);     /* nc + nk + nf, 8-bit */
            si = (u16)(si + 6);
            si = (u16)(si + (u16)(al << 3));
            u16 bp = (u16)(nv << 1);
            si = (u16)(si + bp + bp + bp);               /* anim = p + 8 + 6nv + 8*(u8)(nf+nc+nk) */
            u16 f = (u16)((DSB(DS_wiper_frame) & 7) << 2);
            if (DSB(DS_rain_level) == 0 && DSB(DS_snow_level) == 0) f = 0;
            si = (u16)(si + f + f + f);                  /* 12 bytes per frame */
            for (u16 k = 0; k < 3; k++) {                /* A[0..1], B[0..1], C[0..1] */
                wr16(mes, wd, rd16(mes, si)); si = (u16)(si + 2); wd = (u16)(wd + 2);
                wr16(mes, wd, rd16(mes, si)); si = (u16)(si + 2); wd = (u16)(wd + 2);
                wd = (u16)(wd + bp - 4);
            }
            return;
        }
        if (ah == 0x60) {                                /* 12: crossing flasher */
            ax = 0x0808;
            if (DSB(DS_gate_pos) != 0 && ((DSW(DS_frame_counter) ^ di) & 2)) ax = 0x0C0C;
        } else if (ah == 0x58) {                         /* 11: traffic light lamp */
            u8 dl = DSB(DS_light_phase), dh = DSB(DS_tlight_lamp);
            bool on;
            if (dh == 0) { ax = 0x0A0A; on = dl == 0; }
            else if (dh == 3) { ax = 0x0A0A; on = dl == 2; }
            else if (dh == 1) { ax = 0x0E0E; on = dl == 1; }
            else if (dh == 4) { ax = 0x0E0E; on = dl == 3; }
            else if (dh == 2) { ax = 0x0C0C; on = dl >= 2; }
            else { ax = 0x0C0C; on = dl <= 1; }
            if (!on) ax = 0;
            dh++;
            if (dh > 5) dh = 0;
            DSB(DS_tlight_lamp) = dh;
        } else if (ah == 0x50) {                         /* 10: police beacon */
            if (!(DSW(DS_obj_flags + bx) & 0x4000)) goto next;   /* keep the emitted colour */
            ax = (u16)(DSW(DS_beacon_colour) + 3);
            if (ax > 0x0F) ax = 9;
            DSW(DS_beacon_colour) = ax;
            ax = (u16)((ax & 0xFF) | (ax << 8));
        } else if (ah == 0x48) {                         /* 9: brake light */
            ax = (DSW(DS_obj_flags + bx) & 0x8000) ? 0x0C0C : 0x0404;
        } else if (ah == 0x40) {                         /* 8: strobe */
            ax = (DSB(DS_frame8) & 7) == 0 ? 0x0F0F : 0;
        } else if (ah == 0x38) {                         /* 7: random blink */
            ax = (DSB(DS_rand_lo) & 4) ? 0x0F0F : 0;
        } else {                                         /* 4, 5, 6: lamp by viewing side */
            if (ah < 0x20) return;
            u16 bp;
            if (ah > 0x28) { ax = 0x0E0E; bp = 0; }
            else if (ah < 0x28) { ax = 0x0404; bp = 0x0F0F; }
            else { ax = 0x0F0F; bp = 0; }
            u16 dx = (u16)(DSW(DS_obj_heading + bx) & 0xFF00);
            u16 si = (u16)((rd16(es, di) & 0x7FF) << 1);
            dx = (u16)(dx + 0x4000);
            dx = (u16)(dx - DSW(DS_cam_heading));
            dx = (u16)(dx - R3_SX(si));
            if ((s8)(u8)((dx >> 8) + DSB(DS_view_cx_hi)) >= 0) ax = bp;
        }
        wr16(es, (u16)(di + 8), ax);
    next:
        di = (u16)(di + 10);
        if (--n == 0) return;                            /* PORT: see above */
    }
}

/* 0e12:4fc7 parked_vehicles_emit — render3d.md §4.14 */
void parked_vehicles_emit(void)
{
    u16 oes = DSW(DS_objects_set + 2);
    DSW(DS_vert_count) = DSW(DS_static_vert_end);
    u16 old = DSW(DS_face_count);
    DSW(DS_face_count) = DSW(DS_static_face_end);
    if (DSB(DS_menu_preview) == 0) {
        u16 bx = (u16)(DSW(DS_obj_first_static) << 1);
        for (;;) {
            bx = (u16)(bx - 2);
            if (r3_neg(bx)) break;
            if (bx == 0 && DSB(DS_ext_view) == 0) continue;     /* the player's car in cockpit view */
            u8 ch = (u8)(DSW(DS_obj_flags + bx) >> 8);
            if (!(ch & 0x10) || (ch & 0x20)) continue;          /* parked: 1000h set, 2000h clear */
            u16 ax = (u16)(DSW(DS_obj_x + bx) - DSW(DS_cam_x));
            ax = (u16)((s16)(u16)(ax << 1) >> 1);               /* 15-bit difference, sign-extended */
            ax = (u16)(ax + DSW(DS_view_dist));
            if (ax > DSW(DS__2)) goto clear;
            ax = (u16)(ax - DSW(DS_view_dist));
            if (r3_neg(ax)) ax = (u16)-ax;
            u16 dx = ax;
            ax = (u16)(DSW(DS_obj_z + bx) - DSW(DS_cam_z));
            ax = (u16)((s16)(u16)(ax << 2) >> 2);               /* 14-bit */
            ax = (u16)(ax + DSW(DS_view_dist));
            if (ax > DSW(DS__2)) goto clear;
            ax = (u16)(ax - DSW(DS_view_dist));
            if (r3_neg(ax)) ax = (u16)-ax;
            dx = (u16)(dx + ax);
            if (ch == 0x1F) {                                   /* lightning object */
                if (DSB(DS_rain_level) < 6) goto clear;
                u16 cx = (u16)((u16)(u8)((u8)bx << 1) << 8);
                cx = (u16)(cx + DSW(DS_rand_lo));
                if (cx & 0x05D9) goto clear;
                DSB(DS_sky_flash) = 2;
                sfx_play_ax(9);
                /* TODO(verify): ES is assumed preserved across the far call to 0c1c:111d (the port passes the
                 * objects set segment explicitly). */
            }
            obj_emit_vehicle(bx, dx, oes);
            continue;
        clear:
            DSW(DS_obj_vert_base + bx) = 0;
            DSW(DS_obj_vert_count + bx) = 0;
            DSW(DS_obj_face_base + bx) = 0;
            DSW(DS_obj_face_count + bx) = 0;
        }
    }
    u16 cur = DSW(DS_face_count);
    DSW(DS_face_count) = old;
    DSW(DS_parked_face_end) = cur;
    DSW(DS_parked_vert_end) = DSW(DS_vert_count);
}

/* 0e12:52a3 obj_emit_vehicle_moving — render3d.md §4.14
 * Returns CX; *faces_es_si = ES:SI for the caller's model_emit_faces. */
u16 obj_emit_vehicle_moving(u16 slot2_bx, u16 dist_dx, FarPtr *faces_es_si)
{
    u16 bx = slot2_bx;
    DSW(DS_place_x) = DSW(DS_obj_x + bx);
    DSW(DS_place_z) = DSW(DS_obj_z + bx);
    DSW(DS_place_y) = (u16)(DSW(DS_obj_y8 + bx) >> 3);
    DSB(DS_place_rot) = (u8)(DSW(DS_obj_heading + bx) >> 8);
    DSB(DS_place_shear) = (u8)(DSW(DS_obj_pitch + bx) >> 8);
    DSW(DS_cur_obj_slot1) = (u16)(bx + 2);
    FarPtr p = obj_model_ptr(bx);
    u16 es = p.seg, si = p.off;
    u8 ch = rd8(es, si);                                 /* nf */
    si++;
    u8 cl = rd8(es, si);                                 /* nv */
    si = (u16)(si + 7);                                  /* near vertices at +8 */
    if (far_lod(dist_dx)) {                              /* far LOD (ENH) */
        si = (u16)(si - 4);
        ch = rd8(es, si);
        si++;
        cl = rd8(es, si);
        si++;
        si = (u16)(si + rd16(es, si));
        si = (u16)(si + 2);
    }
    u16 pushed_cx = (u16)(ch << 8 | cl);
    DSB(DS_vert_overflow) = 0;
    u16 cx = cl;
    if (DSW(DS_obj_vert_base + bx) == DSW(DS_vert_count) && cx == DSW(DS_obj_vert_count + bx)
        && DSB(DS_obj_full_transform) == 0) {
        /* Translated-vertex cache: reserve nv vertices, transform only vertices 0 and 1 again, move the rest
         * by this frame's movement. Reachable only for object 128 in cockpit view (509b sets BCC3 = 0 when
         * DS:948B == low byte of 2*i and skips i = 0). Faithful outputs: CX = nf:nv (not converted) and
         * SI = 2*(945E + B0F9[i]) (a vertex offset, not the face array); the caller then uses them as
         * they are unless it reuses last frame's face records. */
        DSB(DS_place_nverts) = 2;
        model_emit_vertices(far_make(es, si), cx);
        si = (u16)(DSW(DS_vert_base) + 2);
        cx = (u16)(DSW(DS_obj_vert_count + bx) - 2);
        si = (u16)(si << 1);
        u16 ax = (u16)(DSW(DS_obj_move_dx) << 2);
        u16 dx = (u16)(DSW(DS_dz) << 2);
        u16 bp = (u16)(DSW(DS_dy) >> 3);
        do {                                             /* `loop`: CX = 0 would run 65536 times (nv = 2) */
            R3_VX(si) = (s16)(R3_VX(si) + ax);
            R3_VZ(si) = (s16)(R3_VZ(si) + dx);
            R3_VY(si) = (s16)(R3_VY(si) + bp);
            si = (u16)(si + 2);
        } while (--cx);
        if (faces_es_si) *faces_es_si = far_make(es, si);
        return pushed_cx;
    }
    DSB(DS_place_nverts) = cl;
    si = model_emit_vertices(far_make(es, si), cx);
    if (faces_es_si) *faces_es_si = far_make(es, si);
    return ch;
}

/* 0e12:539d obj_emit_vehicle — render3d.md §4.14 (always the objects set, ES preloaded by 4fc7) */
void obj_emit_vehicle(u16 slot2_bx, u16 dist_dx, u16 oset_es)
{
    u16 bx = slot2_bx, es = oset_es;
    u16 base = DSW(DS_objects_set);
    u16 si = (u16)(base + (u16)((DSW(DS_obj_flags + bx) & 0x3F) << 1));
    si = (u16)(rd16(es, si) + base);
    u8 ch = rd8(es, si);
    si++;
    u8 cl = rd8(es, si);
    si = (u16)(si + 7);
    if (far_lod(dist_dx)) {                              /* far LOD (ENH) */
        si = (u16)(si - 4);
        ch = rd8(es, si);
        si++;
        cl = rd8(es, si);
        si++;
        si = (u16)(si + rd16(es, si));
        si = (u16)(si + 2);
    }
    u16 cx = cl;
    if (DSW(DS_obj_vert_base + bx) == DSW(DS_vert_count) && DSW(DS_obj_face_base + bx) == DSW(DS_face_count)
        && cx == DSW(DS_obj_vert_count + bx)) {
        /* same slots as last frame: reuse the vertex and face ranges */
        DSW(DS_vert_count) = (u16)(DSW(DS_vert_count) + DSW(DS_obj_vert_count + bx));
        DSW(DS_face_count) = (u16)(DSW(DS_face_count) + DSW(DS_obj_face_count + bx));
        return;
    }
    DSB(DS_vert_overflow) = 0;
    DSW(DS_place_x) = DSW(DS_obj_x + bx);
    DSW(DS_place_z) = DSW(DS_obj_z + bx);
    DSW(DS_place_y) = (u16)(DSW(DS_obj_y8 + bx) >> 3);
    DSB(DS_place_rot) = (u8)(DSW(DS_obj_heading + bx) >> 8);
    DSB(DS_place_shear) = (u8)(DSW(DS_obj_pitch + bx) >> 8);
    DSW(DS_cur_obj_slot1) = (u16)(bx + 2);
    DSB(DS_place_nverts) = cl;
    si = model_emit_vertices(far_make(es, si), cx);
    if (DSB(DS_vert_overflow) != 0) {
        DSW(DS_obj_face_base + bx) = 0;
        DSW(DS_obj_face_count + bx) = 0;
        return;
    }
    DSW(DS_model_face_cut) = 0;
    model_emit_faces(far_make(es, si), ch);
}

/* 0e12:5df9 lighthouse_rotate — render3d.md §4.14 */
void lighthouse_rotate(void)
{
    u16 si = DSW(DS_lighthouse_slot);
    if (si == 0xFFFF) return;
    u16 ax = (u16)((DSW(DS_frame_counter) & 0xFF) << 9);    /* one turn per 128 frames */
    u16 dx = (u16)(DSB(DS_lighthouse_speed) << 1);          /* 95D5: beam radius / 16 */
    if (dx == 0) return;
    dx = (u16)(dx << 3);
    polar(ax, dx);
    u16 bx = si;                                            /* slot */
    si = DSW(DS_obj_face_base + bx);
    si = (u16)(si * 10 + DSW(DS_face_block));
    si = (u16)((r3_f16(si) & 0x7FF) << 1);                  /* w0 of the object's first face */
    ax = (u16)((DSW(DS_obj_x + bx) << 2) + DSW(DS_dxl2));
    R3_VX(si) = (s16)ax;
    R3_VX(si + 2) = (s16)ax;
    ax = (u16)((DSW(DS_obj_z + bx) << 2) + DSW(DS_dxr2));
    R3_VZ(si) = (s16)ax;
    R3_VZ(si + 2) = (s16)ax;
}

/* 0e12:5f57 crossing_gate_arm_place — render3d.md §4.14 (SI = 2*gate slot) */
void crossing_gate_arm_place(u16 slot2_si)
{
    u16 ax = 0x1000;
    u8 dl = (u8)(DSB(DS_gate_pos) - 0x10);
    if ((s8)dl >= 0) ax = (u16)(ax + (u16)(dl << 8));      /* 22.5 deg .. 90 deg */
    u16 dx = DSB(DS_gate_speed);                            /* 95D6: arm length */
    if (dx == 0) return;
    polar(ax, dx);
    u16 bx = slot2_si;
    u16 si = (u16)(DSW(DS_obj_face_base + bx) + 2);         /* third face: arm tip vertices v, v+1 */
    si = (u16)(si * 10 + DSW(DS_face_block));
    si = (u16)((r3_f16(si) & 0x7FF) << 1);
    ax = (u16)(R3_VY(si + 6) + DSW(DS_dxr2));               /* pivot height = vertex v+3 */
    R3_VY(si) = (s16)ax;
    R3_VY(si + 2) = (s16)(ax + 8);
    u16 cx = DSW(DS_dxl2);
    u16 hd = DSW(DS_obj_heading + bx);
    u16 bp = 8;
    if (hd & 0x8000) { cx = (u16)-cx; bp = (u16)-bp; }
    if (hd & 0x4000) {
        cx = (u16)-cx; bp = (u16)-bp;
        ax = (u16)((DSW(DS_obj_z + bx) << 2) - cx);
        R3_VZ(si) = (s16)ax;
        R3_VZ(si + 2) = (s16)(ax + bp);
    } else {
        ax = (u16)((DSW(DS_obj_x + bx) << 2) - cx);
        R3_VX(si) = (s16)ax;
        R3_VX(si + 2) = (s16)(ax + bp);
    }
}

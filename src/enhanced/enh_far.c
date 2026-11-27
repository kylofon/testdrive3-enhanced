/* Enhanced renderer: the far ring (ENHANCED.md "Draw distance"). The game builds the 3, 6 or 10 map cells nearest
 * the camera (world_build_visible, its per-octant table); here, for every game frame, the cells within
 * enh_draw_dist cells of the camera that it did not build are built as well, with their static objects, from
 * the same tile and object models: model_place / vertex_rotate / model_emit_faces read-only, into the snapshot's
 * own arrays. Nothing of the game's state is written. The faces are sorted farthest first by the original's key
 * rule at the snapshot's camera (per game frame, so coplanar faces do not swap between displayed frames). */
#include "enh_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int enh_draw_dist = ENH_DEFAULT_DRAW_DIST;

/* the cells of the game's last world build (enh_world_begin / enh_world_cell from world_build_visible) */
static bool game_cell[512];
u16 enh_far_lh_slot = 0xFFFF;

void enh_world_begin(void) { memset(game_cell, 0, sizeof game_cell); }
void enh_world_cell(u16 c) { game_cell[c & 0x1FF] = true; }

static EnhSnap *F;
static u8 far_outlier_logged_leg = 0xFF;

void polar(u16 angle, u16 len);                            /* render.c */

static void far_lighthouse(EnhObj *eo, u8 model, int px, int pz)
{
    if (eo->far_count == 0) return;
    u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
    if (es == 0) return;
    u16 si = (u16)(rd16(es, (u16)(set + 2 * model)) + set);
    int nf = rd8(es, si), nv = rd8(es, (u16)(si + 1));
    if (nf <= 0 || nv <= 1) return;
    u16 face0 = (u16)(si + 4 + 6 * nv);                   /* first face, 4-byte model format */
    u16 v0 = rd16(es, face0) & 0x7FF;
    if (v0 + 1 >= (u16)nv) return;
    u16 ax = (u16)((DSW(DS_frame_counter) & 0xFF) << 9);
    u16 dx = (u16)(DSB(DS_lighthouse_speed) << 1);
    if (dx == 0) return;
    dx = (u16)(dx << 3);
    polar(ax, dx);
    int cx4 = (int)F->cam_x * 4, cz4 = (int)F->cam_z * 4;
    int x4 = px * 4 - cx4 + (s16)DSW(DS_dxl2);
    int z4 = pz * 4 - cz4 + (s16)DSW(DS_dxr2);
    u16 b = eo->far_base;
    F->fvx[b + v0] = x4; F->fvz[b + v0] = z4;
    F->fvx[b + v0 + 1] = x4; F->fvz[b + v0 + 1] = z4;
}

/* x4 units from the camera the ring may reach (one cell past its radius, for models that overhang their cell).
 * Offsets are 32-bit, so this is no longer tied to the 16-bit wrap of the game's vertex arrays. */
static int F_reach;

/* The leg map is a torus: X wraps at 8000h (32 cells), Z at 4000h (16 cells). Return d moved to the nearest
 * image of the object, in (-period/2, period/2]. Cell centres and positions are in the same position units. */
static int wrap_delta(int d, int period)
{
    int half = period / 2;
    d %= period;
    if (d < 0) d += period;
    return d >= half ? d - period : d;
}

static void emit(u16 es, u16 a, int nf, int nv, int px, int pz, u16 py, u8 rot, s8 shear);

/* 0e12:7653 vertex_rotate on the model's arrays A (height), B, C at si, stride 2 nv */
static void rotate(u16 es, u16 si, u16 stride, u8 rot, s8 shear, s16 *a_out, s16 *b_out, s16 *c_out)
{
    s16 dx = (s16)rd16(es, si), ax = (s16)rd16(es, (u16)(si + stride)), cx = (s16)rd16(es, (u16)(si + 2 * stride));
    if (shear) {
        u8 n = (u8)(shear < 0 ? -shear : shear);
        s16 v = (s16)(ax << 1), bp = n >= 15 ? (s16)(v < 0 ? -1 : 0) : (s16)(v >> n);
        if (shear >= 0) bp = (s16)-bp;
        dx = (s16)(dx + bp);
    }
    u8 r = rot;
    if (r & 0x3F) {
        u16 i2 = (u16)((r & 0x3F) * 2);
        s16 sn = DSS(DS_sin64 + i2), cs = DSS(DS_cos64 + i2);
        s16 nb = (s16)((s16)(((s32)ax * cs) >> 15) + (s16)(((s32)cx * sn) >> 15));
        s16 nc = (s16)((s16)-(s16)(((s32)ax * sn) >> 15) + (s16)(((s32)cx * cs) >> 15));
        ax = nb; cx = nc;
        r &= 0xC0;
    }
    if (r >= 0x40) {
        if (r == 0x40)      { s16 t = ax; ax = cx; cx = (s16)-t; }
        else if (r == 0x80) { ax = (s16)-ax; cx = (s16)-cx; }
        else                { s16 t = ax; ax = (s16)-cx; cx = t; }
    }
    *a_out = dx; *b_out = ax; *c_out = cx;
}

/* model_emit_faces' colour pair of the codes c1, c2 (w1, w2 >> 11) */
static u16 face_pair(u8 c1, u8 c2)
{
    u16 fseg = DSW(DS_face_block + 2), remap = DSW(DS_remap_ptr);
    u8 cl = rd8(fseg, (u16)(remap + c1)), ch = rd8(fseg, (u16)(remap + c2));
    if (DSW(DS_video_mode) != 0x13) return (u16)(((u16)ch << 8 | cl) & 0x0F0F);
    if (!(ch & 0x10) && !(cl & 0x10)) return DSW(DS_colour_pairs + 2 * (u8)((u8)(ch << 4) | cl));
    if (ch & 0x10) ch = DSB(DS_colour_direct + (ch & 0x0F));
    if (cl & 0x10) cl = DSB(DS_colour_direct + (cl & 0x0F));
    return (u16)((u16)ch << 8 | cl);
}

/* a sprite of the far ring (not in the game's list: instance FFFFh) */
static void add_sprite(u16 inst, u16 id, int x, u16 y, int z)
{
    if (F->nspr >= ENH_MAX_SPRITES_ALL) return;
    int cx4 = (int)F->cam_x * 4, cz4 = (int)F->cam_z * 4;
    if (abs(x * 4 - cx4) > F_reach || abs(z * 4 - cz4) > F_reach) return;
    EnhSpr *e = &F->spr[F->nspr++];
    e->inst = inst;
    e->id = id;
    e->x = x; e->y = y; e->z = z;
}

/* model_place's sprite children of a tile (trees, signs...): after the faces, placed by quarter turns, those
 * from id 12h on thinned by the vegetation seed rotated by the cell's position, exactly as the game does, so
 * that a cell looks the same when the game builds it itself */
static void children(u16 es, u16 si, int px, int pz, u16 py, u8 rot)
{
    int nc = rd8(es, (u16)(si + 2));
    u16 c = (u16)(si + 4 + rd8(es, si) * 8 + rd8(es, (u16)(si + 1)) * 6);
    u16 di = DSW(DS_veg_seed);
    u8 cnt = (u8)((u16)(px + pz) >> 12);
    if (cnt) di = (u16)((di << cnt) | (di >> (16 - cnt)));
    for (int k = 0; k < nc; k++, c = (u16)(c + 8)) {
        u16 id = rd16(es, c);
        if ((u8)id >= 0x12) {
            u16 carry = di & 1;
            di = (u16)((di >> 1) | (carry << 15));
            if (!carry) continue;
        }
        u16 x = rd16(es, (u16)(c + 2)), z = rd16(es, (u16)(c + 4)), h = rd16(es, (u16)(c + 6));
        if (rot >= 0x40) {
            if (rot == 0x40)      { u16 t = x; x = z; z = (u16)-t; }
            else if (rot == 0x80) { x = (u16)-x; z = (u16)-z; }
            else                  { u16 t = x; x = (u16)-z; z = t; }
        }
        if (id) add_sprite(0xFFFF, id, x + px, (u16)(h + py), z + pz);
    }
}

/* obj_model_ptr (render_object.c): the model of a moving vehicle by the low bits of its flags. Vehicles use the
 * 8-byte model header (vertices at +8), unlike the static objects model_place places at +4. Unless --far-lod is
 * on, this is the same near model the game builds when the car is in its window. */
static FarPtr vehicle_model(u16 flags)
{
    u16 m = flags & 0x3F;
    if (m > 3) {
        u16 es = DSW(DS_objects_set + 2), base = DSW(DS_objects_set);
        return far_make(es, (u16)(rd16(es, (u16)(base + 2 * m)) + base));
    }
    if (m == 3) return ds_ptr(0xD7A4);
    if (m == 2) return ds_ptr(0xCEBC);
    return ds_far(DS_car_pob);
}

/* The 8-byte model header's face count, as obj_emit_vehicle reads it: the near model's nf / nv at +0 / +1 with
 * vertices at +8, or, with --far-lod beyond 200h (Manhattan), the far model's at +4 / +5, its vertex offset at
 * +6 and vertices at +8 + offset. Writes the vertex count and the vertex offset; returns the face count. */
static int model_header(FarPtr p, u16 dist, int *nv, u16 *voff)
{
    if (enh_far_lod_enabled() && dist > 0x200) {
        *nv = rd8(p.seg, (u16)(p.off + 5));
        *voff = (u16)(p.off + 8 + rd16(p.seg, (u16)(p.off + 6)));
        return rd8(p.seg, (u16)(p.off + 4));
    }
    *nv = rd8(p.seg, (u16)(p.off + 1));
    *voff = (u16)(p.off + 8);
    return rd8(p.seg, p.off);
}

/* model_place (0e12:72f8) without its sprite children: model al of the tiles (kind 0) or the objects (kind 1) at
 * place x / z (position units), height y, rotation and shear */
static void place(u8 al, int kind, int px, int pz, u16 py, u8 rot, s8 shear)
{
    u16 es, set, bx, cut = 0;
    if (kind) {
        es = DSW(DS_objects_set + 2); set = DSW(DS_objects_set);
        bx = (u16)(set + 2 * al);
    } else {
        if (al >= 0x40) { es = DSW(DS_tiles_scene + 2); set = DSW(DS_tiles_scene); al = (u8)(al - 0x40); }
        else            { es = DSW(DS_tiles_shared + 2); set = DSW(DS_tiles_shared); }
        bx = (u16)(set + 2 * al);
        u16 e = rd16(es, bx);
        if (e <= 0x10) { cut = e; bx = (u16)(bx - 2); }       /* alias of the previous model */
    }
    if (es == 0) return;
    u16 si = (u16)(rd16(es, bx) + set);
    int nf = rd8(es, si), nv = rd8(es, (u16)(si + 1));
    if (!kind) children(es, si, px, pz, py, rot);
    if (nf == 0 || nv == 0) return;
    nf -= cut;
    emit(es, (u16)(si + 4), nf, nv, px, pz, py, rot, shear);
}

/* the vertices at a (arrays A, B, C) and the nf faces after them of a model placed at px, pz (position
 * units), height py; dropped whole if a vertex would lie beyond the ring's reach of the camera */
static void emit(u16 es, u16 a, int nf, int nv, int px, int pz, u16 py, u8 rot, s8 shear)
{
    if (nf <= 0 || nv <= 0 || F->nfv + nv > ENH_MAX_FAR_VERTS || F->nff + nf > ENH_MAX_FAR_FACES) return;
    int base = F->nfv;
    u16 stride = (u16)(nv * 2);
    int cx4 = (int)F->cam_x * 4, cz4 = (int)F->cam_z * 4;
    int x4 = (int)px * 4, z4 = (int)pz * 4;
    for (int k = 0; k < nv; k++) {
        s16 va, vb, vc;
        rotate(es, (u16)(a + 2 * k), stride, rot, shear, &va, &vb, &vc);
        int vy = (int)va + py;
        if (vy < (int)py - 512) vy = (int)py - 512;
        F->fvy[base + k] = (s16)vy;
        F->fvx[base + k] = vb + x4 - cx4;
        F->fvz[base + k] = vc + z4 - cz4;
        if (abs(F->fvx[base + k]) > F_reach || abs(F->fvz[base + k]) > F_reach) return;   /* nothing kept */
    }

    /* Workaround for outlier vertices in original TD3 model data (not modified): some models contain an
     * extreme Y vertex far above the rest (e.g. barn model 0x38, A[0]=15133). In the far ring these project
     * far above screen and rasterize as sky-filling "monoliths", especially visible at night on OR faces.
     * Skip faces referencing such outliers; keep the rest of the model intact. Log once per leg. */
    int outlier_thr = 0x7fffffff;
    {
        int max1 = -32768, max2 = -32768;
        for (int k = 0; k < nv; k++) {
            int y = F->fvy[base + k];
            if (y > max1) { max2 = max1; max1 = y; }
            else if (y > max2) max2 = y;
        }
        if (max1 > (int)py + 1024 && max1 > max2 + 512) {
            outlier_thr = max2 + 512;
            u8 leg = DSB(DS_leg_index);
            if (far_outlier_logged_leg != leg) {
                fprintf(stderr, "enh_far: skipping faces with extreme Y outlier vertices in original model data (leg=%u py=%d max=%d next=%d)\n",
                        (unsigned)leg, (int)py, max1, max2);
                far_outlier_logged_leg = leg;
            }
        }
    }

    F->nfv += nv;
    u16 f = (u16)(a + 3 * stride);
    for (int k = 0; k < nf; k++, f = (u16)(f + 8)) {
        u16 w0 = rd16(es, f), w1 = rd16(es, (u16)(f + 2)), w2 = rd16(es, (u16)(f + 4)), w3 = rd16(es, (u16)(f + 6));
        int n = (w0 >> 14) + 1;
        u32 v[4] = { w0 & 0x7FFu, w1 & 0x7FFu, w2 & 0x7FFu, w3 & 0x7FFu };
        bool ok = true;
        for (int j = 0; j < n; j++) {
            if (v[j] >= (u32)nv) { ok = false; break; }
            if (F->fvy[base + v[j]] > outlier_thr) { ok = false; break; }
        }
        if (!ok) continue;
        for (int j = 0; j < n; j++) v[j] = v[j] + F->nv + base;
        EnhFarFace *e = &F->ff[F->nff++];
        e->w0 = (u16)(w0 & 0xF800);
        e->w3 = (u16)(w3 & 0xF800);
        e->pair = face_pair((u8)(w1 >> 11), (u8)(w2 >> 11));
        for (int j = 0; j < 4; j++) e->v[j] = j < n ? v[j] : v[0];
    }
}

/* the original's depth key (361c) at the snapshot's camera */
static double key_depth(int v)
{
    int i = v - F->nv;
    double dx = F->fvx[i], dz = F->fvz[i];                  /* already offsets from the camera */
    return fabs((double)F->fvy[i] - F->cam_y) + sqrt(dx * dx + dz * dz);
}

static double far_key(const EnhFarFace *e)
{
    int n = e->w0 >> 14;
    double d0 = key_depth(e->v[0]);
    if (n == 0) return d0;
    double d1 = key_depth(e->v[1]), d2 = n >= 2 ? key_depth(e->v[2]) : d0, d3 = n == 3 ? key_depth(e->v[3]) : d0;
    if (e->w0 & 0x2000) return fmax(fmax(d0, d1), fmax(d2, d3));
    if (n == 1) return (d0 + d1) / 2;
    if (n == 2) return (d0 + d1 + d2) * 11.0 / 32.0;
    return (d0 + d1 + d2 + d3) / 4;
}

static double keys[ENH_MAX_FAR_FACES];
static int cmp_far(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    return keys[ia] > keys[ib] ? -1 : keys[ia] < keys[ib] ? 1 : ia - ib;
}

/* ENH: sprite_animate (3352) drifts the sprites with id bit 40h / 80h (the aeroplane, clouds, birds) only while
 * they are in the game's sprite list, which reaches a short distance; the far ring draws the leg's own sprites
 * much farther, where they then hung still. Those not in the list this frame drift here by the same step while
 * the ring shows them (the game's own rule, at the ring's reach). Called right after sprite_animate. */
void enh_drift_far_sprites(void)
{
    if (!enh_enabled() || enh_draw_dist <= 0 || DSB(DS_menu_preview) != 0) return;
    static bool listed[320];
    memset(listed, 0, sizeof listed);
    for (u16 k = 0; k < DSW(DS_sprite_vis_count); k++) {
        u16 i = (u16)(DSW(DS_spr_inst + 2 * k) >> 1);
        if (i < 320) listed[i] = true;
    }
    int R = enh_draw_dist, cam_x = DSW(DS_cam_x), cam_z = DSW(DS_cam_z);
    int n = DSW(DS_sprite_count);
    static double plane_acc[320][2];                          /* the aeroplane's fraction of a unit, z / x */
    s16 dz = DSS(DS_sprite_drift_z), dx = DSS(DS_sprite_drift_x);
    for (int i = 9; i < n && i < 320; i++) {
        u16 id = DSW(DS_sprite_id + 2 * i);
        if (!id || !(id & 0xC0)) continue;
        u16 x = DSW(DS_sprite_x + 2 * i), z = DSW(DS_sprite_z + 2 * i);
        bool plane = enh_is_plane(id);
        if (listed[i]) {
            if (!plane) continue;                             /* the game drifted it */
            if (id & 0x40) z = (u16)(z - dz);                 /* the aeroplane: undo the game's step */
            if (id & 0x80) x = (u16)(x - dx);
        } else {
            int dx = wrap_delta((int)x - cam_x, 0x8000), dz = wrap_delta((int)z - cam_z, 0x4000);
            long long limit = (long long)(R + 1) * 0x400;
            if ((long long)dx * dx + (long long)dz * dz > limit * limit) continue;  /* as enh_far_build */
        }
        if (plane) {                                          /* ENH_PLANE_SPEED of the step, with its fraction */
            double *a = plane_acc[i];
            if (id & 0x40) { a[0] += dz * ENH_PLANE_SPEED; s16 k = (s16)trunc(a[0]); a[0] -= k; z = (u16)(z + k); }
            if (id & 0x80) { a[1] += dx * ENH_PLANE_SPEED; s16 k = (s16)trunc(a[1]); a[1] -= k; x = (u16)(x + k); }
        } else {
            if (id & 0x40) z = (u16)(z + dz);
            if (id & 0x80) x = (u16)(x + dx);
        }
        DSW(DS_sprite_z + 2 * i) = z;
        DSW(DS_sprite_x + 2 * i) = x;
    }
}

void enh_far_build(EnhSnap *s)
{
    F = s;
    s->nfv = s->nff = 0;
    s->nspr = s->nspr_game;
    for (int o = 0; o < s->nobj && o < ENH_MAX_OBJS; o++) s->obj[o].far_base = s->obj[o].far_count = 0;
    enh_far_lh_slot = DSW(DS_lighthouse_slot);
    if (enh_draw_dist <= 0 || s->menu_preview) return;
    int R = enh_draw_dist, cam_x = DSW(DS_cam_x), cam_z = DSW(DS_cam_z);
    F_reach = (R + 1) * 0x1000;
    long long limit = (long long)R * 0x400;
    limit *= limit;                                            /* squared, in position units */

    /* Collect every wrap image of each canonical cell within range. A 32x16 torus can place the same cell
     * more than once (nearest image may be behind an untraversable edge while another is along the road). */
    struct Cell {
        int c, px, pz;                                         /* cell index and the image's centre position */
        u8 ch_x, cz;                                           /* the canonical cell bytes objects are matched by */
        int xs, zs;                                            /* image position - canonical position */
        long long d;
    } cells[2048];
    int nc = 0;
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 32; col++) {
            int c = col + row * 32;
            if (game_cell[c]) continue;
            int canon_px = col * 0x400 + 0x200, canon_pz = (15 - row) * 0x400 + 0x200;
            int bdx = wrap_delta(canon_px - cam_x, 0x8000), bdz = wrap_delta(canon_pz - cam_z, 0x4000);
            for (int ox = -0x8000; ox <= 0x8000; ox += 0x8000)
                for (int oz = -0x4000; oz <= 0x4000; oz += 0x4000) {
                    int dx = bdx + ox, dz = bdz + oz;
                    long long d2 = (long long)dx * dx + (long long)dz * dz;
                    if (d2 > limit) continue;
                    if (fabs((double)dx / 1024.0) > R + 0.75 || fabs((double)dz / 1024.0) > R + 0.75) continue;
                    if (nc >= (int)(sizeof cells / sizeof cells[0])) continue;
                    struct Cell *e = &cells[nc++];
                    e->c = c; e->px = cam_x + dx; e->pz = cam_z + dz;
                    e->ch_x = (u8)(col << 2); e->cz = (u8)((15 - row) << 2);
                    e->xs = e->px - canon_px; e->zs = e->pz - canon_pz;
                    e->d = d2;
                }
        }
    for (int i = 1; i < nc; i++) {                             /* nearest first */
        struct Cell t = cells[i];
        int j = i;
        while (j > 0 && cells[j - 1].d > t.d) { cells[j] = cells[j - 1]; j--; }
        cells[j] = t;
    }
    for (int i = 0; i < nc; i++) {
        const struct Cell *cl = &cells[i];
        u16 m = DSW(DS_leg_map + 2 * cl->c);
        place((u8)m, 0, cl->px, cl->pz, (u16)(m & 0x3F00), (u8)((m >> 8) & 0xC0), 0);
        /* its static objects, matched in the canonical cell but placed in the image nearest the camera */
        for (s16 so = (s16)(2 * DSW(DS_obj_count) - 2); so >= 0; so -= 2) {
            u16 o = (u16)so;
            if ((u8)(DSB(DS_obj_x + o + 1) & 0x7C) != cl->ch_x) continue;
            u16 z = DSW(DS_obj_z + o);
            if ((u8)((z >> 8) & 0x3C) != cl->cz) continue;
            u16 f = DSW(DS_obj_flags + o);
            if ((f >> 8) & 0x30) continue;                     /* moving / parked */
            if ((f & 0x3F) == 0) continue;
            int base = F->nfv;
            int px = (int)DSW(DS_obj_x + o) + cl->xs, pz = (int)z + cl->zs;
            place((u8)f, 1, px, pz,
                  (u16)(DSW(DS_obj_y8 + o) >> 3), (u8)((DSW(DS_obj_heading + o) >> 8) & 0xC0),
                  (s8)(DSW(DS_obj_pitch + o) >> 8));
            if (F->nfv > base && (int)(o / 2) < F->nobj) {
                EnhObj *eo = &F->obj[o / 2];
                eo->far_base = (u16)base; eo->far_count = (u16)(F->nfv - base);
                eo->far_x = px; eo->far_z = pz;
                if (f == 0x0F) {
                    if (enh_far_lh_slot == 0xFFFF) enh_far_lh_slot = o;
                    if (enh_far_lh_slot == o) far_lighthouse(eo, (u8)(f & 0x3F), px, pz);
                }
            }
        }
    }
    /* parked objects (houses, boats: 1000h set, 2000h clear) that the game draws only within its window
     * (509b, B6E0 around the car): those it did not emit this frame, within the ring */
    for (int o = 1; o < DSW(DS_obj_first_static) && o < 160; o++) {
        u16 f = DSW(DS_obj_flags + 2 * o);
        if ((f & 0x3000) != 0x1000 || (f & 0x3F) <= 3) continue;
        if ((f >> 8) == 0x1F) continue;             /* the lightning bolt: the game shows it itself, in a storm */
        if (DSW(DS_obj_vert_count + 2 * o) != 0) continue;          /* the game's own */
        int x = DSW(DS_obj_x + 2 * o), z = DSW(DS_obj_z + 2 * o);
        int dx = wrap_delta(x - cam_x, 0x8000), dz = wrap_delta(z - cam_z, 0x4000);
        if ((long long)dx * dx + (long long)dz * dz > limit) continue;
        u16 es = DSW(DS_objects_set + 2), set = DSW(DS_objects_set);
        if (es == 0) continue;
        u16 p = (u16)(rd16(es, (u16)(set + 2 * (f & 0x3F))) + set);
        int nv; u16 voff;
        int nf = model_header(far_make(es, p), (u16)(abs(dx) + abs(dz)), &nv, &voff);
        int base = F->nfv;
        int px = cam_x + dx, pz = cam_z + dz;
        emit(es, voff, nf, nv, px, pz, (u16)(DSW(DS_obj_y8 + 2 * o) >> 3),
             (u8)(DSW(DS_obj_heading + 2 * o) >> 8), (s8)(DSW(DS_obj_pitch + 2 * o) >> 8));
        if (F->nfv > base && o < F->nobj) {
            EnhObj *eo = &F->obj[o];
            eo->far_base = (u16)base; eo->far_count = (u16)(F->nfv - base);
            eo->far_x = px; eo->far_z = pz;
        }
    }

    /* Moving vehicles the game left out of its small visibility window (sim_traffic.c: the ranges are zeroed,
     * but the position / heading were updated just before). The game keeps the same model alive, so a car does
     * not appear with a different shape when it crosses into the window. Object 0 is the player's car (skipped
     * in the cockpit view anyway) and is never part of the scenery. */
    for (int o = 1; o < s->nobj && o < ENH_MAX_OBJS; o++) {
        EnhObj *e = &s->obj[o];
        if (!(e->flags & 0x2000) || e->vcount) continue;
        int dx = wrap_delta((int)e->x - cam_x, 0x8000), dz = wrap_delta((int)e->z - cam_z, 0x4000);
        if ((long long)dx * dx + (long long)dz * dz > limit) continue;
        FarPtr p = vehicle_model(e->flags);
        if (far_is_null(p)) continue;
        int nv; u16 voff;
        int nf = model_header(p, (u16)(abs(dx) + abs(dz)), &nv, &voff);
        int base = F->nfv;
        e->far_x = cam_x + dx; e->far_z = cam_z + dz;
        emit(p.seg, voff, nf, nv, e->far_x, e->far_z, (u16)(e->y8 >> 3),
             (u8)(e->heading >> 8), (s8)(DSW(DS_obj_pitch + 2 * o) >> 8));
        if (F->nfv > base) { e->far_base = (u16)base; e->far_count = (u16)(F->nfv - base); }
    }

    /* the leg's own sprites (trees, rocks, animals: instances 9 .. sprite_count - 1) that the game lists only
     * within a cell of the car, as far as the ring reaches; their ids are live (a knocked-over sign) */
    static bool listed[320];
    memset(listed, 0, sizeof listed);
    for (int k = 0; k < s->nspr_game; k++) if (s->spr[k].inst < 320) listed[s->spr[k].inst] = true;
    int n = DSW(DS_sprite_count);
    for (int i = 9; i < n && i < 320; i++) {
        u16 id = DSW(DS_sprite_id + 2 * i);
        if (!id || listed[i]) continue;
        int x = DSW(DS_sprite_x + 2 * i), z = DSW(DS_sprite_z + 2 * i);
        int dx = wrap_delta(x - cam_x, 0x8000), dz = wrap_delta(z - cam_z, 0x4000);
        if ((long long)dx * dx + (long long)dz * dz > limit) continue;
        add_sprite((u16)i, id, cam_x + dx, DSW(DS_sprite_y + 2 * i), cam_z + dz);
    }

    /* farthest first */
    static int idx[ENH_MAX_FAR_FACES];
    static EnhFarFace tmp[ENH_MAX_FAR_FACES];
    for (int i = 0; i < s->nff; i++) { keys[i] = far_key(&s->ff[i]); idx[i] = i; }
    qsort(idx, (size_t)s->nff, sizeof idx[0], cmp_far);
    for (int i = 0; i < s->nff; i++) tmp[i] = s->ff[idx[i]];
    memcpy(s->ff, tmp, sizeof tmp[0] * (size_t)s->nff);
}

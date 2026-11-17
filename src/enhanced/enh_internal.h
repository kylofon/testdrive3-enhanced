#pragma once
/* Enhanced renderer internals (ENHANCED.md): snapshots of the game's frames, the smooth view state, the
 * draw lists and the sample buffers. */
#include "enhanced.h"
#include "../mem.h"
#include "../symbols.h"

#define ENH_MAX_VERTS   0x640          /* the game's vertex arrays */
#define ENH_MAX_FACES   0x640
#define ENH_MAX_OBJS    160
#define ENH_MAX_SPRITES 0x98           /* the sprite list DS:8902.. */
#define ENH_MAX_SPRITES_ALL (ENH_MAX_SPRITES + 8192)   /* and the far ones (enh_far.c) */
#define ENH_MAX_OVPIX   8192           /* overlay pixels of one frame */
#define ENH_MAX_OVCMD   64
#define ENH_MAX_FAR_VERTS 65536        /* the far ring (enh_far.c) */
#define ENH_MAX_FAR_FACES 65536
#define ENH_MAX_ALL_VERTS (ENH_MAX_VERTS + ENH_MAX_FAR_VERTS)

/* ---- snapshots (enhanced.c) ---- */

typedef struct {
    u16 x, z, y8, heading;             /* obj_x / obj_z / obj_y8 / obj_heading, as the game keeps them */
    u16 flags;
    u16 vbase, vcount;                 /* vertices emitted by the game this frame (moving vehicles; 0 = none) */
    u16 far_base, far_count;           /* vertices emitted into the far ring (enh_far.c; 0 = none) */
    int far_x, far_z;                  /* the wrapped image of x/z used by those far-ring vertices */
    u16 rbase, rcount;                 /* obj_vert_base / obj_vert_count as they are (lamp owners) */
} EnhObj;

typedef struct {
    u16 inst;                          /* sprite instance index */
    u16 id;                            /* sprite_id word (low byte: class/number, high: flags/countdown) */
    int x, y, z;                       /* position units; far-ring sprites use the image nearest the camera */
} EnhSpr;

/* A face of the far ring: the flag bits of the game's record words w0 (count, farthest flag, size) and w3 (type),
 * the colour pair, and full vertex indices (the game's records hold 11 bits). */
typedef struct {
    u16 w0, w3, pair;
    u16 v[4];
} EnhFarFace;

typedef enum { OV_PIXELS, OV_QUAD, OV_LINE } EnhOvKind;
typedef struct {
    EnhOvKind kind;
    int first, count;                  /* OV_PIXELS: range in ov_pix[] */
    float x[4], y[4];                  /* OV_QUAD: the cycle in V pixels (x from 1/32 px); OV_LINE: 2 points */
    u16 colour;                        /* colour pair */
    bool or_mode;
    u16 w0;                            /* OV_LINE: w0 of the record (width flags) */
} EnhOvCmd;

typedef struct {
    bool valid;
    u64  t_ns;                         /* tick time of the frame */
    u16  frame_counter;
    /* view */
    u16  w32m;                         /* DS:BA95: (view width - 1) * 32 */
    u8   cx_hi;                        /* DS:BA99 */
    u16  rows;                         /* DS:BA91 */
    u16  mirror_base;                  /* DS:BD3D */
    bool mirror_on;                    /* the mirror pass ran (B6D2, not in the replay layout) */
    bool ext_view, menu_preview;
    bool day;                          /* DS:95C7 colour mode: OR faces of pair 0707h are hidden by day */
    /* camera */
    u16  cam_x, cam_z;                 /* position units, unwrapped: the origin of the far ring's offsets */
    u16  cam_x4, cam_z4, heading;      /* x4, wrapped in 16 bits, as the game keeps it */
    s16  cam_y, cam_row, pitch;        /* cam_row = pitch + horizon base (9496); pitch = car pitch 948F */
    s8   roll;                         /* DS:94A0 roll code */
    /* sky / ground */
    u16  sky_pair, ground_pair;        /* DS:BAA2 / BAA4 */
    u8   sky_base;                     /* first colour of the gradient */
    bool gradient;
    /* geometry */
    int  nv, nf, static_vert_end;
    s16  vx[ENH_MAX_VERTS], vy[ENH_MAX_VERTS], vz[ENH_MAX_VERTS];
    u16  face[ENH_MAX_FACES][5];       /* w0, w1, w2, w3, colour pair */
    u16  order[ENH_MAX_FACES];         /* the game's depth order (nearest first), face indices */
    int  nobj;
    EnhObj obj[ENH_MAX_OBJS];
    u8   point_sizes[4], line_widths[4];
    /* sprites */
    int  nspr;
    EnhSpr spr[ENH_MAX_SPRITES_ALL];   /* the game's list, then the far ring's (inst FFFFh) and the leg's beyond it */
    int  nspr_game;
    u16  sprite_count;                 /* DS:9A71: instances from here on are the tiles' children */
    u16  world_cell, world_tab;        /* DS:BD34 / BD36: the cell and octant table the world was built for */
    u16  spr_min_dist, spr_far_limit;  /* DS:95CD, DS:B6E2 */
    u8   sprite_kind[32];              /* DS:95E1 */
    u8   sprite_max_size;              /* DS:BA0D */
    /* cockpit overlays, in drawing order */
    int  novcmd, novpix;
    EnhOvCmd ov[ENH_MAX_OVCMD];
    u32  ovpix[ENH_MAX_OVPIX];         /* V offset << 8 | colour */
    /* V as the frame left it (after the overlays) */
    u8   vbuf[0x7800];
    /* the far ring (enh_far.c): cells beyond the game's own, vertices numbered on from nv, faces farthest first */
    int  nfv, nff;
    /* the far ring's x4 offsets from the snapshot camera, in 32 bits: signed 16-bit deltas wrap 8 cells from
     * the camera, which is what capped the old ring at 7. Heights stay 16 bits. */
    int  fvx[ENH_MAX_FAR_VERTS], fvz[ENH_MAX_FAR_VERTS];
    s16  fvy[ENH_MAX_FAR_VERTS];
    EnhFarFace ff[ENH_MAX_FAR_FACES];
} EnhSnap;

/* The two newest frames (prev, cur). */
extern EnhSnap *enh_prev, *enh_cur;

/* Where the last view_present put V and M on the screen. */
typedef struct {
    bool active;
    int  x0, y0;                       /* screen position of V column 0 / row 0 */
    int  w, rows;                      /* V columns and rows shown */
    bool hole;                         /* V rows 0..13 skip screen x 168..255 (the mirror) */
    int  msg_rows;                     /* rows of V not copied because of a message box (msg_protect) */
    bool mirror;                       /* M was copied with 3D content (mirror on, not invalid) */
    u8   vbuf[0x7800];                 /* V as presented */
    u8   fbuf[0x7800];                 /* V as the frame drawn before that left it (the snapshot then) */
    u8   mbuf[88 * 19];                /* M as presented */
} EnhPresent;
extern EnhPresent enh_present;

/* ---- options (enhanced.c) ---- */
extern int enh_scale, enh_aa, enh_motion_delay, enh_haze, enh_draw_dist;
extern int enh_fog_start, enh_beam_night, enh_beam_day, enh_beam_soft;
extern bool enh_lights;

/* ---- the smooth view state of one displayed frame (enhanced.c) ---- */
typedef struct {
    double cam_x4, cam_z4, cam_y, heading, cam_row, pitch;
    double roll_slope;                 /* front rows per 1/32 px of x (the roll term), 0 = level */
    double mroll_slope;                /* mirror: rows per 1/32 px of (sx - 9400h) before the halving */
    s8     roll;                       /* nearest code (sky flat/gradient choice) */
    /* vertices of the current snapshot, moving vehicles carried to the view time */
    float  vx[ENH_MAX_ALL_VERTS], vy[ENH_MAX_ALL_VERTS], vz[ENH_MAX_ALL_VERTS];   /* then the far ring's */
    /* per object: heading at the view time (u16 units, unwrapped) */
    double obj_heading[ENH_MAX_OBJS];
    float  spr_x[ENH_MAX_SPRITES_ALL], spr_y[ENH_MAX_SPRITES_ALL], spr_z[ENH_MAX_SPRITES_ALL];
} EnhView;

/* ---- the far ring (enh_far.c) ---- */
/* Build the snapshot's far ring: the cells within enh_draw_dist of the camera that the game did not build. */
void enh_far_build(EnhSnap *s);

/* ---- sprites (enh_sprite.c) ---- */
typedef struct {
    int w, h;                          /* native size */
    u8 *pix;                           /* w * h, 0 = transparent (NULL: none) */
} EnhSprImg;
void enh_sprites_capture(void);
/* The image and the drawn size (in view pixels) of sprite number s (low 6 bits of the id) at the apparent
 * size a (8-pixel units, continuous). Returns false if nothing is drawn. */
bool enh_sprite_pick(u8 s, double a, const EnhSprImg **img, double *w, double *h);

/* the aeroplane: sprite 2 with a drift bit (SCENE01's; world.md "Sprites"), drawn smaller and flown slower */
#define ENH_PLANE_SIZE  0.25                     /* of its size */
#define ENH_PLANE_SPEED 0.2                      /* of the leg's drift a frame */
static inline bool enh_is_plane(u16 id) { return (id & 0x3F) == 2 && (id & 0xC0) != 0; }

/* ---- rasterising (enh_raster.c) ---- */

/* A sample holds a colour pair, the weight of its high colour and its distance haze:
 * lo | hi << 8 | weight << 16 | haze << 24 (weight 128 = the original's two-colour dither seen from afar;
 * haze 0..255 = none .. all the horizon's sky colour). */
#define ENH_PAIR(p)          ((u32)(p) | 0x800000u)
#define ENH_SOLID(c)         ((u32)(c) * 0x101u | 0x800000u)
#define ENH_HAZE(h)          ((u32)(h) << 24)
#define ENH_HAZE_ROWS        256       /* P_SKY ground haze table: entries, 1/32 view pixel apart below the horizon */
#define ENH_FOG_STEPS        1024      /* per-sample fog table: entries over the distance (enh_scene.c) */
extern u8 enh_fog_lut[ENH_FOG_STEPS];  /* haze by the distance along the line of sight, enh_fog_scale entries a unit */
extern float enh_fog_scale;

typedef enum { P_POLY, P_SPRITE, P_BLOCKS, P_SKY, P_ELLIPSE } EnhPrimKind;
typedef struct {
    EnhPrimKind kind;
    u32 value;                         /* sample value (P_POLY, P_SKY's sky; P_SPRITE: the haze bits only) */
    bool or_mode;                      /* a light (headlight beam): lights what is below, soft-edged (see lt) */
    bool fog;                          /* P_POLY with a plane: haze each sample by its own distance */
    int n;                             /* P_POLY: points */
    float x[16], y[16];                /* P_POLY: sample coordinates */
    /* P_SPRITE: destination rectangle in samples, image; P_ELLIPSE: the ellipse inscribed in the rectangle,
     * less the one inscribed in the cut rectangle when cut is set (the moon's crescent) */
    float sx0, sy0, sx1, sy1;
    bool cut;
    float kx0, ky0, kx1, ky1;
    const EnhSprImg *img;
    /* P_BLOCKS: overlay pixels [first, first + count) of the snapshot, K x K samples each */
    int first, count;
    /* P_SKY: horizon row at sample x = 0 and its slope (rows per sample), base colour, gradient on; below the
     * horizon the ground, hazed by the table (view pixels below the horizon * 32; NULL = no haze) */
    float hy0, hslope;
    u8 sky_base;
    bool gradient;
    u32 ground;
    const u8 *ground_haze;
    /* depth (ENHANCED.md "Sprites"): what P_POLY writes into the target's depth buffer (the distance from the
     * camera along the sample's line of sight): nothing, "nothing there" (clear), a constant, or its plane
     * n . X = d (unit normal, camera at the origin). P_SPRITE: drawn only where zval is below the depth. */
    u8 zmode;
    float nx, ny, nz, d, zc;
    bool ztest;
    float zval;
    /* the far ring's faces also write the far depth; the game's faces are drawn only where they are nearer
     * than it (a cell the game did not build can be nearer than one it did) */
    bool zfar_write, zfar_test;
} EnhPrim;
enum { Z_NONE, Z_CLEAR, Z_CONST, Z_PLANE };

/* How a target's samples map back to lines of sight (the inverse of enh_scene.c's projection). */
typedef struct {
    bool mirror;
    double base;                       /* bearing units: sx - base = atan2(dx, dz) * RAD2A */
    double cam_row, roll_slope, cx32;  /* front: row = cam_row - elevation + roll_slope * (sx - cx32) */
    double mirror_base, pitch, mroll;  /* mirror: sx = mirror_base + 1600h - 2 x; row per 8b5f */
} EnhMap;

typedef struct {
    int w, h;                          /* samples */
    u32 *s;
    float *z;                          /* depth per sample */
    float *zf;                         /* the far ring's depth per sample */
    u16 *lt;                           /* light per sample: amount 0..255 | the bits it ORs into the colours << 8,
                                        * LT_BLOCKED: drawn over after the first light (the light does not reach it) */
    u8 *lt_tmp;                        /* the softening's intermediate amounts */
    int first_light;                   /* index of the first light primitive (nprim: none) */
    bool use_lt;                       /* the light buffer is in use this frame (enhanced lights and a light);
                                        * else lt is not written nor read and lights OR into the colours */
    int lt_x0, lt_y0, lt_x1, lt_y1;    /* the samples the lights and their soft rims can reach */
    u16 light_bits;                    /* the lights' bits << 8 */
    u8 light_max;                      /* the lights' full amount (--beam-night / --beam-day) */
    int light_soft;                    /* the lights' soft rim, view pixels each side (--beam-soft) */
    EnhPrim *prim;
    int nprim, cap;
    int k;                             /* samples per view pixel */
    EnhMap map;
    /* per column: sin / cos of the bearing and of the column's part of the elevation; per row: its part */
    float *col_sb, *col_cb, *col_se, *col_ce, *row_se, *row_ce;
} EnhTarget;

#define LT_BLOCKED 0x8000u

void enh_target_reset(EnhTarget *t, int w_px, int h_px, int k);
EnhPrim *enh_prim_add(EnhTarget *t, EnhPrimKind kind);
void enh_target_raster(EnhTarget *t, const EnhSnap *snap);

/* ---- the scene (enh_scene.c) ---- */
extern EnhTarget enh_front, enh_mirror;
void enh_scene_build(const EnhSnap *s, const EnhView *v);
/* the colour the haze tends to: the sky at the horizon, as a sample value (resolved through the current DAC) */
u32 enh_haze_colour(const EnhSnap *s);
/* the game's ground under (x4, z4): the highest ground-like face there not more than `above` over y (-1e9: none) */
double enh_ground_height(const EnhSnap *s, double x4, double z4, double y, double above);
#define ENH_EYE_CLEAR 20.0             /* the least height of the view's eye over the ground (a steep slope) */

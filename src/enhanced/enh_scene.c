/* Enhanced renderer: the 3D view and the mirror of one displayed frame as draw lists (ENHANCED.md
 * "Projection", "Faces", "Sprites", "Mirror").
 *
 * This is the original's front view code (render3d.md §4.5-4.13) evaluated in floating point for the smooth
 * camera of the displayed frame: the same angular (cylindrical) projection, the same visibility tests, face
 * depth keys and face / sprite merge order, the same face types, the ground around the camera, lamps and
 * lines, the same mirror transform. Only the pixel grid is gone: coordinates stay continuous until the
 * sample buffers. */
#include "enh_internal.h"

#include <math.h>
#include <stdlib.h>

#define TURN   65536.0                           /* angle units per turn (u16 headings, 1/32 px) */
#define RAD2A  (TURN / (2.0 * M_PI))             /* radians -> angle units */
#define RAD2PX (1024.0 / M_PI)                   /* radians -> pixels (256 px = 45 degrees) */

static const EnhSnap *S;
static const EnhView *V;
static int K;                                    /* samples per view pixel */
static double W32;                               /* view width * 32 (DS:BA95 + 20h) */
static double CX32;                              /* view centre, 1/32 px (DS:BA97) */
static double VROWS;                             /* view rows */

static int NV;                                   /* vertices: the game's, then the far ring's */
static double cam_abs_x, cam_abs_z;              /* the smooth camera in unwrapped x4 units (sprite distances) */
static double sx[ENH_MAX_ALL_VERTS];             /* bearing on screen, [0, 65536) (sx[] of 394c) */
static double vdist[ENH_MAX_ALL_VERTS];          /* horizontal distance (dist[]) */
static double fy[ENH_MAX_ALL_VERTS];             /* front row (sy[] of 39fd) */
static double my[ENH_MAX_ALL_VERTS];             /* mirror row (my[] of 8b5f) */
static double fkey[ENH_MAX_FACES];
static int    forder[ENH_MAX_FACES];

typedef struct { int k; double key, angle, dist, ydist; } SprItem;
static SprItem sprs[ENH_MAX_SPRITES_ALL];

static inline double wrapu(double a)             /* to [0, 65536) */
{
    a = fmod(a, TURN);
    return a < 0 ? a + TURN : a;
}
static inline double wraps(double a)             /* to [-32768, 32768) */
{
    a = wrapu(a);
    return a >= 32768.0 ? a - TURN : a;
}
static inline bool NEG(double v) { return wrapu(v) >= 32768.0; }   /* sign of a 16-bit result */

/* relative to the smooth camera: the game's 16-bit vertices wrap, the far ring's 32-bit offsets do not */
static inline double vx_rel(int v) { return v >= S->nv ? V->vx[v] - V->cam_x4 : wraps(V->vx[v] - V->cam_x4); }
static inline double vz_rel(int v) { return v >= S->nv ? V->vz[v] - V->cam_z4 : wraps(V->vz[v] - V->cam_z4); }

/* elevation of a height difference over a distance in pixels (the 1AFA table: rows up for dy > 0) */
static inline double elev(double dy, double dist) { return atan2(dy, dist < 1e-3 ? 1e-3 : dist) * RAD2PX; }

/* ------------------------------------------------------------------------------------------------------
 * Projection (394c, 39fd, 8b5f)
 * ------------------------------------------------------------------------------------------------------ */

static void project(void)
{
    double base = (double)(S->cx_hi << 8) - V->heading;
    for (int v = 0; v < NV; v++) {
        double dx = vx_rel(v), dz = vz_rel(v);
        double d = sqrt(dx * dx + dz * dz);
        sx[v] = wrapu(atan2(dx, dz) * RAD2A + base);
        vdist[v] = d;
        double e = elev(V->vy[v] - V->cam_y, d);
        fy[v] = V->cam_row - e + V->roll_slope * wraps(sx[v] - CX32);
        my[v] = (-e - V->mroll_slope * wraps(sx[v] - 0x9400) - V->pitch) * 0.5 + 10.0;
    }
}

static double depth(int v) { return fabs(V->vy[v] - V->cam_y) + vdist[v]; }

/* ENH: the height of the game's ground under the point (x4, z4): the highest plane, at that point, of the
 * snapshot's triangles and quads that contain it (seen from above), not steeper than 1.5 and not more than
 * `above` over y (a bridge deck over the camera is not its ground). -1e9 when there is none. */
double enh_ground_height(const EnhSnap *s, double x4, double z4, double y, double above)
{
    double best = -1e9;
    for (int f = 0; f < s->nf; f++) {
        const u16 *r = s->face[f];
        int n = (r[0] >> 14) + 1;
        if (n < 3 || (r[3] >> 11) == 0) continue;                /* polygons, not the OR faces */
        int v[4];
        bool ok = true;
        for (int k = 0; k < n; k++) { v[k] = r[k] & 0x7FF; if (v[k] >= s->nv) ok = false; }
        if (!ok) continue;
        for (int t = 0; t + 2 < n; t++) {                         /* the fan (0, 1 + t, 2 + t) */
            int a = v[0], b = v[1 + t], c = v[2 + t];
            double px[3] = { wraps(s->vx[a] - x4), wraps(s->vx[b] - x4), wraps(s->vx[c] - x4) };
            double pz[3] = { wraps(s->vz[a] - z4), wraps(s->vz[b] - z4), wraps(s->vz[c] - z4) };
            double py[3] = { s->vy[a], s->vy[b], s->vy[c] };
            double c0 = px[0] * pz[1] - pz[0] * px[1], c1 = px[1] * pz[2] - pz[1] * px[2], c2 = px[2] * pz[0] - pz[2] * px[0];
            if (!((c0 >= 0 && c1 >= 0 && c2 >= 0) || (c0 <= 0 && c1 <= 0 && c2 <= 0))) continue;
            double ux = px[1] - px[0], uy = py[1] - py[0], uz = pz[1] - pz[0];
            double wx = px[2] - px[0], wy = py[2] - py[0], wz = pz[2] - pz[0];
            double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
            if (fabs(ny) < 1e-6 || sqrt(nx * nx + nz * nz) > 1.5 * fabs(ny)) continue;   /* a wall */
            double h = py[0] - (nx * -px[0] + nz * -pz[0]) / ny;
            if (h <= y + above && h > best) best = h;
        }
    }
    return best;
}

/* ------------------------------------------------------------------------------------------------------
 * Distance fog (ENH): what is far away takes on the sky's colour at the horizon, from --fog-start to --haze
 * of it at haze_far (the edge of the far ring when there is one, so that it fades out, else ENH_HAZE_FAR),
 * rising as exponential-squared fog does (1 - e^(-k x^2), scaled to reach 1 at the edge): hardly anything
 * near, then the scenery sinks into it gradually. The polygons take it per sample, by the distance along the
 * sample's line of sight (enh_fog_lut); sprites by their depth key; the ground by its angle below the horizon,
 * as seen from ENH_HAZE_EYE over flat ground.
 * ------------------------------------------------------------------------------------------------------ */

#define FOG_K 2.5                                /* the curve's steepness: e^-2.5 of the way at the edge */

static double haze_max;                          /* 0..255 at haze_far; 0 = off */
static double haze_far;
static u8 ground_haze[ENH_HAZE_ROWS];
u8 enh_fog_lut[ENH_FOG_STEPS];
float enh_fog_scale;

static double haze_near;                         /* where it begins: --fog-start percent of haze_far */

static u32 haze_amount(double d)
{
    if (haze_max <= 0 || d <= haze_near) return 0;
    double x = (d - haze_near) / (haze_far - haze_near);
    if (x > 1) x = 1;
    return (u32)((1 - exp(-FOG_K * x * x)) / (1 - exp(-FOG_K)) * haze_max + 0.5);
}

static void haze_setup(void)
{
    haze_max = S->menu_preview ? 0 : enh_haze * 2.55;
    haze_far = S->nff > 0 ? enh_draw_dist * 0x1000 : ENH_HAZE_FAR;
    haze_near = haze_far * enh_fog_start / 100.0;
    enh_fog_scale = (float)((ENH_FOG_STEPS - 1) / haze_far);
    for (int i = 0; i < ENH_FOG_STEPS; i++) enh_fog_lut[i] = (u8)haze_amount(i / (double)enh_fog_scale);
    for (int i = 0; i < ENH_HAZE_ROWS; i++) {
        double a = (i + 0.5) / 32.0 / RAD2PX;              /* radians below the horizon */
        ground_haze[i] = (u8)haze_amount(ENH_HAZE_EYE + ENH_HAZE_EYE / tan(a));
    }
}

u32 enh_haze_colour(const EnhSnap *s)
{
    return s->gradient ? ENH_SOLID((u8)(s->sky_base + 5)) : ENH_PAIR(s->sky_pair);   /* raster_sky at d = 0 */
}

/* ------------------------------------------------------------------------------------------------------
 * Primitive helpers (coordinates in view pixels; x from 1/32 px)
 * ------------------------------------------------------------------------------------------------------ */

static EnhTarget *T;                             /* the target being filled */

/* what the polygons being added leave in the depth buffer (EnhPrim zmode): set per face by draw_face; while the
 * far ring is drawn they write the far depth too, the game's faces after it are tested against that */
static u8 z_mode = Z_NONE;
static float z_plane[4], z_const;
static bool z_far_write, z_far_test;

static bool face_fog;                            /* the face being drawn takes the fog per sample */

static void poly(int n, const double *x32, const double *y, u32 value, bool or_mode)
{
    EnhPrim *p = enh_prim_add(T, P_POLY);
    if (!p) return;
    p->n = n;
    p->value = value;
    p->or_mode = or_mode;
    p->fog = face_fog && !or_mode;
    p->zmode = or_mode ? Z_NONE : z_mode;
    p->nx = z_plane[0]; p->ny = z_plane[1]; p->nz = z_plane[2]; p->d = z_plane[3];
    p->zc = z_const;
    p->zfar_write = z_far_write;
    p->zfar_test = z_far_test;
    for (int i = 0; i < n; i++) {
        p->x[i] = (float)(x32[i] / 32.0 * K);
        p->y[i] = (float)(y[i] * K);
    }
}

/* convex hull of up to 8 points (monotone chain), for lines and lamps */
static int hull(int n, double *x, double *y)
{
    int idx[8];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++)                  /* sort by x, then y */
        for (int j = i; j > 0 && (x[idx[j]] < x[idx[j - 1]] ||
                                  (x[idx[j]] == x[idx[j - 1]] && y[idx[j]] < y[idx[j - 1]])); j--) {
            int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t;
        }
    int h[32], m = 0;
    for (int pass = 0; pass < 2; pass++) {
        int start = m;
        for (int ii = 0; ii < n; ii++) {
            int i = pass ? idx[n - 1 - ii] : idx[ii];
            while (m >= start + 2) {
                int a = h[m - 2], b = h[m - 1];
                double cr = (x[b] - x[a]) * (y[i] - y[a]) - (y[b] - y[a]) * (x[i] - x[a]);
                if (cr > 0) break;
                m--;
            }
            h[m++] = i;
        }
        m--;                                     /* the last point starts the other chain */
    }
    double hx[16], hy[16];
    for (int i = 0; i < m; i++) { hx[i] = x[h[i]]; hy[i] = y[h[i]]; }
    for (int i = 0; i < m; i++) { x[i] = hx[i]; y[i] = hy[i]; }
    return m;
}

/* a line from (xa, ya) to (xb, yb) (x in 1/32 px), half width hw (1/32 px) and half a row high: the
 * original draws at least one row, and thin lines one pixel wide */
static void line_prim(double xa, double ya, double xb, double yb, double hw, u32 value)
{
    double x[8], y[8];
    const double dx[4] = { -1, 1, 1, -1 }, dy[4] = { -0.5, -0.5, 0.5, 0.5 };
    for (int i = 0; i < 4; i++) {
        x[i] = xa + dx[i] * hw; y[i] = ya + dy[i];
        x[i + 4] = xb + dx[i] * hw; y[i + 4] = yb + dy[i];
    }
    int n = hull(8, x, y);
    if (n >= 3) poly(n, x, y, value, false);
}

/* unwrap the x values of a face around its first vertex and move them next to the view */
static void unwrap(int n, double *x, double centre)
{
    double b = x[0], m = 0;
    for (int i = 0; i < n; i++) { x[i] = b + wraps(x[i] - b); m += x[i]; }
    m /= n;
    double shift = floor((m - centre) / TURN + 0.5) * TURN;
    for (int i = 0; i < n; i++) x[i] -= shift;
}

/* ------------------------------------------------------------------------------------------------------
 * Visibility tests (3a9e / 3b0e front test, mirror test)
 * ------------------------------------------------------------------------------------------------------ */

static bool front_visible(int n, const int *v)
{
    bool all = true;
    for (int k = 0; k < n; k++) if (!NEG(sx[v[k]])) all = false;
    if (all) return false;
    bool any = false;
    for (int k = 0; k < n; k++) if (NEG(sx[v[k]] - 0x2800)) any = true;
    if (!any) return false;
    for (int k = 0; k < n; k++) if (NEG(sx[v[k]] - 0x5400)) return true;
    return false;
}

static bool mirror_visible(int n, const int *v)
{
    double b = S->mirror_base;
    bool all = true;
    for (int k = 0; k < n; k++) if (!NEG(sx[v[k]] - b)) all = false;
    if (all) return false;
    bool any = false;
    for (int k = 0; k < n; k++) if (NEG(sx[v[k]] - b - 0x1600)) any = true;
    if (!any) return false;
    for (int k = 0; k < n; k++) if (NEG(sx[v[k]] - b - 0x4B00)) return true;
    return false;
}

/* the vertices lie within half a turn (a gap between neighbours of more than half a turn) */
static bool has_big_gap(int n, const int *v)
{
    double a[4];
    for (int k = 0; k < n; k++) a[k] = sx[v[k]];
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && a[j] < a[j - 1]; j--) { double t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
    for (int k = 0; k + 1 < n; k++) if (a[k + 1] - a[k] - 1 >= 32768.0 || a[k + 1] - a[k] - 1 < 0) return true;
    return NEG(a[0] - a[n - 1] - 1);
}

/* ------------------------------------------------------------------------------------------------------
 * Faces
 * ------------------------------------------------------------------------------------------------------ */

static u32 face_value;
static bool face_or;
static u8 face_type;                             /* w3 >> 11 */

static void front_poly(int n, const int *v)
{
    double x[4], y[4];
    for (int k = 0; k < n; k++) { x[k] = sx[v[k]]; y[k] = fy[v[k]]; }
    unwrap(n, x, W32 / 2);
    poly(n, x, y, face_value, face_or);
}

static void mirror_poly(int n, const int *v)
{
    double u[4], x[4], y[4];
    for (int k = 0; k < n; k++) { u[k] = wrapu(sx[v[k]] - S->mirror_base); y[k] = my[v[k]]; }
    unwrap(n, u, 0x0B00);
    for (int k = 0; k < n; k++) x[k] = (0x1600 - u[k]) * 0.5;
    poly(n, x, y, face_value, face_or);
}

/* height of the triangle's plane at the camera (6316) */
static double plane_height(const int *v)
{
    double ax = V->vx[v[0]], ay = V->vy[v[0]], az = V->vz[v[0]];
    double ux = wraps(V->vx[v[1]] - ax), uy = V->vy[v[1]] - ay, uz = wraps(V->vz[v[1]] - az);
    double wx = wraps(V->vx[v[2]] - ax), wy = V->vy[v[2]] - ay, wz = wraps(V->vz[v[2]] - az);
    double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    if (fabs(ny) < 1e-6) return fmax(ay, fmax(V->vy[v[1]], V->vy[v[2]]));
    double cx = -vx_rel(v[0]), cz = -vz_rel(v[0]);
    return ay - (nx * cx + nz * cz) / ny;
}

/* 3d25 / 3ebf: the triangle around the camera. Each edge covers the arc of bearings between its ends that
 * does not hold the third vertex; the region below the edge (above it when the plane is over the camera)
 * is filled across that arc. */
static void around_camera(const int *v)
{
    bool above = plane_height(v) > V->cam_y;
    int o[3] = { v[0], v[1], v[2] };
    for (int i = 1; i < 3; i++)
        for (int j = i; j > 0 && sx[o[j]] < sx[o[j - 1]]; j--) { int t = o[j]; o[j] = o[j - 1]; o[j - 1] = t; }
    double big = VROWS + 4;
    for (int e = 0; e < 3; e++) {
        int a = o[e], b = o[(e + 1) % 3];
        double x0 = sx[a], x1 = sx[b] + (e == 2 ? TURN : 0.0);
        for (int s = 0; s < 2; s++) {
            double sh = s ? -TURN : 0.0, xa = x0 + sh, xb = x1 + sh;
            if (xb < 0 || xa > W32) continue;
            double x[4] = { xa, xb, xb, xa }, y[4];
            if (above) { y[0] = fy[a]; y[1] = fy[b]; y[2] = -4; y[3] = -4; }
            else       { y[0] = fy[a]; y[1] = fy[b]; y[2] = big; y[3] = big; }
            poly(4, x, y, face_value, face_or);
        }
    }
}

static void draw_triangle(const int *v)
{
    if (front_visible(3, v)) {
        T = &enh_front;
        if (has_big_gap(3, v)) front_poly(3, v);
        else if (!z_far_write) around_camera(v);
        /* ENH: not a face of the far ring: the view runs up to a game frame behind, and just after the game moves
         * to a new cell the view can still be in the old one, which the far ring then holds; filled around the
         * camera its ground covered the sky or, as depth, hid what is near */
        return;
    }
    if (S->mirror_on && mirror_visible(3, v)) {
        T = &enh_mirror;
        mirror_poly(3, v);
    }
}

static void draw_quad(const int *v)
{
    if (front_visible(4, v)) {
        T = &enh_front;
        if (has_big_gap(4, v)) front_poly(4, v);
        else {
            int a[3] = { v[0], v[1], v[2] }, b[3] = { v[0], v[2], v[3] };
            draw_triangle(a);
            draw_triangle(b);
        }
        return;
    }
    if (S->mirror_on && mirror_visible(4, v)) {
        T = &enh_mirror;
        mirror_poly(4, v);
    }
}

static void draw_line(int v0, int v1, u16 w0)
{
    int v[2] = { v0, v1 };
    bool front = front_visible(2, v);
    if (!front && !(S->mirror_on && mirror_visible(2, v))) return;
    double hw = 16;                                          /* thin: one pixel */
    if (w0 & 0x1800) {                                       /* thick: angular half width at the far end */
        double size = S->line_widths[(w0 >> 11) & 3], d = vdist[v0] * (front ? 1 : 2);   /* p = v0 here */
        hw = fmin(atan2(size, d < 1e-3 ? 1e-3 : d) * RAD2A, 255.0 * 32) + 16;   /* the original widens by a pixel */
    }
    double x[2], y[2];
    if (front) {
        T = &enh_front;
        x[0] = sx[v0]; x[1] = sx[v1];
        unwrap(2, x, W32 / 2);
        y[0] = fy[v0]; y[1] = fy[v1];
    } else {
        T = &enh_mirror;
        double u[2] = { wrapu(sx[v0] - S->mirror_base), wrapu(sx[v1] - S->mirror_base) };
        unwrap(2, u, 0x0B00);
        x[0] = (0x1600 - u[0]) * 0.5; x[1] = (0x1600 - u[1]) * 0.5;
        y[0] = my[v0]; y[1] = my[v1];
    }
    line_prim(x[0], y[0], x[1], y[1], hw, face_value);
}

/* 66ad / 81c7: a lamp, a lens-shaped blob that is narrow when seen edge-on */
static void draw_point(int v0, u16 w0)
{
    double x = sx[v0];
    bool front = !NEG(x) && !NEG(x + 0x5800) && !NEG(x + 0x2C00);
    if (!front) {
        if (!S->mirror_on) return;
        double m = x - S->mirror_base;
        if (NEG(m) || NEG(m + 0x6A00) || NEG(m + 0x3500)) return;
    }
    double size = S->point_sizes[(w0 >> 11) & 3], d = vdist[v0] * (front ? 1 : 2);
    /* radius, pixels. The original truncates it to whole pixels (nothing below 1), half a pixel smaller on
     * average: the blob is drawn at that average, which keeps small ones from outgrowing their car */
    double r = fmin(atan2(size, d < 1e-3 ? 1e-3 : d) * RAD2PX, 255.0) - 0.5;
    if (r < 0.5) return;
    int owner = -1;                                          /* last object whose range holds v */
    for (int o = S->nobj - 1; o >= 0; o--) {
        const EnhObj *e = &S->obj[o];
        if (e->rbase <= v0 && e->rbase + e->rcount >= v0) { owner = o; break; }
    }
    if (owner < 0) return;
    double a = floor(V->obj_heading[owner] / 256.0) * 256.0;
    if (!(w0 & 0x2000)) a += 0x4000;                         /* bit 13 clear: the lamp faces sideways */
    a = a - V->heading - x + (double)((front ? S->cx_hi : 0x14) << 8);
    a = fmod(wrapu(a), 32768.0);
    if (a > 16384.0) a = 32768.0 - a;
    double s = sin(a / 16384.0 * M_PI / 2);                  /* 0 edge-on, 1 face-on */
    double q = (r < 12 ? 48.0 : 56.0) / (r > 2 ? r - 1 : 1);   /* the original divides by r - 1 from r = 2 on */
    double inc = q * s / 32.0;                               /* widening per row, pixels */
    double half = (r < 12 ? 8 * r : 4 * r) * s / 32.0 + 0.25; /* half width at the top and bottom, pixels */
    double cx, cy;
    if (front) {
        T = &enh_front;
        double xs[1] = { x };
        unwrap(1, xs, W32 / 2);
        cx = xs[0] / 32.0;
        cy = fy[v0];
    } else {
        T = &enh_mirror;
        double u[1] = { wrapu(x - S->mirror_base) };
        cx = (0x1600 - (u[0] >= 32768 ? u[0] - TURN : u[0])) / 64.0;
        cy = my[v0];
    }
    double px[16], py[16];
    const int N = 6;
    /* The original widens row k (0 .. 2r-1) by inc * (k*r - k*(k+1)/2) = inc * k * (2r - 1 - k) / 2: zero at
     * both ends, inc * (r - 1/2)^2 / 2 in the middle (about r for large r). The rows span 2r. */
    double k1 = 2 * r - 1 > 0 ? 2 * r - 1 : 0;
    for (int i = 0; i <= N; i++) {                           /* right side down, left side up */
        double u = (double)i / N, k = u * k1, hw = half + inc * k * (k1 - k) / 2, t = 2 * r * u;
        px[i] = (cx + hw) * 32; py[i] = cy - r + t;
        px[2 * N + 1 - i] = (cx - hw) * 32; py[2 * N + 1 - i] = cy - r + t;
    }
    poly(2 * N + 2, px, py, face_value, false);
}

/* a face from its record: the flag bits of w0 and w3, the colour pair, the vertices */
static void draw_face_rec(u16 w0, u16 w3, u16 pair, int v0, int v1, int v2, int v3)
{
    face_type = (u8)(w3 >> 11);
    if (pair == 0x10F && (S->frame_counter & 1)) pair = (u16)(pair << 8 | pair >> 8);   /* blinking pair */
    face_or = face_type == 0;
    if (face_or && pair == 0x0707 && S->day) return;         /* OR faces: hidden by day */
    face_value = ENH_PAIR(pair);
    if (v0 >= NV || v1 >= NV) return;
    /* ENH: haze by the average depth of the face's corners (what has no plane: lamps, lines), polygons per
     * sample by their plane (face_fog); not the headlight beams (lights) nor, at night, the lamps */
    int n = w0 >> 14;
    if (haze_max > 0 && !face_or && (n != 0 || S->day)) {
        int vs[4] = { v0, v1, v2, v3 }, m = n == 0 ? 1 : n + 1;
        double d = 0;
        int used = 0;
        for (int k = 0; k < m; k++) if (vs[k] < NV) { d += depth(vs[k]); used++; }
        face_value |= ENH_HAZE(haze_amount(d / used));
    }
    /* ENH: its depth for the sprites: polygons their plane, lamps and lines the distance of their nearest end */
    {
        int vs[4] = { v0, v1, v2, v3 }, m = n + 1;
        double p[4][3];
        bool ok = true;
        for (int k = 0; k < m; k++) {
            if (vs[k] >= NV) { ok = false; break; }
            p[k][0] = vx_rel(vs[k]);
            p[k][1] = V->vy[vs[k]] - V->cam_y;
            p[k][2] = vz_rel(vs[k]);
        }
        z_mode = Z_NONE;
        if (ok && m >= 3) {
            double ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
            double wx = p[2][0] - p[0][0], wy = p[2][1] - p[0][1], wz = p[2][2] - p[0][2];
            double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
            double len = sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-9) {
                nx /= len; ny /= len; nz /= len;
                z_plane[0] = (float)nx; z_plane[1] = (float)ny; z_plane[2] = (float)nz;
                z_plane[3] = (float)(nx * p[0][0] + ny * p[0][1] + nz * p[0][2]);
                z_mode = Z_PLANE;
            }
        }
        if (ok && z_mode == Z_NONE) {
            double dmin = 1e30;
            for (int k = 0; k < m; k++)
                dmin = fmin(dmin, sqrt(p[k][0] * p[k][0] + p[k][1] * p[k][1] + p[k][2] * p[k][2]));
            z_const = (float)dmin;
            z_mode = Z_CONST;
        }
    }
    face_fog = haze_max > 0 && !face_or && n >= 2;
    switch (w0 >> 14) {
    case 0: draw_point(v0, w0); break;
    case 1: draw_line(v1, v0, w0); break;                     /* line_draw(bx = v1, si = v0) */
    case 2: {
        if (v2 >= NV) break;
        int v[3] = { v0, v1, v2 };
        draw_triangle(v);
        break;
    }
    default: {
        if (v2 >= NV || v3 >= NV) break;
        int v[4] = { v0, v1, v2, v3 };
        draw_quad(v);
        break;
    }
    }
    face_fog = false;
}

static void draw_face(int f)
{
    const u16 *r = S->face[f];
    draw_face_rec(r[0], r[3], r[4], r[0] & 0x7FF, r[1] & 0x7FF, r[2] & 0x7FF, r[3] & 0x7FF);
}

/* 361c: the depth key of a face */
static double face_key(int f)
{
    const u16 *r = S->face[f];
    u16 w0 = r[0];
    int n = w0 >> 14;
    int v0 = w0 & 0x7FF, v1 = r[1] & 0x7FF, v2 = r[2] & 0x7FF, v3 = r[3] & 0x7FF;
    if (v0 >= NV) return 0;
    double d0 = depth(v0);
    if (n == 0) return d0;
    double d1 = v1 < NV ? depth(v1) : d0;
    double d2 = n >= 2 && v2 < NV ? depth(v2) : d0;
    double d3 = n == 3 && v3 < NV ? depth(v3) : d0;
    if (w0 & 0x2000) {                                       /* the farthest vertex */
        double k = fmax(d0, d1);
        if (n >= 2) k = fmax(k, d2);
        if (n == 3) k = fmax(k, d3);
        return k;
    }
    if (n == 1) return (d0 + d1) / 2;
    if (n == 2) return (d0 + d1 + d2) * 11.0 / 32.0;         /* the original's s/2 - s/8 - s/32 */
    return (d0 + d1 + d2 + d3) / 4;
}

/* ------------------------------------------------------------------------------------------------------
 * Sprites (316c, 323e, 2e49, 28eb, 2b74, 2999)
 * ------------------------------------------------------------------------------------------------------ */

static int cmp_spr(const void *a, const void *b)
{
    const SprItem *p = a, *q = b;
    if (p->key > q->key) return -1;
    if (p->key < q->key) return 1;
    return p->k - q->k;
}

#define SPR_ON_FACE 64.0                         /* depth allowance for sprites standing on a surface, world units */

/* the sun and the moon (instance 8, sprites 5 / 4): behind everything */
static bool sky_sprite(const SprItem *it)
{
    const EnhSpr *e = &S->spr[it->k];
    return e->inst == 8 && ((e->id & 0x3F) == 4 || (e->id & 0x3F) == 5);
}

static void draw_sprite(const SprItem *it)
{
    const EnhSpr *e = &S->spr[it->k];
    u8 cl = (u8)e->id;
    double key = it->key;
    double ang = it->angle;
    u8 v = (u8)((u8)(wrapu(ang) / 256.0) + 8);
    bool mirror = false;
    if (v >= 0x40) {
        if ((u8)(v + 0x74) >= 0x20 || !S->mirror_on) return;
        mirror = true;
    }
    if (key <= 0x10) return;
    /* ENH: with the far ring the sprites reach as far as it does, not the detail level's B6E2 */
    if (cl > 5) { if (key >= (S->nff > 0 ? enh_draw_dist * 0x1000 : S->spr_far_limit)) return; }
    else if (cl <= 3 && (cl & 1)) { if (key >= 0x980) return; }

    u8 s = e->id & 0x3F;
    double size = (double)((S->sprite_kind[s] & 0xF8) << 3), dk = mirror ? key * 2 : key;
    double a = atan2(size, dk) * RAD2PX / 8.0;               /* apparent size, 8-pixel units */
    const EnhSprImg *img;
    double w, h;
    if (!enh_sprite_pick(s, a, &img, &w, &h)) return;
    /* ENH: the aeroplane (sprite 2, drifting): in the original's coarse pixels it reads as far away; here, drawn
     * sharp at its full size, it looked close and low. Drawn at ENH_PLANE_SIZE of it (enh_far.c slows it too). */
    if (enh_is_plane(e->id)) { w *= ENH_PLANE_SIZE; h *= ENH_PLANE_SIZE; }

    double row = -elev(V->spr_y[it->k] - V->cam_y, it->ydist);   /* sprite_row_angle */
    double sa = wraps(ang);
    EnhPrim *p;
    if (!mirror) {
        double x = sa / 32.0;
        double top = row + 1 + V->roll_slope * wraps(ang - CX32) - h + V->cam_row;
        p = enh_prim_add(&enh_front, P_SPRITE);
        if (!p) return;
        p->sx0 = (float)((x - w / 2) * K); p->sx1 = (float)((x + w / 2) * K);
        p->sy0 = (float)(top * K);         p->sy1 = (float)((top + h) * K);
    } else {
        double x = 0x58 - wraps(ang - S->mirror_base) / 64.0;
        double rt = V->mroll_slope * wraps(ang - 0x9400);
        if (V->roll > 0) rt = -rt;
        double top = (row + 1 - rt - h - V->pitch) * 0.5 + 7;
        p = enh_prim_add(&enh_mirror, P_SPRITE);
        if (!p) return;
        p->sx0 = (float)((x - w / 2) * K); p->sx1 = (float)((x + w / 2) * K);
        p->sy0 = (float)(top * K);         p->sy1 = (float)((top + h) * K);
    }
    p->img = img;
    /* ENH: haze by the depth key; not what stands above the eye (the sun, the moon, clouds, birds) */
    p->value = V->spr_y[it->k] < V->cam_y ? ENH_HAZE(haze_amount(key)) : 0;
    /* ENH: shown only where it is nearer than the surface there, less SPR_ON_FACE so that what stands or lies on
     * a surface (trees on slopes, the road markings) is not hidden by it; not the sun and the moon (drawn first) */
    if (!sky_sprite(it)) {
        double dy = V->spr_y[it->k] - V->cam_y;
        p->ztest = true;
        p->zval = (float)(sqrt(it->dist * it->dist + dy * dy) - SPR_ON_FACE);
    }
    /* ENH: the sun and the moon (instance 8, sprites 5 / 4) are a disc and a crescent in 14 x 11 and 8 x 11
     * images (round with the VGA's tall pixels); scaled up they were blocky blobs. Drawn as smooth shapes in the
     * same rectangle: the sun the inscribed ellipse; the moon a circle 11 image pixels across at the image's
     * left edge less one 10 x 11 centred 10 pixels right of it (fitted to the image: 4 of 88 pixels differ). */
    if (e->inst == 8 && (s == 5 || s == 4) && img->w == (s == 5 ? 14 : 8) && img->h == 11) {
        u8 c = 0;
        for (int i = 0; i < img->w * img->h && !c; i++) c = img->pix[i];
        p->kind = P_ELLIPSE;
        p->value = ENH_SOLID(c);
        p->cut = false;
        if (s == 4) {
            float fx = (p->sx1 - p->sx0) / 8.0f, fy = (p->sy1 - p->sy0) / 11.0f, x0 = p->sx0;
            p->sx1 = x0 + 11 * fx;
            p->cut = true;
            p->kx0 = x0 + 5 * fx;  p->kx1 = x0 + 15 * fx;
            p->ky0 = p->sy0;       p->ky1 = p->sy0 + 11 * fy;
        }
    }
}

/* ------------------------------------------------------------------------------------------------------
 * Sky, ground, overlays; the whole frame
 * ------------------------------------------------------------------------------------------------------ */

static void sky_ground(void)
{
    double wpx = W32 / 32.0;
    double yl = V->cam_row + V->roll_slope * (0 - CX32), yr = V->cam_row + V->roll_slope * (W32 - CX32);
    EnhPrim *p = enh_prim_add(&enh_front, P_SKY);
    if (p) {
        p->value = ENH_PAIR(S->sky_pair);
        p->gradient = S->gradient;
        p->sky_base = S->sky_base;
        p->hy0 = (float)(yl * K);
        p->hslope = (float)((yr - yl) / wpx);
        p->ground = ENH_PAIR(S->ground_pair);                /* the ground below the horizon */
        p->ground_haze = haze_max > 0 ? ground_haze : NULL;
    }

    if (!S->mirror_on) return;
    /* 7b9b: the mirror's horizon from the car's pitch; the edge fills halve the rows once more */
    T = &enh_mirror;
    double d = 0x580 * 2 * V->mroll_slope;
    double a = (-V->pitch - d) * 0.5 + 15;
    double ml = a * 0.5, mr = (a + 2 * d) * 0.5;
    double sxs[4] = { 0, 0x0B00, 0x0B00, 0 };
    double sky[4] = { -4, -4, mr, ml }, gnd[4] = { ml, mr, 23, 23 };
    z_mode = Z_CLEAR;                                        /* nothing there yet */
    poly(4, sxs, sky, ENH_PAIR(S->sky_pair), false);
    poly(4, sxs, gnd, ENH_PAIR(S->ground_pair), false);
}

static void overlays(void)
{
    T = &enh_front;
    for (int i = 0; i < S->novcmd; i++) {
        const EnhOvCmd *c = &S->ov[i];
        if (c->kind == OV_PIXELS) {
            EnhPrim *p = enh_prim_add(&enh_front, P_BLOCKS);
            if (!p) return;
            p->first = c->first;
            p->count = c->count;
        } else if (c->kind == OV_QUAD) {
            double x[4], y[4];
            for (int k = 0; k < 4; k++) { x[k] = c->x[k] * 32.0; y[k] = c->y[k]; }
            poly(4, x, y, ENH_PAIR(c->colour), c->or_mode);
        } else {
            line_prim(c->x[0] * 32.0, c->y[0], c->x[1] * 32.0, c->y[1], 16, ENH_PAIR(c->colour));
        }
    }
}

void enh_scene_build(const EnhSnap *s, const EnhView *v)
{
    S = s;
    V = v;
    cam_abs_x = (double)((int)s->cam_x * 4) + wraps(v->cam_x4 - s->cam_x4);
    cam_abs_z = (double)((int)s->cam_z * 4) + wraps(v->cam_z4 - s->cam_z4);
    K = enh_scale * enh_aa;
    W32 = (double)s->w32m + 0x20;
    CX32 = W32 / 2;
    VROWS = s->rows;
    enh_target_reset(&enh_front, (int)(W32 / 32), s->rows, K);
    enh_target_reset(&enh_mirror, 88, 19, K);
    enh_front.light_max = enh_mirror.light_max = (u8)((s->day ? enh_beam_day : enh_beam_night) * 255 / 100);
    enh_front.light_soft = enh_mirror.light_soft = enh_beam_soft;

    /* how the samples map back to lines of sight, for the depth (enh_raster.c) */
    EnhMap m = { false, (double)(s->cx_hi << 8) - v->heading, v->cam_row, v->roll_slope, CX32,
                 s->mirror_base, v->pitch, v->mroll_slope };
    enh_front.map = m;
    m.mirror = true;
    enh_mirror.map = m;

    NV = s->nv + s->nfv;
    project();
    haze_setup();
    sky_ground();

    /* faces farthest first in the game's own order; the keys at the view time only place the sprites */
    for (int f = 0; f < s->nf; f++) { fkey[f] = face_key(f); forder[f] = s->order[s->nf - 1 - f]; }

    int ns = 0;
    double base = (double)(s->cx_hi << 8) - v->heading;
    for (int k = 0; k < s->nspr; k++) {
        const EnhSpr *e = &s->spr[k];
        if ((u8)e->id == 0) continue;
        /* the game's own sprites are in its wrapping coordinates, near the camera: the 16-bit wrap carries them
         * across the map's seams. The far ring's are already at their image nearest the camera, in unwrapped
         * position units, and may stand beyond 8 cells, where that wrap would fold them. */
        double dx, dz;
        if (k < s->nspr_game) {
            dx = wraps(4.0 * v->spr_x[k] - v->cam_x4);
            dz = wraps(4.0 * v->spr_z[k] - v->cam_z4);
        } else {
            dx = 4.0 * v->spr_x[k] - cam_abs_x;
            dz = 4.0 * v->spr_z[k] - cam_abs_z;
        }
        double d = sqrt(dx * dx + dz * dz);
        SprItem *it = &sprs[ns++];
        it->k = k;
        it->angle = wrapu(atan2(dx, dz) * RAD2A + base);
        it->key = fabs(v->spr_y[k] - v->cam_y) + d;
        it->dist = d;
        it->ydist = ((u8)e->id > 8 && d < s->spr_min_dist) ? s->spr_min_dist : d;
    }
    /* the car's own entry (list slot 0) stays first in the cockpit view, as the original leaves it out of
     * the sort */
    int first = (!s->ext_view && ns > 0 && sprs[0].k == 0) ? 1 : 0;
    qsort(sprs + first, (size_t)(ns - first), sizeof sprs[0], cmp_spr);

    /* ENH: instead of 323e's merge of the sprites into the faces by depth key: the sun and the moon first, then
     * all the faces (which leave their depth in the targets), then the other sprites farthest first, each shown
     * only where it is nearer than the surface already there (ENHANCED.md "Sprites") */
    for (int k = 0; k < ns; k++) if (sky_sprite(&sprs[k])) draw_sprite(&sprs[k]);
    /* ENH: the far ring first, farthest first, writing the far depth; then the game's faces where they are
     * nearer than it (ENHANCED.md "Draw distance") */
    z_far_write = true;
    for (int i = 0; i < s->nff; i++) {
        const EnhFarFace *e = &s->ff[i];
        draw_face_rec(e->w0, e->w3, e->pair, e->v[0], e->v[1], e->v[2], e->v[3]);
    }
    z_far_write = false;
    z_far_test = s->nff > 0;
    for (int fi = 0; fi < s->nf; fi++) draw_face(forder[fi]);
    z_far_test = false;
    for (int k = 0; k < ns; k++) if (!sky_sprite(&sprs[k])) draw_sprite(&sprs[k]);
    z_mode = Z_NONE;                                         /* the cockpit overlays */
    overlays();
}

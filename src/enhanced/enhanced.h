#pragma once
/* Enhanced renderer (ENHANCED.md). Not bound by the faithful-engine rules in ENGINE.md.
 *
 * The faithful engine keeps running every frame at its own pace (frame_update / frame_draw / view_present,
 * ~6.3 frames a second in a race). This module records what each of those frames built (camera, world
 * vertices and faces, sprites, the cockpit overlays), draws the 3D view and the rear-view mirror again on
 * the host at a higher resolution with anti-aliasing, for every displayed frame, with the camera and the
 * moving objects carried smoothly between the game's frames, and lays the result over the VGA screen.
 * The hooks below are called from the engine at the places marked `ENH:`. */
#include "../types.h"

#define ENH_DEFAULT_RES_SCALE 4        /* output = 320x200 times this */
#define ENH_MAX_RES_SCALE     8
#define ENH_DEFAULT_AA        2        /* samples per output pixel along each axis */
#define ENH_MAX_AA            4
#define ENH_MAX_SAMPLES       16       /* res scale x aa at most (samples per original pixel, each axis) */
#define ENH_DEFAULT_MOTION_DELAY 100   /* percent of a game frame the smooth view runs behind the game: 100 = it
                                          only interpolates between frames it has (never overshoots when the
                                          steering changes) */
#define ENH_PITCH_SMOOTH_MS   60.0     /* low-pass on the camera pitch (horizon row) */
#define ENH_DEFAULT_HAZE      70       /* distance fog: percent of the horizon's sky colour at the far edge */
#define ENH_DEFAULT_DRAW_DIST 18       /* draw distance in map cells (enh_far.c; 0 = the game's own cells only) */
#define ENH_MAX_DRAW_DIST     18       /* the map is a 32 x 16 torus: 18 reaches every distinct cell */
#define ENH_DEFAULT_FOG_START 10       /* where the fog begins: percent of the way to its full distance */
#define ENH_DEFAULT_BEAM_NIGHT 100     /* headlight beams' strength at night, percent */
#define ENH_DEFAULT_BEAM_DAY   35      /* ... by day (in rain or snow): the day palette's lit colours are far
                                          brighter than the night's */
#define ENH_DEFAULT_BEAM_SOFT  3       /* the beams' soft rim: view pixels each side of the edge (0 = hard) */
#define ENH_MAX_BEAM_SOFT      10
#define ENH_HAZE_FAR      0x3000       /* ... and where it is full (the farthest faces: about 2800h at medium detail) */
#define ENH_HAZE_EYE        50.0       /* the eye's height over the ground, for the ground's distance by row */
#ifndef ENH_BLEND_SPAN
#define ENH_BLEND_SPAN 1.0             /* game frames over which a new frame's motion takes over */
#endif

/* main.c: enabled = false is --classic (the original's picture, scaled). res_scale 1..8, aa 1..4,
 * motion_delay 0..100, haze 0..100 (0 = off). */
void enh_init(bool enabled, int res_scale, int aa, int motion_delay, int haze, int draw_dist);
/* main.c: fog_start 0..90 (percent of the fog's full distance); lights = the enhanced headlight beams (off:
 * full strength with a hard edge, as the original), their strengths 0..100 (percent), beam_soft
 * 0..ENH_MAX_BEAM_SOFT (view pixels); far_lod = keep the original's reduced far models beyond 200h. */
void enh_init_look(int fog_start, bool lights, int beam_night, int beam_day, int beam_soft, bool far_lod);

/* world_build_visible (render_world.c): the world is built anew (begin), cell c of the leg map is built. */
void enh_world_begin(void);
void enh_world_cell(u16 c);
void enh_drift_far_sprites(void);          /* drifting sprites the far ring shows, after sprite_animate */
bool enh_enabled(void);
bool enh_far_lod_enabled(void);       /* --far-lod 1: the original's reduced far models in the enhanced view too */
int  enh_res_scale(void);             /* the output scale (1 with --classic unless --res-scale says otherwise) */

/* sprites_prescale (flow_race.c stage_load_objects): the raw sprite set is at DS:2500 and the pre-scaled
 * cache has just been built from it; keep the native images and the sizes of the cached copies. */
void enh_sprites_loaded(void);

/* frame_draw end (render.c): the frame the game built — camera, vertices, faces, sprites, overlays and the
 * view buffer V — becomes the newest snapshot. */
void enh_frame_drawn(void);
/* view_present end (platform/view.c): V and the mirror image M reached the screen; records where. */
void enh_view_presented(void);
/* race_run / main_menu leave the 3D view: stop laying the enhanced view over the screen. */
void enh_stop(void);

/* Cockpit overlays drawn into V after the 3D (render.c frame_draw, render_overlay.c): recorded in drawing
 * order and replayed over the enhanced view. Pixel sections (rain, snow, windscreen drops, the mirror rim)
 * are taken from V as they are; the polygons (headlight beams, dashboard edge, wipers) are drawn again at
 * the output resolution. */
void enh_ov_begin(void);              /* before the first overlay: V snapshot */
void enh_ov_pixels(void);             /* after a pixel section: what changed in V since the last mark */
void enh_ov_skip(void);               /* after a polygon section: V changes are covered by recorded polygons */
/* quad_fill of an overlay: vertices as passed (BX, SI, DI, BP: byte offsets of scr_x / scr_y slots), the
 * fake face record DS:rec (vertex cycle), the colour pair and OR mode (face type 0). */
void enh_ov_quad(u16 a_bx, u16 b_si, u16 c_di, u16 d_bp, u16 rec, u16 colour, bool or_mode);
/* line_draw of an overlay: end points (byte offsets) and the fake record DS:rec (width flags in w0). */
void enh_ov_line(u16 p_bx, u16 q_si, u16 rec, u16 colour);

/* ---- key bindings (enh_keys.c) ----
 * A key is an XT set-1 make code, ENH_KEY_GREY = sent after an E0 prefix, with the modifiers held with it. */
#define ENH_KEY_GREY  0x0100
#define ENH_KEY_SHIFT 0x1000
#define ENH_KEY_CTRL  0x2000
#define ENH_KEY_ALT   0x4000
/* main.c --keys "name=code,...": the actions not on their default keys (code 0 = no key). False if the list
 * can't be read. */
bool enh_keys_parse(const char *spec);
/* host.c key_event: key x went down / up. Returns -1 to send x as it is, or the number of keys put in out[]
 * (at most 16; 80h = break, ENH_KEY_GREY = E0 prefix) to send instead. */
int enh_key_map(u16 x, bool down, u16 *out);
void enh_keys_focus_lost(void);       /* platform/kbd.c: every key counts as up */
void enh_keys_race(bool on);          /* race_run: the bindings apply while a race runs */
void enh_keys_suspend(bool on);       /* message_box: ... but not to a message box's answer */

/* platform/vga.c compose: the VGA picture has been scaled into xrgb (320*S x 200*S); lay the enhanced view
 * over it. pal = the DAC as XRGB. Returns true while the enhanced view is shown (the picture changes with
 * time even when the game draws nothing). */
bool enh_compose(u32 *xrgb, const u32 pal[256]);

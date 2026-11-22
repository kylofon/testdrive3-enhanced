/* Test Drive III Enhanced (SDL3) — entry point.
 *
 * usage: testdrive3-enhanced [--game-dir DIR] [--scale N] [--fullscreen] [--frame-ticks N] [--sound adlib|speaker]
 *                [--car CODE] [--course CODE] [--skill N] [--res-scale N] [--aa N] [--motion-delay P]
 *                [--haze P] [--draw-distance N] [--fog-start P] [--beam-night P] [--beam-day P]
 *                [--beam-soft N] [--enh-lights 0|1] [--keys NAME=CODE,...] [--finish-marker 0|1]
 *                [--random-rivals 0|1] [--weight 1|2|3] [--classic] [--check]
 *   --game-dir    folder with the original game files (default: "Game" in the working directory)
 *   --scale       initial window scale: 320x240 times N (default 3)
 *   --fullscreen  start in full screen (Alt+Enter switches)
 *   --frame-ticks race frame pacing in 145.6 Hz timer ticks (default HOST_DEFAULT_FRAME_TICKS = 14;
 *                 5 = the original engine cap, 29 = the race clock's design rate; PLAN.md decision 4)
 *   --sound       overrides TD3.CFG's audio device: adlib (AdLib / Sound Blaster) or speaker (PC speaker)
 *   --car         the car as if chosen in the game's menu: a slot's base name, e.g. CCNSX (PLAYDISK.DAT)
 *   --course      the course as if chosen in the game's menu: a scene's base name, e.g. SCENE02
 *   --skill       the skill level 1..9 as shown in the game (1-3 automatic gearbox)
 *   --res-scale   ENH: output resolution, 320x200 times N (default 4, 1..8)
 *   --aa          ENH: anti-aliasing, N x N samples per output pixel (default 2, 1..4; 1 = off)
 *   --motion-delay ENH: how far the smooth view runs behind the game, percent of a game frame (default 100;
 *                 0 = no delay, extrapolated; 100 = a whole frame, interpolated only)
 *   --haze        ENH: distance fog, percent of the horizon's sky colour on what is farthest (default 70,
 *                 0 = off)
 *   --draw-distance ENH: map cells drawn around the camera (default 18, 0..18; 0 = the game's own 3/6/10 cells)
 *   --fog-start   ENH: where the fog begins, percent of the way to where it is full (default 10, 0..90)
 *   --enh-lights  ENH: the enhanced headlight beams, 1 = on (default), 0 = full strength with a hard edge,
 *                 as the original (the --beam-* options are then ignored)
 *   --beam-night  ENH: headlight beams' strength at night, percent (default 100)
 *   --beam-day    ENH: headlight beams' strength by day, in rain or snow, percent (default 35)
 *   --beam-soft   ENH: the beams' soft rim, view pixels each side of the edge (default 3, 0..10; 0 = hard)
 *   --keys        ENH: key bindings, "name=code,..." for the actions not on their default keys (the launcher's
 *                 Game settings > Key Bindings; enhanced/enh_keys.c), applied while a race runs
 *   --finish-marker ENH: the direction of the leg's finish (the gas station) on the compass (default 1; 0 = off)
 *   --random-rivals ENH: the two computer cars are drawn from the other cars on the play disk (default 1;
 *                 0 = the original's selection from the three-car game, for all legs of the course)
 *   --weight      ENH: car weight multiplier, 1/2/3 (default 1 = original). Higher weight increases
 *                 gravity so cars stay planted over hills.
 *   --classic     ENH: the original's picture only (no enhanced view), at --res-scale (default 1)
 *   --check       load and verify the original executable, print a summary and exit (no window)
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "mem.h"
#include "portcfg.h"
#include "platform/vga.h"
#include "game/game.h"
#include "enhanced/enhanced.h"

static int usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--game-dir DIR] [--scale N] [--fullscreen] [--frame-ticks N] [--sound adlib|speaker]\n"
            "          [--car CODE] [--course CODE] [--skill N] [--res-scale N] [--aa N] [--motion-delay P]\n"
            "          [--haze P] [--draw-distance N] [--fog-start P] [--beam-night P] [--beam-day P]\n"
            "          [--beam-soft N] [--enh-lights 0|1] [--far-lod 0|1] [--keys NAME=CODE,...]\n"
            "          [--random-rivals 0|1] [--finish-marker 0|1] [--weight 1|2|3] [--classic] [--check]\n", prog);
    return 2;
}

int main(int argc, char **argv)
{
    const char *dir = "Game";
    int scale = 3;
    bool check = false, fullscreen = false, classic = false;
    int res_scale = -1, aa = ENH_DEFAULT_AA, motion_delay = ENH_DEFAULT_MOTION_DELAY;
    int haze = ENH_DEFAULT_HAZE, draw_dist = ENH_DEFAULT_DRAW_DIST;
    int fog_start = ENH_DEFAULT_FOG_START, beam_night = ENH_DEFAULT_BEAM_NIGHT, beam_day = ENH_DEFAULT_BEAM_DAY;
    int beam_soft = ENH_DEFAULT_BEAM_SOFT;
    bool enh_lights = true, far_lod = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--frame-ticks") && v) { host_set_frame_ticks(atoi(v)); i++; }
        else if (!strcmp(a, "--sound") && v) {
            if (!SDL_strcasecmp(v, "adlib")) portcfg.sound = 4;
            else if (!SDL_strcasecmp(v, "speaker")) portcfg.sound = 0;
            else return usage(argv[0]);
            i++;
        }
        else if (!strcmp(a, "--car") && v) { SDL_strlcpy(portcfg.car, v, sizeof portcfg.car); i++; }
        else if (!strcmp(a, "--course") && v) { SDL_strlcpy(portcfg.course, v, sizeof portcfg.course); i++; }
        else if (!strcmp(a, "--skill") && v) { portcfg.skill = atoi(v); i++; }
        else if (!strcmp(a, "--res-scale") && v) { res_scale = atoi(v); i++; }
        else if (!strcmp(a, "--aa") && v) { aa = atoi(v); i++; }
        else if (!strcmp(a, "--motion-delay") && v) { motion_delay = atoi(v); i++; }
        else if (!strcmp(a, "--haze") && v) { haze = atoi(v); i++; }
        else if (!strcmp(a, "--draw-distance") && v) { draw_dist = atoi(v); i++; }
        else if (!strcmp(a, "--fog-start") && v) { fog_start = atoi(v); i++; }
        else if (!strcmp(a, "--beam-night") && v) { beam_night = atoi(v); i++; }
        else if (!strcmp(a, "--beam-day") && v) { beam_day = atoi(v); i++; }
        else if (!strcmp(a, "--beam-soft") && v) { beam_soft = atoi(v); i++; }
        else if (!strcmp(a, "--enh-lights") && v) { enh_lights = atoi(v) != 0; i++; }
        else if (!strcmp(a, "--far-lod") && v) { far_lod = atoi(v) != 0; i++; }
        else if (!strcmp(a, "--keys") && v) {
            if (!enh_keys_parse(v)) { fprintf(stderr, "bad --keys list: %s\n", v); return usage(argv[0]); }
            i++;
        }
        else if (!strcmp(a, "--finish-marker") && v) { portcfg.finish_marker = atoi(v) != 0; i++; }
        else if (!strcmp(a, "--random-rivals") && v) { portcfg.random_rivals = atoi(v) != 0; i++; }
        else if (!strcmp(a, "--weight") && v) {
            int w = atoi(v);
            if (w < 1 || w > 3) { fprintf(stderr, "bad --weight: %s (expected 1, 2, or 3)\n", v); return usage(argv[0]); }
            portcfg.weight = w;
            i++;
        }
        else if (!strcmp(a, "--classic")) classic = true;
        else if (!strcmp(a, "--check")) check = true;
        else return usage(argv[0]);
    }

    char exe_path[1024];
    snprintf(exe_path, sizeof exe_path, "%s/%s", dir, TD_EXE_NAME);
    FILE *probe = fopen(exe_path, "rb");
    if (probe) fclose(probe);
    else snprintf(exe_path, sizeof exe_path, "%s/%s", dir, TD_EXE_ALT_NAME);
    char err[256];
    if (!mem_load_exe(exe_path, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Test Drive III Enhanced", err, NULL);
        return 1;
    }
    if (check) {
        printf("%s ok: image %u bytes at %04X:0000, DGROUP %04X, frame pacing %d ticks\n", exe_path,
               mem_image_size, LOAD_SEG, DGROUP, host_frame_ticks());
        return 0;
    }

    if (res_scale < 0) res_scale = classic ? 1 : ENH_DEFAULT_RES_SCALE;
    enh_init(!classic, res_scale, aa, motion_delay, haze, draw_dist);
    enh_init_look(fog_start, enh_lights, beam_night, beam_day, beam_soft, far_lod);
    if (!host_init(dir, scale, fullscreen)) return 1;
    vga_init();      /* mode 13h model: frame source, DAC, CRTC start */
    modules_init();  /* host handlers, code-pointer tables (modules.c) */
    int rc = game_main();
    host_shutdown();
    return rc;
}

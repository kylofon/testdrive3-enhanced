#include "portcfg.h"

#include <SDL3/SDL.h>

#include "mem.h"
#include "symbols.h"

PortConfig portcfg = { "", "", 0, -1, true, true, 1, true };

void portcfg_apply_config(void)
{
    if (portcfg.sound >= 0) DSW(DS_snd_device_cfg) = (u16)portcfg.sound;
}

/* Index of the slot whose base name is `code` (slots of `width` bytes, `count` of them), or -1. */
static int find_slot(u16 slots_ds, int width, int count, const char *code)
{
    size_t n = SDL_strlen(code);
    if (n == 0 || (int)n > width) return -1;
    for (int i = 0; i < count; i++) {
        const char *s = (const char *)mp(DGROUP, (u16)(slots_ds + i * width));
        if (SDL_strncasecmp(s, code, n) == 0 && (n == (size_t)width || s[n] == 0 || s[n] == ' '))
            return i;
    }
    return -1;
}

void portcfg_apply_playdisk(void)
{
    int car = find_slot(DS_car_slots, 6, DSB(DS_cars_on_disk), portcfg.car);
    if (car >= 0) DSW(DS_car_index) = (u16)car;
    int scene = find_slot(DS_scene_slots, 8, DSB(DS_scenes_on_disk), portcfg.course);
    if (scene >= 0) DSW(DS_scene_index) = (u16)scene;
    if (portcfg.skill >= 1 && portcfg.skill <= 9) DSW(DS_skill_level) = (u16)(portcfg.skill - 1);
}

#pragma once
/* Port options from the command line (main.c) that act inside the game, applied at the places the
 * game reads the corresponding setting, so the game behaves as if they were chosen in its own menus
 * (and remembers them in PLAYDISK.DAT like its own choices). Coordinator-owned. */
#include "types.h"

typedef struct {
    char car[8];        /* --car CODE: a car slot's base name, e.g. "CCNSX"; "" = PLAYDISK.DAT's choice */
    char course[10];    /* --course CODE: a scene slot's base name, e.g. "SCENE02"; "" = PLAYDISK.DAT's */
    int skill;          /* --skill 1..9 as shown in the game (1-3 automatic gearbox); 0 = PLAYDISK.DAT's */
    int sound;          /* --sound adlib|speaker -> TD3.CFG audio 4 / 0; -1 = TD3.CFG's */
    bool finish_marker; /* ENH --finish-marker 0|1: the finish's direction on the compass (default on) */
    bool random_rivals; /* ENH --random-rivals 0|1: random cars for the computer opponents (default on);
                         * 0 = the original's selection from the three-car game */
    int weight;         /* ENH --weight 1|2|3: car weight multiplier (gravity scaled 1x/2x/3x); 1 = original */
    bool weather_fix;   /* ENH --weather-fix 0|1: separate wind/rain/snow trigger direction and don't lose
                         * transitions during external view (default on); 0 = original shared direction */
    int ai_opponent_routing; /* ENH --ai-opponent-routing legacy|deterministic|random (0|1|2): generate opponent AI
                              * branch scripts at leg load by probing valid lane routes to the finish. 0 = legacy
                              * scene scripts, 1 = modern deterministic, 2 = modern random. */
} PortConfig;

extern PortConfig portcfg;

/* After TD3.CFG is read (config_load 0000:092a): overrides the audio device. */
void portcfg_apply_config(void);

/* After PLAYDISK.DAT is read (playdisk_load 01f4:28ae), before the car and scene .LST are loaded:
 * selects the car and scene slots and the skill level. Unknown codes are ignored. */
void portcfg_apply_playdisk(void);

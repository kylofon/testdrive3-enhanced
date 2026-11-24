/* Game flow (game_flow.md §4.3, §4.12; descriptions.md; FORMATS.md "Archive directory"): archive lookup
 * and the file loaders, the name hash, PLAYDISK.DAT, car / scene .LST, the .HI file and the shared
 * front-end data. Segments 0000 / 01f4. */
#include "game/game.h"
#include "enhanced/enhanced.h"
#include "portcfg.h"

#define ARCHIVE_DIR 0x049E          /* DS:049E, 14-byte entries {h2, h1, archive, 0, offset u32, size u32} */

/* 0000:0d62 pal_load — game_flow.md §4.3 (verified) */
void pal_load(u16 name_ds)
{
    s16 h = archive_open(name_ds);
    if (h == 0) {
        h = dos_open_read(name_ds);
        if (h == -1) fatal_exit(2);
        else dos_file_size(h);
    }
    dos_read(ds_ptr((u16)(DSB(DS_colour_offset) * 3 + 0x0B9A)), 0x150, h);   /* colour 16 + DS:90F0 */
    dos_close(h);
    if (flow_vga() && DSW(DS_game_state) != 5) pal_set();
}

/* 0000:0df6 pal_load_32 — game_flow.md §4.3 (verified in the disassembly: 60h bytes to DS:0C2A + 3*DS:90F0,
 * i.e. colour 64 + DS:90F0 — the spec's "48 +" is off by 16) */
void pal_load_32(u16 name_ds)
{
    s16 h = archive_open(name_ds);
    if (h == 0) {
        h = dos_open_read(name_ds);
        if (h == -1) fatal_exit(2);
        else dos_file_size(h);
    }
    dos_read(ds_ptr((u16)(DSB(DS_colour_offset) * 3 + 0x0C2A)), 0x60, h);
    dos_close(h);
}

/* 0000:0e74 file_load_near — game_flow.md §4.3 (as file_load_far; DS:E86A is not rewritten) */
void file_load_near(u16 name_ds, u16 dst_ds)
{
    u16 n = 0;
    s16 h = archive_open(name_ds);
    if (h == 0) {
        h = dos_open_read(name_ds);
        if (h == -1) fatal_exit(2);
        else n = dos_file_size(h);
    } else {
        n = (u16)(DSW(DS_archive_size) - 1);    /* archived entries: size - 1 */
    }
    dos_read(ds_ptr(dst_ds), n, h);
    dos_close(h);
}

/* 0000:0ee0 file_load_far — game_flow.md §4.3 */
void file_load_far(u16 name_ds, FarPtr dst)
{
    u16 n = 0;
    s16 h = archive_open(name_ds);
    if (h == 0) {
        h = dos_open_read(name_ds);
        if (h == -1) fatal_exit(2);
        else n = dos_file_size(h);
    } else {
        n = (u16)(DSW(DS_archive_size) - 1);
    }
    DSW(DS_archive_size) = n;
    DSW(DS_archive_size + 2) = 0;
    dos_read(dst, n, h);
    dos_close(h);
}

/* 0000:151c archive_open — game_flow.md §4.3 (verified) */
s16 archive_open(u16 name_ds)
{
    u32 key = name_hash((u16)(name_ds + 2));        /* name without the drive */
    for (u16 i = 0; ; i++) {
        u16 e = (u16)(ARCHIVE_DIR + i * 14);
        if (DSW(e) == 0 && DSW(e + 2) == 0) return 0;              /* not archived */
        if (DSW(e) == (u16)key && DSW(e + 2) == (u16)(key >> 16)) {
            u8 arc;
            s16 h;
            DSW(DS_archive_size) = DSW(e + 10);
            DSW(DS_archive_size + 2) = DSW(e + 12);
            flow_strcpy(DS_archive_path + 2, 0x0446);                /* "datax.dat" */
            DSB(DS_archive_path) = DSB(DS_path_program);
            arc = DSB(e + 4);
            DSB(DS_archive_path + 6) = arc;                          /* 'a'..'e' */
            if (arc == 'c') {
                DSB(DS_archive_path) = DSB(DS_path_playdisk);
            } else if (arc == 'd') {
                flow_strcpy(DS_archive_path, DS_path_car);
                flow_strcpy(DS_archive_path + 7, 0x0450);            /* ".DAT" -> "A:CCERV.DAT" */
            } else if (arc == 'e') {
                flow_strcpy(DS_archive_path, DS_path_scene);
                flow_strcpy(DS_archive_path + 9, 0x0455);            /* ".DAT" -> "A:SCENE01.DAT" */
            }
            h = dos_open_read(DS_archive_path);
            if (h == -1) {
                /* PORT: the original prints "Insert the BOOT DISK" / "PROGRAM DISK" / "your PLAY DISK" and
                 * busy-waits for a key until the archive opens; the port has no disks: a missing archive
                 * is the "Important file open failed" error. */
                fatal_exit(2);
            }
            dos_seek(h, DSW(e + 6), DSW(e + 8));
            return h;
        }
    }
}

/* 0000:16d0 name_hash_h1 — FORMATS.md (verified: from the last character to the first, h = c + mul*h,
 * signed characters, 16-bit) */
u16 name_hash_h1(u16 s_ds, u16 mul)
{
    u16 h = 0;
    s16 i = (s16)(strlen(ds_str(s_ds)) - 1);
    for (; i > -1; i--)
        h = (u16)((u16)(mul * h) + (u16)(s16)(s8)DSB((u16)(s_ds + i)));
    return h;
}

/* 0000:171a name_hash_h2 — FORMATS.md (verified: sum of c[i]*i over all characters but the last) */
u16 name_hash_h2(u16 s_ds)
{
    u16 h = 0;
    s16 n = (s16)(strlen(ds_str(s_ds)) - 1);
    for (s16 i = 0; i < n; i++)
        h = (u16)(h + (u16)((s16)(s8)DSB((u16)(s_ds + i)) * i));
    return h;
}

/* 0000:1760 name_hash — FORMATS.md (DX = h1, AX = h2) */
u32 name_hash(u16 s_ds)
{
    u16 h1 = name_hash_h1(s_ds, 0x101);
    u16 h2 = name_hash_h2(s_ds);
    return (u32)h1 << 16 | h2;
}

/* 01f4:2736 playdisk_write — descriptions.md PLAYDISK.DAT (PORT: written to the game directory) */
void playdisk_write(void)
{
    u16 f;
    playdisk_verify();
    f = crt_fopen(DS_path_playdisk_dat, 0x102E);               /* "A:PLAYDISK.DAT", "wb+" */
    DSW(DS_file_handle) = f;
    if (f == 0) {
        message_box(8);                                         /* "File Save Failure!" */
        return;
    }
    crt_fwrite(ds_ptr(DS_playdisk_label), 1, 0x12, f);
    crt_fwrite(ds_ptr(DS_car_slots), 1, 0x54, f);
    crt_fwrite(ds_ptr(DS_scene_slots), 1, 0x40, f);
    crt_fwrite(ds_ptr(DS_car_index), 2, 4, f);
    crt_fwrite(ds_ptr(DS_cars_on_disk), 1, 4, f);
    crt_fwrite(ds_ptr(DS_steering_response), 1, 1, f);
    crt_fclose(f);
}

/* 01f4:2812 playdisk_verify — descriptions.md (the PLAY DISK label check) */
void playdisk_verify(void)
{
    s16 ok = 0;
    u16 f = crt_fopen(DS_path_playdisk_dat, 0x1032);           /* "rb" */
    DSW(DS_file_handle) = f;
    if (f != 0) {
        if (crt_fread(ds_ptr(DS_scratch_buf), 1, 0x12, f) == 0x12
            && strcmp(ds_str(DS_scratch_buf), ds_str(DS_playdisk_label)) == 0)
            ok = 1;
        crt_fclose(f);
    }
    flow_strcpy(DS_msg_insert_label, DS_playdisk_label);        /* message 14h "Insert <label> in A:" */
    DSB(0x191D) = ' ';
    /* PORT: the original loops on message 14h ("Insert <label> in A:") until the label read back matches;
     * the port has no disks, so a mismatch (or a missing file) is ignored (game_flow.md §6). */
    (void)ok;
}

/* 01f4:28ae playdisk_load — descriptions.md (0 = loaded with a loadable car and scene) */
s16 playdisk_load(void)
{
    u16 f = crt_fopen(DS_path_playdisk_dat, 0x1035);           /* "rb" */
    DSW(DS_file_handle) = f;
    if (f != 0) {
        if (crt_fread(ds_ptr(DS_playdisk_label), 1, 0x12, f) == 0x12
            && crt_fread(ds_ptr(DS_car_slots), 1, 0x54, f) == 0x54
            && crt_fread(ds_ptr(DS_scene_slots), 1, 0x40, f) == 0x40
            && crt_fread(ds_ptr(DS_car_index), 2, 4, f) == 4
            && crt_fread(ds_ptr(DS_cars_on_disk), 1, 4, f) == 4) {
            crt_fread(ds_ptr(DS_steering_response), 1, 1, f);
            crt_fclose(f);
            portcfg_apply_playdisk();       /* PORT: --car / --course / --skill (launcher options) */
            if (car_lst_load((s16)DSW(DS_car_index), DS_path_car, DS_car_name, DS_car_pic_pairs) == 0
                && scene_lst_load((s16)DSW(DS_scene_index), DS_path_scene, DS_scene_name) == 0) {
                DSB(DS_playdisk_ok) = 1;
                return 0;
            }
        } else {
            crt_fclose(f);
        }
    }
    return 1;
}

/* 01f4:29f0 car_lst_load — descriptions.md car .LST (0 = loaded; 1 = empty slot / short file) */
s16 car_lst_load(s16 slot, u16 path_ds, u16 name_ds, u16 pics_ds)
{
    u16 f, slot_ds = (u16)(slot * 6 + DS_car_slots);
    if (DSB(slot_ds) != 'C') return 1;
    flow_strcpy((u16)(path_ds + 2), slot_ds);
    flow_strcpy((u16)(path_ds + 7), 0x1038);                   /* ".LST" */
    f = crt_fopen(path_ds, 0x103D);                            /* "rb" */
    DSW(DS_file_handle) = f;
    if (f == 0) return 1;
    if (crt_fread(ds_ptr(name_ds), 1, 0x13, f) == 0x13
        && crt_fread(ds_ptr(pics_ds), 2, 0xd, f) == 0xd
        && crt_fread(ds_ptr(DS_car_top_gear), 1, 0x42, f) == 0x42
        && crt_fread(ds_ptr(DS_car_unused_e541), 1, 7, f) == 7
        && crt_fread(ds_ptr(DS_car_steer_marker), 2, 5, f) == 5
        && crt_fread(ds_ptr(DS_car_colours), 1, 10, f) == 10
        && crt_fread(ds_ptr(DS_car_gauges), 2, 0x14, f) == 0x14
        && crt_fread(ds_ptr(DS_car_imperial), 1, 0xd9, f) == 0xd9
        && crt_fread(ds_ptr(DS_car_physics), 2, 0x23, f) == 0x23
        && crt_fread(ds_ptr(0x074C), 1, 0xd2, f) == 0xd2) {    /* archive directory 'd' part */
        crt_fclose(f);
        return 0;
    }
    crt_fclose(f);
    return 1;
}

/* 01f4:2bc6 scene_lst_load — descriptions.md scene .LST and <scene>.HI (0 = loaded) */
s16 scene_lst_load(s16 slot, u16 path_ds, u16 name_ds)
{
    u16 f, slot_ds = (u16)(slot * 8 + DS_scene_slots);
    if (DSB(slot_ds) != 'S') return 1;
    flow_strcpy((u16)(path_ds + 2), slot_ds);
    flow_strcpy((u16)(path_ds + 9), 0x1040);                   /* ".LST" */
    f = crt_fopen(path_ds, 0x1045);                            /* "rb" */
    DSW(DS_file_handle) = f;
    if (f == 0) return 1;
    if (!(crt_fread(ds_ptr(name_ds), 1, 0x13, f) == 0x13
          && crt_fread(ds_ptr(DS_leg_count), 1, 1, f) == 1
          && crt_fread(ds_ptr(DS_scene_pic_pairs), 2, 0x1d, f) == 0x1d
          && crt_fread(ds_ptr(DS_leg_data_digit), 1, 10, f) == 10
          && crt_fread(ds_ptr(DS_scene_leg_names), 1, 0x1b0, f) == 0x1b0
          && crt_fread(ds_ptr(DS_protection_sheet), 1, 0x2c8, f) == 0x2c8
          && crt_fread(ds_ptr(0x081E), 1, 0x196, f) == 0x196)) {   /* archive directory 'e' part */
        crt_fclose(f);
        return 1;
    }
    crt_fclose(f);

    /* the .HI: cleared first, accepted only if exactly 1C2h bytes with a matching checksum */
    flow_strcpy((u16)(path_ds + 9), 0x1048);                   /* ".HI" */
    for (s16 i = 0; i < 7; i++) DSL(DS_hi_scores + i * 4) = 0;
    for (s16 i = 0; i < 7; i++) DSB(DS_hi_car + i) = 0;
    for (s16 i = 0; i < 7; i++)
        for (s16 j = 0; j < 0xf; j++) DSB((u16)(DS_hi_names + 2 + i * 0x12 + j)) = ' ';
    for (s16 i = 0; i < 0x120; i++) DSB(DS_hi_route_best + i) = 0;   /* route + cumulative bests */
    f = crt_fopen(path_ds, 0x104C);                            /* "rb" */
    DSW(DS_file_handle) = f;
    if (f != 0) {
        if (crt_fread(ds_ptr(DS_sprite_set), 1, 0x1c2, f) == 0x1c2
            && crt_fread(ds_ptr(DS_sprite_set), 1, 1, f) == 0) {
            u8 x = 0;
            for (s16 i = 0; i < 0x1c1; i++) x ^= DSB(DS_sprite_set + i);
            if ((u8)(x ^ 0x5b) == DSB(DS_sprite_set + 0x1c1)) {
                for (s16 i = 0; i < 0x1c1; i++) DSB(DS_hi_car + i) = DSB(DS_sprite_set + i);
            }
        }
        crt_fclose(f);
    }
    return 0;
}

/* 01f4:2e66 hi_write — descriptions.md .HI (PORT: written to the game directory) */
void hi_write(void)
{
    u16 f, n;
    u8 x = 0;
    playdisk_verify();
    flow_strcpy(DS_path_scene + 2, (u16)(DSW(DS_scene_index) * 8 + DS_scene_slots));
    flow_strcpy(DS_path_scene + 9, 0x104F);                    /* ".HI" */
    f = crt_fopen(DS_path_scene, 0x1053);                      /* "wb+" */
    DSW(DS_file_handle) = f;
    for (s16 i = 0; i < 0x1c1; i++) x ^= DSB(DS_hi_car + i);
    DSB(DS_hi_checksum) = (u8)(x ^ 0x5b);
    /* PORT: the original passes a NULL FILE* to fwrite when the open failed; treated as a short write. */
    n = f ? crt_fwrite(ds_ptr(DS_hi_car), 1, 0x1c2, f) : 0;
    if ((s16)n < 0x1c2) fatal_exit(3);
    crt_fclose(f);
}

/* 01f4:53c4 shared_scene_load — game_flow.md §4.12 (front-end pictures and the shared scene sets) */
s16 shared_scene_load(void)
{
    s16 prompted = 0;
    u16 f;
    if (DSB(DS_shared_data_loaded) != 0) return 0;
    flow_strcpy(DS_path_program + 2, 0x1112);                  /* "DATAB.DAT" */
    f = crt_fopen(DS_path_program, 0x111C);                    /* "rb" */
    DSW(DS_file_handle) = f;
    if (f == 0) {
        /* PORT: the original loops on message 1Eh "Insert PROGRAM DISK in A: and press space"; no disks
         * in the port: a missing DATAB.DAT is the "Important file open failed" error. */
        prompted = 1;
        fatal_exit(2);
    }
    crt_fclose(f);
    if (DSW(DS_game_state) == 3) {
        if (prompted) music_for_state(DSB(DS_radio_station));
        return 0;
    }
    flow_strcpy(DS_path_program + 2, 0x111F);                  /* "SELECT.LZ" */
    file_load_far(DS_path_program, ds_far(DS_select_pic));
    flow_strcpy(DS_path_program + 2, 0x1129);                  /* "DIFFLEVA.LZ" */
    file_load_far(DS_path_program, ds_far(DS_diffleva_pic));
    flow_strcpy(DS_path_program + 2, 0x1135);                  /* "DIFFLEVB.LZ" */
    file_load_far(DS_path_program, ds_far(DS_difflevb_pic));
    flow_strcpy(DS_path_program + 2, 0x1141);                  /* "DIFFLEVC.LZ" */
    file_load_far(DS_path_program, ds_far(DS_difflevc_pic));
    flow_strcpy(DS_path_program + 2, 0x114D);                  /* "SSBJ.LZ" */
    file_load_far(DS_path_program, ds_far(DS_ssbj_pic));
    flow_strcpy(DS_path_program + 2, 0x1155);                  /* "SCENETTT.BIN" */
    file_load_far(DS_path_program, ds_far(DS_tiles_shared));
    flow_strcpy(DS_path_program + 9, 0x1162);                  /* "SCENETTO.BIN" */
    file_load_far(DS_path_program, ds_far(DS_objects_set));
    flow_strcpy(DS_path_program + 9, 0x1168);                  /* "SCENETTP.BIN" */
    file_load_far(DS_path_program, ds_far(DS_lanes_set));
    flow_strcpy(DS_path_program + 9, 0x116E);                  /* "SCENETTA.DAT" */
    file_load_near(DS_path_program, DS_leg_file);
    if (portcfg.weight > 1)
        DSW(DS_leg_gravity) = (u16)(DSW(DS_leg_gravity) * (u16)portcfg.weight);  /* ENH: --weight */
    flow_strcpy(DS_path_program + 9, 0x1174);                  /* "SCENETT1.DAT" */
    file_load_near(DS_path_program, DS_sprite_set);
    sprites_prescale();
    enh_sprites_loaded();                                      /* ENH: the menu preview's trees and signs */
    flow_strcpy(DS_path_program + 2, 0x117A);                  /* "NEWWAVE.MUS" */
    music_stop(0);
    file_load_far(DS_path_program, ds_far(DS_music_buf));
    DSB(DS_shared_data_loaded) = 1;
    return 0;
}

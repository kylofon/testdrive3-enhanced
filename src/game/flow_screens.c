/* Game flow (game_flow.md §4.5-4.7, §4.12, §4.19): the message box, text records, key waits, the palette
 * wrappers, the title / credits sequence, the copy-protection stubs and the PLAY DISK screen.
 * Segments 0000 / 01f4. */
#include "game/game.h"
#include "enhanced/enhanced.h"

/* wait_any(n) of game_flow.md §4.5: until a key is in DS:915B, BIOS-tick waits, the joystick counts as a key
 * (stored as 1 unless `store_code`). Inline in message_box. */
static void msg_wait_any(s16 n, bool store_code)
{
    u16 k;
    while (DSB(DS_kbd_key) == 0) {
        u16 r;
        bios_wait_ticks(n);
        r = joystick_keys(&k);
        if (r != 0) DSB(DS_kbd_key) = store_code ? (u8)r : 1;
    }
}

/* 0000:179c message_box — game_flow.md §4.5 (verified against the jump table 0000:1d8e) */
static void message_box_body(s16 id)
{
    u16 save_bg, save_page, save_fg, m;
    s16 x, keep_bg = 0;
    u16 last;

    DSB(DS_msg_protect) = 1;
    DSW(DS_top_row_prev) = 0x15;
    DSW(DS_top_row) = 0x15;
    if (id == 0x11) {                                       /* pause */
        music_stop(0);
        sfx_play(0);
    }
    save_bg = DSW(DS_text_bg);
    save_page = DSW(DS_page_cur);
    save_fg = DSW(DS_text_fg);
    m = DSW((u16)(DS_msg_table + id * 2));
    x = (DSB(m) < 5) ? 0 : (s16)(DSB(m) * 8 - 0x28);        /* box from x to 319 - x */
    if (id == 0x21) x = 0;                                  /* ENH: the reworded ticket message, from column 3 */
    flow_page(0);
    rect_save(x, (s16)(0x13f - x), 0x71, 0x85);
    gfx_set_colour(7);
    gfx_fill_rect(x, (s16)(0x13f - x), 0x71, 0x85);
    gfx_set_colour(0);
    gfx_frame(x, (s16)(0x13f - x), 0x71, 0x85);
    gfx_set_colour(8);
    gfx_frame((s16)(x + 1), (s16)(0x13e - x), 0x72, 0x84);
    text_set_colours(0, 7);
    if (id == 0x21) {
        /* ENH: the original reads "You just got a ticket, :20 penalty" (":20" is 0:20 on the clock: the ticket adds
         * 20 seconds to the race clock). Reworded, centred at column 3, row 15 as the original's record. */
        static const char ticket[] = "You just got a ticket, 20 second penalty";
        text_goto_cell(0x0F, 3);
        for (const char *c = ticket; *c; c++) text_draw_char((const u8 *)c);
    } else {
        print_records(m, 0);
    }

    switch (id) {
    case 5: {                                               /* joystick calibration */
        u16 shown = 0xff;
        joy_read_raw();
        DSW(DS_joy_x_prev) = DSW(DS_joy_x);
        DSW(DS_joy_y_prev) = DSW(DS_joy_y);
        joy_read_raw();
        DSW(DS_joy_xmin) = (u16)(DSW(DS_joy_x_prev) + DSW(DS_joy_x)) >> 1;
        DSW(DS_joy_y_min) = (u16)(DSW(DS_joy_y_prev) + DSW(DS_joy_y)) >> 1;
        DSW(DS_joy_xcentre) = DSW(DS_joy_xmin);
        DSW(DS_joy_xmax) = DSW(DS_joy_xmin);
        DSW(DS_joy_y_ctr) = DSW(DS_joy_y_min);
        DSW(DS_joy_y_max) = DSW(DS_joy_y_min);
        for (;;) {
            host_pump();                                    /* PORT: busy loop of the original */
            if (joy_read() != 0) goto keep_key;
            if (DSB(DS_kbd_key) == 0x80 || DSB(DS_kbd_key) == 0x0d) break;
            DSB(DS_joy_bits) = 0;
            joystick_direction();
            if (shown != DSB(DS_joy_bits)) {
                if (shown != 0xff) {
                    s16 cx = DSB((u16)(DS_joy_cal_col + (shown >> 2)));
                    s16 cy = DSB((u16)(DS_joy_cal_row + (shown & 3)));
                    gfx_set_colour(7);
                    gfx_fill_rect(cx, (s16)(cx + 1), cy, (s16)(cy + 1));
                }
                gfx_set_colour(0);
                shown = DSB(DS_joy_bits);
                {
                    s16 cx = DSB((u16)(DS_joy_cal_col + (shown >> 2)));
                    s16 cy = DSB((u16)(DS_joy_cal_row + (shown & 3)));
                    gfx_fill_rect(cx, (s16)(cx + 1), cy, (s16)(cy + 1));
                }
            }
        }
        break;
    }
    default:
        bios_wait_ticks(0x0c);                              /* ~0.66 s */
        goto keep_key;
    case 7: case 0x0c: case 0x0d: case 0x17: case 0x18: case 0x27: case 0x28: case 0x2c:
        bios_wait_ticks(0x1e);                              /* ~1.6 s */
        goto keep_key;
    case 8: case 0x10: case 0x14: case 0x1a: case 0x1e: case 0x23: case 0x24:
        msg_wait_any(4, false);
        if (DSB(DS_kbd_key) == 0x80) exit_to_dos();
        break;
    case 0x12:                                              /* "Exit to DOS (Y/N)?" */
        msg_wait_any(4, false);
        if (DSB(DS_kbd_key) == 'Y' || DSB(DS_kbd_key) == 'y' || DSB(DS_kbd_key) == 0x80) exit_to_dos();
        break;
    case 0x13:                                              /* PLAY DISK drive letter */
        for (;;) {
            msg_wait_any(4, true);
            last = DSB(DS_kbd_key);
            DSB(DS_kbd_key) = 0;
            if (last == 0x80) exit_to_dos();
            if (last == 0x0d) break;
            if (last == 0x91 || last == 0x92 || last == 0x94) {
                if (DSB(DS_playdisk_drive) == 'A') DSB(DS_playdisk_drive) = 'F';
                else DSB(DS_playdisk_drive)--;
            }
            if (last == 0x96 || last == 0x98 || last == 0x99) {
                if (DSB(DS_playdisk_drive) == 'F') DSB(DS_playdisk_drive) = 'A';
                else DSB(DS_playdisk_drive)++;
            }
            last &= 0xdf;
            if (last > 0x40 && last < 0x47) DSB(DS_playdisk_drive) = (u8)last;
            DSB(DS_path_scene) = DSB(DS_playdisk_drive);
            DSB(DS_path_playdisk) = DSB(DS_playdisk_drive);
            DSB(DS_path_opponent) = DSB(DS_playdisk_drive);
            DSB(DS_path_car) = DSB(DS_playdisk_drive);
            DSB(DS_path_playdisk_dat) = DSB(DS_playdisk_drive);
            DSB(0x1FB8) = DSB(DS_playdisk_drive);
            DSB(0x1921) = DSB(DS_playdisk_drive);
            print_records(m, 0);
        }
        goto keep_key;
    case 0x15: case 0x16: case 0x25: case 0x2b:
        msg_wait_any(4, false);
        if (DSB(DS_replay_state) != 0 && DSB(DS_kbd_key) == 0x8a) DSB(DS_replay_request) = 1;   /* F10 */
        break;
    case 0x21: {                                            /* ticket: prepare message 2Dh */
        s16 v = (s16)(DSB(DS_clocked_speed) * 7 - (DSW(DS_rand_hi) & 7));
        s16 h = v / 100;
        DSB(0x1B87) = h == 0 ? ' ' : (u8)(h + '0');
        DSB(0x1B88) = (u8)((u8)(v / 10) + (u8)h * (u8)-10 + '0');
        DSB(0x1B89) = (u8)(v % 10 + '0');
        DSW(DS_pending_msg) = 0x2d;
    }
        /* fall through */
    case 0x2d:
        sfx_play(0);
        /* fall through */
    case 9: case 0x11: case 0x1f: case 0x2e:
        bios_wait_ticks(0x0c);
        DSB(DS_kbd_key) = 0;
        msg_wait_any(4, false);
        break;
    case 0x26:                                              /* "Exit to MAIN SELECT SCREEN (Y/N)?" */
        DSB(DS_msg_answer) = 0;
        msg_wait_any(4, false);
        if (DSB(DS_kbd_key) == 'Y' || DSB(DS_kbd_key) == 'y') DSB(DS_msg_answer) = 1;
        break;
    case 0x31:                                              /* steering sensitivity */
        for (;;) {
            u8 raw;
            u16 k;
            while ((raw = DSB(DS_kbd_key)) == 0) {
                u16 r;
                bios_wait_ticks(2);
                r = joystick_keys(&k);
                if (r != 0) DSB(DS_kbd_key) = (u8)r;
            }
            last = DSB(DS_kbd_key);
            DSB(DS_kbd_key) = 0;
            if (last == 0x80 || last == 0x0d) break;
            if ((last == 0x91 || last == 0x92 || last == 0x94) && DSB(DS_steering_response) > 1)
                DSB(DS_steering_response)--;
            if ((last == 0x96 || last == 0x98 || last == 0x99) && DSB(DS_steering_response) < 9)
                DSB(DS_steering_response)++;
            if (last > 0x30 && last < 0x3a) DSB(DS_steering_response) = (u8)(raw - '0');
            DSB(0x1C18) = (u8)(DSB(DS_steering_response) + '0');
            print_records(m, 0);
        }
        goto keep_key;
    case 0x32:                                              /* mouse sensitivity digit */
        while (DSB(DS_kbd_key) == 0) {
            bios_wait_ticks(2);
            if (id == 0x32 && (DSB(DS_kbd_key) < '1' || DSB(DS_kbd_key) > '9')) DSB(DS_kbd_key) = 0;
        }
        DSB(DS_msg_answer) = DSB(DS_kbd_key);
        break;
    }
    DSB(DS_kbd_key) = 0;
keep_key:
    if (keep_bg == 0) rect_restore(x, (s16)(0x13f - x), 0x71, 0x85);
    DSB(DS_clock_frames) = (u8)DSW(DS_tick_count);          /* quirk, game_flow.md §9 */
    flow_page(save_page);
    gfx_set_copy_page(1);
    DSW(DS_text_fg) = save_fg;
    DSW(DS_text_bg) = save_bg;
    DSW(DS_top_row_prev) = 0;
}

/* ENH: the key bindings stand aside while a message box waits for its answer (Y / N, digits, Enter) */
void message_box(s16 id)
{
    enh_keys_suspend(true);
    message_box_body(id);
    enh_keys_suspend(false);
}

/* 01f4:1a82 print_records — game_flow.md §4.6 (verified) */
s16 print_records(u16 s_ds, s16 off)
{
    u8 c;
    do {
        text_goto_cell(DSB((u16)(s_ds + off + 1)), DSB((u16)(s_ds + off)));
        while ((c = DSB((u16)(s_ds + off + 2))) < 0x80) {
            text_draw_char(&c);
            off++;
        }
        off = (s16)(off + 3);
    } while (c != 0xaa);
    return off;
}

/* 01f4:1ad6 print_text — game_flow.md §4.6 */
s16 print_text(u16 s_ds, s16 off)
{
    u8 c;
    while ((c = DSB((u16)(s_ds + off))) < 0x80) {
        text_draw_char(&c);
        off++;
    }
    return (s16)(off + 1);
}

/* 01f4:1b0a print_record — game_flow.md §4.6 */
s16 print_record(u16 s_ds, s16 off)
{
    u8 c;
    text_goto_cell(DSB((u16)(s_ds + off + 1)), DSB((u16)(s_ds + off)));
    for (off = (s16)(off + 2); (c = DSB((u16)(s_ds + off))) < 0x80; off++)
        text_draw_char(&c);
    return (s16)(off + 1);
}

/* 01f4:1b58 print_chars — game_flow.md §4.6 */
void print_chars(u16 s_ds, s16 n)
{
    for (s16 i = 0; i < n; i++) {
        u8 c = DSB((u16)(s_ds + i));
        text_draw_char(&c);
    }
}

/* 01f4:1b8e wait_key — game_flow.md §4.6 (n BIOS-tick pairs, 0 = forever; returns the key or n) */
s16 wait_key(s16 n)
{
    u16 k;
    for (u16 i = 1; i != (u16)n; ) {
        bios_wait_ticks(2);
        k = td3_random();
        key_poll(&k);
        if (k != 0) return (s16)(k & 0xff);
        if (n != 0) i++;
    }
    return n;
}

/* 01f4:1be2 screen_present — game_flow.md §4.6 (page 1 -> screen by the 64-step dissolve) */
void screen_present(void)
{
    flow_page(0);
    dissolve_page1_to_0();
}

/* 01f4:1864 gfx_frame — game_flow.md §4.6 (verified) */
void gfx_frame(s16 x0, s16 x1, s16 y0, s16 y1)
{
    gfx_move_to(x0, y0);
    gfx_line_to(x1, y0);
    gfx_line_to(x1, y1);
    gfx_line_to(x0, y1);
    gfx_line_to(x0, y0);
}

/* 01f4:59ac pic_draw — game_flow.md §4.6 (PORT: the non-VGA rle_draw_dither path is not ported) */
void pic_draw(u16 pairs_ds, u16 npairs, u16 width, s16 x, s16 y_bottom, s16 flag)
{
    (void)flag;
    if (flow_vga()) rle_draw_page(pairs_ds, npairs, width, x, y_bottom);
}

/* 01f4:1c00 ega_pal_black — game_flow.md §4.6 */
void ega_pal_black(void)
{
    for (s16 i = 0; i < 0x20; i++) DSB(DS_scratch_buf + i) = 0;
    if (!flow_vga()) gfx_set_ega_palette(DS_scratch_buf);
}

/* 01f4:1c34 ega_pal_restore — game_flow.md §4.6 */
void ega_pal_restore(void)
{
    if (!flow_vga()) gfx_set_ega_palette(0x0E6A);
}

/* 01f4:1c48 pal_apply — game_flow.md §4.6 */
void pal_apply(void)
{
    if (flow_vga()) pal_set();
}

/* 01f4:1c56 pal_black — game_flow.md §4.6 */
void pal_black_01F4_1C56(void)
{
    if (flow_vga()) pal_black_0C1C_0C16();
}

/* 01f4:1c64 pal_fade_in — game_flow.md §4.6 */
void pal_fade_in_01F4_1C64(void)
{
    if (flow_vga()) pal_fade_in_0C1C_0BE5();
}

/* 01f4:1c72 pal_fade_out — game_flow.md §4.6 */
void pal_fade_out_01F4_1C72(void)
{
    if (flow_vga()) pal_fade_out_0C1C_0B5B();
}

/* 01f4:1c80 pal_fade_out_low — game_flow.md §4.6 */
void pal_fade_out_low_01F4_1C80(void)
{
    if (flow_vga()) pal_fade_out_low_0C1C_0BB7();
}

/* 01f4:1c8e page_keep_high — game_flow.md §4.6 */
void page_keep_high(void)
{
    if (flow_vga()) page_clear_low_colours();
}

/* 01f4:1c9c ega_pal_init — game_flow.md §4.6 (Tandy / EGA palette registers; nothing in VGA) */
void ega_pal_init(void)
{
    if (DSW(DS_video_mode) == 9) {
        for (s16 i = 0; i < 0x10; i++) {
            u16 a = (u16)(0x0E6A + i * 2);
            DSW(a) = (u16)(((DSB(a) & 0x10) >> 1) | (DSW(a) & 7));
        }
    } else if (DSW(DS_video_mode) != 0x0d) {
        return;
    }
    gfx_set_ega_palette(0x0E6A);
}

/* 01f4:0070 title_sequence — game_flow.md §4.7 (VGA path; every step skipped once a key was seen) */
s16 title_sequence(void)
{
    s16 c80, c91, c92, c94;
    u16 k;

    DSB(DS_colour_offset) = 0x80;
    flow_strcpy(DS_path_program + 2, 0x0E8A);                  /* "ACCOCOLR.BIN" */
    pal_load(DS_path_program);
    DSB(DS_skip_enabled) = 0;
    DSB(DS_skip_flag) = 0;
    DSB(DS_radio_station) = 0;
    music_for_state(0);                                        /* THEME.MUS */
    DSW(DS_scene_index) = 0;
    DSW(DS_car_index) = 0;
    flow_strcpy(DS_path_program + 2, 0x0E97);                  /* "ACCO.LZ" */
    flow_load_decode(DS_path_program);
    flow_page(1);
    pic_draw(DS_sprite_set, 0xa77, 0x140, 0, 199, 0);
    if (flow_vga()) { c80 = 0x80; c91 = 0x91; c92 = 0x92; c94 = 0x94; }
    else            { c80 = 0;    c91 = 4;    c92 = 0x0c; c94 = 4; }
    screen_present();
    if (DSB(DS_skip_flag) != 0) return 0;

    /* light sweep across the logo */
    gfx_set_colour(c80);
    gfx_move_to(0, 0x41);
    gfx_read_bitmap(DS_scratch_buf, 1, 5);
    gfx_set_colour(c91);
    gfx_read_bitmap(DS_scratch_buf + 0x0a, 2, 5);
    gfx_set_colour(c92);
    gfx_read_bitmap(DS_scratch_buf + 0x14, 2, 5);
    gfx_set_colour(c94);
    gfx_read_bitmap(DS_scratch_buf + 0x1e, 2, 5);
    DSW(DS_tick_count) = 0;
    for (s16 x = 0; x < 0x121; x += 2) {
        u16 t = DSW(DS_tick_count);
        key_poll(&k);
        if (DSB(DS_skip_flag) != 0) return 0;
        while (t == DSW(DS_tick_count)) {                      /* one 145.6 Hz tick */
            k = td3_random();
            host_pump();                                       /* PORT: busy wait */
        }
        gfx_move_to(x, 0x41);
        gfx_set_colour(c94);
        gfx_draw_bitmap(DS_scratch_buf + 0x1e, 2, 5);
        gfx_set_colour(c92);
        gfx_draw_bitmap(DS_scratch_buf + 0x14, 2, 5);
        gfx_set_colour(c91);
        gfx_draw_bitmap(DS_scratch_buf + 0x0a, 2, 5);
        gfx_set_colour(c80);
        gfx_draw_bitmap(DS_scratch_buf, 1, 5);
    }
    wait_key(flow_vga() ? 0x2c : 3);
    if (DSB(DS_skip_flag) != 0) return 0;

    DSB(DS_colour_offset) = 0;
    flow_strcpy(DS_path_program + 2, 0x0E9F);                  /* "TITLCOLR.BIN" */
    pal_load(DS_path_program);
    flow_page(1);
    flow_strcpy(DS_path_program + 2, 0x0EAC);                  /* "TITLE2.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x2464, 0xa0, 0, 199, 0);
    flow_strcpy(DS_path_program + 2, 0x0EB6);                  /* "TITLE1.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x2715, 0xa0, 0, 99, 1);
    page_mirror_left_half();
    screen_present();
    if (DSB(DS_skip_flag) != 0) return 0;

    flow_page(1);
    DSB(DS_colour_offset) = 0x80;
    flow_strcpy(DS_path_program + 2, 0x0EC0);                  /* "TITL2COL.BIN" */
    pal_load(DS_path_program);
    DSB(DS_colour_offset) = 0;
    if (flow_vga()) {
        flow_strcpy(DS_path_program + 2, 0x0ECD);              /* "TITLEANI.LZ" */
        flow_load_decode(DS_path_program);
        pic_draw(DS_sprite_set, 0x2de3, 0x140, 0, 199, 0);
    }
    flow_strcpy(DS_path_program + 2, 0x0ED9);                  /* "TITLELET.LZ" */
    flow_load_decode(DS_path_program);
    if (flow_vga()) {
        static const s16 lets[11][6] = {                       /* gfx_copy_rect(x0,x1,y0,y1,dx,dyb,1,0) */
            {0x53, 0x69, 0x89, 0x91, 0x94, 0x86}, {0x3a, 0x52, 0x88, 0x91, 0x93, 0x87},
            {0x1e, 0x39, 0x87, 0x91, 0x92, 0x87}, {0x00, 0x1d, 0x86, 0x91, 0x91, 0x88},
            {0xdf, 0xff, 0x98, 0xa5, 0x8f, 0x8a}, {0xb0, 0xd3, 0x8d, 0x9c, 0x8e, 0x8b},
            {0x7e, 0xaf, 0x86, 0x9c, 0x87, 0x91}, {0x101, 0x13f, 0x8a, 0xa5, 0x80, 0x94},
            {0xdf, 0x12d, 0xa6, 199, 0x78, 0x98}, {0x7e, 0xde, 0x9d, 199, 0x6f, 0x9e},
            {0x00, 0x7d, 0x92, 199, 0x61, 0xa5} };
        for (s16 i = 0; i < 11; i++) {
            gfx_copy_rect(lets[i][0], lets[i][1], lets[i][2], lets[i][3], lets[i][4], lets[i][5], 1, 0);
            bios_wait_ticks(2);
        }
    }
    pic_draw(DS_sprite_set, 0x1fd1, 0xf0, 0x30, 0x52, 0);     /* TITLELET on page 1 */
    gfx_copy_rect(0x30, 0x11f, 0x0e, 0x52, 0x30, 0x52, 1, 0);
    flow_strcpy(DS_path_program + 2, 0x0EE5);                  /* "TITLEL2.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0xb12, 0x100, 0x20, 0xc6, 0);
    gfx_copy_rect(0x20, 0x11f, 0xb4, 0xc6, 0x20, 0xc6, 1, 0);
    flow_strcpy(DS_path_program + 2, 0x0EF0);                  /* "TITLECAR.LZ" */
    flow_load_decode(DS_path_program);
    if (flow_vga()) {
        wait_key(0x28);
        if (DSB(DS_skip_flag) != 0) return 0;
    }
    pic_draw(DS_sprite_set, 0xf5e, 0x80, 0x60, 0xa5, 0);
    gfx_copy_rect(0x60, 0xdf, 0x70, 0xa5, 0x60, 0xa5, 1, 0);
    pal_fade_out_low_01F4_1C80();                              /* colours 0-127 */
    flow_page(0);
    page_keep_high();
    pal_apply();
    DSB(DS_colour_offset) = 0x80;
    wait_key(flow_vga() ? 0x28 : 3);
    if (DSB(DS_skip_flag) != 0) return 0;

    if (flow_vga()) {
        pal_black_01F4_1C56();
        flow_page(0);
        gfx_set_colour(0);
        gfx_fill_rect(0, 0x13f, 0, 199);
        pal_apply();
    }
    DSB(DS_skip_enabled) = 0;
    flow_page(1);
    DSB(DS_colour_offset) ^= 0x80;
    flow_strcpy(DS_path_program + 2, 0x0EFC);                  /* "CREDCOLR.BIN" */
    pal_load(DS_path_program);
    flow_strcpy(DS_path_program + 2, 0x0F09);                  /* "CREDITC.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x2e71, 0x140, 0, 199, 0);
    flow_strcpy(DS_path_program + 2, 0x0F14);                  /* "CREDITB.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x3224, 0x140, 0, 0x86, 1);
    flow_strcpy(DS_path_program + 2, 0x0F1F);                  /* "CREDITA.LZ" */
    flow_load_decode(DS_path_program);
    pic_draw(DS_sprite_set, 0x2cce, 0x140, 0, 0x45, 1);
    gfx_set_colour(flow_vga() ? 0x13 : 0);                     /* text bar */
    gfx_fill_rect(0, 0x13f, 0, 0x0d);
    gfx_set_colour(8);
    gfx_fill_rect(1, 0x13e, 1, 0x0c);
    gfx_set_colour(7);
    gfx_fill_rect(2, 0x13d, 2, 0x0b);
    flow_page(0);
    screen_present();
    if (DSB(DS_skip_flag) != 0) return 0;
    text_set_colours(0, 7);
    return credits_text(0x1C5C);
}

/* 01f4:19f4 credits_text — game_flow.md §4.7 (each line 39 x 2 BIOS ticks; list ends with 00 00) */
s16 credits_text(u16 s_ds)
{
    s16 o = 0;
    u16 k;
    flow_page(0);
    do {
        text_goto(3, 1);
        o = print_text(s_ds, o);
        for (s16 i = 0; i < 0x27; i++) {
            bios_wait_ticks(2);
            k = td3_random();
            key_poll(&k);
            if (k != 0) return 0;
        }
    } while ((u16)DSB((u16)(s_ds + o + 1)) + DSB((u16)(s_ds + o)) != 0);
    return 0;
}

/* 01f4:1ec2 copy_protection_check — game_flow.md §4.19 (PORT: the "passed" path; the check is patched out in
 * this copy as well, the dead code 01f4:1ed7-262a is not ported) */
s16 copy_protection_check(void)
{
    return 0;
}

/* 01f4:262a protection_picture_draw — game_flow.md §4.19 (dead code: only the patched-out check calls it) */
void protection_picture_draw(s16 n, s16 x)
{
    s16 px = (s16)((n / 6) * 0x40), py = (s16)((n % 6) * 0x18);
    gfx_copy_rect((s16)(px + 0x80), (s16)(px + 0xbf), py, (s16)(py + 0x17), x, 0xbf, 1, 0);
}

/* 01f4:26fe playdisk_prompt — game_flow.md §4.12 */
void playdisk_prompt(void)
{
    /* PORT: the original loops { message 13h (drive A-F), floppy: message 10h "Insert PLAY DISK" } until
     * playdisk_load succeeds. No disks in the port: PLAYDISK.DAT is read from the game directory once; if it
     * (or its car / scene) cannot be loaded the game cannot run. */
    if (playdisk_load() != 0)
        host_fatal("PLAYDISK.DAT (or the car / scene it selects) could not be loaded from the game directory.");
    playdisk_screen();
}

/* 01f4:2f0e playdisk_screen — game_flow.md §4.12 (the PLAY DISK screen) */
void playdisk_screen(void)
{
    s16 o;
    flow_page(0);
    gfx_set_colour(0);
    gfx_fill_rect(0, 0x13f, 0, 199);
    gfx_set_colour(10);
    gfx_frame(4, 0x9c, 0x29, 0xaa);
    gfx_frame(0xa4, 0x13c, 0x29, 0xaa);
    text_set_colours(0x0f, 0);
    o = print_records(0x1F7E, 0);                              /* "PLAY DISK ", "Press space...", "CARS", "COURSES" */
    text_set_colours(0x0e, 0);
    print_records(0x1F7E, o);                                  /* the "A:" record (quirk, game_flow.md §9) */
    text_goto_cell(2, 0x0d);
    print_chars(DS_playdisk_label, 0x11);
    text_set_colours(0x0b, 0);
    for (s16 s = 0; s < 0x0e; s++) {
        if (car_lst_load(s, DS_path_car, DS_car_name, DS_car_pic_pairs) == 0) {
            text_goto_cell((s16)(s + 6), 1);
            print_chars(DS_car_name, 0x12);
        }
    }
    car_lst_load((s16)DSW(DS_car_index), DS_path_car, DS_car_name, DS_car_pic_pairs);
    text_set_colours(0x0c, 0);
    for (s16 s = 0; s < 0x0e; s++) {
        if (scene_lst_load(s, DS_path_scene, DS_scene_name) == 0) {
            text_goto_cell((s16)(s + 6), 0x15);
            print_chars(DS_scene_name, 0x12);
        }
    }
    scene_lst_load((s16)DSW(DS_scene_index), DS_path_scene, DS_scene_name);
    DSB(DS_kbd_key) = 0;
    wait_key(0);
}

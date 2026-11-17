/* Platform: page helpers, dissolve, the race view presenter and the mirror blit (segment 0c1c, mode 13h
 * paths) — platform.md §4.3.4, §4.3.5, §4.3.7.
 * The page segments are DS:90CC[2] (page 0 = A000h VRAM, page 1 = RAM), the 3D render buffer DS:90D0
 * (320 x 96) and the mirror image DS:90D2. Offsets are 16-bit and wrap inside their segment as on the
 * original. The EGA/Tandy paths of these functions are not ported (PORT). */
#include "platform/platform.h"
#include "platform/plat_priv.h"
#include "game/flow.h"
#include "host.h"
#include "enhanced/enhanced.h"

#define VIDEO_MODE_VGA 0x13

static bool vga_build(void) { return DSB(DS_video_mode) == VIDEO_MODE_VGA; }

static u16 page_seg_cur(void) { return DSW((u16)(DS_page_seg + (DSW(DS_page_cur) << 1))); }

/* rep movsb es:di <- ds:si for n bytes, 16-bit offsets. */
static void seg_copy(u16 dseg, u16 *di, u16 sseg, u16 *si, u16 n)
{
    for (; n; n--) {
        wr8(dseg, *di, rd8(sseg, *si));
        *di = (u16)(*di + 1);
        *si = (u16)(*si + 1);
    }
}

/* 0c1c:0b89 page_clear_low_colours — platform.md §4.3.4 (page DS:009A: bytes < 80h := 0) */
void page_clear_low_colours(void)
{
    u16 seg = page_seg_cur();
    for (u16 i = 0; i < 64000; i++)
        if (!(rd8(seg, i) & 0x80)) wr8(seg, i, 0);
}

/* 0c1c:15c7 page_mirror_left_half — platform.md §4.3.4 (13h path 15f4: pixel 319-x := pixel x, x < 160) */
void page_mirror_left_half(void)
{
    if (!vga_build()) return;                   /* PORT: EGA / Tandy paths not ported */
    u16 seg = page_seg_cur();
    u16 si = 0, di = 0x13E;
    for (u8 bl = 200; bl; bl--) {
        for (u16 cx = 0x50; cx; cx--) {
            u16 ax = rd16(seg, si);
            si = (u16)(si + 2);
            wr16(seg, di, (u16)(ax << 8 | ax >> 8));
            di = (u16)(di - 2);
        }
        si = (u16)(si + 0xA0);
        di = (u16)(di + 0x1E0);
    }
}

/* 0c1c:11f7 rect_save — platform.md §4.3.4 (13h path 1286: page DS:009A rectangle -> DS:90D0:0000,
 * packed rows, top row first) */
void rect_save(s16 x0, s16 x1, s16 y0, s16 y1)
{
    if (!vga_build()) return;                   /* PORT: EGA / Tandy paths not ported */
    u16 es = DSW(DS_viewbuf_seg), di = 0;
    u16 rows = (u16)(y1 - y0 + 1);
    u16 si = (u16)(plat_row320((u16)y0) + (u16)x0);
    u16 w = (u16)(x1 - x0 + 1);
    u16 ds = page_seg_cur();
    do {
        u16 s = si;
        seg_copy(es, &di, ds, &s, w);
        si = (u16)(si + 0x140);
    } while (--rows);
}

/* 0c1c:1303 rect_restore — platform.md §4.3.4 (13h path 1367: inverse of rect_save) */
void rect_restore(s16 x0, s16 x1, s16 y0, s16 y1)
{
    if (!vga_build()) return;                   /* PORT: EGA / Tandy paths not ported */
    u16 es = page_seg_cur(), si = 0;
    u16 rows = (u16)(y1 - y0 + 1);
    u16 di = (u16)(plat_row320((u16)y0) + (u16)x0);
    u16 w = (u16)(x1 - x0 + 1);
    u16 ds = DSW(DS_viewbuf_seg);
    do {
        u16 d = di;
        seg_copy(es, &d, ds, &si, w);
        di = (u16)(di + 0x140);
    } while (--rows);
}

/* 0c1c:0aa5 dissolve_poll — platform.md §4.3.5 (random(), then key_poll(&DS:90DA) if DS:009F, else
 * DS:008C = 0). The original restores SI/DI/ES from DS:90D4..90D8 for the C calls: registers are not
 * modelled. */
static void dissolve_poll(void)
{
    (void)td3_random();
    if (DSB(DS_skip_enabled) == 0) {
        DSB(DS_skip_flag) = 0;
        return;
    }
    key_poll(&DSW(DS_scratch_key));
}

/* 0c1c:08c5 dissolve_page1_to_0 — platform.md §4.3.5 (VGA path 08f6: 64 ordered 8x8 steps, one per
 * timer tick, abort on DS:008C) */
void dissolve_page1_to_0(void)
{
    dissolve_poll();
    if (DSB(DS_skip_flag)) return;
    if (DSB(DS_video_mode) <= 0x0D) return;     /* PORT: EGA (0Dh) / Tandy (09h) paths not ported */
    for (u16 bx = 0; bx < 0x40; bx++) {
        u16 es = DSW(DS_page_seg);
        u8 col = DSB((u16)(DS_dissolve_cols + bx));
        u16 di = (u16)((DSW((u16)(DS_dissolve_rows + ((bx & 7) << 1))) << 3) + col);
        u16 t0 = DSW(DS_tick_count);
        u16 ds = DSW(DS_page1_seg);
        for (u8 ah = 0x19; ah; ah--) {          /* 25 rows of this phase */
            for (u16 cx = 0x0A; cx; cx--) {     /* 40 pixels, 8 apart */
                for (int k = 0; k < 4; k++) {
                    wr8(es, di, rd8(ds, di));
                    di = (u16)(di + 8);
                }
            }
            di = (u16)(di + 0x8C0);             /* 7 rows down */
        }
        dissolve_poll();
        if (DSB(DS_skip_flag)) return;
        while (DSW(DS_tick_count) == t0) host_pump();   /* one step per timer tick */
    }
}

/* 0c1c:17cd mirror_present — platform.md §4.3.7 (waits for the vertical retrace, then the mirror image
 * DS:90D2 -> screen, 13h path 182b) */
static void mirror_present(void)
{
    /* PORT: the 3DAh retrace poll becomes host_wait_vretrace() (presents the frame). */
    host_wait_vretrace();
    if (DSB(DS_lzw_mirror_dirty) != 0 || DSB(DS_external_panel_on) != 0) return;
    if (DSB(DS_mirror_on) == 0) {
        if (DSB(DS_mirror_dirty) == 0) return;
        DSB(DS_mirror_dirty) = 0;
    }
    if (!vga_build()) return;                   /* PORT: EGA / Tandy paths not ported */
    u16 es = DSW(DS_page_seg), ds = DSW(DS_mirrorbuf_seg);
    u16 si = 0x18, di = 0x0E80;
    static const u16 len[5] = { 0x14, 0x24, 0x26, 0x27, 0x28 };        /* words per top row */
    static const u16 dadd[4] = { 0x108, 0xF6, 0xF3, 0xF1 };
    static const u16 sadd[4] = { 0x20, 0x0E, 0x0B, 0x09 };
    for (int i = 0; i < 5; i++) {
        seg_copy(es, &di, ds, &si, (u16)(len[i] << 1));
        if (i < 4) { di = (u16)(di + dadd[i]); si = (u16)(si + sadd[i]); }
    }
    di = 0x14A8;
    si = 0x1B8;
    for (u8 bl = 0x0E; bl; bl--) {              /* 14 rows of 88 pixels at x = 168 */
        seg_copy(es, &di, ds, &si, 0x58);
        di = (u16)(di + 0xE8);
    }
}

/* 0c1c:13d8 view_present — platform.md §4.3.7 (13h path 1549: 3D buffer DS:90D0 -> page 0, with the hole
 * for the rear-view mirror in its first 14 rows) */
void view_present(void)
{
    mirror_present();
    u16 si = 0;
    u16 cx = (u16)(((DSW(DS_view_w32m) >> 5) + 1) >> 1);        /* words per row */
    u16 ax = (u16)-(s16)(cx - 0xA0);                            /* x offset (bytes) */
    u16 bp = (u16)(ax << 1);                                    /* bytes skipped per row */
    u16 di = ax;
    u16 t = DSW(DS_view_top_row);
    di = (u16)(di + (u16)(t << 8 | t >> 8));                    /* xchg ah, al */
    u16 dx = DSW(DS_view_rows);
    u16 vc = (u16)((u16)-(s16)(dx - 0x60) >> 1);                /* vertical centring */
    if (DSB(DS_menu_preview) != 0) di = (u16)(di + (u16)(vc << 8 | vc >> 8));
    ax = 0x15;
    if (DSB(DS_msg_protect) == 0) {
        ax = DSW(DS_top_row);
        u16 b = ax;
        if ((s16)ax > (s16)DSW(DS_top_row_prev)) ax = DSW(DS_top_row_prev);
        DSW(DS_top_row_prev) = b;
    }
    ax = (u16)(ax - vc);
    if (!((s16)ax < 0)) {                       /* skip unchanged top rows */
        dx = (u16)(dx - ax);
        if ((s16)dx <= 0) return;
        ax = (u16)(ax << 8 | ax >> 8);          /* xchg ah, al */
        di = (u16)(di + ax);
        si = (u16)(si + ax + (ax >> 2));
    }
    if (!vga_build()) return;                   /* PORT: EGA (0Dh) / Tandy (09h) paths not ported */
    u16 es = DSW(DS_page_seg);
    u16 ds = DSW(DS_viewbuf_seg);
    di = (u16)(di + ((di & 0xFF00) >> 2));      /* row * 256 + row * 64 */
    u8 dl = (u8)dx;
    if (DSB(DS_external_panel_on) == 0) {
        u8 dh = (u8)(DSB(DS_view_rows) - dl);
        if (dh < 0x0E) {                        /* rows under the mirror: copy around the hole */
            dh = (u8)(0x0E - dh);
            do {
                if (DSB(DS_half_window) != 0) {
                    seg_copy(es, &di, ds, &si, 0x80);
                    si = (u16)(si + 0x58); di = (u16)(di + 0x58);
                    seg_copy(es, &di, ds, &si, 0x18);
                    di = (u16)(di + 0x50); si = (u16)(si + 0x50);
                } else {
                    seg_copy(es, &di, ds, &si, 0xA8);
                    si = (u16)(si + 0x58); di = (u16)(di + 0x58);
                    seg_copy(es, &di, ds, &si, 0x40);
                }
                dl--;
            } while (--dh);
            /* TODO(verify): if DL reached 0 here the original's final loop runs 256 rows; the port
             * reproduces that (it cannot happen while the view is taller than 14 rows). */
        }
    }
    do {
        seg_copy(es, &di, ds, &si, (u16)(cx << 1));
        si = (u16)(si + bp);
        di = (u16)(di + bp);
    } while (--dl);
    enh_view_presented();                       /* ENH: where V and M are on the screen now */
}

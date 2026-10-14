#include "host.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enhanced.h"   /* ENH: key bindings */
#include "opl3.h"

#define AUDIO_RATE 44100
#define SPEAKER_AMPLITUDE 5000

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioStream *audio;
static SDL_Gamepad *gamepad;
static char *game_dir;

static void (*tick_handler)(void);
static void (*kbd_handler)(u8);
static void (*focus_lost_handler)(void);
static bool (*frame_source)(u32 *);
static u32 *frame;                      /* frame_w x frame_h */
static int frame_w = 320, frame_h = 200;
#define VIEW_W(w) (w)                   /* logical presentation: frame width x 3/4 of it (4:3) */
#define VIEW_H(w) ((w) * 3 / 4)

/* Tick clock: tick n is due at start + n * 8192 / 1193182 s (exact rational arithmetic). */
static Uint64 clock_start_ns;
static Uint64 ticks_run;

/* Audio: the OPL2 (as one OPL3 in OPL2 mode) and the speaker's square wave, mixed to stereo. */
static opl3_chip opl;
static u16 spk_div;
static bool spk_on;
static double spk_phase;
static double samples_per_tick_frac;

/* Mouse: position in window-logical units and accumulated relative motion. */
static float mouse_x, mouse_y;
static float mouse_dx, mouse_dy;

static int frame_ticks = HOST_DEFAULT_FRAME_TICKS;

static void process_events(void);

bool host_init(const char *dir, int window_scale, bool fullscreen)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    game_dir = SDL_strdup(dir);
    if (window_scale < 1) window_scale = 3;
    if (!SDL_CreateWindowAndRenderer("Test Drive III Enhanced", 320 * window_scale, 240 * window_scale,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        fprintf(stderr, "window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);
    if (fullscreen) SDL_SetWindowFullscreen(window, true);
    host_set_frame_source(NULL, 320, 200);

    OPL3_Reset(&opl, AUDIO_RATE);
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, AUDIO_RATE };
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio) {
        SDL_ResumeAudioStreamDevice(audio);
        static s16 silence[AUDIO_RATE / 20 * 2];           /* 50 ms of lead-in against underruns */
        SDL_PutAudioStreamData(audio, silence, sizeof silence);
    } else {
        fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    }

    clock_start_ns = SDL_GetTicksNS();
    ticks_run = 0;
    return true;
}

static void pool_shutdown(void);

void host_shutdown(void)
{
    pool_shutdown();
    if (gamepad) SDL_CloseGamepad(gamepad);
    if (audio) SDL_DestroyAudioStream(audio);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_free(game_dir);
    SDL_Quit();
}

void host_set_tick_handler(void (*handler)(void)) { tick_handler = handler; }
void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }
void host_set_focus_lost_handler(void (*handler)(void)) { focus_lost_handler = handler; }

void host_set_frame_source(bool (*compose)(u32 *), int w, int h)
{
    frame_source = compose;
    frame_w = SDL_clamp(w, 1, HOST_FRAME_MAX_W);
    frame_h = SDL_clamp(h, 1, HOST_FRAME_MAX_H);
    SDL_free(frame);
    frame = SDL_calloc((size_t)frame_w * frame_h, sizeof *frame);
    if (!frame) host_fatal("out of memory for a %dx%d frame", frame_w, frame_h);
    /* The 200-line picture fills a 4:3 area, as it did on a VGA monitor. */
    SDL_SetRenderLogicalPresentation(renderer, VIEW_W(frame_w), VIEW_H(frame_w), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (texture) SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, frame_w, frame_h);
    /* ENH: a high-resolution frame is filtered when the window is not an exact multiple of it */
    SDL_SetTextureScaleMode(texture, frame_w > 320 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

static Uint64 tick_due_ns(Uint64 n)
{
    return clock_start_ns + n * PIT_DIV_GAME * SDL_NS_PER_SECOND / PIT_HZ;
}

static void audio_for_one_tick(void)
{
    if (!audio) return;
    samples_per_tick_frac += (double)AUDIO_RATE * PIT_DIV_GAME / PIT_HZ;
    int n = (int)samples_per_tick_frac;
    samples_per_tick_frac -= n;
    /* Drop output if the device is far behind (e.g. after a stall) instead of building latency. */
    if (SDL_GetAudioStreamQueued(audio) > AUDIO_RATE / 4 * 2 * (int)sizeof(s16)) return;
    s16 buf[2 * 512];
    if (n > 512) n = 512;
    OPL3_GenerateStream(&opl, buf, (uint32_t)n);
    double freq = (double)PIT_HZ / (spk_div ? spk_div : 65536);
    double step = freq / AUDIO_RATE;
    for (int i = 0; i < n; i++) {
        int s = 0;
        if (spk_on) {
            s = spk_phase < 0.5 ? SPEAKER_AMPLITUDE : -SPEAKER_AMPLITUDE;
            spk_phase += step;
            spk_phase -= (int)spk_phase;
        }
        for (int c = 0; c < 2; c++) {
            int v = buf[2 * i + c] + s;
            buf[2 * i + c] = (s16)SDL_clamp(v, -32768, 32767);
        }
    }
    SDL_PutAudioStreamData(audio, buf, n * 2 * (int)sizeof(s16));
}

void host_opl_write(u8 reg, u8 value) { OPL3_WriteReg(&opl, reg, value); }

void host_speaker(u16 divisor, bool on)
{
    spk_div = divisor;
    spk_on = on;
}

/* Developer aid: TD3_SNAPSHOT_DIR, see host.h. */
static void snapshot(void)
{
    static const char *dir;
    static bool checked;
    static Uint64 last_ns;
    static int n;
    if (!checked) { dir = SDL_getenv("TD3_SNAPSHOT_DIR"); checked = true; }
    if (!dir) return;
    Uint64 now = SDL_GetTicksNS();
    if (n && now - last_ns < 2 * SDL_NS_PER_SECOND) return;
    last_ns = now;
    SDL_Surface *s = SDL_CreateSurfaceFrom(frame_w, frame_h, SDL_PIXELFORMAT_XRGB8888, frame, frame_w * 4);
    if (!s) return;
    char path[512];
    SDL_snprintf(path, sizeof path, "%s/snap%04d.bmp", dir, n++);
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

static void present(void)
{
    snapshot();
    if (!texture) return;
    SDL_UpdateTexture(texture, NULL, frame, frame_w * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_FRect dst = { 0, 0, (float)VIEW_W(frame_w), (float)VIEW_H(frame_w) };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}

static Uint64 last_present_ns;
static bool repaint;  /* the window changed size or mode: present the unchanged frame again */

/* Developer aid: TD3_KEYS, see host.h. */
static void scripted_keys(void)
{
    static const char *spec;
    static bool checked;
    if (!checked) { spec = SDL_getenv("TD3_KEYS"); checked = true; }
    if (!spec || !*spec || !kbd_handler) return;
    char *end;
    double at = SDL_strtod(spec, &end);
    if (end == spec || *end != ':') { spec = NULL; return; }
    if ((double)(SDL_GetTicksNS() - clock_start_ns) / 1e9 < at) return;
    const char *p = end + 1;
    u16 keys[8];
    int n = 0;
    bool press = true, release = true;
    while (n < 8) {
        keys[n++] = (u16)SDL_strtoul(p, &end, 16);
        p = end;
        if (*p == 'p') { release = false; p++; }        /* held down */
        else if (*p == 'r') { press = false; p++; }      /* let go */
        if (*p != '+') break;
        p++;
    }
    if (press)
        for (int i = 0; i < n; i++) {
            if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
            kbd_handler((u8)keys[i]);
        }
    if (release)
        for (int i = n - 1; i >= 0; i--) {
            if (keys[i] >> 8) kbd_handler((u8)(keys[i] >> 8));
            kbd_handler((u8)(keys[i] | 0x80));
        }
    spec = *p == ',' ? p + 1 : NULL;
}

void host_pump(void)
{
    process_events();
    scripted_keys();

    bool worked = false;
    Uint64 now = SDL_GetTicksNS();
    int budget = 73;                                /* at most 0.5 s of catch-up per call */
    while (tick_due_ns(ticks_run + 1) <= now && budget-- > 0) {
        ticks_run++;
        if (tick_handler) tick_handler();
        audio_for_one_tick();
        worked = true;
    }
    if (budget < 0) {                               /* fell too far behind: resynchronise the clock */
        clock_start_ns = now - (tick_due_ns(ticks_run) - clock_start_ns);
    }

    /* Present at most once per ~8 ms; VSync paces it further. */
    if (frame_source && now - last_present_ns >= 8 * SDL_NS_PER_MS) {
        if (frame_source(frame) || repaint) {
            repaint = false;
            present();
            last_present_ns = SDL_GetTicksNS();
            worked = true;
        }
    }
    if (!worked) {
        Uint64 next = tick_due_ns(ticks_run + 1);
        now = SDL_GetTicksNS();
        if (next > now) SDL_DelayNS(SDL_min(next - now, SDL_NS_PER_MS));
    }
}

/* VGA 320x200 (mode 13h) refresh: 25.175 MHz / (800 x 449) = 70.086 Hz. */
#define VRETRACE_NUM (800ull * 449ull * SDL_NS_PER_SECOND)
#define VRETRACE_DEN (25175000ull)

void host_wait_vretrace(void)
{
    Uint64 since = SDL_GetTicksNS() - clock_start_ns;
    Uint64 next = (since * VRETRACE_DEN / VRETRACE_NUM + 1) * VRETRACE_NUM / VRETRACE_DEN;
    while (SDL_GetTicksNS() - clock_start_ns < next) host_pump();
}

/* ---------------------------------------------------------------- ENH: clock, worker pool */

u64 host_time_ns(void) { return SDL_GetTicksNS(); }
u64 host_tick_ns(void) { return tick_due_ns(ticks_run); }

#define POOL_MAX 15
static SDL_Thread *pool_threads[POOL_MAX];
static int pool_size = -1;                          /* -1 = not started */
static SDL_Semaphore *pool_wake, *pool_done;
static SDL_AtomicInt pool_next;
static int pool_count;
static void (*pool_fn)(int, void *);
static void *pool_ctx;
static bool pool_quit;

static void pool_run(void)
{
    for (;;) {
        int i = SDL_AddAtomicInt(&pool_next, 1);     /* returns the previous value */
        if (i >= pool_count) return;
        pool_fn(i, pool_ctx);
    }
}

static int pool_worker(void *unused)
{
    (void)unused;
    for (;;) {
        SDL_WaitSemaphore(pool_wake);
        if (pool_quit) return 0;
        pool_run();
        SDL_SignalSemaphore(pool_done);
    }
}

static void pool_start(void)
{
    int n = SDL_GetNumLogicalCPUCores() - 1;
    if (n > POOL_MAX) n = POOL_MAX;
    pool_size = 0;
    if (n <= 0) return;
    pool_wake = SDL_CreateSemaphore(0);
    pool_done = SDL_CreateSemaphore(0);
    if (!pool_wake || !pool_done) return;
    for (int i = 0; i < n; i++) {
        pool_threads[i] = SDL_CreateThread(pool_worker, "pool", NULL);
        if (!pool_threads[i]) break;
        pool_size++;
    }
}

static void pool_shutdown(void)
{
    if (pool_size <= 0) return;
    pool_quit = true;
    for (int i = 0; i < pool_size; i++) SDL_SignalSemaphore(pool_wake);
    for (int i = 0; i < pool_size; i++) SDL_WaitThread(pool_threads[i], NULL);
    SDL_DestroySemaphore(pool_wake);
    SDL_DestroySemaphore(pool_done);
    pool_size = 0;
}

void host_parallel_for(int n, void (*fn)(int i, void *ctx), void *ctx)
{
    if (n <= 0) return;
    if (pool_size < 0) pool_start();
    pool_fn = fn;
    pool_ctx = ctx;
    pool_count = n;
    SDL_SetAtomicInt(&pool_next, 0);
    int helpers = pool_size < n - 1 ? pool_size : n - 1;
    for (int i = 0; i < helpers; i++) SDL_SignalSemaphore(pool_wake);
    pool_run();
    for (int i = 0; i < helpers; i++) SDL_WaitSemaphore(pool_done);
}

void host_set_frame_ticks(int ticks) { frame_ticks = ticks < 1 ? 1 : ticks; }
int  host_frame_ticks(void) { return frame_ticks; }

/* ---------------------------------------------------------------- keyboard */

/* XT set-1 make code of an SDL key; bit 8 set = grey key sent with an E0 prefix. 0 = not reported.
 * (platform.md 4.4.1; keypad digits send their plain codes, the grey block its E0 codes.) */
#define GREY 0x100
static u16 xt_scan(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const u8 letter_scan[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                            0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
        return letter_scan[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return (u16)(0x02 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (u16)(0x3B + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_F11:          return 0x57;
    case SDL_SCANCODE_F12:          return 0x58;
    case SDL_SCANCODE_ESCAPE:       return 0x01;
    case SDL_SCANCODE_MINUS:        return 0x0C;
    case SDL_SCANCODE_EQUALS:       return 0x0D;
    case SDL_SCANCODE_BACKSPACE:    return 0x0E;
    case SDL_SCANCODE_TAB:          return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN:       return 0x1C;
    case SDL_SCANCODE_KP_ENTER:     return GREY | 0x1C;
    case SDL_SCANCODE_LCTRL:        return 0x1D;
    case SDL_SCANCODE_RCTRL:        return GREY | 0x1D;
    case SDL_SCANCODE_SEMICOLON:    return 0x27;
    case SDL_SCANCODE_APOSTROPHE:   return 0x28;
    case SDL_SCANCODE_GRAVE:        return 0x29;
    case SDL_SCANCODE_LSHIFT:       return 0x2A;
    case SDL_SCANCODE_BACKSLASH:    return 0x2B;
    case SDL_SCANCODE_COMMA:        return 0x33;
    case SDL_SCANCODE_PERIOD:       return 0x34;
    case SDL_SCANCODE_SLASH:        return 0x35;
    case SDL_SCANCODE_KP_DIVIDE:    return GREY | 0x35;
    case SDL_SCANCODE_RSHIFT:       return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY:  return 0x37;
    case SDL_SCANCODE_LALT:         return 0x38;
    case SDL_SCANCODE_RALT:         return GREY | 0x38;
    case SDL_SCANCODE_SPACE:        return 0x39;
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK:   return 0x46;
    case SDL_SCANCODE_KP_7:         return 0x47;
    case SDL_SCANCODE_KP_8:         return 0x48;
    case SDL_SCANCODE_KP_9:         return 0x49;
    case SDL_SCANCODE_KP_MINUS:     return 0x4A;
    case SDL_SCANCODE_KP_4:         return 0x4B;
    case SDL_SCANCODE_KP_5:         return 0x4C;
    case SDL_SCANCODE_KP_6:         return 0x4D;
    case SDL_SCANCODE_KP_PLUS:      return 0x4E;
    case SDL_SCANCODE_KP_1:         return 0x4F;
    case SDL_SCANCODE_KP_2:         return 0x50;
    case SDL_SCANCODE_KP_3:         return 0x51;
    case SDL_SCANCODE_KP_0:         return 0x52;
    case SDL_SCANCODE_KP_PERIOD:    return 0x53;
    case SDL_SCANCODE_HOME:         return GREY | 0x47;
    case SDL_SCANCODE_UP:           return GREY | 0x48;
    case SDL_SCANCODE_PAGEUP:       return GREY | 0x49;
    case SDL_SCANCODE_LEFT:         return GREY | 0x4B;
    case SDL_SCANCODE_RIGHT:        return GREY | 0x4D;
    case SDL_SCANCODE_END:          return GREY | 0x4F;
    case SDL_SCANCODE_DOWN:         return GREY | 0x50;
    case SDL_SCANCODE_PAGEDOWN:     return GREY | 0x51;
    case SDL_SCANCODE_INSERT:       return GREY | 0x52;
    case SDL_SCANCODE_DELETE:       return GREY | 0x53;
    default:                        return 0;
    }
}

static void feed(const u8 *bytes, int n)
{
    if (!kbd_handler) return;
    static FILE *klog; static int kst = -1;
    if (kst < 0) { const char *p = SDL_getenv("TD3_KBD_LOG"); klog = p ? fopen(p, "w") : NULL; kst = 1; }
    for (int i = 0; i < n; i++) {
        kbd_handler(bytes[i]);
        if (klog) { fprintf(klog, "%.4f %02X\n", SDL_GetTicksNS() / 1e9, bytes[i]); fflush(klog); }
    }
}

static void key_event(SDL_Scancode sc, bool down)
{
    if (sc == SDL_SCANCODE_PAUSE) {                 /* make only, no break sequence */
        static const u8 pause[] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
        if (down) feed(pause, sizeof pause);
        return;
    }
    if (sc == SDL_SCANCODE_PRINTSCREEN) {
        static const u8 make[] = { 0xE0, 0x2A, 0xE0, 0x37 }, brk[] = { 0xE0, 0xB7, 0xE0, 0xAA };
        if (down) feed(make, sizeof make); else feed(brk, sizeof brk);
        return;
    }
    u16 x = xt_scan(sc);
    if (!x) return;
    u16 mapped[16];
    int m = enh_key_map(x, down, mapped);          /* ENH: key bindings */
    if (m >= 0) {
        for (int i = 0; i < m; i++) {
            u8 seq[2];
            int n = 0;
            if (mapped[i] & GREY) seq[n++] = 0xE0;
            seq[n++] = (u8)mapped[i];
            feed(seq, n);
        }
        return;
    }
    u8 seq[2];
    int n = 0;
    if (x & GREY) seq[n++] = 0xE0;
    seq[n++] = (u8)(down ? (x & 0x7F) : (x & 0x7F) | 0x80);
    feed(seq, n);
}

static void process_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            host_shutdown();
            exit(0);
        case SDL_EVENT_KEY_DOWN:
            /* Alt+Enter toggles fullscreen; everything else goes to the game, repeats included. */
            if (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                if (!ev.key.repeat)
                    SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                break;
            }
            key_event(ev.key.scancode, true);
            break;
        case SDL_EVENT_KEY_UP:
            key_event(ev.key.scancode, false);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (focus_lost_handler) focus_lost_handler();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            mouse_dx += ev.motion.xrel;             /* mickeys: window pixels, as the driver's default */
            mouse_dy += ev.motion.yrel;
            SDL_ConvertEventToRenderCoordinates(renderer, &ev);
            mouse_x = ev.motion.x;
            mouse_y = ev.motion.y;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:     /* Alt+Enter, resizing, a static picture: redraw it */
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_EXPOSED:
            repaint = true;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad) gamepad = SDL_OpenGamepad(ev.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad && SDL_GetGamepadID(gamepad) == ev.gdevice.which) {
                SDL_CloseGamepad(gamepad);
                gamepad = NULL;
            }
            break;
        default:
            break;
        }
    }
}

/* ---------------------------------------------------------------- mouse, joystick */

void host_mouse_read(s16 *x, s16 *y, u8 *buttons)
{
    process_events();
    /* Logical presentation is 320 x 240 (4:3); the game's screen is 320 x 200. */
    float px = SDL_clamp(mouse_x, 0.0f, (float)VIEW_W(frame_w) - 1);
    float py = SDL_clamp(mouse_y, 0.0f, (float)VIEW_H(frame_w) - 1);
    if (x) *x = (s16)(px * 320 / VIEW_W(frame_w));
    if (y) *y = (s16)(py * 200 / VIEW_H(frame_w));
    SDL_MouseButtonFlags b = SDL_GetMouseState(NULL, NULL);
    if (buttons) *buttons = (u8)(((b & SDL_BUTTON_LMASK) ? 1 : 0) | ((b & SDL_BUTTON_RMASK) ? 2 : 0));
}

void host_mouse_motion(s16 *dx, s16 *dy)
{
    process_events();
    if (dx) *dx = (s16)mouse_dx;
    if (dy) *dy = (s16)mouse_dy;
    mouse_dx -= (s16)mouse_dx;
    mouse_dy -= (s16)mouse_dy;
}

bool host_joy_read(s16 *x, s16 *y, u8 *buttons)
{
    if (!gamepad) return false;
    s16 ax = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    s16 ay = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  ax = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) ax = 32767;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))    ay = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  ay = 32767;
    u8 b = 0;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 1;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST))  b |= 2;
    if (x) *x = ax;
    if (y) *y = ay;
    if (buttons) *buttons = b;
    return true;
}

/* ---------------------------------------------------------------- files, errors */

char *host_game_path(const char *name, bool create)
{
    char *direct = NULL;
    SDL_asprintf(&direct, "%s/%s", game_dir, name);
    if (SDL_GetPathInfo(direct, NULL)) return direct;

    int count = 0;
    char **entries = SDL_GlobDirectory(game_dir, NULL, 0, &count);
    char *found = NULL;
    for (int i = 0; entries && i < count; i++) {
        if (SDL_strcasecmp(entries[i], name) == 0) {
            SDL_asprintf(&found, "%s/%s", game_dir, entries[i]);
            break;
        }
    }
    SDL_free(entries);
    if (found) { SDL_free(direct); return found; }
    if (create) return direct;
    SDL_free(direct);
    return NULL;
}

void host_free(void *p)
{
    SDL_free(p);
}

_Noreturn void host_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "fatal: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Test Drive III Enhanced", msg, window);
    host_shutdown();
    exit(3);
}

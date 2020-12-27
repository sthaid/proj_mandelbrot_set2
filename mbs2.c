#include <common.h>

#include <util_sdl.h>

//
// defines
//

#ifdef ANDROID
#define main SDL_main
#endif

#define INITIAL_FULL_SCREEN       false
#define INITIAL_WIN_WIDTH         1700
#define INITIAL_WIN_HEIGHT        850 

#if 0  // xxx temp
#define INITIAL_CTR               (-0.75 + 0.0*I)
#define INITIAL_ZOOM              0
#define INITIAL_ZOOM_FRACTION     0.
#define INITIAL_WAVELEN_START     400
#define INITIAL_WAVELEN_SCALE     2
#else
#define INITIAL_CTR               (-0.155117074119472870 + -1.027629953839807042*I)
#define INITIAL_ZOOM              12
#define INITIAL_ZOOM_FRACTION     0.7
#define INITIAL_WAVELEN_START     400
#define INITIAL_WAVELEN_SCALE     2
#endif

#define ZOOM_STEP                 .1   // must be a submultiple of 1

#define WAVELEN_FIRST             400
#define WAVELEN_LAST              700

// xxx make bigger on android
#ifndef ANDROID
#define FONTSZ       80
#define FONTSZ_HELP  40
#define FONTSZ_INFO  50
#define FONTSZ_ALERT 80
#else
#define FONTSZ       80  // 60
#define FONTSZ_HELP  40  // 30
#define FONTSZ_INFO  50  // 30
#define FONTSZ_ALERT 80  // 60
#endif

#define SLIDE_SHOW_INTVL_US  5000000

#define PIXEL_SIZE_AT_ZOOM0 (4. / 1000.)

//
// typedefs
//

//
// variables
//

static bool full_screen;

//
// prototypes
//

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static void set_alert(int color, char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void display_alert(rect_t *pane);

static void render_hndlr_mbs(pane_cx_t *pane_cx);
static int event_hndlr_mbs(pane_cx_t *pane_cx, sdl_event_t *event);
static void zoom_step(bool dir_is_incr);
static void init_color_lut(int wavelen_start, int wavelen_scale, unsigned int *color_lut);
static void display_info_proc(rect_t *pane, uint64_t update_intvl_ms);
static void create_file(char *params);
static int save_file(void);
static void show_file(int idx);

static void render_hndlr_help(pane_cx_t *pane_cx);
static int event_hndlr_help(pane_cx_t *pane_cx, sdl_event_t *event);

static void render_hndlr_directory(pane_cx_t *pane_cx);
static int event_hndlr_directory(pane_cx_t *pane_cx, sdl_event_t *event);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    int win_width;
    int win_height;
    char create_file_param[100] = "";

    // debug print program startup
    INFO("program starting\n");

    // xxx comments
    while (true) {
        unsigned char opt_char = getopt(argc, argv, "c:d");
        if (opt_char == 0xff) {
            break;
        }
        switch (opt_char) {
        case 'c':
            snprintf(create_file_param, sizeof(create_file_param), "%s", optarg);  // xxx retest
            break;
        case 'd':
            debug_enabled = true;
            break;
        default:
            return 1;
        }
    }

    // initialize the caching code
    cache_init(PIXEL_SIZE_AT_ZOOM0);

    // xxx comment
    if (create_file_param[0] != '\0') {
        create_file(create_file_param);
        return 0;
    }

    // init sdl
    full_screen = INITIAL_FULL_SCREEN;
    win_width   = !full_screen ? INITIAL_WIN_WIDTH  : 0;
    win_height  = !full_screen ? INITIAL_WIN_HEIGHT : 0;
    if (sdl_init(&win_width, &win_height, full_screen, true, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    INFO("actual win_width=%d win_height=%d\n", win_width, win_height);

    // run the pane manger;
    // the sdl_pane_manager is the runtime loop, and it will repeatedly call the pane_hndlr,
    //  first to initialize the pane_hndlr and subsequently to render the display and
    //  process events
    sdl_pane_manager(
        NULL,           // context
        NULL,           // called prior to pane handlers
        NULL,           // called after pane handlers
        50000,          // 0=continuous, -1=never, else us
        1,              // number of pane handler varargs that follow
        pane_hndlr, NULL, 0, 0, win_width, win_height, PANE_BORDER_STYLE_NONE);

    // done
    INFO("program terminating\n");
    return 0;
}

// -----------------  PANE_HNDLR  ---------------------------------------

#define DISPLAY_MBS        1
#define DISPLAY_HELP       2
#define DISPLAY_DIRECTORY  3

#define SET_DISPLAY(x) \
    do { \
        display_select = (x); \
        display_select_count++; \
    } while (0)

typedef struct {
    char     str[200];
    int      color;
    uint64_t expire_us;
} alert_t;

static int     display_select       = DISPLAY_MBS;
static int     display_select_count = 1;
static alert_t alert                = {.expire_us = 0};

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event)
{
    rect_t * pane = &pane_cx->pane;

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        DEBUG("PANE x,y,w,h  %d %d %d %d\n", pane->x, pane->y, pane->w, pane->h);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        // if window size has changed then update the pane's location
        int win_width, win_height;
        static int last_win_width, last_win_height;
        sdl_get_window_size(&win_width, &win_height);
        if (win_width != last_win_width || win_height != last_win_height) {
            INFO("new window size = %d x %d\n", win_width, win_height);
            sdl_pane_update(pane_cx, 0, 0, win_width, win_height);
            last_win_width = win_width;
            last_win_height = win_height;
        }

        // call the selected render_hndlr
        switch (display_select) {
        case DISPLAY_MBS:
            render_hndlr_mbs(pane_cx);
            break;
        case DISPLAY_HELP:
            render_hndlr_help(pane_cx);
            break;
        case DISPLAY_DIRECTORY:
            render_hndlr_directory(pane_cx);
            break;
        }

        // display alert messages in the center of the pane
        display_alert(pane);

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        int rc = PANE_HANDLER_RET_NO_ACTION;

        // first handle events common to all displays
        switch (event->event_id) {
        case 'f':  // full screen
            full_screen = !full_screen;
            INFO("set full_screen to %d\n", full_screen);
            sdl_full_screen(full_screen);
            break;
        case 'q':  // quit
            rc = PANE_HANDLER_RET_PANE_TERMINATE;
            break;

        // it is not a common event, so call the selected event_hndlr
        default:
            switch (display_select) {
            case DISPLAY_MBS:
                rc = event_hndlr_mbs(pane_cx, event);
                break;
            case DISPLAY_HELP:
                rc = event_hndlr_help(pane_cx, event);
                break;
            case DISPLAY_DIRECTORY:
                rc = event_hndlr_directory(pane_cx, event);
                break;
            }
            break;
        }

        return rc;
    }

    // ---------------------------
    // -------- TERMINATE --------
    // ---------------------------

    if (request == PANE_HANDLER_REQ_TERMINATE) {
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

static void set_alert(int color, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(alert.str, sizeof(alert.str), fmt, ap);
    va_end(ap);

    alert.color = color;
    alert.expire_us = microsec_timer() + 3000000;
}

static void display_alert(rect_t *pane)
{
    int x, y;

    if (microsec_timer() > alert.expire_us) {
        return;
    }

    x = pane->w / 2 - strlen(alert.str) * sdl_font_char_width(FONTSZ_ALERT) / 2;
    y = pane->h / 2 - sdl_font_char_height(FONTSZ_ALERT) / 2;
    sdl_render_printf(pane, x, y, FONTSZ_ALERT, alert.color, SDL_BLACK, "%s", alert.str);
}

// - - - - - - - - -  PANE_HNDLR : MBS   - - - - - - - - - - - - - - - -

#define AUTO_ZOOM_OFF  0
#define AUTO_ZOOM_IN   1
#define AUTO_ZOOM_OUT  2

#define ZOOM_TOTAL (zoom + zoom_fraction)

static complex_t    ctr                          = INITIAL_CTR;
static int          zoom                         = INITIAL_ZOOM;
static double       zoom_fraction                = INITIAL_ZOOM_FRACTION;
static int          wavelen_start                = INITIAL_WAVELEN_START;
static int          wavelen_scale                = INITIAL_WAVELEN_SCALE;

static int          auto_zoom                    = AUTO_ZOOM_OFF;
static int          auto_zoom_last               = AUTO_ZOOM_IN;
static bool         display_info                 = false;
static bool         debug_force_cache_thread_run = false;
static bool         ctrls_enabled                = true;
static bool         slide_show_enabled           = false;
static uint64_t     slide_show_time_us           = 0;
static int          slide_show_idx               = -1;
static unsigned int color_lut[65536];

static void render_hndlr_mbs(pane_cx_t *pane_cx)
{
    int      idx = 0, pixel_x, pixel_y;
    uint64_t time_now_us = microsec_timer();
    uint64_t update_intvl_ms;
    rect_t  *pane = &pane_cx->pane;

    static texture_t       texture;
    static unsigned int   *pixels;
    static unsigned short *mbsval;
    static uint64_t        last_update_time_us;
    static unsigned int    last_display_select_count;

    // XXX organize the order
    #define SDL_EVENT_MBS_CTRLS             (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_MBS_SHOW_NEXT_FILE    (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_MBS_SHOW_PRIOR_FILE   (SDL_EVENT_USER_DEFINED + 2)
    #define SDL_EVENT_MBS_HELP              (SDL_EVENT_USER_DEFINED + 3)
    #define SDL_EVENT_MBS_INFO              (SDL_EVENT_USER_DEFINED + 4)
    #define SDL_EVENT_MBS_FILES             (SDL_EVENT_USER_DEFINED + 5)
    #define SDL_EVENT_MBS_ZOOM_AUTO         (SDL_EVENT_USER_DEFINED + 6)
    #define SDL_EVENT_MBS_ZOOM_IN           (SDL_EVENT_USER_DEFINED + 7)
    #define SDL_EVENT_MBS_ZOOM_OUT          (SDL_EVENT_USER_DEFINED + 8)
    #define SDL_EVENT_MBS_PAN               (SDL_EVENT_USER_DEFINED + 9)
    #define SDL_EVENT_MBS_MOUSE_WHEEL_ZOOM  (SDL_EVENT_USER_DEFINED + 10)
    #define SDL_EVENT_MBS_SLIDE_SHOW        (SDL_EVENT_USER_DEFINED + 11)
    #define SDL_EVENT_MBS_SAVE              (SDL_EVENT_USER_DEFINED + 12)

    // if re-entering mbs display then reset/init stuff
    if (display_select_count != last_display_select_count) {
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        auto_zoom = AUTO_ZOOM_OFF;
        last_display_select_count = display_select_count;
    }

    // determine the display update interval, 
    // which may be displayed in the info area at top left
    // XXX is this used
    update_intvl_ms = (last_update_time_us != 0
                       ? (time_now_us - last_update_time_us) / 1000
                       : 0);
    last_update_time_us = time_now_us;

    // XXX comment
    if (pixels == NULL) {
        texture = sdl_create_texture(CACHE_WIDTH,CACHE_HEIGHT);
        pixels = malloc(CACHE_WIDTH*CACHE_HEIGHT*BYTES_PER_PIXEL);
        mbsval = malloc(CACHE_WIDTH*CACHE_HEIGHT*2);
    }

    // if auto_zoom is enabled then increment or decrement the zoom until limit is reached 
    // xxx fix comment
    // xxx kill off auto zoom in slide show
    if (auto_zoom != AUTO_ZOOM_OFF) {
        zoom_step(auto_zoom == AUTO_ZOOM_IN);
        if (ZOOM_TOTAL == 0) {
            auto_zoom = AUTO_ZOOM_IN;
        } else if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
            auto_zoom = AUTO_ZOOM_OUT;
        }
    }

    // inform mandelbrot set cache of the current ctr and zoom
    // xxx comments
    // xxx disable pan/zoom when slide show is enabled, and when ctrls are disabled
    // XXX review this
    static bool first_call = true;
    if (first_call && max_file_info > 0) {
        show_file(0);
    } else if (slide_show_enabled) {
        if (microsec_timer() > slide_show_time_us + SLIDE_SHOW_INTVL_US) {
            slide_show_idx = (slide_show_idx + 1) % max_file_info;  // xxxx must be > 0
            show_file(slide_show_idx);
            slide_show_time_us = microsec_timer();
        }
    } else {
        cache_param_change(ctr, zoom, debug_force_cache_thread_run);
        debug_force_cache_thread_run = false;
    }
    first_call = false;

    // get the cached mandelbrot set values; and
    // convert them to pixel color values
    cache_get_mbsval(mbsval);
    for (pixel_y = 0; pixel_y < CACHE_HEIGHT; pixel_y++) {
        for (pixel_x = 0; pixel_x < CACHE_WIDTH; pixel_x++) {
            pixels[idx] = color_lut[mbsval[idx]];
            idx++;
        }
    }

    // copy the pixels to the texture
    sdl_update_texture(texture, (void*)pixels, CACHE_WIDTH*BYTES_PER_PIXEL);

    // determine the source area of the texture, (based on the zoom_fraction)
    // that will be rendered by the call to sdl_render_scaled_texture_ex below
    rect_t src;
    double tmp = pow(2, -zoom_fraction);
    src.w = (CACHE_WIDTH - 200) * tmp;
    src.h = (CACHE_HEIGHT - 200) * tmp;
    src.x = (CACHE_WIDTH - src.w) / 2;
    src.y = (CACHE_HEIGHT - src.h) / 2;

    // render the src area of the texture to the entire pane
    rect_t dst = {0,0,pane->w,pane->h};
    sdl_render_scaled_texture_ex(pane, &src, &dst, texture);

    // display info in upper left corner
    if (display_info) {
        display_info_proc(pane, update_intvl_ms);
    }

#if 0
    // when debug_enabled display a squae in the center of the pane;
    // the purpose is to be able to check that the screen's pixels are square
    // (if the display settings aspect ratio doesn't match the physical screen
    //  dimensions then the pixels will not be square)
    if (debug_enabled) {
        rect_t loc = {pane->w/2-100, pane->h/2-100, 200, 200};
        sdl_render_rect(pane, &loc, 1, SDL_WHITE);
    }
#endif

    // register for events
    // XXX cleanup this section, and note which are for linux only

    sdl_register_event(pane, pane, SDL_EVENT_MBS_PAN, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    sdl_register_event(pane, pane, SDL_EVENT_MBS_MOUSE_WHEEL_ZOOM, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

    int fcw = sdl_font_char_width(FONTSZ);
    int fch = sdl_font_char_height(FONTSZ);
    rect_t loc;

    loc = (rect_t){pane->w/2-5*fcw/2, 0, 5*fcw, fch};
    if (ctrls_enabled) {
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "CTRLS", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_CTRLS, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
    } else {
        loc = (rect_t){pane->w/2-20*fcw/2, 0, 20*fcw, 2*fch};
        sdl_register_event(pane, &loc, SDL_EVENT_MBS_CTRLS, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
    }

    if (ctrls_enabled) {
        static int first=1;
        if (first) {
            first = 0;
            INFO("fcw/h = %d %d\n", fcw, fch);
        }

        loc = (rect_t){pane->w-4*fcw, 0, 4*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "HELP", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_HELP, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        loc = (rect_t){0, 0, 4*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "INFO", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_INFO, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        loc = (rect_t){0, pane->h/2-fch/2, 3*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, " < ", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_SHOW_PRIOR_FILE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        loc = (rect_t){pane->w-3*fcw, pane->h/2-fch/2, 3*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, " > ", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_SHOW_NEXT_FILE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        loc = (rect_t){pane->w-5*fcw, pane->h-fch, 5*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "FILES", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_FILES, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        loc = (rect_t){pane->w/2-7*fcw/2, pane->h-fch, 7*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "ZM_AUTO", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_ZOOM_AUTO, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        loc = (rect_t){pane->w*.25-6*fcw/2, pane->h-fch, 6*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "ZM_OUT", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_ZOOM_OUT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // xxx simplify this code loc.x, loc.y
        loc = (rect_t){pane->w*.75-5*fcw/2, pane->h-fch, 5*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "ZM_IN", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_ZOOM_IN, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        loc = (rect_t){pane->w*.75-4*fcw/2, 0, 4*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "SHOW", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_SLIDE_SHOW, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // XXX retry
        if (true || cache_thread_first_zoom_lvl_is_finished()) {  // xxx this didn't work ?
            int x,y;
            x = 0; y = pane->h-fch;
            sdl_render_text_and_register_event(
                    pane, x, y, FONTSZ, "SAVE", SDL_LIGHT_BLUE, SDL_BLACK,
                    SDL_EVENT_MBS_SAVE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        }
    }
}

static int event_hndlr_mbs(pane_cx_t *pane_cx, sdl_event_t *event)
{
    //xxx rect_t * pane = &pane_cx->pane;
    int      rc   = PANE_HANDLER_RET_NO_ACTION;

    // xxx organize
    switch (event->event_id) {
    // --- xxx ---
    case SDL_EVENT_MBS_CTRLS:
        ctrls_enabled = !ctrls_enabled;
        break;

    // --- xxx ---
    case SDL_EVENT_MBS_HELP:
        SET_DISPLAY(DISPLAY_HELP);
        break;

    // --- xxx ---
    case SDL_EVENT_MBS_INFO:
        display_info = !display_info;
        break;

    // --- SELECT A SAVED FILE ---
    case SDL_EVENT_MBS_SHOW_NEXT_FILE:
    case SDL_EVENT_MBS_SHOW_PRIOR_FILE: {
        static int idx = -1;

        if (max_file_info == 0) {
            break;
        }

        idx = idx + (event->event_id == SDL_EVENT_MBS_SHOW_NEXT_FILE ? 1 : -1);
        if (idx < 0) {
            idx = max_file_info-1;
        } else if (idx >= max_file_info) {
            idx = 0;
        }

        show_file(idx);
        break; }

    case SDL_EVENT_MBS_FILES:
        SET_DISPLAY(DISPLAY_DIRECTORY);
        break;

    // --- ADUST ZOOM  ---
    case SDL_EVENT_MBS_ZOOM_AUTO:  // xxx name
        // start and stop auto_zoom
        if (auto_zoom != AUTO_ZOOM_OFF) {
            auto_zoom_last = auto_zoom;
            auto_zoom = AUTO_ZOOM_OFF;
        } else {
            if (ZOOM_TOTAL == 0) {
                auto_zoom = AUTO_ZOOM_IN;
            } else if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
                auto_zoom = AUTO_ZOOM_OUT;
            } else {
                auto_zoom = auto_zoom_last;
            }
        }
        break;
    case SDL_EVENT_MBS_ZOOM_IN:
        if (auto_zoom != AUTO_ZOOM_OFF) {
            auto_zoom = AUTO_ZOOM_IN;
            break;
        }
        zoom_step(true);
        break;
    case SDL_EVENT_MBS_ZOOM_OUT:
        if (auto_zoom != AUTO_ZOOM_OFF) {
            auto_zoom = AUTO_ZOOM_OUT;
            break;
        }
        zoom_step(false);
        break;
    case SDL_EVENT_MBS_MOUSE_WHEEL_ZOOM:
        if (event->mouse_wheel.delta_y > 0) {
            zoom_step(true);
        } else if (event->mouse_wheel.delta_y < 0) {
            zoom_step(false);
        }
        break;


    // --- ADUST CENTER ---
    case SDL_EVENT_MBS_PAN: {
        double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-ZOOM_TOTAL);
        ctr += -(event->mouse_motion.delta_x * pixel_size) +
               +(event->mouse_motion.delta_y * pixel_size) * I;
        break; }

    case SDL_EVENT_MBS_SLIDE_SHOW:
        slide_show_enabled = !slide_show_enabled;
        slide_show_time_us = 0;
        slide_show_idx = -1;
        set_alert(SDL_WHITE, "SLIDE SHOW %s", slide_show_enabled ? "STARTING" : "STOPPED");
        break;

    // --- xxx  ---
    case SDL_EVENT_MBS_SAVE:
        save_file();
        break;

    // --- DEBUG EVENTS ---  xxx mark linux only
    case SDL_EVENT_KEY_F(1):
        debug_enabled = !debug_enabled;
        break;
    case SDL_EVENT_KEY_F(2):
        debug_force_cache_thread_run = true;
        break;

    // xxx add clut ctrls
    // xxx add click to ctr, if possible
    // xxx add pinch zoom
    // xxx add 'z'
    // xxx review below
#if 0
    // --- GENERAL ---
    case 'r':  // reset ctr and zoom
        ctr           = INITIAL_CTR;
        zoom          = 0;
        zoom_fraction = 0.0;
        break;
    case 'R':  // reset color lookup table
        wavelen_start = WAVELEN_START_DEFAULT;
        wavelen_scale = WAVELEN_SCALE_DEFAULT;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;

    // --- DEBUG ---

    // --- COLOR CONTROLS ---
    case SDL_EVENT_KEY_UP_ARROW: case SDL_EVENT_KEY_DOWN_ARROW:
        wavelen_scale = wavelen_scale +
                              (event->event_id == SDL_EVENT_KEY_UP_ARROW ? 1 : -1);
        if (wavelen_scale < 0) wavelen_scale = 0;
        if (wavelen_scale > 16) wavelen_scale = 16;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    case SDL_EVENT_KEY_LEFT_ARROW: case SDL_EVENT_KEY_RIGHT_ARROW:
        wavelen_start = wavelen_start +
                              (event->event_id == SDL_EVENT_KEY_RIGHT_ARROW ? 1 : -1);
        if (wavelen_start < WAVELEN_FIRST) wavelen_start = WAVELEN_LAST;
        if (wavelen_start > WAVELEN_LAST) wavelen_start = WAVELEN_FIRST;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;

    // --- CENTER ---
    case SDL_EVENT_PAN: {
        double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-ZOOM_TOTAL);
        ctr += -(event->mouse_motion.delta_x * pixel_size) + 
               -(event->mouse_motion.delta_y * pixel_size) * I;
        break; }
    case SDL_EVENT_CENTER: {
        double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-ZOOM_TOTAL);
        ctr += ((event->mouse_click.x - (pane->w/2)) * pixel_size) + 
               ((event->mouse_click.y - (pane->h/2)) * pixel_size) * I;
        break; }

    // --- ZOOM ---
    case '+': case '=': case '-':   // zoom in/out
    case SDL_EVENT_ZOOM:  // zoom in/out using mouse wheel
        if (event->mouse_wheel.delta_y > 0) {
            zoom_step(true);
        } else if (event->mouse_wheel.delta_y < 0) {
            zoom_step(false);
        }
        break;
    case 'z':  // goto either fully zoomed in or out
        if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
            zoom = 0;
            zoom_fraction = 0;
        } else {
            zoom = (MAX_ZOOM-1);
            zoom_fraction = 0;
        }
        break;

    // --- AUTO ZOOM ---
    case 'A': 
        // flip direction of autozoom
        if (auto_zoom == AUTO_ZOOM_IN) {
            auto_zoom = AUTO_ZOOM_OUT;
        } else if (auto_zoom == AUTO_ZOOM_OUT) {
            auto_zoom = AUTO_ZOOM_IN;
        } else {
            auto_zoom_last = (auto_zoom_last == AUTO_ZOOM_IN ? AUTO_ZOOM_OUT : AUTO_ZOOM_IN);
        }
        break;

#endif
    }

    return rc;
}

static void zoom_step(bool dir_is_incr)
{
    double z = ZOOM_TOTAL;

    z += (dir_is_incr ? ZOOM_STEP : -ZOOM_STEP);
    if (fabs(z - nearbyint(z)) < 1e-6) {
        z = nearbyint(z);
    }

    if (z < 0) {
        z = 0;
    } else if (z > (MAX_ZOOM-1)) {
        z = (MAX_ZOOM-1);
    }

    zoom = z;
    zoom_fraction = z - zoom;
    INFO("z=%f zoom=%d  frac=%f\n", z, zoom, zoom_fraction);
}

static void init_color_lut(int wavelen_start, int wavelen_scale, unsigned int *color_lut)
{
    int           i;
    unsigned char r,g,b;
    double        wavelen;
    double        wavelen_step;

    color_lut[65535]         = PIXEL_BLUE;
    color_lut[MBSVAL_IN_SET] = PIXEL_BLACK;

    if (wavelen_scale == 0) {
        // black and white
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            color_lut[i] = PIXEL_WHITE;
        }
    } else {
        // color
        wavelen_step  = (double)(WAVELEN_LAST-WAVELEN_FIRST) / (MBSVAL_IN_SET-1) * wavelen_scale;
        wavelen = wavelen_start;
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            sdl_wavelen_to_rgb(wavelen, &r, &g, &b);
            color_lut[i] = PIXEL(r,g,b);
            wavelen += wavelen_step;
            if (wavelen > WAVELEN_LAST+.01) {
                wavelen = WAVELEN_FIRST;
            }
        }
    }
}

static void display_info_proc(rect_t *pane, uint64_t update_intvl_ms)
{
    char line[20][50];
    int  line_len[20];
    int  n=0, max_len=0, i;

    // xxx make this smaller

    // print info to line[] array xxx reformat
    sprintf(line[n++], "Window: %d %d", pane->w, pane->h);
    sprintf(line[n++], "Ctr-A:  %+0.9f", creal(ctr));
    sprintf(line[n++], "Ctr-B:  %+0.9f", cimag(ctr));
    sprintf(line[n++], "Zoom:   %0.2f", ZOOM_TOTAL);
    sprintf(line[n++], "Color:  %d %d", wavelen_start, wavelen_scale);   

    // determine each line_len and the max_len
    for (i = 0; i < n; i++) {
        line_len[i] = strlen(line[i]);
        if (line_len[i] > max_len) max_len = line_len[i];
    }

    // extend each line with spaces, out to max_len,
    // so that each line is max_len in length
    for (i = 0; i < n; i++) {
        sprintf(line[i]+line_len[i], "%*s", max_len-line_len[i], "");
    }

    // render the lines
    for (i = 0; i < n; i++) {//xxx vvv
        sdl_render_printf(pane, 0, 1.5*FONTSZ+ROW2Y(i,FONTSZ_INFO), FONTSZ_INFO,  SDL_WHITE, SDL_BLACK, "%s", line[i]);
    }
}

static void create_file(char *params)
{
    double ctr_a, ctr_b;
    int cnt, idx;

    cnt = sscanf(params, "%lf,%lf,%d,%lf,%d,%d",
                 &ctr_a, &ctr_b, &zoom, &zoom_fraction, &wavelen_start, &wavelen_scale);
    if (cnt != 6) {
        FATAL("invalid params '%s'\n", params);
    }
    ctr = ctr_a + ctr_b * I;
    init_color_lut(wavelen_start, wavelen_scale, color_lut);

    INFO("ctr_a,b=%f %f zoom=%d zoom_fraction=%f wavelen_start,scale=%d %d\n",
         ctr_a, ctr_b, zoom, zoom_fraction, wavelen_start, wavelen_scale);

    INFO("- waiting for cache thread\n");
    cache_param_change(ctr, zoom, false);
    while (cache_thread_first_zoom_lvl_is_finished() == false) {
        usleep(100000);
    }

    idx = save_file();
    INFO("- saved file %d\n", idx);
}

static int save_file(void)
{
    int             x_idx, y_idx, pxidx, w, h, idx;
    unsigned int   *pixels;
    unsigned short *mbsval;
    double          x, y, x_step, y_step, x_start, y_start;

    // init
    w = (CACHE_WIDTH - 200) *  pow(2, -zoom_fraction);
    h = (CACHE_HEIGHT - 200) *  pow(2, -zoom_fraction);
    x_start = (CACHE_WIDTH - w) / 2;
    y_start = (CACHE_HEIGHT - h) / 2;
    y_step = (double)h / DIR_PIXELS_HEIGHT;
    x_step = (double)w / DIR_PIXELS_WIDTH;
    pxidx = 0;
    INFO("w,h %d %d x_start,y_start %f %f\n", w, h, x_start, y_start);

    // alloc memory for mbs values and pixels
    mbsval = malloc(CACHE_WIDTH * CACHE_HEIGHT * 2);
    pixels = malloc(CACHE_WIDTH * CACHE_HEIGHT * 4);

    // get the mbs values
    cache_get_mbsval(mbsval);

    // create a reduced size (300x200) array of pixels, 
    // this will be the directory image
    y = y_start;
    for (y_idx = 0; y_idx < DIR_PIXELS_HEIGHT; y_idx++) {
        x = x_start;
        for (x_idx = 0; x_idx < DIR_PIXELS_WIDTH; x_idx++) {
            pixels[pxidx] = color_lut[
                             mbsval[(int)nearbyint(y) * CACHE_WIDTH  +  (int)nearbyint(x)]
                                        ];
            pxidx++;
            x = x + x_step;
        }
        y = y + y_step;
    }

    // create the file, and set an alert to indicate the file has been created
    idx = cache_file_create(ctr, zoom, zoom_fraction, wavelen_start, wavelen_scale, pixels);

    // free memory
    free(mbsval);
    free(pixels);

    // xxx comment
    cache_file_update(idx, 1);

    // set completion alert
    set_alert(SDL_GREEN, "SAVE COMPLETE");

    // return idx
    return idx;
}

static void show_file(int idx)
{
    if (idx < 0 || idx >= max_file_info) {
        FATAL("invalid idx %d, max_file_info=%d\n", idx, max_file_info);
    }

    file_info[idx]->selected = false;

    cache_file_read(idx);

    ctr           = file_info[idx]->ctr;
    zoom          = file_info[idx]->zoom;
    zoom_fraction = file_info[idx]->zoom_fraction;
    wavelen_start = file_info[idx]->wavelen_start;
    wavelen_scale = file_info[idx]->wavelen_scale;

    init_color_lut(wavelen_start, wavelen_scale, color_lut);
}

// - - - - - - - - -  PANE_HNDLR : HELP  - - - - - - - - - - - - - - - -

static int y_top_help;
static int max_help_row;

static void render_hndlr_help(pane_cx_t *pane_cx)
{
    char strbuff[100], *s;
    int row, y;
    rect_t * pane = &pane_cx->pane;

    static asset_file_t *f;
    static int last_display_select_count;

    #define SDL_EVENT_HELP_MOUSE_MOTION  (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_HELP_MOUSE_WHEEL   (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_HELP_BACK    (SDL_EVENT_USER_DEFINED + 2)

    // read help.txt, on first call
    if (f == NULL) {
        f = read_asset_file("help.txt");
        if (f == NULL) {
            FATAL("read_asset_file failed for help.txt\n");
        }
    }

    // if re-entering help display then reset y_top_help to 0
    if (display_select_count != last_display_select_count) {
        y_top_help = 0;
        last_display_select_count = display_select_count;
    }

    // display the help text
    f->offset = 0;
    for (row = 0; ; row++) {
        s = read_file_line(f, strbuff, sizeof(strbuff));
        if (s == NULL) {
            break;
        }
        y = y_top_help + ROW2Y(row,FONTSZ_HELP);
        if (y <= -ROW2Y(1,FONTSZ_HELP) || y >= pane->h) {
            continue;
        }
        sdl_render_printf(pane, 0, y, FONTSZ_HELP, SDL_WHITE, SDL_BLACK, "%s", s);
    }

    // save max_help_row for use below to limit the scrolling range
    max_help_row = row;

    // register for scrool event
    sdl_register_event(pane, pane, SDL_EVENT_HELP_MOUSE_MOTION, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    sdl_register_event(pane, pane, SDL_EVENT_HELP_MOUSE_WHEEL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

    int fcw = sdl_font_char_width(FONTSZ);  // XXX one define
    int fch = sdl_font_char_height(FONTSZ);
    rect_t loc = (rect_t){pane->w-4*fcw, pane->h-fch, 4*fcw, fch};
    sdl_render_text_and_register_event(
            pane, loc.x, loc.y, FONTSZ, "BACK", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_HELP_BACK, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
}

static int event_hndlr_help(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_NO_ACTION;

//XXX add arrow keys
// xxx comment linux only events
    // handle events to adjust the help text scroll position (y_top_help)
    switch (event->event_id) {
    case SDL_EVENT_HELP_MOUSE_MOTION: {
        int y_top_help_limit = (-max_help_row+10) * ROW2Y(1,FONTSZ_HELP);
        y_top_help += event->mouse_motion.delta_y;
        if (y_top_help < y_top_help_limit) y_top_help = y_top_help_limit;
        if (y_top_help > 0) y_top_help = 0;
        break; }
    case SDL_EVENT_HELP_MOUSE_WHEEL: {
        int y_top_help_limit = (-max_help_row+10) * ROW2Y(1,FONTSZ_HELP);
        if (event->mouse_wheel.delta_y > 0) {
            y_top_help += FONTSZ_HELP;
        } else if (event->mouse_wheel.delta_y < 0) {
            y_top_help -= FONTSZ_HELP;
        }
        if (y_top_help < y_top_help_limit) y_top_help = y_top_help_limit;
        if (y_top_help > 0) y_top_help = 0;
        break; }
    case SDL_EVENT_HELP_BACK:
        SET_DISPLAY(DISPLAY_MBS);
        break;
    }

    return rc;
}

// - - - - - - - - -  PANE_HNDLR : DIRECTORY  - - - - - - - - - - - - -

// xxx lots of cleanup here
static bool init_request;
static int  y_top_dir;

#define CLEAR_ALL_FILE_INFO_SELECTED \
    do { \
        int _idx; \
        for (_idx = 0; _idx < max_file_info; _idx++) { \
            file_info[_idx]->selected = false; \
        } \
    } while (0)

static void render_hndlr_directory(pane_cx_t *pane_cx)
{
    rect_t * pane = &pane_cx->pane;
    int      cols = (pane->w/DIR_PIXELS_WIDTH == 0 ? 1 : pane->w/DIR_PIXELS_WIDTH);
    int      idx, x, y, select_cnt;
    rect_t   loc;

    static texture_t texture;
    static int       last_display_select_count;

    #define SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL  (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_DIR_MOUSE_MOTION_SCROLL (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_DIR_DELETE              (SDL_EVENT_USER_DEFINED + 2)
    #define SDL_EVENT_DIR_BACK                (SDL_EVENT_USER_DEFINED + 3)
    #define SDL_EVENT_DIR_CHOICE              (SDL_EVENT_USER_DEFINED + 10)
    #define SDL_EVENT_DIR_SELECT              (SDL_EVENT_USER_DEFINED + 1100)

    // one time init
    if (texture == NULL) {
        texture = sdl_create_texture(DIR_PIXELS_WIDTH, DIR_PIXELS_HEIGHT);
    }

    // initialize when this display has been selected, or
    // when the init_request flag has been set (init_request is set when files are deleted)
    if (display_select_count != last_display_select_count || init_request) {
        CLEAR_ALL_FILE_INFO_SELECTED;
        y_top_dir = 0;
        init_request = false;
        last_display_select_count = display_select_count;
    }

    // xxx
    sdl_register_event(pane, pane, SDL_EVENT_DIR_MOUSE_MOTION_SCROLL, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    sdl_register_event(pane, pane, SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

    // display the directory images, for each image:
    // - the saved place image is first displayed, followed by
    // - red select box at upper left (if selected)
    // - file number
    // - zoom factor
    // - file_type
    for (idx = 0; idx < max_file_info; idx++) {
        cache_file_info_t *fi = file_info[idx];

        // determine location of upper left
        x = (idx % cols) * DIR_PIXELS_WIDTH;
        y = (idx / cols) * DIR_PIXELS_HEIGHT + y_top_dir;

        // continue if location is outside of the pane
        if (y <= -DIR_PIXELS_HEIGHT || y >= pane->h) {
            continue;
        }

        // display the file's directory image
        sdl_update_texture(texture, (void*)fi->dir_pixels, DIR_PIXELS_WIDTH*BYTES_PER_PIXEL);
        sdl_render_texture(pane, x, y, texture);

        // register for events for each directory image that is displayed
        rect_t loc = {x,y,DIR_PIXELS_WIDTH,DIR_PIXELS_HEIGHT};
        sdl_register_event(pane, &loc, SDL_EVENT_DIR_CHOICE + idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        if (!fi->selected) {
            sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "SEL", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_DIR_SELECT+idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        } else {
            int fcw = sdl_font_char_width(FONTSZ);
            int fch = sdl_font_char_height(FONTSZ);
            loc = (rect_t){x,y,3*fcw,fch};
            sdl_render_fill_rect(pane, &loc, SDL_RED);
            sdl_register_event(pane, &loc, SDL_EVENT_DIR_SELECT+idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        }
    }

    // separate the directory images with black lines
    int i;
    for (i = 1; i < cols; i++) {
        x = i * DIR_PIXELS_WIDTH;
        sdl_render_line(pane, x-2, 0, x-2, pane->h-1, SDL_BLACK);
        sdl_render_line(pane, x-1, 0, x-1, pane->h-1, SDL_BLACK);
        sdl_render_line(pane, x+0, 0, x+0, pane->h-1, SDL_BLACK);
        sdl_render_line(pane, x+1, 0, x+1, pane->h-1, SDL_BLACK);
    }
    for (i = 1; i <= max_file_info-1; i++) {
        y = (i / cols) * DIR_PIXELS_HEIGHT + y_top_dir;
        if (y+1 < 0 || y-2 > pane->h-1) {
            continue;
        }
        sdl_render_line(pane, 0, y-2, pane->w-1, y-2, SDL_BLACK);
        sdl_render_line(pane, 0, y-1, pane->w-1, y-1, SDL_BLACK);
        sdl_render_line(pane, 0, y+0, pane->w-1, y+0, SDL_BLACK);
        sdl_render_line(pane, 0, y+1, pane->w-1, y+1, SDL_BLACK);
    }

    // clear bottom of screen where the DELETE and BACK event text is to be displayed
    int fcw = sdl_font_char_width(FONTSZ);  // xxx one define
    int fch = sdl_font_char_height(FONTSZ);
    loc = (rect_t){0, pane->h-(int)(1.5*fch), pane->w, (int)(1.5*fch)};
    sdl_render_fill_rect(pane, &loc, SDL_BLACK);

    // xxx also clear events from this region

    // register for additional events

    loc = (rect_t){pane->w-4*fcw, pane->h-fch, 4*fcw, fch};
    sdl_render_text_and_register_event(
            pane, loc.x, loc.y, FONTSZ, "BACK", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_DIR_BACK, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    for (select_cnt=0, idx=0; idx < max_file_info; idx++) {
        if (file_info[idx]->selected) select_cnt++;
    }
    if (select_cnt > 0 && select_cnt < max_file_info ) {
        loc = (rect_t){0, pane->h-fch, 6*fcw, fch};
        char str[100];
        sprintf(str, "DELETE %d FILE%s", select_cnt, select_cnt>1?"S":"");
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, str, SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_DIR_DELETE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
    }
}

static int event_hndlr_directory(pane_cx_t *pane_cx, sdl_event_t *event)
{
    rect_t * pane = &pane_cx->pane;
    int      cols = (pane->w/DIR_PIXELS_WIDTH == 0 ? 1 : pane->w/DIR_PIXELS_WIDTH);
    int      rc   = PANE_HANDLER_RET_DISPLAY_REDRAW;
    int      idx, cnt;

    switch (event->event_id) {
    case SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL:
    case SDL_EVENT_DIR_MOUSE_MOTION_SCROLL:
        if (event->event_id == SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL) {
            if (event->mouse_wheel.delta_y > 0) {
                y_top_dir += 20;  // xxx why 20
            } else if (event->mouse_wheel.delta_y < 0) {
                y_top_dir -= 20;
            }
        } else {
            y_top_dir += event->mouse_motion.delta_y;
        }

        int y_top_limit = -((max_file_info - 1) / cols + 1) * DIR_PIXELS_HEIGHT + 600;
        if (y_top_dir < y_top_limit) y_top_dir = y_top_limit;
        if (y_top_dir > 0) y_top_dir = 0;
        break;

    case SDL_EVENT_DIR_CHOICE...SDL_EVENT_DIR_CHOICE+1000:
        idx = event->event_id - SDL_EVENT_DIR_CHOICE;
        show_file(idx);

        CLEAR_ALL_FILE_INFO_SELECTED;
        SET_DISPLAY(DISPLAY_MBS);
        break;

    case SDL_EVENT_DIR_SELECT...SDL_EVENT_DIR_SELECT+1000:
        idx = event->event_id - SDL_EVENT_DIR_SELECT;
        file_info[idx]->selected = !file_info[idx]->selected;
        break;

    case SDL_EVENT_DIR_DELETE:
        cnt = 0;
        for (idx = 0; idx < max_file_info; idx++) {
            if (file_info[idx]->selected) {
                cache_file_delete(idx);
                cnt++;
                idx--;
            }
        }
        if (cnt == 0) {
            set_alert(SDL_RED, "NOTHING SELECTED");
            break;
        }
        set_alert(SDL_GREEN, "%d FILE%s DELETED", cnt, cnt>1?"S":"");
        init_request = true;
        break;

    case SDL_EVENT_DIR_BACK:
        CLEAR_ALL_FILE_INFO_SELECTED;
        SET_DISPLAY(DISPLAY_MBS);
        break;
    }

    return rc;
}

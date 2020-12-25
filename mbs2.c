#include <common.h>

#include <util_sdl.h>

//
// defines
//

#ifdef ANDROID
#define main SDL_main
#endif

#define DEFAULT_WIN_WIDTH         1500
#define DEFAULT_WIN_HEIGHT        1000 

#define INITIAL_CTR               (-0.75 + 0.0*I)

#define ZOOM_STEP                 .1   // must be a submultiple of 1

#define WAVELEN_FIRST             400
#define WAVELEN_LAST              700
#define WAVELEN_START_DEFAULT     400
#define WAVELEN_SCALE_DEFAULT     2

// xxx
//#ifndef ANDROID
//#define FONTSZ  60
//#define FONTSZ_HELP 30
//#define FONTSZ_INFO 30
//#else
#define FONTSZ  60
#define FONTSZ_HELP 30
#define FONTSZ_INFO 30
#define FONTSZ_ALERT 60
//#endif

#define SLIDE_SHOW_INTVL_US  5000000

//
// typedefs
//

//
// variables
//

static int     win_width   = DEFAULT_WIN_WIDTH;
static int     win_height  = DEFAULT_WIN_HEIGHT;
static bool    full_screen = true;

static double  pixel_size_at_zoom0;

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
static int save_file(rect_t *pane);
static void show_file(int idx);

static void render_hndlr_help(pane_cx_t *pane_cx);
static int event_hndlr_help(pane_cx_t *pane_cx, sdl_event_t *event);

#if 0 //xxx
static void render_hndlr_color_lut(pane_cx_t *pane_cx);
static int event_hndlr_color_lut(pane_cx_t *pane_cx, sdl_event_t *event);
#endif

static void render_hndlr_directory(pane_cx_t *pane_cx);
static int event_hndlr_directory(pane_cx_t *pane_cx, sdl_event_t *event);

// -----------------  MAIN  -------------------------------------------------

int main(int argc, char **argv)
{ 
    int requested_win_width;
    int requested_win_height;

    // debug print program startup
    INFO("program starting\n");

    // get and process options
    // -g NNNxNNN  : window size
    // -d          : debug mode
    while (true) {
        unsigned char opt_char = getopt(argc, argv, "g:d");
        if (opt_char == 0xff) {
            break;
        }
        switch (opt_char) {
        case 'g': {
            int cnt = sscanf(optarg, "%dx%d", &win_width, &win_height);
            if (cnt != 2 || win_width < 100 || win_width > CACHE_WIDTH || win_height < 100 || win_height > CACHE_HEIGHT) {
                FATAL("-g %s invalid\n", optarg);
            }
            full_screen = false;
            INFO("getopt: geometry %dx%d\n", win_width, win_height);
            break; }
        case 'd':
            debug_enabled = true;
            INFO("getopt: debug_enabled = true\n");
            break;
        default:
            return 1;
        }
    }

    // init sdl
    requested_win_width  = win_width;
    requested_win_height = win_height;
    if (sdl_init(&win_width, &win_height, false, true, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    INFO("requested win_width=%d win_height=%d\n", requested_win_width, requested_win_height);
    INFO("actual    win_width=%d win_height=%d\n", win_width, win_height);

    // if full_screen is initially set then enter full_screen mode
    if (full_screen) {
        sdl_full_screen(true);
    }

    // initialize the caching code
    pixel_size_at_zoom0 = 4. / win_width;
    cache_init(pixel_size_at_zoom0);

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
//#define DISPLAY_COLOR_LUT  3  //xxx
#define DISPLAY_DIRECTORY  4

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
        // if window size has changed then update the pane's 
        // location within the window
        int new_win_width, new_win_height;
        sdl_get_window_size(&new_win_width, &new_win_height);
        if (new_win_width > CACHE_WIDTH) new_win_width = CACHE_WIDTH;
        if (new_win_height > CACHE_HEIGHT) new_win_height = CACHE_HEIGHT;
        if (new_win_width != win_width || new_win_height != win_height) {
            DEBUG("NEW WIN SIZE w=%d %d\n", new_win_width, new_win_height);
            sdl_pane_update(pane_cx, 0, 0, new_win_width, new_win_height);
            win_width = new_win_width;
            win_height = new_win_height;
        }

        // sanity check pane w/h vs win_width/height
        if (pane->w != win_width || pane->h != win_height) {
            FATAL("pane w/h=%d %d win_width/height=%d/%d\n", pane->w, pane->h, win_width, win_height);
        }

        // call the selected render_hndlr
        switch (display_select) {
        case DISPLAY_MBS:
            render_hndlr_mbs(pane_cx);
            break;
        case DISPLAY_HELP:
            render_hndlr_help(pane_cx);
            break;
#if 0 //xxx
        case DISPLAY_COLOR_LUT:
            render_hndlr_color_lut(pane_cx);
            break;
#endif
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
#if 1
        case 'f':  // full screen
            full_screen = !full_screen;
            INFO("set full_screen to %d\n", full_screen);
            sdl_full_screen(full_screen);
            break;
        case 'q':  // quit
            rc = PANE_HANDLER_RET_PANE_TERMINATE;
            break;
#endif

        // it is not a common event, so call the selected event_hndlr
        default:
            switch (display_select) {
            case DISPLAY_MBS:
                rc = event_hndlr_mbs(pane_cx, event);
                break;
            case DISPLAY_HELP:
                rc = event_hndlr_help(pane_cx, event);
                break;
#if 0
            case DISPLAY_COLOR_LUT:
                rc = event_hndlr_color_lut(pane_cx, event);
                break;
#endif
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

static int          wavelen_start                = WAVELEN_START_DEFAULT;
static int          wavelen_scale                = WAVELEN_SCALE_DEFAULT;
static int          auto_zoom                    = AUTO_ZOOM_OFF;
static int          auto_zoom_last               = AUTO_ZOOM_IN;
static complex_t    ctr                          = INITIAL_CTR;
static int          zoom                         = 0;
static double       zoom_fraction                = 0;
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

    // xxx organize the order
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
    update_intvl_ms = (last_update_time_us != 0
                       ? (time_now_us - last_update_time_us) / 1000
                       : 0);
    last_update_time_us = time_now_us;

    // if the texture hasn't been allocated yet, or the size of the
    // texture doesn't match the size of the pane then
    // re-allocate the texture, pixels, and mbsval
    int new_texture_width, new_texture_height;
    if ((texture == NULL) ||
        ((sdl_query_texture(texture, &new_texture_width, &new_texture_height), true) &&
         (new_texture_width != pane->w || new_texture_height != pane->h)))
    {
        DEBUG("allocating texture,pixels,mbsval w=%d h=%d\n", pane->w, pane->h);
        sdl_destroy_texture(texture);
        free(pixels);
        free(mbsval);

        texture = sdl_create_texture(pane->w, pane->h);
        pixels = malloc(pane->w*pane->h*BYTES_PER_PIXEL);
        mbsval = malloc(pane->w*pane->h*2);
    }

    // if auto_zoom is enabled then increment or decrement the zoom until limit is reached xxx comment
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
    static bool first_call = true;
    if (first_call) {
        show_file(0); // xxx what if there is no file 0,  don't allow it to be deleted
        first_call = false;
    } else if (slide_show_enabled && microsec_timer() > slide_show_time_us + SLIDE_SHOW_INTVL_US) {
        // xxx garbage collect ?
        slide_show_idx = (slide_show_idx + 1) % max_file_info;
        show_file(slide_show_idx);
        slide_show_time_us = microsec_timer();
    } else {
        cache_param_change(ctr, zoom, pane->w, pane->h, debug_force_cache_thread_run);
        debug_force_cache_thread_run = false;  // xxx needed?
    }

    // get the cached mandelbrot set values; and
    // convert them to pixel color values
    cache_get_mbsval(mbsval, pane->w, pane->h);
    for (pixel_y = 0; pixel_y < pane->h; pixel_y++) {
        for (pixel_x = 0; pixel_x < pane->w; pixel_x++) {
            pixels[idx] = color_lut[mbsval[idx]];
            idx++;
        }
    }

    // copy the pixels to the texture
    sdl_update_texture(texture, (void*)pixels, pane->w*BYTES_PER_PIXEL);

    // determine the source area of the texture, (based on the zoom_fraction)
    // that will be rendered by the call to sdl_render_scaled_texture_ex below
    rect_t src;
    double tmp = pow(2, -zoom_fraction);
    src.w = pane->w * tmp;
    src.h = pane->h * tmp;
    src.x = (pane->w - src.w) / 2;
    src.y = (pane->h - src.h) / 2;

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
    // xxx cleanup this section, and note which are for linux only

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
        // xxx larger box
        sdl_register_event(pane, &loc, SDL_EVENT_MBS_CTRLS, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
    }

    if (ctrls_enabled) {
        // XXX bring in font as in preoj_entopy
        // xxx later, only display if SHOWN

        static int first=1;
        if (first) {
            first = 0;
            INFO("fcw/h = %d %d\n", fcw, fch);
        }

        loc = (rect_t){pane->w-4*fcw, 0, 4*fcw, fch};
        sdl_render_text_and_register_event(
                pane, loc.x, loc.y, FONTSZ, "HELP", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_HELP, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        //loc = (rect_t){pane->w-4*fcw, fch+1, 4*fcw, fch};  // xxx test
        //sdl_render_fill_rect(pane, &loc, SDL_BLACK);

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

        if (true || cache_thread_first_phase1_zoom_lvl_is_finished()) {  // XXX
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
    rect_t * pane = &pane_cx->pane;
    int      rc   = PANE_HANDLER_RET_NO_ACTION;

    // xxx organize
    switch (event->event_id) {
    // --- XXXXXXXXXXXXXXXXXXX ---
    case SDL_EVENT_MBS_CTRLS:
        ctrls_enabled = !ctrls_enabled;
        break;

    // --- XXX ---
    case SDL_EVENT_MBS_HELP:
        SET_DISPLAY(DISPLAY_HELP);
        break;

    // --- XXX ---
    case SDL_EVENT_MBS_INFO:
        display_info = !display_info;
        break;

    // --- SELECT A SAVED FILE ---
    case SDL_EVENT_MBS_SHOW_NEXT_FILE:
    case SDL_EVENT_MBS_SHOW_PRIOR_FILE: { // xxx also slide show mode ?
        static int idx = -1;

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
        double pixel_size = pixel_size_at_zoom0 * pow(2,-ZOOM_TOTAL);
        ctr += -(event->mouse_motion.delta_x * pixel_size) +
               -(event->mouse_motion.delta_y * pixel_size) * I;
        break; }

    case SDL_EVENT_MBS_SLIDE_SHOW:
        slide_show_enabled = !slide_show_enabled;
        slide_show_time_us = 0;
        slide_show_idx = -1;
        set_alert(SDL_WHITE, "SLIDE SHOW IS %s", slide_show_enabled ? "STARTING" : "STOPPED");
        break;

    // --- XXX  ---
    case SDL_EVENT_MBS_SAVE: {
        int idx;
        idx = save_file(pane);
        INFO("XXX SAVE newfile idx=%d\n",idx);
        cache_file_update(idx, 1);
        break; }

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
    case 's':  // save 
        save_file(pane);
        break;

    // --- DEBUG ---
    case SDL_EVENT_KEY_F(1):  // toggle debug_enabled, used to control debug prints
        debug_enabled = !debug_enabled;
        break;
    case SDL_EVENT_KEY_F(2):  // force the cache thread to run
        debug_force_cache_thread_run = true;
        break;

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
        double pixel_size = pixel_size_at_zoom0 * pow(2,-ZOOM_TOTAL);
        ctr += -(event->mouse_motion.delta_x * pixel_size) + 
               -(event->mouse_motion.delta_y * pixel_size) * I;
        break; }
    case SDL_EVENT_CENTER: {
        double pixel_size = pixel_size_at_zoom0 * pow(2,-ZOOM_TOTAL);
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
    int  phase_inprog, zoom_lvl_inprog;

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

static int save_file(rect_t *pane)
{
    int             x_idx, y_idx, pxidx, w, h, idx;
    unsigned int   *pixels;
    unsigned short *mbsval;
    double          x, y, x_step, y_step;

    // init
    w      = pane->w *  pow(2, -zoom_fraction);
    h      = pane->h *  pow(2, -zoom_fraction);
    x      = 0;
    y      = 0;
    y_step = (double)h / DIR_PIXELS_HEIGHT;
    x_step = (double)w / DIR_PIXELS_WIDTH;
    pxidx  = 0;

    // alloc memory for mbs values and pixels
    mbsval = malloc(w * h * 2);
    pixels = malloc(w * h * 4);

    // get the mbs values
    cache_get_mbsval(mbsval, w, h);

    // create a reduced size (300x200) array of pixels, 
    // this will be the directory image
    for (y_idx = 0; y_idx < DIR_PIXELS_HEIGHT; y_idx++) {
        x = 0;
        for (x_idx = 0; x_idx < DIR_PIXELS_WIDTH; x_idx++) {
            pixels[pxidx] = color_lut[
                             mbsval[(int)nearbyint(y) * w  +  (int)nearbyint(x)]
                                        ];
            pxidx++;
            x = x + x_step;
        }
        y = y + y_step;
    }

    // create the file, and set an alert to indicate the file has been created
    idx = cache_file_create(ctr, zoom, zoom_fraction, wavelen_start, wavelen_scale, pixels);
    set_alert(SDL_GREEN, "SAVE COMPLETE");

    // free memory
    free(mbsval);
    free(pixels);

    // return the file_info array idx
    return idx;
}

static void show_file(int idx)
{
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
    // xxx also wheel and arrow keys for linux
    sdl_register_event(pane, pane, SDL_EVENT_HELP_MOUSE_MOTION, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    // xxx linux only ?
    sdl_register_event(pane, pane, SDL_EVENT_HELP_MOUSE_WHEEL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

    int fcw = sdl_font_char_width(FONTSZ);  // xxx one define
    int fch = sdl_font_char_height(FONTSZ);
    rect_t loc = (rect_t){pane->w-4*fcw, pane->h-fch, 4*fcw, fch};
    sdl_render_text_and_register_event(
            pane, loc.x, loc.y, FONTSZ, "BACK", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_HELP_BACK, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
}

static int event_hndlr_help(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_NO_ACTION;

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

#if 0
// - - - - - - - - -  PANE_HNDLR : COLOR_LUT   - - - - - - - - - - - - -

static void render_hndlr_color_lut(pane_cx_t *pane_cx)
{
    int i, x, y, x_start;
    unsigned char r,g,b;
    char title[100];
    rect_t * pane = &pane_cx->pane;

    // define custom colors for each color_lut tbl entry,
    // and draw a vertical line for each 
    x_start = (pane->w - MBSVAL_IN_SET) / 2;
    for (i = 0; i < MBSVAL_IN_SET; i++) {
        PIXEL_TO_RGB(color_lut[i],r,g,b);           
        x = x_start + i;
        y = pane->h/2 - 400/2;
        sdl_define_custom_color(20, r,g,b);
        sdl_render_line(pane, x, y, x, y+400, 20);
    }

    // display title line
    sprintf(title,"COLOR MAP - START=%d nm  SCALE=%d", wavelen_start, wavelen_scale);
    x   = pane->w/2 - COL2X(strlen(title),30)/2;
    sdl_render_printf(pane, x, 0, 30,  SDL_WHITE, SDL_BLACK, "%s", title);
}

static int event_hndlr_color_lut(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_NO_ACTION;

    switch (event->event_id) {
    case 'R':
        wavelen_start = WAVELEN_START_DEFAULT;
        wavelen_scale = WAVELEN_SCALE_DEFAULT;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    case SDL_EVENT_KEY_UP_ARROW: case SDL_EVENT_KEY_DOWN_ARROW:
        wavelen_scale = wavelen_scale +
                              (event->event_id == SDL_EVENT_KEY_UP_ARROW ? 1 : -1);
        if (wavelen_scale < 0) wavelen_scale = 0;
        if (wavelen_scale > 8) wavelen_scale = 8;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    case SDL_EVENT_KEY_LEFT_ARROW: case SDL_EVENT_KEY_RIGHT_ARROW:
        wavelen_start = wavelen_start +
                              (event->event_id == SDL_EVENT_KEY_RIGHT_ARROW ? 1 : -1);
        if (wavelen_start < WAVELEN_FIRST) wavelen_start = WAVELEN_LAST;
        if (wavelen_start > WAVELEN_LAST) wavelen_start = WAVELEN_FIRST;
        init_color_lut(wavelen_start, wavelen_scale, color_lut);
        break;
    }

    return rc;
}
#endif

// - - - - - - - - -  PANE_HNDLR : DIRECTORY  - - - - - - - - - - - - -

static bool init_request;
static int  y_top;

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
    int      idx, x, y;
    int      cols = (pane->w/DIR_PIXELS_WIDTH == 0 ? 1 : pane->w/DIR_PIXELS_WIDTH);

    static texture_t texture;
    static int       last_display_select_count;

    #define SDL_EVENT_DIR_SCROLL_WHEEL (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_DIR_DELETE       (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_DIR_BACK         (SDL_EVENT_USER_DEFINED + 2)
    #define SDL_EVENT_DIR_CHOICE       (SDL_EVENT_USER_DEFINED + 10)
    #define SDL_EVENT_DIR_SELECT       (SDL_EVENT_USER_DEFINED + 1100)

    // one time init
    if (texture == NULL) {
        texture = sdl_create_texture(DIR_PIXELS_WIDTH, DIR_PIXELS_HEIGHT);
    }

    // initialize when this display has been selected, or
    // when the init_request flag has been set (init_request is set when files are deleted)
    if (display_select_count != last_display_select_count || init_request) {
        CLEAR_ALL_FILE_INFO_SELECTED;
        y_top = 0;
        init_request = false;
        last_display_select_count = display_select_count;
    }

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
        y = (idx / cols) * DIR_PIXELS_HEIGHT + y_top;

        // continue if location is outside of the pane
        if (y <= -DIR_PIXELS_HEIGHT || y >= pane->h) {
            continue;
        }

        // display the file's directory image
        sdl_update_texture(texture, (void*)fi->dir_pixels, DIR_PIXELS_WIDTH*BYTES_PER_PIXEL);
        sdl_render_texture(pane, x, y, texture);

        // display a small red box in it's upper left, if this image is selected
        if (fi->selected) {
            rect_t loc = {x,y,17,20};
            sdl_render_fill_rect(pane, &loc, SDL_RED);
        }

        // register for events for each directory image that is displayed
        // XXX later need to redo how selected event works
        rect_t loc = {x,y,DIR_PIXELS_WIDTH,DIR_PIXELS_HEIGHT};
        sdl_register_event(pane, &loc, SDL_EVENT_DIR_CHOICE + idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        sdl_register_event(pane, &loc, SDL_EVENT_DIR_SELECT + idx, SDL_EVENT_TYPE_MOUSE_RIGHT_CLICK, pane_cx);
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
        y = (i / cols) * DIR_PIXELS_HEIGHT + y_top;
        if (y+1 < 0 || y-2 > pane->h-1) {
            continue;
        }
        sdl_render_line(pane, 0, y-2, pane->w-1, y-2, SDL_BLACK);
        sdl_render_line(pane, 0, y-1, pane->w-1, y-1, SDL_BLACK);
        sdl_render_line(pane, 0, y+0, pane->w-1, y+0, SDL_BLACK);
        sdl_render_line(pane, 0, y+1, pane->w-1, y+1, SDL_BLACK);
    }

    // register for additional events
    sdl_register_event(pane, pane, SDL_EVENT_DIR_SCROLL_WHEEL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

    int fcw = sdl_font_char_width(FONTSZ);  // xxx one define
    int fch = sdl_font_char_height(FONTSZ);
    rect_t loc = (rect_t){pane->w-4*fcw, pane->h-fch, 4*fcw, fch};
    sdl_render_text_and_register_event(
            pane, loc.x, loc.y, FONTSZ, "BACK", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_DIR_BACK, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    loc = (rect_t){0, pane->h-fch, 6*fcw, fch};
    sdl_render_text_and_register_event(
            pane, loc.x, loc.y, FONTSZ, "DELETE", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_DIR_DELETE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
}

static int event_hndlr_directory(pane_cx_t *pane_cx, sdl_event_t *event)
{
    rect_t * pane = &pane_cx->pane;
    int      cols = (pane->w/DIR_PIXELS_WIDTH == 0 ? 1 : pane->w/DIR_PIXELS_WIDTH);
    int      rc   = PANE_HANDLER_RET_DISPLAY_REDRAW;
    int      idx, cnt;

    switch (event->event_id) {
    case SDL_EVENT_DIR_SCROLL_WHEEL:
        if (event->mouse_wheel.delta_y > 0) {
            y_top += 20;  // XXX why 20
        } else if (event->mouse_wheel.delta_y < 0) {
            y_top -= 20;
        }

        int y_top_limit = -((max_file_info - 1) / cols + 1) * DIR_PIXELS_HEIGHT + 600;
        if (y_top < y_top_limit) y_top = y_top_limit;
        if (y_top > 0) y_top = 0;
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

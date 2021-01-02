// XXX
// - find a way to select to ctr
// - test useability on phone
// - display alert when saving a file,  and other times too
// - stop caching when all data is the same
// - make new files
//   - add initial ctr to the first of files0
// - fixup help text

// xxx
// - PASSWORDS
// - define for 200

// xxx
// - when toggling full screen there is an initial pan jump
// - fix QUIT, and rename PGM_EXIT, and move it somewhere else
// - info and help text can be little larger
// - don't allow deleting the last file ?
// - whay a delay here
//     01/01/21 08:08:30.121 INFO cache_file_copy_assets_to_internal_storage: asset files have ...
//     01/01/21 08:08:32.294 INFO sdl_init: sdl_win_width=1700 sdl_win_height=900
// - ensure some of the key cmds are only allowed when in that mode

// DONE
// - move hide to corner
//   - display alert at startup
// - zoom may be too sensitive
// - panning problem
// - add startup alert or first time hidden alert about how to enable ctrls
// - CLUT - + too close
//   . too sensitive
//   . wrong direction
// - startup default in show mode

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
#define INITIAL_WIN_HEIGHT        900 

#define INITIAL_CTR               (-0.75 + 0.0*I)
#define INITIAL_ZOOM              0.0
#define INITIAL_WAVELEN_START     425
#define INITIAL_WAVELEN_SCALE     8

#define WAVELEN_FIRST             400
#define WAVELEN_LAST              700
#define MAX_WAVELEN_SCALE         40

#define ZOOM_STEP_SIZE            0.025  // should be a submultiple of 1

#define SLIDE_SHOW_INTVL_US       5000000

#define FONTSZ_SMALL              50
#define FONTSZ_LARGE              90

//
// typedefs
//

//
// variables
//

static bool full_screen;
static int  fcw, fch;

//
// prototypes
//

static int pane_hndlr(pane_cx_t * pane_cx, int request, void * init_params, sdl_event_t * event);
static void set_alert(int color, char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void display_alert(rect_t *pane);

static void render_hndlr_mbs(pane_cx_t *pane_cx);
static int event_hndlr_mbs(pane_cx_t *pane_cx, sdl_event_t *event);
static void zoom_step(int n);
static void init_color_lut(void);
static void display_info_proc(rect_t *pane);
static void create_file(char *params);
static int save_file(int file_type);
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

    // get options (Linux verion only)
    // -c ctr_a,ctr_b,zoom,zoom_fraction,wavelen_start,wavelen_scale,file_type
    //     used to script creation of mbs data files, the file is created and pgm exits
    // -d enable debug prints at pgm startup
    while (true) {
        unsigned char opt_char = getopt(argc, argv, "c:d");
        if (opt_char == 0xff) {
            break;
        }
        switch (opt_char) {
        case 'c':
            snprintf(create_file_param, sizeof(create_file_param), "%s", optarg);
            break;
        case 'd':
            debug_enabled = true;
            break;
        default:
            return 1;
        }
    }

    // initialize the caching code
    cache_init();

    // create the file specified by the '-c' option, and exit
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
    fcw = sdl_font_char_width(FONTSZ_LARGE);
    fch = sdl_font_char_height(FONTSZ_LARGE);
    INFO("window=%dx%d, fcw/h=%d,%d\n", win_width, win_height, fcw, fch);

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
        case 'f':   // Linux version
            // toggle full screen
            full_screen = !full_screen;
            sdl_full_screen(full_screen);
            break;
        case 'q':   // Linux version
            // program terminate
            rc = PANE_HANDLER_RET_PANE_TERMINATE;
            break;
        case SDL_EVENT_KEY_F(1):   // Linux version
            debug_enabled = !debug_enabled;
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

    x = pane->w / 2 - strlen(alert.str) * sdl_font_char_width(FONTSZ_LARGE) / 2;
    y = pane->h / 2 - sdl_font_char_height(FONTSZ_LARGE) / 2;
    sdl_render_printf(pane, x, y, FONTSZ_LARGE, alert.color, SDL_BLACK, "%s", alert.str);
}

// - - - - - - - - -  PANE_HNDLR : MBS   - - - - - - - - - - - - - - - -

#define MODE_NORMAL 0
#define MODE_CLUT   1
#define MODE_AUTOZ  2
#define MODE_SHOW   3

#define ZOOM_TOTAL (zoom + zoom_fraction)

static complex_t    ctr                          = INITIAL_CTR;
static int          zoom                         = INITIAL_ZOOM;
static double       zoom_fraction                = INITIAL_ZOOM - (int)INITIAL_ZOOM;
static int          wavelen_start                = INITIAL_WAVELEN_START;
static int          wavelen_scale                = INITIAL_WAVELEN_SCALE;
static unsigned int color_lut[65536];

static bool         ctrls_hidden                 = true;
static bool         display_info                 = false;
static bool         debug_force_cache_thread_run = false;

static int          mode                         = MODE_SHOW;
static bool         auto_zoom_in                 = true;
static bool         auto_zoom_pause              = false;
static uint64_t     slide_show_time_us           = 0;
static int          slide_show_idx               = -1;

static void render_hndlr_mbs(pane_cx_t *pane_cx)
{
    int      idx = 0, pixel_x, pixel_y, x, y;
    rect_t  *pane = &pane_cx->pane;

    static texture_t       texture;
    static unsigned int   *pixels;
    static unsigned short *mbsval;
    static unsigned int    last_display_select_count;
    static bool            first_call = true;

    // COMMON events
    #define SDL_EVENT_MBS_INFO              (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_MBS_CLUT              (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_MBS_AUTOZ             (SDL_EVENT_USER_DEFINED + 2)
    #define SDL_EVENT_MBS_SHOW              (SDL_EVENT_USER_DEFINED + 3)
    #define SDL_EVENT_MBS_HELP              (SDL_EVENT_USER_DEFINED + 4)
    #define SDL_EVENT_MBS_HIDE              (SDL_EVENT_USER_DEFINED + 5)

    // MODE_NORMAL events
    #define SDL_EVENT_MBS_PRIOR             (SDL_EVENT_USER_DEFINED + 10)
    #define SDL_EVENT_MBS_NEXT              (SDL_EVENT_USER_DEFINED + 11)
    #define SDL_EVENT_MBS_ZOUT              (SDL_EVENT_USER_DEFINED + 12)
    #define SDL_EVENT_MBS_ZIN               (SDL_EVENT_USER_DEFINED + 13)
    #define SDL_EVENT_MBS_SAVE              (SDL_EVENT_USER_DEFINED + 14)
    #define SDL_EVENT_MBS_SVZM              (SDL_EVENT_USER_DEFINED + 15)
    #define SDL_EVENT_MBS_FILES             (SDL_EVENT_USER_DEFINED + 16)
    #define SDL_EVENT_MBS_MOUSE_MOTION_PAN  (SDL_EVENT_USER_DEFINED + 17)
    #define SDL_EVENT_MBS_MOUSE_MOTION_ZOOM (SDL_EVENT_USER_DEFINED + 18)
    #define SDL_EVENT_MBS_MOUSE_WHEEL_ZOOM  (SDL_EVENT_USER_DEFINED + 19)

    // MODE_AUTOZ events
    #define SDL_EVENT_MBS_REV               (SDL_EVENT_USER_DEFINED + 30)
    #define SDL_EVENT_MBS_PAUSE             (SDL_EVENT_USER_DEFINED + 31)

    // MODE_CLUT events
    #define SDL_EVENT_MBS_WVLEN_SCALE_MINUS (SDL_EVENT_USER_DEFINED + 40)
    #define SDL_EVENT_MBS_WVLEN_SCALE_PLUS  (SDL_EVENT_USER_DEFINED + 41)
    #define SDL_EVENT_MBS_WVLEN_START       (SDL_EVENT_USER_DEFINED + 42)

    // perform initialization first time called
    if (first_call) {
        first_call = false;

        // allocate texture, pixels and mbsval
        texture = sdl_create_texture(CACHE_WIDTH,CACHE_HEIGHT);
        pixels = malloc(CACHE_WIDTH*CACHE_HEIGHT*BYTES_PER_PIXEL);
        mbsval = malloc(CACHE_WIDTH*CACHE_HEIGHT*2);

        // initially show file idx 0
        if (max_file_info > 0) {
            show_file(0);
        }

        // init color lut; 
        // this would normally be done by the above call to show_file, except
        //  when max_file_info is zero
        init_color_lut();

        // set startup alert
        set_alert(SDL_WHITE, "TAP BOTTOM LEFT FOR CTRLS");
    }

    // if re-entering mbs display then 
    // there may be stuff to do; but currently nothing is needed
    if (display_select_count != last_display_select_count) {
        last_display_select_count = display_select_count;
    }

    // if mode is auto_zoom then increment or decrement the zoom,
    // when zoom limit is reached then reverse auto zoom direction
    if (mode == MODE_AUTOZ) {
        if (!auto_zoom_pause) {
            zoom_step(auto_zoom_in ? 4 : -4);
            if (ZOOM_TOTAL == 0) {
                auto_zoom_in = true;
            } else if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
                auto_zoom_in = false;
            }
        }
    }

    // if mode is slide-show then the files are displayed for SLIDE_SHOW_INTVL_US
    if (mode == MODE_SHOW) {
        if ((max_file_info > 0) &&
            (microsec_timer() > slide_show_time_us + SLIDE_SHOW_INTVL_US)) 
        {
            slide_show_idx = (slide_show_idx + 1) % max_file_info;
            show_file(slide_show_idx);
            slide_show_time_us = microsec_timer();
        }
    }

    // inform caching code of the possibly updated ctr and zoom
    cache_param_change(ctr, zoom, debug_force_cache_thread_run);
    debug_force_cache_thread_run = false;

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
        display_info_proc(pane);
    }

    // if mode is normal then register for PAN/ZOOM via MOUSE_MOTION event and
    // ZOOM via MOUSE_WHEEL events:
    // - this needs to be done prior to the MOUSE_CLICK event registrations below
    // - this also needs to be done before returning when ctrls are hidden
    if (mode == MODE_NORMAL) {
        int zoom_area_width = 6*fcw;
        rect_t loc = (rect_t){pane->w-zoom_area_width, 0, zoom_area_width, pane->h};
        sdl_register_event(pane, pane, SDL_EVENT_MBS_MOUSE_MOTION_PAN, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
        sdl_register_event(pane, &loc, SDL_EVENT_MBS_MOUSE_MOTION_ZOOM, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
        sdl_register_event(pane, pane, SDL_EVENT_MBS_MOUSE_WHEEL_ZOOM, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);
    }

    // if controls are hidden then 
    //   register for an event to unhide 
    //   return
    // endif
    if (ctrls_hidden) {
        int xsz=300, ysz=200;
        rect_t loc = (rect_t){0, pane->h-ysz, xsz, ysz};
        sdl_register_event(pane, &loc, SDL_EVENT_MBS_HIDE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        //sdl_render_fill_rect(pane, &loc, SDL_BLACK);  // xxx temp
        return;
    }

    // controls are not hidden ...

    // the following are always shown:
    // - on top line: INFO, CLUT, AUTOZ, SHOW, and HELP
    x = 0;
    y = 0;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, " INFO", 
            display_info ? SDL_BLUE : SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_MBS_INFO, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    x = pane->w*.30-4*fcw/2;
    y = 0;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, "CLUT", 
            mode == MODE_CLUT ? SDL_BLUE : SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_MBS_CLUT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    x = pane->w*.50-5*fcw/2;
    y = 0;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, "AUTOZ", 
            mode == MODE_AUTOZ ? SDL_BLUE : SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_MBS_AUTOZ, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    x = pane->w*.70-4*fcw/2;
    y = 0;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, "SHOW", 
            mode == MODE_SHOW ? SDL_BLUE : SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_MBS_SHOW, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    x = pane->w-5*fcw;
    y = 0;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, "HELP ", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_MBS_HELP, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    // register for mode dependant events ...

    if (mode == MODE_NORMAL) {
        // PRIOR_FILE and NEXT_FILE
        x = 0;
        y = pane->h/2-fch/2;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, " < ", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_PRIOR, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        x = pane->w-3*fcw;
        y = pane->h/2-fch/2;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, " > ", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_NEXT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // HIDE
        x = 0; 
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, " HIDE", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_HIDE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // ZM-OUT and ZM-IN
        x = pane->w*.30-4*fcw/2;
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, "ZOUT", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_ZOUT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        x = pane->w*.50-3*fcw/2;
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, "ZIN", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_ZIN, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // SAVE or SVZM 
        if (cache_thread_percent_complete() == 100) {
            x = pane->w*.70-4*fcw/2;
            y = pane->h-fch;
            sdl_render_text_and_register_event(
                    pane, x, y, FONTSZ_LARGE, "SVZM", SDL_LIGHT_BLUE, SDL_BLACK,
                    SDL_EVENT_MBS_SVZM, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        } else if (cache_thread_first_zoom_lvl_is_finished()) {
            x = pane->w*.70-4*fcw/2;
            y = pane->h-fch;
            sdl_render_text_and_register_event(
                    pane, x, y, FONTSZ_LARGE, "SAVE", SDL_LIGHT_BLUE, SDL_BLACK,
                    SDL_EVENT_MBS_SAVE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        }

        // FILE
        x = pane->w-6*fcw;
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, "FILES ", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_FILES, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    } else if (mode == MODE_CLUT) {
        int i, spacing, height=150;
        rect_t loc;

        // clear bottom of display
        loc = (rect_t){0, pane->h-height-10, pane->w, height+10};
        sdl_render_fill_rect(pane, &loc, SDL_BLACK);

        // display example: ----CLUT----  -   +   400 22

        // - clut
        x = 0;
        y = pane->h - height;
        for (i = 0; i < MBSVAL_IN_SET; i++) {
            unsigned char r,g,b;
            PIXEL_TO_RGB(color_lut[i],r,g,b);
            sdl_define_custom_color(FIRST_SDL_CUSTOM_COLOR, r,g,b);
            sdl_render_line(pane, x+i, y, x+i, pane->h-1, FIRST_SDL_CUSTOM_COLOR);
        }

        // xxx
        spacing = ((pane->w - MBSVAL_IN_SET) - 14 * fcw) / 3;
        // xxx INFO("spacing %d\n", spacing);

        // - SDL_EVENT_MBS_WVLEN_SCALE_MINUS event, " - "
        x += MBSVAL_IN_SET + fcw + spacing;
        y = (pane->h - height) + (height - fch) / 2;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, " - ", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_WVLEN_SCALE_MINUS, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // - SDL_EVENT_MBS_WVLEN_SCALE_PLUS event, " + "
        x += 3 * fcw + spacing;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, " + ", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_WVLEN_SCALE_PLUS, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // - print wavelen_start and wavelen_scale
        x += 3 * fcw + spacing;
        sdl_render_printf(
            pane, x, y,
            FONTSZ_LARGE, SDL_WHITE, SDL_BLACK, "%d %d", wavelen_start, wavelen_scale);

        // register for SDL_EVENT_MBS_WVLEN_START mouse motion event
        loc = (rect_t){0, pane->h-height, MBSVAL_IN_SET, height};
        sdl_register_event(pane, &loc, SDL_EVENT_MBS_WVLEN_START, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    } else if (mode == MODE_AUTOZ) {
        // HIDE
        x = 0; 
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, " HIDE", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_HIDE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // REV
        x = pane->w*.30-3*fcw/2;
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, "REV", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_REV, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // PAUSE
        x = pane->w*.50-5*fcw/2;
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, "PAUSE", 
                auto_zoom_pause ? SDL_BLUE : SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_PAUSE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // print the current zoom level, and auto zoom direction
        sdl_render_printf(
            pane, pane->w*0.70-3*fcw/2, pane->h-fch,
            FONTSZ_LARGE, SDL_WHITE, SDL_BLACK, "%0.1f %s", 
            ZOOM_TOTAL, (auto_zoom_in ? "IN" : "OUT"));
    } else if (mode == MODE_SHOW) {
        // HIDE
        x = 0; 
        y = pane->h-fch;
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, " HIDE", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_MBS_HIDE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // print SLIDE-SHOW at bottom center
        sdl_render_printf(
            pane, pane->w*0.50-10*fcw/2, pane->h-fch,
            FONTSZ_LARGE, SDL_WHITE, SDL_BLACK, "%s", "SLIDE-SHOW");
    } else {
        FATAL("invalid mode %d\n", mode);
    }
}

static int event_hndlr_mbs(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_NO_ACTION;
    rect_t *pane = &pane_cx->pane;

    switch (event->event_id) {
    // --- GOTO HELP DISPLAY ---
    case SDL_EVENT_MBS_HELP:
        SET_DISPLAY(DISPLAY_HELP);
        break;

    // --- TOGGLES ---
    case SDL_EVENT_MBS_INFO:
        display_info = !display_info;
        break;
    case SDL_EVENT_MBS_HIDE:
        ctrls_hidden = !ctrls_hidden;
        break;

    // --- MODE SELECT ---
    case SDL_EVENT_MBS_CLUT:
        mode = (mode == MODE_CLUT ? MODE_NORMAL : MODE_CLUT);
        break;
    case SDL_EVENT_MBS_AUTOZ:
        mode = (mode == MODE_AUTOZ ? MODE_NORMAL : MODE_AUTOZ);
        auto_zoom_pause = false;
        break;
    case SDL_EVENT_MBS_SHOW:
        mode = (mode == MODE_SHOW ? MODE_NORMAL : MODE_SHOW);
        slide_show_time_us = 0;
        slide_show_idx = -1;
        break;

    // --- MODE_NORMAL: PAN & ZOOM ---
    case SDL_EVENT_MBS_MOUSE_MOTION_PAN: {
        double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-ZOOM_TOTAL);
        int dx, dy;
        // xxx needs a comment
        if (!event->mouse_motion.end) {
            ctr += -(event->mouse_motion.delta_x * pixel_size * ((double)(CACHE_WIDTH - 200) / pane->w) ) +
                -(event->mouse_motion.delta_y * pixel_size * ((double)(CACHE_HEIGHT - 200) / pane->h)) * I;
        } else {
            if (event->mouse_motion.end_abs_total_delta_x == 0 &&
                event->mouse_motion.end_abs_total_delta_y == 0)
            {
                dx = (pane->w/2 - event->mouse_motion.x);
                dy = (pane->h/2 - event->mouse_motion.y);
                ctr += -(dx * pixel_size * ((double)(CACHE_WIDTH - 200) / pane->w) ) +
                       -(dy * pixel_size * ((double)(CACHE_HEIGHT - 200) / pane->h)) * I;
            }
        }
        break; }
    case SDL_EVENT_MBS_MOUSE_MOTION_ZOOM: {
        double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-ZOOM_TOTAL);
        int dx, dy;
        if (!event->mouse_motion.end) {
            zoom_step(-event->mouse_motion.delta_y);
        } else {
            if (event->mouse_motion.end_abs_total_delta_x == 0 &&
                event->mouse_motion.end_abs_total_delta_y == 0)
            {
                dx = (pane->w/2 - event->mouse_motion.x);
                dy = (pane->h/2 - event->mouse_motion.y);
                ctr += -(dx * pixel_size * ((double)(CACHE_WIDTH - 200) / pane->w) ) +
                       -(dy * pixel_size * ((double)(CACHE_HEIGHT - 200) / pane->h)) * I;
            }
        }
        break; }
    case SDL_EVENT_MBS_ZIN:
        zoom_step(40);
        break;
    case SDL_EVENT_MBS_ZOUT:
        zoom_step(-40);
        break;
    case SDL_EVENT_MBS_MOUSE_WHEEL_ZOOM:   // Linux version
        if (event->mouse_wheel.delta_y > 0) {
            zoom_step(4);
        } else if (event->mouse_wheel.delta_y < 0) {
            zoom_step(-4);
        }
        break;

    // --- MODE_NORMAL: FILES ---
    case SDL_EVENT_MBS_FILES:
        SET_DISPLAY(DISPLAY_DIRECTORY);
        break;
    case SDL_EVENT_MBS_SAVE:
        save_file(1);
        break;
    case SDL_EVENT_MBS_SVZM:
        save_file(2);
        break;
    case SDL_EVENT_MBS_NEXT:
    case SDL_EVENT_MBS_PRIOR: {
        static int idx = -1;
        if (max_file_info == 0) {
            break;
        }
        // xxx pick up where it left off
        idx = idx + (event->event_id == SDL_EVENT_MBS_NEXT ? 1 : -1);
        idx = (idx < 0 ? max_file_info-1 : idx >= max_file_info ? 0 : idx);
        show_file(idx);
        break; }

    // --- MODE_NORMAL: MISC - LINUX VERSION ONLY ---
    case SDL_EVENT_KEY_F(2):
        debug_force_cache_thread_run = true;
        break;
    case 'r':  
        // reset to initial params
        ctr           = INITIAL_CTR;
        zoom          = INITIAL_ZOOM;
        zoom_fraction = INITIAL_ZOOM - (int)INITIAL_ZOOM;
        wavelen_start = INITIAL_WAVELEN_START;
        wavelen_scale = INITIAL_WAVELEN_SCALE;
        init_color_lut();
        break;
    case 'z':  
        // goto either fully zoomed in or out
        if (ZOOM_TOTAL == (MAX_ZOOM-1)) {
            zoom = 0;
            zoom_fraction = 0;
        } else {
            zoom = (MAX_ZOOM-1);
            zoom_fraction = 0;
        }
        break;

    // --- MODE_AUTOZ ---
    case SDL_EVENT_MBS_REV:
        auto_zoom_in = !auto_zoom_in;
        break;
    case SDL_EVENT_MBS_PAUSE:
        auto_zoom_pause = !auto_zoom_pause;
        break;

    // --- MODE_CLUT ---
    case SDL_EVENT_MBS_WVLEN_START: 
    case SDL_EVENT_KEY_LEFT_ARROW:     // Linux version
    case SDL_EVENT_KEY_RIGHT_ARROW: {  // Linux version
        int delta;
        if (event->event_id == SDL_EVENT_MBS_WVLEN_START) {
            static double static_delta;
            static_delta += event->mouse_motion.delta_x / 8.;
            delta = static_delta;
            static_delta -= delta;
        } else {
            delta = (event->event_id == SDL_EVENT_KEY_LEFT_ARROW ? -1 : +1);
        }
        wavelen_start += delta;
        if (wavelen_start < WAVELEN_FIRST) wavelen_start = WAVELEN_LAST;
        if (wavelen_start > WAVELEN_LAST) wavelen_start = WAVELEN_FIRST;
        init_color_lut();
        break; }
    case SDL_EVENT_MBS_WVLEN_SCALE_MINUS:
    case SDL_EVENT_MBS_WVLEN_SCALE_PLUS:
    case SDL_EVENT_KEY_UP_ARROW:      // Linux version
    case SDL_EVENT_KEY_DOWN_ARROW: {  // Linux version
        int delta = ((event->event_id == SDL_EVENT_MBS_WVLEN_SCALE_MINUS ||
                      event->event_id == SDL_EVENT_KEY_DOWN_ARROW) ? -1 : +1);
        wavelen_scale += delta;
        if (wavelen_scale < 0) wavelen_scale = MAX_WAVELEN_SCALE;
        if (wavelen_scale > MAX_WAVELEN_SCALE) wavelen_scale = 0;
        init_color_lut();
        break; }

    // xxx
    // - add click to ctr, if possible    PROBABY NOT
    // - add pinch zoom, maybe            MAYBE
    // - review below, and add these too
#if 0
    // --- GENERAL ---
    case 'R':  // reset color lookup table
        wavelen_start = WAVELEN_START_DEFAULT;
        wavelen_scale = WAVELEN_SCALE_DEFAULT;
        init_color_lut();
        break;

    // --- CENTER ---
    case SDL_EVENT_CENTER: {
        double pixel_size = PIXEL_SIZE_AT_ZOOM0 * pow(2,-ZOOM_TOTAL);
        ctr += ((event->mouse_click.x - (pane->w/2)) * pixel_size) + 
               ((event->mouse_click.y - (pane->h/2)) * pixel_size) * I;
        break; }

    // --- ZOOM ---
#endif
    }

    return rc;
}

static void zoom_step(int n)
{
    double z = ZOOM_TOTAL;
    int i;

    for (i = 0; i < abs(n); i++) {
        z += (n > 0 ? ZOOM_STEP_SIZE : -ZOOM_STEP_SIZE);

        if (z <= 0) {
            z = 0;
            break;
        } else if (z >= (MAX_ZOOM-1)) {
            z = (MAX_ZOOM-1);
            break;
        }

        if (fabs(z - nearbyint(z)) < 1e-6) {
            z = nearbyint(z);
            break;  // xxx comment why
        }
    }

    zoom = z;
    zoom_fraction = z - zoom;

    INFO("z=%f zoom=%d  frac=%f\n", z, zoom, zoom_fraction);// xxx temp print
}

static void init_color_lut(void)
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

static void display_info_proc(rect_t *pane)
{
    char line[20][50];
    int  line_len[20];
    int  n=0, max_len=0, i;

    // print info to line[] array
    sprintf(line[n++], "Ctr-A: %+0.9f", creal(ctr));
    sprintf(line[n++], "Ctr-B: %+0.9f", cimag(ctr));
    sprintf(line[n++], "Zoom:  %0.2f", ZOOM_TOTAL);
    sprintf(line[n++], "CLUT:  %d %d", wavelen_start, wavelen_scale);   
    sprintf(line[n++], "Cache: %d%%", cache_thread_percent_complete());

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
    for (i = 0; i < n; i++) {
        sdl_render_printf(
            pane, 0, 1.2*fch+ROW2Y(i,FONTSZ_SMALL), 
            FONTSZ_SMALL,  SDL_WHITE, SDL_BLACK, "%s", line[i]);
    }
}

static void create_file(char *params)
{
    double ctr_a, ctr_b;
    int cnt, idx, file_type;

    cnt = sscanf(params, "%lf,%lf,%d,%lf,%d,%d,%d",
                 &ctr_a, &ctr_b, &zoom, &zoom_fraction, &wavelen_start, &wavelen_scale, &file_type);
    if (cnt != 7) {
        FATAL("invalid params '%s'\n", params);
    }
    if (file_type != 1 && file_type != 2) {
        FATAL("invalid file_type %d\n", file_type);
    }
    INFO("ctr_a,b=%f %f zoom=%d zoom_fraction=%f wavelen_start,scale=%d %d file_type=%d\n",
         ctr_a, ctr_b, zoom, zoom_fraction, wavelen_start, wavelen_scale, file_type);

    INFO("- start caching mandelbrot set\n");
    ctr = ctr_a + ctr_b * I;
    init_color_lut();
    cache_param_change(ctr, zoom, false);

    INFO("- waiting for caching to complete ...\n");
    while (true) {
        if (file_type == 1 && cache_thread_first_zoom_lvl_is_finished()) {
            break;
        } else if (file_type == 2 && cache_thread_percent_complete() == 100) {
            break;
        }
        usleep(100000);
    }

    INFO("- writing file\n");
    idx = save_file(file_type);
    INFO("- done, idx=%d\n", idx);
}

static int save_file(int file_type)
{
    int             x_idx, y_idx, pxidx, w, h, idx;
    unsigned int   *pixels;
    unsigned short *mbsval;
    double          x, y, x_step, y_step, x_start, y_start;

    // set alert
    // xxx doesn't work
    //set_alert(SDL_WHITE, "SAVING FILE");

    // init
    w = (CACHE_WIDTH - 200) *  pow(2, -zoom_fraction);
    h = (CACHE_HEIGHT - 200) *  pow(2, -zoom_fraction);
    x_start = (CACHE_WIDTH - w) / 2;
    y_start = (CACHE_HEIGHT - h) / 2;
    y_step = (double)h / DIR_PIXELS_HEIGHT;
    x_step = (double)w / DIR_PIXELS_WIDTH;
    pxidx = 0;
    DEBUG("w,h %d %d x_start,y_start %f %f\n", w, h, x_start, y_start);

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

    // the above call to cache_file_created just created a file containing the 
    // file header (cache_file_info_t); the call to cache_file_update adds the mbsvals for
    // either (when file_type==1) the current zoom level, or (when file_type==2) all zoom levels
    cache_file_update(idx, file_type);

    // set completion alert
    set_alert(SDL_GREEN, file_type == 1 ? "SAVE COMPLETE" : "SAVE-ZOOM COMPLETE");

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

    init_color_lut();
}

// - - - - - - - - -  PANE_HNDLR : HELP  - - - - - - - - - - - - - - - -

static int y_top_help;
static int max_help_row;

static void render_hndlr_help(pane_cx_t *pane_cx)
{
    char strbuff[100], *s;
    int row, x, y;
    rect_t * pane = &pane_cx->pane;

    static asset_file_t *f;
    static int last_display_select_count;

    #define SDL_EVENT_HELP_MOUSE_MOTION  (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_HELP_MOUSE_WHEEL   (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_HELP_BACK          (SDL_EVENT_USER_DEFINED + 2)
    #define SDL_EVENT_HELP_QUIT          (SDL_EVENT_USER_DEFINED + 3)

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
        y = y_top_help + ROW2Y(row,FONTSZ_SMALL);
        if (y <= -ROW2Y(1,FONTSZ_SMALL) || y >= pane->h) {
            continue;
        }
        sdl_render_printf(pane, 0, y, FONTSZ_SMALL, SDL_WHITE, SDL_BLACK, "%s", s);
    }

    // save max_help_row for use below to limit the scrolling range
    max_help_row = row;

    // register for scrool event
    sdl_register_event(pane, pane, SDL_EVENT_HELP_MOUSE_MOTION, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    sdl_register_event(pane, pane, SDL_EVENT_HELP_MOUSE_WHEEL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

    x = pane->w-5*fcw;
    y = pane->h-fch;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, "BACK ", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_HELP_BACK, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    x = pane->w-5*fcw;
    y = 0;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, "QUIT ", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_HELP_QUIT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
}

static int event_hndlr_help(pane_cx_t *pane_cx, sdl_event_t *event)
{
    int rc = PANE_HANDLER_RET_NO_ACTION;

    // handle events to adjust the help text scroll position (y_top_help)
    switch (event->event_id) {
    case SDL_EVENT_HELP_MOUSE_MOTION:
        y_top_help += event->mouse_motion.delta_y;
        break;
    case SDL_EVENT_HELP_MOUSE_WHEEL:  // Linux version
        if (event->mouse_wheel.delta_y > 0) {
            y_top_help += 20;
        } else if (event->mouse_wheel.delta_y < 0) {
            y_top_help -= 20;
        }
        break;
    case SDL_EVENT_KEY_PGUP:  // Linux version
        y_top_help += 600;
        break;
    case SDL_EVENT_KEY_PGDN:  // Linux version
        y_top_help -= 600;
        break;
    case SDL_EVENT_KEY_UP_ARROW:  // Linux version
        y_top_help += 20;
        break;
    case SDL_EVENT_KEY_DOWN_ARROW:   // Linux version
        y_top_help -= 20;
        break;
    case SDL_EVENT_KEY_HOME:  // Linux version
        y_top_help = 0;
        break;
    case SDL_EVENT_KEY_END:  // Linux version
        y_top_help = -999999999;
        break;
    case SDL_EVENT_HELP_BACK:
        SET_DISPLAY(DISPLAY_MBS);
        break;
    case SDL_EVENT_HELP_QUIT:
        rc = PANE_HANDLER_RET_PANE_TERMINATE;
        break;
    }

    // ensure y_top is in range
    int y_top_help_limit = (-max_help_row+10) * ROW2Y(1,FONTSZ_SMALL);
    if (y_top_help < y_top_help_limit) y_top_help = y_top_help_limit;
    if (y_top_help > 0) y_top_help = 0;

    return rc;
}

// - - - - - - - - -  PANE_HNDLR : DIRECTORY  - - - - - - - - - - - - -

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
    int      idx, x, y, select_cnt, total_file_size, cols;
    rect_t   loc;
    char     str[100];

    static texture_t texture;
    static int       last_display_select_count;

    #define SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL  (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_DIR_MOUSE_MOTION_SCROLL (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_DIR_DELETE              (SDL_EVENT_USER_DEFINED + 2)
    #define SDL_EVENT_DIR_BACK                (SDL_EVENT_USER_DEFINED + 3)
    #define SDL_EVENT_DIR_NOOP                (SDL_EVENT_USER_DEFINED + 4)
    #define SDL_EVENT_DIR_CHOICE              (SDL_EVENT_USER_DEFINED + 10)
    #define SDL_EVENT_DIR_SELECT              (SDL_EVENT_USER_DEFINED + 1100)

    // one time init
    if (texture == NULL) {
        texture = sdl_create_texture(DIR_PIXELS_WIDTH, DIR_PIXELS_HEIGHT);
    }

    // determine the number of cols; subtract 100 from pane width to leave
    // some space on the right for mouse-motion scroll
    cols = (pane->w - 100) / DIR_PIXELS_WIDTH;
    if (cols <= 0) cols = 1;

    // initialize, when this display has been selected, or
    // when the init_request flag has been set (init_request is set when files are deleted)
    if (display_select_count != last_display_select_count || init_request) {
        CLEAR_ALL_FILE_INFO_SELECTED;
        y_top_dir = 0;
        init_request = false;
        last_display_select_count = display_select_count;
    }

    // register events to scroll
    sdl_register_event(pane, pane, SDL_EVENT_DIR_MOUSE_MOTION_SCROLL, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
    sdl_register_event(pane, pane, SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

    // display the directory images
    for (idx = 0; idx < max_file_info; idx++) {
        cache_file_info_t *fi = file_info[idx];

        // determine location of upper left of the image 
        x = (idx % cols) * DIR_PIXELS_WIDTH;
        y = (idx / cols) * DIR_PIXELS_HEIGHT + y_top_dir;

        // continue if location is outside of the pane
        if (y <= -DIR_PIXELS_HEIGHT || y >= pane->h) {
            continue;
        }

        // display the file's directory image
        sdl_update_texture(texture, (void*)fi->dir_pixels, DIR_PIXELS_WIDTH*BYTES_PER_PIXEL);
        sdl_render_texture(pane, x, y, texture);

        // if this file is file_type 2 (containing full zoom cache) then
        // display letter 'Z' in upper right
        if (fi->file_type == 2) {
            sdl_render_printf(pane, x+DIR_PIXELS_WIDTH-2-1*fcw, y, FONTSZ_LARGE, SDL_WHITE, SDL_BLACK, "Z");
        }

        // register SDL_EVENT_DIR_CHOICE event
        loc = (rect_t){x,y,DIR_PIXELS_WIDTH,DIR_PIXELS_HEIGHT};
        sdl_register_event(pane, &loc, SDL_EVENT_DIR_CHOICE + idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        // if the image id not selected then 
        //   register for the SDL_EVENT_DIR_SELECT+idx event, using "SEL" text
        // else
        //   register for the SDL_EVENT_DIR_SELECT+idx event, and display a red rectangle 
        //    which inidcates it is selected
        // endif
        if (!fi->selected) {
            sdl_render_text_and_register_event(
                pane, x+2, y, FONTSZ_LARGE, "SEL", SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_DIR_SELECT+idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        } else {
            loc = (rect_t){x+2,y,3*fcw,fch};
            sdl_render_fill_rect(pane, &loc, SDL_RED);
            sdl_register_event(pane, &loc, SDL_EVENT_DIR_SELECT+idx, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        }

        // display green border around image
        loc = (rect_t){x-2, y-2, DIR_PIXELS_WIDTH+4, DIR_PIXELS_HEIGHT+4};
        sdl_render_rect(pane, &loc, 4, SDL_GREEN);
    }

    // clear bottom of screen where the DELETE and BACK event text is to be displayed, and
    // associate this region with the NOOP event to override any DIR_SELECT events that
    // can be in this region as a result of the above code
    loc = (rect_t){0, pane->h-(int)(1.5*fch), pane->w, (int)(1.5*fch)};
    sdl_render_fill_rect(pane, &loc, SDL_BLACK);
    sdl_register_event(pane, &loc, SDL_EVENT_DIR_NOOP, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    // register for BACK and DELETE events
    x = pane->w-5*fcw;
    y = pane->h-fch;
    sdl_render_text_and_register_event(
            pane, x, y, FONTSZ_LARGE, "BACK ", SDL_LIGHT_BLUE, SDL_BLACK,
            SDL_EVENT_DIR_BACK, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

    for (select_cnt=0, idx=0; idx < max_file_info; idx++) {
        if (file_info[idx]->selected) select_cnt++;
    }
    if (select_cnt > 0) {
        x = 0;
        y = pane->h-fch;
        sprintf(str, " DEL-%d", select_cnt);
        sdl_render_text_and_register_event(
                pane, x, y, FONTSZ_LARGE, str, SDL_LIGHT_BLUE, SDL_BLACK,
                SDL_EVENT_DIR_DELETE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
    }

    // print total_file_size
    total_file_size = 0;
    for (idx = 0; idx < max_file_info; idx++) {
        total_file_size += file_info[idx]->file_size;
    }
    sprintf(str, "%dM", (int)nearbyint((double)total_file_size/(1024*1024)));
    x = pane->w/2 - fcw*strlen(str)/2;
    y = pane->h - fch;
    sdl_render_printf(pane, x, y, FONTSZ_LARGE, SDL_WHITE, SDL_BLACK, "%s", str);
}

static int event_hndlr_directory(pane_cx_t *pane_cx, sdl_event_t *event)
{
    rect_t * pane = &pane_cx->pane;
    int      cols = (pane->w/DIR_PIXELS_WIDTH == 0 ? 1 : pane->w/DIR_PIXELS_WIDTH);
    int      rc   = PANE_HANDLER_RET_DISPLAY_REDRAW;
    int      idx, cnt;

    switch (event->event_id) {
    case SDL_EVENT_DIR_MOUSE_MOTION_SCROLL:
    case SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL:   // Linux version
        if (event->event_id == SDL_EVENT_DIR_MOUSE_WHEEL_SCROLL) {
            if (event->mouse_wheel.delta_y > 0) {
                y_top_dir += 20;
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

// build and run example:
//
// gcc -Wall -g `sdl2-config --cflags` -lSDL2 -lSDL2_ttf -lpng -lm -o create_ic_launcher create_ic_launcher.c
// ./create_ic_launcher icon.png 512

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <complex.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <SDL.h>
#include <SDL_ttf.h>

// #define PNG_DEBUG 3
#include <png.h>

// max,min values
#define MAX_HEIGHT 512
#define MAX_WIDTH  512
#define MIN_HEIGHT 42
#define MIN_WIDTH  42

// colors
#define SDL_PURPLE     0 
#define SDL_BLUE       1
#define SDL_LIGHT_BLUE 2
#define SDL_GREEN      3
#define SDL_YELLOW     4
#define SDL_ORANGE     5
#define SDL_PINK       6
#define SDL_RED        7
#define SDL_GRAY       8
#define SDL_WHITE      9
#define SDL_BLACK      10

// pixels
#define BYTES_PER_PIXEL  4
#define PIXEL(r,g,b)     (((r) << 0) | ((g) << 8) | ((b) << 16) | (255 << 24))
#define PIXEL_PURPLE     PIXEL(127,0,255)
#define PIXEL_BLUE       PIXEL(0,0,255)
#define PIXEL_LIGHT_BLUE PIXEL(0,255,255)
#define PIXEL_GREEN      PIXEL(0,255,0)
#define PIXEL_YELLOW     PIXEL(255,255,0)
#define PIXEL_ORANGE     PIXEL(255,128,0)
#define PIXEL_PINK       PIXEL(255,105,180)
#define PIXEL_RED        PIXEL(255,0,0)
#define PIXEL_GRAY       PIXEL(224,224,224)
#define PIXEL_WHITE      PIXEL(255,255,255)
#define PIXEL_BLACK      PIXEL(0,0,0)

#define PIXEL_TO_RGB(p,r,g,b) \
    do { \
        r = ((p) >>  0) & 0xff; \
        g = ((p) >>  8) & 0xff; \
        b = ((p) >> 16) & 0xff; \
    } while (0)

#define MAX_SDL_COLOR_TO_RGBA  100
#define FIRST_SDL_CUSTOM_COLOR 20

static uint32_t sdl_color_to_rgba[MAX_SDL_COLOR_TO_RGBA] = {
                            PIXEL_PURPLE,
                            PIXEL_BLUE,
                            PIXEL_LIGHT_BLUE,
                            PIXEL_GREEN,
                            PIXEL_YELLOW,
                            PIXEL_ORANGE,
                            PIXEL_PINK,
                            PIXEL_RED,
                            PIXEL_GRAY,
                            PIXEL_WHITE,
                            PIXEL_BLACK,
                                        };

// variables
SDL_Window   * sdl_window;
SDL_Renderer * sdl_renderer;

// prototypes
void draw_launcher(int width, int height);
void usage(char * progname);

TTF_Font *sdl_create_font(int font_ptsize);
void sdl_render_text(TTF_Font *font, int x, int y, int fg_color, int bg_color, char *str);
void sdl_set_color(int color);
void sdl_define_custom_color(int32_t color, uint8_t r, uint8_t g, uint8_t b);
void sdl_render_fill_rect(SDL_Rect *rect, int color);
void sdl_render_rect(SDL_Rect * rect_arg, int line_width, int color);
void sdl_render_circle(int x, int y, int r, int color);
void sdl_render_point(int x, int y, int color);
void write_png_file(char* file_name, int width, int height, void * pixels, int pitch);

// -----------------  DRAW YOUR LAUNCHER HERE  -----------------------------

// defines
#define MBSVAL_IN_SET 1000
#define WAVELEN_FIRST 400
#define WAVELEN_LAST  700

// variables
unsigned int color_lut[65536];
int wavelen_start, wavelen_scale;

// prototypes
int mandelbrot_set(double complex c);
void init_color_lut(void);
void sdl_wavelen_to_rgb(double wavelength, uint8_t *r, uint8_t *g, uint8_t *b);

// code ...
void draw_launcher(int width, int height)
{
    double complex c, center;
    int idxa, idxb;
    unsigned short mbsval;
    unsigned int pixel;
    uint8_t r,g,b;
    double pixel_size;

    // params
    wavelen_start = 425;
    wavelen_scale = 8;
    pixel_size    = 2. / width;
    center        = (-0.50 + 0.0*I);

    // initialize the color lookup table
    init_color_lut();

    // loop over the pixel coords:
    // - compute mandelbrot set value
    // - convert mbs value to pixel
    // - draw the pixel
    for (idxa = 0; idxa < width; idxa++) {
        for (idxb = 0; idxb < height; idxb++) {
            c = ((idxa - width/2) * pixel_size - (idxb - height/2) * pixel_size * I) + center;
            mbsval = mandelbrot_set(c);

            pixel = color_lut[mbsval];
            PIXEL_TO_RGB(pixel, r,g,b);

            sdl_define_custom_color(20, r,g,b);
            sdl_render_point(idxa, idxb, 20);
        }
    }
}

int mandelbrot_set(double complex c)
{
    double complex z = 0;
    double  abs_za, abs_zb;
    int     mbsval;

    for (mbsval = 0; mbsval < MBSVAL_IN_SET; mbsval++) {
        z = z * z + c;

        abs_za = fabs(creal(z));
        abs_zb = fabs(cimag(z));
        if (abs_za < M_SQRT2 && abs_zb < M_SQRT2) {
            continue;
        } else if (abs_za >= 2 || abs_zb >= 2) {
            break;
        } else if (abs_za*abs_za + abs_zb*abs_zb >= 4) {
            break;
        }
    }

    return mbsval;
}

void init_color_lut(void)
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

// ported from http://www.noah.org/wiki/Wavelength_to_RGB_in_Python
void sdl_wavelen_to_rgb(double wavelength, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double attenuation;
    double gamma = 0.8;
    double R,G,B;

    if (wavelength >= 380 && wavelength <= 440) {
        double attenuation = 0.3 + 0.7 * (wavelength - 380) / (440 - 380);
        R = pow((-(wavelength - 440) / (440 - 380)) * attenuation, gamma);
        G = 0.0;
        B = pow(1.0 * attenuation, gamma);
    } else if (wavelength >= 440 && wavelength <= 490) {
        R = 0.0;
        G = pow((wavelength - 440) / (490 - 440), gamma);
        B = 1.0;
    } else if (wavelength >= 490 && wavelength <= 510) {
        R = 0.0;
        G = 1.0;
        B = pow(-(wavelength - 510) / (510 - 490), gamma);
    } else if (wavelength >= 510 && wavelength <= 580) {
        R = pow((wavelength - 510) / (580 - 510), gamma);
        G = 1.0;
        B = 0.0;
    } else if (wavelength >= 580 && wavelength <= 645) {
        R = 1.0;
        G = pow(-(wavelength - 645) / (645 - 580), gamma);
        B = 0.0;
    } else if (wavelength >= 645 && wavelength <= 750) {
        attenuation = 0.3 + 0.7 * (750 - wavelength) / (750 - 645);
        R = pow(1.0 * attenuation, gamma);
        G = 0.0;
        B = 0.0;
    } else {
        R = 0.0;
        G = 0.0;
        B = 0.0;
    }

    if (R < 0) R = 0; else if (R > 1) R = 1;
    if (G < 0) G = 0; else if (G > 1) G = 1;
    if (B < 0) B = 0; else if (B > 1) B = 1;

    *r = R * 255;
    *g = G * 255;
    *b = B * 255;
}

// -----------------  MAIN  ------------------------------------------------

int main(int argc, char ** argv) 
{
    char   * filename;
    int      ret, width, height, icon_size;
    SDL_Rect rect;
    int      pixels[MAX_HEIGHT][MAX_WIDTH];

    // parse args: <filename> <icon_size>
    if (argc != 3) {
        usage(argv[0]);
    }
    filename = argv[1];
    if (sscanf(argv[2], "%d", &icon_size) != 1) {
        usage(argv[0]);
    }

    // initialize Simple DirectMedia Layer  (SDL)
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init failed\n");
        exit(1);
    }

    // create SDL Window and Renderer
    if (SDL_CreateWindowAndRenderer(MAX_WIDTH, MAX_HEIGHT, 0, &sdl_window, &sdl_renderer) != 0) {
        printf("SDL_CreateWindowAndRenderer failed\n");
        exit(1);
    }

    // clear display
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(sdl_renderer);

    // draw_launcher
    width = height = icon_size;
    draw_launcher(width, height);

    // present the display
    SDL_RenderPresent(sdl_renderer);

    // delay so the disaplay can be seen briefly
    sleep(1);

    // read the pixels
    rect.x = 0;
    rect.y = 0; 
    rect.w = width;
    rect.h = height;
    ret = SDL_RenderReadPixels(sdl_renderer, &rect, SDL_PIXELFORMAT_ABGR8888, pixels, sizeof(pixels[0]));
    if (ret < 0) {
        printf("ERROR SDL_RenderReadPixels, %s\n", SDL_GetError());
        exit(1);
    }

    // write the png file
    write_png_file(filename, width, height, pixels, sizeof(pixels[0]));

    // cleanup
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    return 0;
}

void usage(char * progname) 
{
    printf("Usage: %s <filename> <icon_size>\n", 
           basename(strdup(progname)));
    exit(1);
}

// -----------------  SUPPORT ROUTINES  -------------------------------------------------

TTF_Font *sdl_create_font(int font_ptsize)
{
    static bool first_call = true;
    static char *font_path;
    TTF_Font *font;

    if (first_call) {
        #define MAX_FONT_SEARCH_PATH 2
        static char font_search_path[MAX_FONT_SEARCH_PATH][PATH_MAX];
        int i;

        if (TTF_Init() < 0) {
            printf("TTF_Init failed\n");
            exit(1);  
        }
        sprintf(font_search_path[0], "%s", "/usr/share/fonts/gnu-free/FreeMonoBold.ttf");
        sprintf(font_search_path[1], "%s", "/usr/share/fonts/truetype/freefont/FreeMonoBold.ttf");
        for (i = 0; i < MAX_FONT_SEARCH_PATH; i++) {
            struct stat buf;
            font_path = font_search_path[i];
            if (stat(font_path, &buf) == 0) {
                break;
            }
        }
        if (i == MAX_FONT_SEARCH_PATH) {
            printf("failed to locate font file\n");
            exit(1);  
        }
        first_call = false;
    }

    font = TTF_OpenFont(font_path, font_ptsize);
    if (font == NULL) {
        printf("failed TTF_OpenFont(%s)\n", font_path);
    }

    return font;
}

void sdl_render_text(TTF_Font *font, int x, int y, int fg_color, int bg_color, char *str)
{
    int          w,h;
    unsigned int fg_rgba, bg_rgba;
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Color    fg_sdl_color;
    SDL_Color    bg_sdl_color;

    fg_rgba = sdl_color_to_rgba[fg_color];
    fg_sdl_color.r = (fg_rgba >> 24) & 0xff;
    fg_sdl_color.g = (fg_rgba >> 16) & 0xff;
    fg_sdl_color.b = (fg_rgba >>  8) & 0xff;
    fg_sdl_color.a = (fg_rgba >>  0) & 0xff;

    bg_rgba = sdl_color_to_rgba[bg_color];
    bg_sdl_color.r = (bg_rgba >> 24) & 0xff;
    bg_sdl_color.g = (bg_rgba >> 16) & 0xff;
    bg_sdl_color.b = (bg_rgba >>  8) & 0xff;
    bg_sdl_color.a = (bg_rgba >>  0) & 0xff;

    surface = TTF_RenderText_Shaded(font, str, fg_sdl_color, bg_sdl_color);
    texture = SDL_CreateTextureFromSurface(sdl_renderer, surface);

    SDL_QueryTexture(texture, NULL, NULL, &w, &h);
    SDL_Rect dst = {x,y,w,h};
    SDL_RenderCopy(sdl_renderer, texture, NULL, &dst);

    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

void sdl_set_color(int32_t color)
{
    uint8_t r, g, b, a;
    uint32_t rgba;

    if (color < 0 || color >= MAX_SDL_COLOR_TO_RGBA) {
        printf("ERROR color %d out of range\n", color);
        exit(1);
    }

    rgba = sdl_color_to_rgba[color];

    r = (rgba >>  0) & 0xff;
    g = (rgba >>  8) & 0xff;
    b = (rgba >> 16) & 0xff;
    a = (rgba >> 24) & 0xff;

    SDL_SetRenderDrawColor(sdl_renderer, r, g, b, a);
}

void sdl_define_custom_color(int32_t color, uint8_t r, uint8_t g, uint8_t b)
{
    if (color < FIRST_SDL_CUSTOM_COLOR || color >= MAX_SDL_COLOR_TO_RGBA) {
        printf("ERROR color %d out of range\n", color);
        exit(1);
    }

    sdl_color_to_rgba[color] = (r << 0) | ( g << 8) | (b << 16) | (0xff << 24);
}

void sdl_render_fill_rect(SDL_Rect *rect, int color)
{
    sdl_set_color(color);

    SDL_RenderFillRect(sdl_renderer, rect);
}

void sdl_render_rect(SDL_Rect * rect_arg, int line_width, int color)
{
    SDL_Rect rect = *rect_arg;
    int i;

    sdl_set_color(color);

    for (i = 0; i < line_width; i++) {
        SDL_RenderDrawRect(sdl_renderer, &rect);
        SDL_RenderDrawPoint(sdl_renderer, rect.x+rect.w-1, rect.y+rect.h-1);  // xxx workaround
        if (rect.w < 2 || rect.h < 2) {
            break;
        }
        rect.x += 1;
        rect.y += 1;
        rect.w -= 2;
        rect.h -= 2;
    }
}

void sdl_render_circle(int x_ctr, int y_ctr, int radius, int color)
{
    #define DRAWLINE(Y, XS, XE) \
        do { \
            SDL_RenderDrawLine(sdl_renderer, (XS)+x_ctr, (Y)+y_ctr,  (XE)+x_ctr, (Y)+y_ctr); \
        } while (0)

    int x = radius;
    int y = 0;
    int radiusError = 1-x;

    sdl_set_color(color);

    while(x >= y) {
        DRAWLINE(y, -x, x);
        DRAWLINE(x, -y, y);
        DRAWLINE(-y, -x, x);
        DRAWLINE(-x, -y, y);
        y++;
        if (radiusError<0) {
            radiusError += 2 * y + 1;
        } else {
            x--;
            radiusError += 2 * (y - x) + 1;
        }
    }
}

void sdl_render_point(int x, int y, int color)
{
    sdl_set_color(color);
    SDL_RenderDrawPoint(sdl_renderer, x, y);
}

void write_png_file(char* file_name, int width, int height, void * pixels, int pitch)
{
    png_byte  * row_pointers[height];
    FILE      * fp;
    png_structp png_ptr;
    png_infop   info_ptr;
    png_byte    color_type, bit_depth;
    int         y;

    // init
    color_type = PNG_COLOR_TYPE_RGB_ALPHA;
    bit_depth = 8;
    for (y = 0; y < height; y++) {
        row_pointers[y] = pixels + (y * pitch);
    }

    // create file 
    fp = fopen(file_name, "wb");
    if (!fp) {
        printf("ERROR fopen %s\n", file_name);
        exit(1);
    }

    // initialize stuff 
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        printf("ERROR png_create_write_struct\n");
        exit(1);
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        printf("ERROR png_create_info_struct\n");
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("ERROR init_io failed\n");
        exit(1);
    }
    png_init_io(png_ptr, fp);

    // write header 
    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("ERROR writing header\n");
        exit(1);
    }
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 bit_depth, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    // write bytes 
    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("ERROR writing bytes\n");
        exit(1);
    }
    png_write_image(png_ptr, row_pointers);

    // end write 
    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("ERROR end of write\n");
        exit(1);
    }
    png_write_end(png_ptr, NULL);

    // close fp
    fclose(fp);
}


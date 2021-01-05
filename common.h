#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <complex.h>
#include <tgmath.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <zlib.h>
#include <inttypes.h>

#include <util_misc.h>

// long is not portable between Linux and Android
#define long DO_NOT_USE_LONG

//
// defines
//

#define PIXEL_SIZE_AT_ZOOM0 (4. / 1000.)

#define MAX_ZOOM             47

#define MBSVAL_IN_SET        1000
#define MBSVAL_NOT_COMPUTED  65535

#define CACHE_WIDTH          1200
#define CACHE_HEIGHT         700

#define DIR_PIXELS_WIDTH     500
#define DIR_PIXELS_HEIGHT    300

//
// typedefs
//

typedef double complex complex_t;

typedef struct {
    uint64_t      magic;
    char          file_name[100];
    int           file_type;
    int           file_size;
    complex_t     ctr;
    int           zoom;
    double        zoom_fraction;
    int           wavelen_start;
    int           wavelen_scale;
    bool          selected;
    int           reserved[10];
    unsigned int  dir_pixels[DIR_PIXELS_HEIGHT][DIR_PIXELS_WIDTH];
} cache_file_info_t;

//
// variables
//

bool                debug_enabled;

cache_file_info_t * file_info[1000];
int                 max_file_info;

//
// prototypes
//

int mandelbrot_set(complex_t c);

void cache_init(void);
void cache_param_change(complex_t ctr, int zoom, bool force);
void cache_get_mbsval(unsigned short *mbsval);

bool cache_thread_first_zoom_lvl_is_finished(void);
int cache_thread_percent_complete(void);
int cache_get_last_zoom(void);

int cache_file_create(complex_t ctr, int zoom, double zoom_fraction, int wavelen_start, int wavelen_scale,
                      unsigned int *dir_pixels);
void cache_file_update(int idx, int file_type);
void cache_file_delete(int idx);
void cache_file_read(int idx);

#endif

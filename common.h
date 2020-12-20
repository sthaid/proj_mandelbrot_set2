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
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define DEBUG_PRINT_ENABLED (debug_enabled)
#include <util_misc.h>

//
// defines
//

#define MAX_ZOOM             47

#define MBSVAL_IN_SET        1000
#define MBSVAL_NOT_COMPUTED  65535

#define CACHE_WIDTH          2000
#define CACHE_HEIGHT         2000

#define DIR_PIXELS_WIDTH     300
#define DIR_PIXELS_HEIGHT    200

//
// typedefs
//

typedef struct {
    unsigned long magic;
    char          file_name[300];
    int           file_type;
    complex       ctr;
    int           zoom;
    double        zoom_fraction;
    int           wavelen_start;
    int           wavelen_scale;
    bool          deleted;
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

int mandelbrot_set(complex c);

void cache_init(double pixel_size_at_zoom0);
void cache_param_change(complex ctr, int zoom, int win_width, int win_height, bool force);
void cache_get_mbsval(unsigned short *mbsval, int width, int height);
void cache_status(int *phase_inprog, int *zoom_lvl_inprog);

bool cache_thread_first_phase1_zoom_lvl_is_finished(void);
bool cache_thread_all_is_finished(void);

int cache_file_create(complex ctr, int zoom, double zoom_fraction, int wavelen_start, int wavelen_scale,
                      unsigned int *dir_pixels);
void cache_file_update(int idx, int file_type);
void cache_file_delete(int idx);
void cache_file_read(int idx);
void cache_file_garbage_collect(void);

#endif

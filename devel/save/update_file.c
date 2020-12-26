// gcc -Wall -g -o update_file update_file.c -lz

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <complex.h>
#include <stdbool.h>

#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <zlib.h>

#define EXPECTED_FILE_SIZE  8240488
#define MAGIC_MBS_FILE      0x5555555500000002
#define MAGIC_FFCE          0x11223344

#define CACHE_WIDTH          2000
#define CACHE_HEIGHT         2000


#define DIR_PIXELS_WIDTH     300
#define DIR_PIXELS_HEIGHT    200

typedef double complex complex_t;

typedef struct {
    int x;
    int y;
    int dir;
    int cnt;
    int maxcnt;
} spiral_t;

typedef struct {
    uint64_t      magic;
    char          file_name[300];
    int           file_type;
    complex_t     ctr;
    int           zoom;
    double        zoom_fraction;
    int           wavelen_start;
    int           wavelen_scale;
    bool          selected;
    int           reserved[10];
    unsigned int  dir_pixels[DIR_PIXELS_HEIGHT][DIR_PIXELS_WIDTH];
} cache_file_info_t;

typedef struct {
    complex_t        ctr;
    int              zoom;
    double           pixel_size;
    spiral_t         phase1_spiral;
    bool             phase1_spiral_done;
    spiral_t         phase2_spiral;
    bool             phase2_spiral_done;
    union {
        unsigned short (*mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
        uint64_t pad;  // ensure the cache_t is the same size on Android
    };
} cache_t;

typedef struct {
    int magic;
    cache_t cache;
    int compressed_mbsval_datalen;
    char compressed_mbsval_data[0];
} file_format_cache_t;

typedef struct {
    cache_file_info_t   fi;
    file_format_cache_t cache[0];
} file_format_t;

char buff[10000000];

int main(int argc, char **argv)
{
    int fd, len, rc, write_len;
    file_format_t *f = (void*)buff;
    file_format_cache_t *ffce = &f->cache[0];
    uLongf destlen;

    // check argc
    if (argc == 0) {
        printf("ERROR filename needed\n");
        return 1;
    }
    printf("processing file %s\n", argv[1]);

    // read the whole file, and close
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("ERROR open, %s\n", strerror(errno));
        return 1;
    }
    len = read(fd, buff, sizeof(buff));
    if (len != EXPECTED_FILE_SIZE) {
        printf("ERROR read %d, %s\n", len, strerror(errno));
        return 1;
    }
    close(fd);

    // verify magic
    if (f->fi.magic != MAGIC_MBS_FILE) {
        printf("ERROR bad file magix 0x%lx\n", f->fi.magic);
        return 1;
    }

    // extract old fields (using the old format)
    // - cache
    // - mbsval
    cache_t *cache_ptr = (cache_t*)((void*)f + sizeof(cache_file_info_t));
    char *mbsval = malloc(CACHE_HEIGHT*CACHE_WIDTH*2);
    cache_t cache = *cache_ptr;
    memcpy(mbsval, cache_ptr+1, CACHE_HEIGHT*CACHE_WIDTH*2);

    // fix the hdr
    strcpy(f->fi.file_name, argv[1]);

    // fix the file_format_cache_t that follows the hdr
    ffce->magic = MAGIC_FFCE;
    ffce->cache = cache;
    destlen = sizeof(buff) - 100000;
    rc = compress((void*)ffce->compressed_mbsval_data, &destlen, (void*)mbsval, CACHE_HEIGHT*CACHE_WIDTH*2);
    if (rc != Z_OK) {
        printf("ERROR compress %d\n", rc);
        return 1;
    }
    ffce->compressed_mbsval_datalen = destlen;
    printf("  compressed in=%d out=%ld\n", CACHE_HEIGHT*CACHE_WIDTH*2, destlen);

    // open the file for writing and truncate; and write it
    fd = open(argv[1], O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if (fd < 0) {
        printf("ERROR open for writing, %s\n", strerror(errno));
        return 1;
    }
    write_len = offsetof(file_format_t,cache[0].compressed_mbsval_data) + destlen;
    len = write(fd, f, write_len);
    if (len != write_len) {
        printf("ERROR write %d, %s\n", len, strerror(errno));
        return 1;
    }
    close(fd);
}

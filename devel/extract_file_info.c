// gcc -Wall -g -o extract_file_info extract_file_info.c

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

#define MAGIC_MBS_FILE       0x5555555500000002

#define DIR_PIXELS_WIDTH     300
#define DIR_PIXELS_HEIGHT    200

typedef double complex complex_t;

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

int main(int argc, char **argv)
{
    cache_file_info_t fi;
    int fd, len;

    // check argc
    if (argc == 0) {
        printf("ERROR filename needed\n");
        return 1;
    }
    printf("processing file %s\n", argv[1]);

    // read the file hdr, aka cache_file_info_t
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("ERROR open, %s\n", strerror(errno));
        return 1;
    }
    len = read(fd, &fi, sizeof(fi));
    if (len != sizeof(fi)) {
        printf("ERROR read %d, %s\n", len, strerror(errno));
        return 1;
    }
    close(fd);

    // verify magic
    if (fi.magic != MAGIC_MBS_FILE) {
        printf("ERROR bad file magix 0x%lx\n", fi.magic);
        return 1;
    }

    // print info
    printf("-c %.18f,%.18f,%d,%.3f,%d,%d\n",
        creal(fi.ctr), cimag(fi.ctr), fi.zoom, fi.zoom_fraction, fi.wavelen_start, fi.wavelen_scale);

    return 0;
}

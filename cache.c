#include <common.h>

//
// defines
//

#define CACHE_THREAD_REQUEST_NONE   0
#define CACHE_THREAD_REQUEST_RUN    1
#define CACHE_THREAD_REQUEST_STOP   2

#define MAGIC_MBS_FILE              0x5555555500000202
#define MAGIC_FFCE                  0x11223344

#define CTR_INVALID                 (999 + 0 * I)

#define MBSVAL_BYTES                (CACHE_HEIGHT*CACHE_WIDTH*2)

#define PATHNAME(fn) \
    ({static char s[500]; sprintf(s, "%s/%s", cache_dir, fn), s;})

#define OPEN(fn,fd,mode) \
    do { \
        fd = open(PATHNAME(fn), mode, 0644); \
        if (fd < 0) { \
            FATAL("failed to open %s, %s\n", fn, strerror(errno)); \
        } \
    } while (0)
#define READ(fn,fd,addr,len) \
    do { \
        int rc; \
        if ((rc = read(fd, addr, len)) != (len)) {  \
            FATAL("failed to read %s, %s, req=%d act=%d\n", fn, strerror(errno), (int)len, rc); \
        } \
    } while (0)
#define WRITE(fn,fd,addr,len) \
    do { \
        int rc; \
        if ((rc = write(fd, addr, len)) != (len)) {  \
            FATAL("failed to write %s, %s, req=%d act=%d\n", fn, strerror(errno), (int)len, rc); \
        } \
    } while (0)
#define CLOSE(fn,fd) \
    do { \
        if (close(fd) != 0) { \
            FATAL("failed to close %s, %s\n", fn, strerror(errno)); \
        } \
    } while (0)
#define STAT(fn,fd,size) \
    do { \
        int rc; \
        struct stat statbuf; \
        if ((rc = fstat(fd, &statbuf)) != 0) { \
            FATAL("failed to stat %s, %s\n", fn, strerror(errno)); \
        } \
        (size) = statbuf.st_size; \
    } while (0)
#define LSEEK(fn,fd,offset) \
    do { \
        int rc; \
        if ((rc = lseek(fd, offset, SEEK_SET)) != offset) { \
            FATAL("failed to lseek %s, %s rc=%d\n", fn, strerror(errno), rc); \
        } \
    } while (0)

#define IDX_TO_FI(idx) \
    ({  if (idx >= max_file_info) \
            FATAL("idx=%d too large, max_file_info=%d\n", idx, max_file_info); \
        if (file_info[idx] == NULL) \
            FATAL("file_info[%d] is null, max_file_info=%d\n", idx, max_file_info); \
        file_info[idx]; })

//
// typedefs
//

typedef struct {
    int x;
    int y;
    int dir;
    int cnt;
    int maxcnt;
} spiral_t;

typedef struct {
    complex_t        ctr;
    int              zoom;
    double           pixel_size;
    spiral_t         spiral;
    bool             spiral_done;
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

//
// variables
//

static complex_t cache_ctr;
static int       cache_zoom;
static cache_t   cache[MAX_ZOOM];

static spiral_t  cache_initial_spiral;
static int       cache_thread_request;
static bool      cache_thread_first_zoom_lvl_finished;
static int       cache_thread_percent_done;

static char      cache_dir[200];

//
// prototypes
//

static void cache_file_init(void);
static void cache_file_copy_assets_to_internal_storage(void);

static void cache_adjust_mbsval_ctr(cache_t *cp);

static void *cache_thread(void *cx);
static void cache_thread_get_zoom_lvl_tbl(int *zoom_lvl_tbl);
static void cache_thread_issue_request(int req);
static void cache_spiral_init(spiral_t *s, int x, int y);
static void cache_spiral_get_next(spiral_t *s, int *x, int *y);

// -----------------  INITIALIZATION  -------------------------------------------------

void cache_init(void)
{
    pthread_t id;
    int       z;

    cache_spiral_init(&cache_initial_spiral, CACHE_WIDTH/2, CACHE_HEIGHT/2);

    for (z = 0; z < MAX_ZOOM; z++) {
        cache_t *cp = &cache[z];

        cp->mbsval = malloc(MBSVAL_BYTES);

        memset(cp->mbsval, 0xff, MBSVAL_BYTES);
        cp->ctr         = CTR_INVALID;
        cp->zoom        = z;
        cp->pixel_size  = PIXEL_SIZE_AT_ZOOM0 * pow(2,-z);
        cp->spiral      = cache_initial_spiral;
        cp->spiral_done = true;
    }

    cache_file_init();

    pthread_create(&id, NULL, cache_thread, NULL);
}

// -----------------  PARAMETER CHANGE  -----------------------------------------------

void cache_param_change(complex_t ctr, int zoom, bool force)
{
    // if zoom, ctr remain the same then return
    if (zoom == cache_zoom && ctr == cache_ctr && force == false) {
        return;
    }

    // stop the cache_thread
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);

    // update cache_ctr, cache_zoom
    cache_ctr  = ctr;
    cache_zoom = zoom;

    // when the cache params change it is likely to be followed by a 
    // call to cache_get_mbsval, so we need to adjust the array of cached
    // mbs values at the cache_zoom level to be prepared for this call
    cache_adjust_mbsval_ctr(&cache[cache_zoom]);

    // run the cache_thread, the cache thread restarts from the begining
    cache_thread_issue_request(CACHE_THREAD_REQUEST_RUN);
}

// -----------------  GET MBS VALUES  -------------------------------------------------

void cache_get_mbsval(unsigned short *mbsval)
{
    int i;
    cache_t *cp = &cache[cache_zoom];

    if ((fabs(creal(cp->ctr) - creal(cache_ctr)) > 1.1 * cp->pixel_size) ||
        (fabs(cimag(cp->ctr) - cimag(cache_ctr)) > 1.1 * cp->pixel_size))
    {
        FATAL("cache_zoom=%d cp->ctr=%lg+%lgI cache_ctr=%lg+%lgI\n",
              cache_zoom,
              creal(cp->ctr), cimag(cp->ctr),
              creal(cache_ctr), cimag(cache_ctr));
    }

    for (i = CACHE_HEIGHT-1; i >= 0; i--) {
        memcpy(mbsval, &(*cp->mbsval)[i][0], CACHE_WIDTH * 2);
        mbsval += CACHE_WIDTH;
    }
}

// -----------------  FILE SUPPORT  ---------------------------------------------------

static int last_file_num;

static int compare(const void *arg1, const void *arg2)
{
    return *(int*)arg1 - *(int*)arg2;
}

static void cache_file_init(void)
{
    int            idx, file_num, max_file, file_num_array[1000];
    DIR           *d;
    struct dirent *de;

    // set the location of the cache_dir to the internal_storage_path, which is either
    // - $HOME/.mbs2  on Linux, or
    // - /data/data/org.sthaid.mbs2/files    on Android
    strcpy(cache_dir, get_internal_storage_path());

    // copy the asset files, that are mbs2_nnnn.dat, to internal_storage;
    // the files being copied are the samples that are provided with this program,
    //  the program's user can save additional files to internal_storage
    cache_file_copy_assets_to_internal_storage();

    // get sorted list of file_num contained in the cache_dir;
    // the file_name format is 'mbs2_NNNN.dat', where NNNN is the file_num
    d = opendir(cache_dir);
    if (d == NULL) {
        FATAL("failed opendir %s, %s\n", cache_dir, strerror(errno));
    }
    max_file = 0;
    while ((de = readdir(d)) != NULL) {
        if (sscanf(de->d_name, "mbs2_%d_.dat", &file_num) != 1) {
            continue;
        }
        file_num_array[max_file++] = file_num;
    }
    closedir(d);
    qsort(file_num_array, max_file, sizeof(int), compare);

    // make available max_file_info and last_file_num
    last_file_num = (max_file > 0 ? file_num_array[max_file-1] : -1);
    max_file_info = max_file;

    // read each file's header, which is a cache_file_info_t, and
    // store it in the global file_info array
    // xxx should this be tolerant of bad files?
    for (idx = 0; idx < max_file_info; idx++) {
        char fn[100];
        cache_file_info_t *fi;
        int fd, len;

        // allocate memory for, and read the cache_file_info
        sprintf(fn, "mbs2_%04d.dat", file_num_array[idx]);
        fi = calloc(1,sizeof(cache_file_info_t));
        fd = open(PATHNAME(fn), O_RDONLY);
        if (fd < 0) {
            FATAL("open %s, %s\n", fn, strerror(errno));
        }
        len = read(fd, fi, sizeof(cache_file_info_t));
        if (len != sizeof(cache_file_info_t)) {
            FATAL("read %s, %s, req=%zd act=%d\n", fn, strerror(errno), sizeof(cache_file_info_t), len);
        }
        close(fd);
        file_info[idx] = fi;

        // sanity checks on the cache_file_info just read
        if (fi->magic != MAGIC_MBS_FILE) {
            FATAL("file %s invalid magic 0x%" PRIx64 "\n", fn, fi->magic);
        }
        if (strcmp(fi->file_name, fn) != 0) {
            FATAL("file %s invalid file_name %s\n", fn, fi->file_name);
        }
        if (fi->file_type < 0 || fi->file_type > 2) {
            FATAL("file %s invald type=%d\n", fn, fi->file_type);
        }
    }

    // debug print file_info
    DEBUG("max_file_info=%d last_file_num=%d\n", max_file_info, last_file_num);
    for (idx = 0; idx < max_file_info; idx++) {
        cache_file_info_t *fi = file_info[idx];
        DEBUG("idx=%d name=%s type=%d\n", idx, fi->file_name, fi->file_type);
    }
}

static void cache_file_copy_assets_to_internal_storage(void)
{
    int32_t max, i, fd, len;
    char **pathnames;
    asset_file_t *f;
    char internal_storage_pathname[300];
    FILE *fp;
    extern char *version;

    // if asset files have already been copied then return
    sprintf(internal_storage_pathname, "%s/version", cache_dir);
    fp = fopen(internal_storage_pathname, "r");
    if (fp) {
        char ver_str[100] = "";
        fgets(ver_str, sizeof(ver_str), fp);
        fclose(fp);
        if (strcmp(ver_str, version) == 0) {
            INFO("asset files have already been copied, returning\n");
            return;
        }
    }

    // save version string in <cache_dir>/version_file, so that next time
    // this program starts the asset files will not be copied to internal 
    // storage again
    INFO("creating %s file containing '%s'\n", internal_storage_pathname, version);
    fp = fopen(internal_storage_pathname, "w");
    if (fp == NULL) {
        FATAL("failed to create %s, %s\n", internal_storage_pathname, strerror(errno));
    }
    fputs(version, fp);
    fclose(fp);

    // get list of asset files in the root of the assets folder
    list_asset_files("", &max, &pathnames);
    if (pathnames == NULL) {
        FATAL("list_asset_files failed\n");
    }

    // loop over the list of files, and copy those whose names are mbs2_nnnn.dat to
    // internal storage
    for (i = 0; i < max; i++) {
        if (strstr(pathnames[i], "mbs2_") == NULL || strstr(pathnames[i], ".dat") == NULL) {
            continue;
        }

        sprintf(internal_storage_pathname, "%s/%s", cache_dir, basename(pathnames[i]));
        INFO("copying asset file %s to %s\n", pathnames[i], internal_storage_pathname);

        f = read_asset_file(pathnames[i]);
        if (f == NULL) {
            FATAL("read_asset_file %s failed\n", pathnames[i]);
        }

        fd = open(internal_storage_pathname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd < 0) {
            FATAL("failed create %s, %s\n", internal_storage_pathname, strerror(errno));
        }
        len = write(fd, f->buff, f->len);
        if (len != f->len) {
            FATAL("failed write %s, len written %d, expected %zd, %s\n", 
                  internal_storage_pathname, len, f->len, strerror(errno));
        }
        close(fd);

        read_asset_file_free(f);
    }

    // free memory that was allocated by list_asset_files
    list_asset_files_free(max, pathnames);
}

int cache_file_create(complex_t ctr, int zoom, double zoom_fraction, int wavelen_start, int wavelen_scale,
                      unsigned int *dir_pixels)
{
    int                fd, idx;
    char               file_name[100];
    file_format_t      file;
    cache_file_info_t *fi = &file.fi;

    // This routine creates a new save place file in the MBS_CACHE dir. 
    // The created file does not contain any cached mbs values, but
    // just contains the values needed to recreate the mbs image (such as ctr, zoom and 
    // color lut info). Also the directory image pixel array is included in the file.
    // What is written to the file is considered the file's header. Subsequent calls to 
    // the cache_file_update routine can be made to add cached mbs values to this file.

    // create a new file_name
    sprintf(file_name, "mbs2_%04d.dat", ++last_file_num);

    // initialize file header buffer
    memset(fi, 0, sizeof(cache_file_info_t));
    fi->magic         = MAGIC_MBS_FILE;
    strcpy(fi->file_name, file_name);
    fi->file_type     = 0;
    fi->file_size     = sizeof(cache_file_info_t);    
    fi->ctr           = ctr;
    fi->zoom          = zoom;
    fi->zoom_fraction = zoom_fraction;
    fi->wavelen_start = wavelen_start;
    fi->wavelen_scale = wavelen_scale;
    fi->selected      = false;
    memcpy(fi->dir_pixels, dir_pixels, sizeof(fi->dir_pixels));

    // create the file
    OPEN(file_name, fd, O_CREAT|O_EXCL|O_WRONLY);
    WRITE(file_name, fd, fi, sizeof(cache_file_info_t));
    CLOSE(file_name, fd);

    // save the new fi at the end of the file_info array
    idx = max_file_info;
    if (file_info[idx] != NULL) {
        FATAL("file_info[%d] not NULL\n", idx);
    }
    file_info[idx] = calloc(1,sizeof(cache_file_info_t));
    *file_info[idx] = *fi;
    max_file_info++;

    // return the idx of the new file_info entry
    return idx;
}

void cache_file_delete(int idx)
{
    if (idx < 0 || idx >= max_file_info) {
        FATAL("invalid idx=%d, max_file_info=%d\n", idx, max_file_info);
    }

    // unlink the file
    unlink(PATHNAME(file_info[idx]->file_name));

    // remove file_info[idx] from file_info array
    free(file_info[idx]);
    memmove(&file_info[idx], &file_info[idx+1], (max_file_info-idx-1)*sizeof(void*));
    max_file_info--;
    file_info[max_file_info] = NULL;
}

void cache_file_read(int idx)
{
    cache_file_info_t *fi = IDX_TO_FI(idx);
    cache_file_info_t  fi_tmp;
    int                fd, i, n;

    // This routine will read the cached mbs values from the file into the
    // cache[] array which is defined globally in this file.
    // File_type:
    // - 0     : has just the cache_file_info_t (the file header) and no cached mbs values
    // - 1 & 2 : both have cached mbs values; (1) has just the cached values for the 
    //           zoom level that was saved; (2) has cached mbs values for all zoom levels

    // file_type 0 does not contain any cached mbs values
    if (fi->file_type == 0) {
        return;
    }

    // open and read the file hdr
    OPEN(fi->file_name, fd, O_RDONLY);
    READ(fi->file_name, fd, &fi_tmp, sizeof(fi_tmp));

    // sanity check that the file hdr just read is correct;
    // it most be equal to the file_info that we already have 
    if (memcmp(&fi_tmp, fi, sizeof(cache_file_info_t)) != 0) {
        FATAL("file hdr error in %s, fn=%s\n", fi->file_name, fi_tmp.file_name);
    }

    // set the number of zoom levels that are expected to be in this file
    n = (fi->file_type == 1 ? 1 : 
         fi->file_type == 2 ? MAX_ZOOM
                            : ({FATAL("file_type=%d\n",fi->file_type);0;}));
    
    // read cached mbsvals from the file , and store them in the cache[] array
    cache_thread_issue_request(CACHE_THREAD_REQUEST_STOP);
    for (i = 0; i < n; i++) {
        static file_format_cache_t ffce;
        int z;
        Bytef *compressed_mbsval_data;
        uLongf destlen;
        int rc;

        // read the file_format_cache_t up to the begining of the compressed_mbsval_data
        // field that is at the end of this structure
        READ(fi->file_name, fd, &ffce, offsetof(file_format_cache_t, compressed_mbsval_data));

        // verify fields of the file_format_cache_t (ffce)
        if (ffce.magic != MAGIC_FFCE) {
            FATAL("ffce.magic=0x%x, expected=0x%x\n", ffce.magic, MAGIC_FFCE);
        }
        z = ffce.cache.zoom;
        if (z < 0 || z >= MAX_ZOOM) {
            FATAL("ffce.cache.zoom=%d out of range\n", z);
        }

        // set cache[z] to the ffce.cache value that was read above;
        // while preserving the cache[z].mbsval
        ffce.cache.mbsval = cache[z].mbsval;
        cache[z] = ffce.cache;

        // read mbsval_data, and decompress to cache[z].mbsval ...

        // - allocate buffer to read the compressed_mbsval_data into
        compressed_mbsval_data = malloc(ffce.compressed_mbsval_datalen);
        if (compressed_mbsval_data == NULL) {
            FATAL("malloc %d failed\n", ffce.compressed_mbsval_datalen);
        }
        INFO("comp datalen = %d\n", ffce.compressed_mbsval_datalen);

        // - read the compressed_mbsval_data
        READ(fi->file_name, fd, compressed_mbsval_data, ffce.compressed_mbsval_datalen);

        // - decompress compressed_mbsval_data to cache[z].mbsval
        destlen = MBSVAL_BYTES;
        rc = uncompress((void*)cache[z].mbsval, &destlen, compressed_mbsval_data, ffce.compressed_mbsval_datalen);

        // - free the compressed_mbsval_data
        free(compressed_mbsval_data);

        // - check that the uncompress was successful
        if (rc != Z_OK) {
            FATAL("uncompress rc=%d\n", rc);
        }
        if (destlen != MBSVAL_BYTES) {
            FATAL("uncompressed destlen=%d, expected=%d\n", (int)destlen, MBSVAL_BYTES);
        }
    }

    // close file
    CLOSE(fi->file_name, fd);

    // update the cache ctr and zoom with the new ctr and zoom for this file;
    // note that this call will also issue the CACHE_THREAD_REQUEST_RUN
    cache_param_change(fi->ctr, fi->zoom, true);
}

void cache_file_update(int idx, int file_type)
{
    cache_file_info_t *fi = IDX_TO_FI(idx);
    int z, fd, rc;
    Bytef *compressed_mbsval_data;
    uLongf destlen;
    file_format_cache_t ffce;

    #define COMPRESSED_MBSVAL_DATA_BUFF_SIZE (10*1000000)

    DEBUG("idx=%d fn=%s file_type=%d->%d\n", idx, fi->file_name, fi->file_type, file_type);

    // This routine will re-write the specified file using the requested file_type.
    // File_type:
    // 0 - just the hdr (255K file size)
    // 1 - hdr plus cached mbs value for the file's zoom level (7.9M file size)
    // 2 - hdr plus cached mbs values for all zoom levels (355M file size);
    //     this is useful if autozoom is going to be used to view all zoom levels

    // when called for file_type 1 or 2 the cache_ctr and cache_zoom
    // are supposed to be equal to the file_info
    if (file_type == 1 || file_type == 2) {
        if (fi->ctr != cache_ctr || fi->zoom != cache_zoom) {
            FATAL("cache_ctr/zoom don't match file_info\n");
        }
    }

    // open for writing, and truncate
    OPEN(fi->file_name, fd, O_TRUNC|O_WRONLY);

    // write the file header
    fi->file_type = file_type;
    WRITE(fi->file_name, fd, fi, sizeof(cache_file_info_t));

    // allocate buffer for the compressed_mbsval_data
    compressed_mbsval_data = malloc(COMPRESSED_MBSVAL_DATA_BUFF_SIZE);
    if (compressed_mbsval_data == NULL) {
        FATAL("malloc %d failed\n", ffce.compressed_mbsval_datalen);
    }

    // write the desired zoom levels
    for (z = 0; z < MAX_ZOOM; z++) {
        if ((file_type == 1 && z == fi->zoom) || (file_type == 2)) {
            DEBUG("- writing zoom lvl %d\n", z);

            // compress cache[z].mbsval to compressed_mbsval_data
            destlen = COMPRESSED_MBSVAL_DATA_BUFF_SIZE;
            rc = compress(compressed_mbsval_data, &destlen, (void*)cache[z].mbsval, MBSVAL_BYTES);
            if (rc != Z_OK) {
                FATAL("compress rc=%d\n", rc);
            }

            // construct file_format_cache_t (ffce)
            memset(&ffce,0,sizeof(ffce));
            ffce.magic = MAGIC_FFCE;
            ffce.cache = cache[z];    
            ffce.cache.mbsval = NULL;
            ffce.compressed_mbsval_datalen = destlen;

            // write the ffce and compressed_mbsval_data
            WRITE(fi->file_name, fd, &ffce,  offsetof(file_format_cache_t,compressed_mbsval_data));
            WRITE(fi->file_name, fd, compressed_mbsval_data, destlen);
        }
    }

    // free compressed_mbsval_data
    free(compressed_mbsval_data);

    // update the file hdr's file_size field
    STAT(fi->file_name, fd, fi->file_size);
    LSEEK(fi->file_name, fd, offsetof(cache_file_info_t,file_size));
    WRITE(fi->file_name, fd, &fi->file_size, sizeof(fi->file_size));
    INFO("XXX updated SIZE to %d\n", fi->file_size);

    // close
    CLOSE(fi->file_name, fd);
}

// -----------------  ADJUST MBSVAL CENTER  -------------------------------------------

static void cache_adjust_mbsval_ctr(cache_t *cp)
{
    int old_y, new_y, delta_x, delta_y;
    unsigned short (*new_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];
    unsigned short (*old_mbsval)[CACHE_HEIGHT][CACHE_WIDTH];

    // This routine is called to update cached mbs values when the cache center
    // has changed. If the cache center has changed dramatically then this routine
    // will result in setting all cached mbs values to MBSVAL_NOT_COMPUTED (65535 aka -1).
    // If the cache center has changed less dramatically then the cached mbs values are
    // copied from the current array of cached mbs values to a new array, adjusting for
    // the change in the center (delta_x,delta_y). In this case the new array will have 
    // some areas where the mbs values are MBSVAL_NOT_COMPUTED.

    delta_x = nearbyint((creal(cp->ctr) - creal(cache_ctr)) / cp->pixel_size);
    delta_y = nearbyint((cimag(cp->ctr) - cimag(cache_ctr)) / cp->pixel_size);

    if (delta_x == 0 && delta_y == 0) {
        return;
    }

    new_mbsval = malloc(MBSVAL_BYTES);
    old_mbsval = cp->mbsval;

    for (new_y = 0; new_y < CACHE_HEIGHT; new_y++) {
        old_y = new_y + delta_y;
        if (old_y < 0 || old_y >= CACHE_HEIGHT) {
            memset(&(*new_mbsval)[new_y][0], 0xff, CACHE_WIDTH*2);
            continue;
        }

        if (delta_x <= -CACHE_WIDTH || delta_x >= CACHE_WIDTH) {
            memset(&(*new_mbsval)[new_y][0], 0xff, CACHE_WIDTH*2);
            continue;
        }

        memset(&(*new_mbsval)[new_y][0], 0xff, CACHE_WIDTH*2);
        if (delta_x <= 0) {
            memcpy(&(*new_mbsval)[new_y][0],
                   &(*old_mbsval)[old_y][-delta_x],
                   (CACHE_WIDTH + delta_x) * 2);
        } else {
            memcpy(&(*new_mbsval)[new_y][delta_x],
                   &(*old_mbsval)[old_y][0],
                   (CACHE_WIDTH - delta_x) * 2);
        }
    }

    free(cp->mbsval);
    cp->mbsval      = new_mbsval;
    cp->ctr         = cache_ctr;
    cp->spiral      = cache_initial_spiral;
    cp->spiral_done = false;
}

// -----------------  CACHE THREAD  ---------------------------------------------------

// The cache thread's job is to pre-compute and save mbs values from locations that
// are likely to be accessed soon by the user. For example: if the user has been zooming
// in on a location, then the cache thread will precompute cache values at subsequent 
// zoom levels for the same location.

bool cache_thread_first_zoom_lvl_is_finished(void)
{
    return cache_thread_first_zoom_lvl_finished;
}

int cache_thread_percent_complete(void)
{
    return cache_thread_percent_done;
}

static void *cache_thread(void *cx)
{
    #define CHECK_FOR_STOP_REQUEST \
        do { \
            if (cache_thread_request == CACHE_THREAD_REQUEST_STOP) { \
                was_stopped = true; \
                cache_thread_request = CACHE_THREAD_REQUEST_NONE; \
                __sync_synchronize(); \
                goto restart; \
            } \
        } while (0)

    #define COMPUTE_MBSVAL(_idx_a,_idx_b,_cp) \
        do { \
            if ((*(_cp)->mbsval)[_idx_b][_idx_a] == MBSVAL_NOT_COMPUTED) { \
                complex_t c; \
                c = ((((_idx_a)-(CACHE_WIDTH/2)) * (_cp)->pixel_size) -  \
                     (((_idx_b)-(CACHE_HEIGHT/2)) * (_cp)->pixel_size) * I) +  \
                    cache_ctr; \
                (*(_cp)->mbsval)[_idx_b][_idx_a] = mandelbrot_set(c); \
                mbs_calc_count++; \
            } else { \
                mbs_not_calc_count++; \
            } \
        } while (0)

    #define SPIRAL_OUTSIDE_CACHE \
        (idx_a < 0 || idx_a >= CACHE_WIDTH || idx_b < 0 || idx_b >= CACHE_HEIGHT)
    #define SPIRAL_COMPLETE_CACHE \
        (CACHE_WIDTH >= CACHE_HEIGHT ? idx_a < 0 : idx_b < 0)

    cache_t * cp;
    int       n, idx_a, idx_b;
    uint64_t  start_us = 0;
    bool      was_stopped;
    int       mbs_calc_count;
    int       mbs_not_calc_count;
    int       zoom_lvl_tbl[MAX_ZOOM];

    while (true) {
restart:
        // debug print the completion status
        if (start_us != 0) {
            INFO("%s  mbs_calc_count=%d,%d  duration=%.3f secs\n",
                 !was_stopped ? "DONE" : "STOPPED",
                 mbs_calc_count,
                 mbs_not_calc_count,
                 (microsec_timer() - start_us) / 1000000.);
            start_us = 0;
        }

        // wait here for a request
        //
        // note that a request can either be:
        // - STOP: since the cache thread is already stopped this is a noop
        // - RUN:  the cache thread will run, and compute the mbs values at the
        //         current cache center, and for all zoom levels
        while (cache_thread_request == CACHE_THREAD_REQUEST_NONE) {
            usleep(100);
        }

        // handle stop request received when we are already stopped
        CHECK_FOR_STOP_REQUEST;

        // the request must be a run request; ack the request
        if (cache_thread_request != CACHE_THREAD_REQUEST_RUN) {
            FATAL("invalid cache_thread_request %d\n", cache_thread_request);
        }
        cache_thread_request = CACHE_THREAD_REQUEST_NONE;
        __sync_synchronize();

        // init variables at the start of the cache thread running
        start_us           = microsec_timer();
        was_stopped        = false;
        mbs_calc_count     = 0;
        mbs_not_calc_count = 0;

        // the zoom_lvl_tbl is an array of zoom levels in the order that the
        // cache thread will be processing them; for example suppose the
        // user has recently been zooming in, and currently at zoom level 42,
        // then the returned zoom_lvl_tbl will be:
        //   42,  43,44,45,46,  41,40,39,...,0
        cache_thread_get_zoom_lvl_tbl(zoom_lvl_tbl);

        // loop over all zoom levels,
        //
        // this code section will compute mbs values, for the zoom levels in zoom_lvl_tbl, and
        // extending to the window size dimensions.
        DEBUG("STARTING\n");
        for (n = 0; n < MAX_ZOOM; n++) {
            CHECK_FOR_STOP_REQUEST;

            // xxx comment
            if (n == 1) {
                cache_thread_first_zoom_lvl_finished = true;
                __sync_synchronize();
                DEBUG("FINISHED lvl0\n");
            }

            // since it is likely the cache thread has been requested to run becuae
            // the cache center has changed, we need to first call cache_adjust_mbsval_ct
            // to move the currently saved mbs values to locations in the array that
            // are consistent with the new cache center
            //
            // note that the cache_adjust_mbsval_ctr routine will reset the 
            // spirals if an adjustment was made
            DEBUG("starting: idx=%d lvl=%d\n", n, zoom_lvl_tbl[n]);
            cache_thread_percent_done = 100 * n / MAX_ZOOM;
            cp = &cache[ zoom_lvl_tbl[n] ];
            cache_adjust_mbsval_ctr(cp);

            // if spiral is done (meaning that the cache thread has computed 
            // the mbs values in locations spiralling out from the center to encompass
            // the entire cache then continue with the next zoom_lvl_tbl index
            if (cp->spiral_done) {
                continue;
            }

            // loop until the cache zoom level being processed has all of its
            // mbs values computed encompassing the entire window dimenstions
            while (true) {
                CHECK_FOR_STOP_REQUEST;

                cache_spiral_get_next(&cp->spiral, &idx_a, &idx_b);

                if (SPIRAL_OUTSIDE_CACHE) {
                    if (SPIRAL_COMPLETE_CACHE) {
                        cp->spiral_done = true;
                        break;
                    } else {
                        continue;
                    }
                }

                COMPUTE_MBSVAL(idx_a,idx_b,cp);
            }
        }

        // caching of all zoom levels has completed
        cache_thread_percent_done = 100;
        __sync_synchronize();
        DEBUG("ALL FINISHED \n");
    }
}

static void cache_thread_get_zoom_lvl_tbl(int *zoom_lvl_tbl)
{
    int n, idx;

    static bool dir_is_up      = true;
    static int  last_cache_zoom = 0;

    if (cache_zoom == 0) {
        dir_is_up = true;
    } else if (cache_zoom == (MAX_ZOOM-1)) {
        dir_is_up = false;
    } else if (cache_zoom > last_cache_zoom) {
        dir_is_up = true;
    } else if (cache_zoom < last_cache_zoom) {
        dir_is_up = false;
    } else {
        // no change to dir
    }

    n = 0;
    if (dir_is_up) {
        for (idx = cache_zoom; idx <= (MAX_ZOOM-1); idx++) zoom_lvl_tbl[n++] = idx;
        for (idx = cache_zoom-1; idx >= 0; idx--) zoom_lvl_tbl[n++] = idx;
    } else {
        for (idx = cache_zoom; idx >= 0; idx--) zoom_lvl_tbl[n++] = idx;
        for (idx = cache_zoom+1; idx <= (MAX_ZOOM-1); idx++) zoom_lvl_tbl[n++] = idx;
    }
    if (n != MAX_ZOOM) FATAL("n = %d\n",n);

#if 0
    char str[200], *p=str;
    for (n = 0; n < MAX_ZOOM; n++) {
        p += sprintf(p, "%d ", zoom_lvl_tbl[n]);
    }
    DEBUG("dir=%s zoom=%d last=%d - %s\n", 
         (dir_is_up ? "UP" : "DOWN"),
         cache_zoom, last_cache_zoom, str);
#endif

    last_cache_zoom = cache_zoom;
}

static void cache_thread_issue_request(int req)
{
    // reset cache thread progress status flags
    cache_thread_first_zoom_lvl_finished = false;
    cache_thread_percent_done = 0;

    // set the cache_thread_request; the cache_thread is polling on this variable
    __sync_synchronize();
    cache_thread_request = req;
    __sync_synchronize();

    // wait here until the cache_thread acknowledges receipt of the request;
    // note - once the cache_thread sees the new request the cache thread
    //        will first set cache_thread_request back to CACHE_THREAD_REQUEST_NONE and
    //        then start doing its work
    while (cache_thread_request != CACHE_THREAD_REQUEST_NONE) {
        usleep(100);
    }
}

static void cache_spiral_init(spiral_t *s, int x, int y)
{
    // This routine resets the spiral to a starting location (x,y).

    memset(s, 0, sizeof(spiral_t));
    s->x = x;
    s->y = y;
}

static void cache_spiral_get_next(spiral_t *s, int *x, int *y)
{
    #define DIR_RIGHT  0
    #define DIR_DOWN   1
    #define DIR_LEFT   2
    #define DIR_UP     3

    // The first call to this routine, following cache_spiral_init, will
    // return the initial x,y of the spiral.
    // Subsequent calls will return the next x,y in the spiral.

    if (s->maxcnt == 0) {
        s->maxcnt = 1;
    } else {
        switch (s->dir) {
        case DIR_RIGHT:  s->x++; break;
        case DIR_DOWN:   s->y--; break;
        case DIR_LEFT:   s->x--; break;
        case DIR_UP:     s->y++; break;
        default:         FATAL("invalid dir %d\n", s->dir); break;
        }

        s->cnt++;
        if (s->cnt == s->maxcnt) {
            s->cnt = 0;
            if (s->dir == DIR_DOWN || s->dir == DIR_UP) {
                s->maxcnt++;
            }
            s->dir = (s->dir+1) % 4;
        }
    }

    *x = s->x;
    *y = s->y;
}

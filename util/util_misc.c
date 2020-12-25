#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#include <inttypes.h>
#include <sys/time.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <math.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include "util_misc.h"

#include <SDL.h>

#ifdef ANDROID
#include <android/asset_manager_jni.h>
#endif

// -----------------  LOGMSG  --------------------------------------------

#ifndef ANDROID
void logmsg(char *lvl, const char *func, char *fmt, ...) 
{
    va_list ap;
    char    msg[1000];
    int     len;
    char    time_str[MAX_TIME_STR];

    // construct msg
    va_start(ap, fmt);
    len = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    // remove terminating newline
    if (len > 0 && msg[len-1] == '\n') {
        msg[len-1] = '\0';
        len--;
    }

    // log to stderr 
    fprintf(stderr, "%s %s %s: %s\n",
            time2str(time_str, get_real_time_us(), false, true, true),
            lvl, func, msg);
}

#else

void logmsg(char *lvl, const char *func, char *fmt, ...) 
{
    va_list ap;
    char    msg[1000];
    int     len;
    char    time_str[MAX_TIME_STR];

    // construct msg
    va_start(ap, fmt);
    len = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    // remove terminating newline
    if (len > 0 && msg[len-1] == '\n') {
        msg[len-1] = '\0';
        len--;
    }

    // log the message
    if (strcmp(lvl, "INFO") == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "%s %s %s: %s\n",
                    time2str(time_str, get_real_time_us(), false, true, true),
                    lvl, func, msg);
    } else if (strcmp(lvl, "WARN") == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "%s %s %s: %s\n",
                    time2str(time_str, get_real_time_us(), false, true, true),
                    lvl, func, msg);
    } else if (strcmp(lvl, "FATAL") == 0) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "%s %s %s: %s\n",
                        time2str(time_str, get_real_time_us(), false, true, true),
                        lvl, func, msg);
    } else if (strcmp(lvl, "DEBUG") == 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "%s %s %s: %s\n",
                     time2str(time_str, get_real_time_us(), false, true, true),
                     lvl, func, msg);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "%s %s %s: %s\n",
                     time2str(time_str, get_real_time_us(), false, true, true),
                     lvl, func, msg);
    }
}
#endif

// -----------------  ASSET FILE SUPPORT  --------------------------------

// - - - - - - - - -  LIST ASSET FILES   - - - - - - - - - - - - - - 

// the list_asset_files routine returns *pathnames set to NULL 
// when an error occurs

#ifndef ANDROID
void list_asset_files(char *location, int32_t *max, char ***pathnames)
{
    DIR           * dir;
    struct dirent * dirent;
    int32_t         ret;
    struct stat     buf;
    char            filename[300];
    char            tmpfn[310];
    char            dirpath[300];

    sprintf(dirpath, "assets/%s", location);
    INFO("dirpath %s\n", dirpath);

    *max = 0;
    *pathnames = calloc(1000,sizeof(char*)); //xxx realloc if needed
    if (*pathnames == NULL) {
        ERROR("calloc\n");
        return;
    }

    dir = opendir(dirpath);
    if (dir == NULL) {
        ERROR("opendir %s, %s\n", dirpath, strerror(errno));
        free(*pathnames);
        *pathnames = NULL;
        return;
    }

    while ((dirent = readdir(dir)) != NULL) {
        if (location[0] == '\0') {
            sprintf(filename, "%s", dirent->d_name);
        } else {
            sprintf(filename, "%s/%s", location, dirent->d_name);
        }
        INFO("filename '%s'\n", filename);

        sprintf(tmpfn, "assets/%s", filename);
        ret = stat(tmpfn, &buf);
        if (ret != 0 || !S_ISREG(buf.st_mode)) {
            INFO("%s is not a regular file\n", tmpfn);
            continue;
        }

        (*pathnames)[*max] = malloc(strlen(filename)+1);
        strcpy((*pathnames)[*max], filename);
        (*max)++;
    }

    closedir(dir);
}
#else
void list_asset_files(char *location, int32_t *max, char ***pathnames)
{
    jmethodID       mid;
    jobject         context;
    jobject         java_asset_manager;
    AAssetManager * asset_manager;
    AAssetDir     * asset_dir;
    const char    * fn;
    int32_t         len;

    // this code is derived from 
    // src/core/android/SDL_android.c

    *max = 0;
    *pathnames = calloc(1000,sizeof(char*));
    if (*pathnames == NULL) {
        ERROR("calloc\n");
        return;
    }

    // reference: https://wiki.libsdl.org/SDL_AndroidGetActivity
    // - retrieve the JNI environment.
    // - retrieve the Java instance of the SDLActivity
    // - find the Java class of the activity. It should be SDLActivity or a subclass of it.
    JNIEnv* mEnv = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass mActivityClass = (*mEnv)->GetObjectClass(mEnv,activity);

    // not sure what this is for, it was in SDL_android.c
    (*mEnv)->PushLocalFrame(mEnv, 16);

    // context = SDLActivity.getContext(); 
    mid = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
            "getContext","()Landroid/content/Context;");
    context = (*mEnv)->CallStaticObjectMethod(mEnv, mActivityClass, mid);

    // asset_manager = context.getAssets();
    mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, context),
            "getAssets", "()Landroid/content/res/AssetManager;");
    java_asset_manager = (*mEnv)->CallObjectMethod(mEnv, context, mid);

    // get pointer to asset_manager
    asset_manager = AAssetManager_fromJava(mEnv, java_asset_manager);

    // open asset_dir and get the asset filenames
    asset_dir = AAssetManager_openDir(asset_manager, location);
    while ((fn = AAssetDir_getNextFileName(asset_dir)) != NULL) {
        (*pathnames)[*max] = malloc(strlen(location)+strlen(fn)+2);
        if (location[0] == '\0') {
            sprintf((*pathnames)[*max], "%s", fn);
        } else {
            sprintf((*pathnames)[*max], "%s/%s", location, fn);
        }
        INFO("got pathane %s\n", (*pathnames)[*max]);
        (*max)++;
    }

    // close the asset_dir
    AAssetDir_close(asset_dir);

    // not sure what this is for, it was in SDL_android.c
    (*mEnv)->PopLocalFrame(mEnv, NULL);
}
#endif

void list_asset_files_free(int32_t max, char **pathnames)
{
    int32_t i;
    for (i = 0; i < max; i++) {
        free(pathnames[i]);
    }
    free(pathnames);
}

// - - - - - - - - -  READ ASSET FILES   - - - - - - - - - - - - - - 

asset_file_t * read_asset_file(char * pathname_arg)
{
    #define MAX_BUFF  20000000  //xxx check for overflow

    asset_file_t * f = NULL;
    void         * buff;
    SDL_RWops    * rw;
    size_t         len;
    char           pathname[300];

#ifndef ANDROID
    sprintf(pathname, "assets/%s", pathname_arg);
#else
    sprintf(pathname, "%s", pathname_arg);
#endif

    buff = malloc(MAX_BUFF);
    f = calloc(1,sizeof(asset_file_t));
    if (buff == NULL || f == NULL) {
        goto error;
    }

    rw = SDL_RWFromFile(pathname, "r");
    if (rw == NULL) {
        ERROR("open %s, %s\n", pathname, SDL_GetError());
        goto error;
    }

    len = SDL_RWread(rw, buff, 1, MAX_BUFF);
    if (len == 0) {
        ERROR("read %s, len=%d\n", pathname, (int32_t)len);
        SDL_RWclose(rw);
        goto error;
    }

    SDL_RWclose(rw);

    f->buff = buff;
    f->len = len;
    f->offset = 0;
    return f;

error:
    free(f);
    free(buff);
    return NULL;
}

void read_asset_file_free(asset_file_t * f)
{
    if (f == NULL) {
        return;
    }

    free(f->buff);
    free(f);
}

// - - - - - - - - -  READ FILE LINES FROM MEMORY BUFFER   - - - - - 

char * read_file_line(asset_file_t *f, char *str, size_t strsize)
{
    int32_t cnt = 0;

    // return NULL if at end of file
    if (f->offset >= f->len) {
        return NULL;
    }

    // copy chars to return buffer until newline char or EOF
    while (true) {
        if (f->offset == f->len) {
            break;
        }
        if (f->buff[f->offset] == '\n') {
            f->offset++;
            break;
        }
        if (cnt < strsize-1) {
            str[cnt++] = f->buff[f->offset];
        }
        f->offset++;
    }

    // null terminate, and return 
    str[cnt] = '\0';
    return str;
}

// -----------------  INTERNAL STORAGE SUPPORT  --------------------------

char *get_internal_storage_path(void)
{
    static char storage_path[200];

    if (storage_path[0] != '\0') {
        return storage_path;
    }

#ifndef ANDROID
    char *home_dir;
    int rc;
    home_dir = getenv("HOME");
    if (home_dir == NULL) {
        FATAL("env var HOME not set\n");
    }
    sprintf(storage_path, "%s/%s", home_dir, ".mbs2");  // XXX  
    rc = mkdir(storage_path, 0755);
    if (rc < 0 && errno != EEXIST) {
        FATAL("failed to create %s, %s\n", storage_path, strerror(errno));
    }
#else
    const char *android_internal_storage_path;
    android_internal_storage_path = SDL_AndroidGetInternalStoragePath();
    if (android_internal_storage_path == NULL) {
        FATAL("android internal storage path not set\n");
    }
    sprintf(storage_path, "%s", android_internal_storage_path);
#endif

    INFO("storage_path = '%s'\n", storage_path);
    return storage_path;
}

// -----------------  TIME UTILS  -----------------------------------------

uint64_t microsec_timer(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC,&ts);
    return  ((uint64_t)ts.tv_sec * 1000000) + ((uint64_t)ts.tv_nsec / 1000);
}

uint64_t get_real_time_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME,&ts);
    return ((uint64_t)ts.tv_sec * 1000000) + ((uint64_t)ts.tv_nsec / 1000);
}

char * time2str(char * str, int64_t us, bool gmt, bool display_ms, bool display_date) 
{
    struct tm tm;
    time_t secs;
    int32_t cnt;
    char * s = str;

    secs = us / 1000000;

    if (gmt) {
        gmtime_r(&secs, &tm);
    } else {
        localtime_r(&secs, &tm);
    }

    if (display_date) {
        cnt = sprintf(s, "%2.2d/%2.2d/%2.2d ",
                         tm.tm_mon+1, tm.tm_mday, tm.tm_year%100);
        s += cnt;
    }

    cnt = sprintf(s, "%2.2d:%2.2d:%2.2d",
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
    s += cnt;

    if (display_ms) {
        cnt = sprintf(s, ".%3.3"PRId64, (us % 1000000) / 1000);
        s += cnt;
    }

    if (gmt) {
        strcpy(s, " GMT");
    }

    return str;
}

// -----------------  CONFIG READ / WRITE  -------------------------------

static char      config_path[210];
static config_t *config;
static int       config_version;

int config_read(char * config_filename, config_t * config_arg, int config_version_arg)
{
    FILE       *fp;
    int         i, version=0, len;
    char       *name;
    char       *value;
    char        s[100] = "";
    const char *config_dir;

    // get the directory to use for the config file
    config_dir = get_internal_storage_path();

    // create the config pathname
    sprintf(config_path, "%s/%s", config_dir, config_filename);

    // save global copies of config_version_arg, and config_arg;
    // these are used in subsequent calls to config_write
    config_version = config_version_arg;
    config = config_arg;

    // open config_file and verify version, 
    // if this fails then write the config file with default values
    if ((fp = fopen(config_path, "re")) == NULL ||
        fgets(s, sizeof(s), fp) == NULL ||
        sscanf(s, "VERSION %d", &version) != 1 ||
        version != config_version)
    {
        if (fp != NULL) {
            fclose(fp);
        }
        INFO("creating default config file %s, version=%d\n", config_path, config_version);
        return config_write();
    }

    // read config entries
    while (memset(s, 0, sizeof(s)), fgets(s, sizeof(s), fp) != NULL) {
        // remove trailing \n
        len = strlen(s);
        if (len > 0 && s[len-1] == '\n') {
            s[len-1] = '\0';
        }

        // use strtok to get name
        name = strtok(s, " ");
        if (name == NULL || name[0] == '#') {
            continue;
        }

        // value can have embedded spaces, but leading spaces are skipped;
        // value starts after skipping spaces and extends to the end of s
        value = name + strlen(name) + 1;
        while (*value == ' ' && *value != '\0') {
            value++;
        }

        // search the config array for a matching name;
        // if found, save the new value associated with the name
        for (i = 0; config[i].name[0]; i++) {
            if (strcmp(name, config[i].name) == 0) {
                strcpy(config[i].value, value);
                break;
            }
        }
    }

    // close
    fclose(fp);
    return 0;
}

int config_write(void)
{
    FILE * fp;
    int    i;

    // open
    fp = fopen(config_path, "we");  // mode: truncate-or-create, close-on-exec
    if (fp == NULL) {
        ERROR("failed to write config file %s, %s\n", config_path, strerror(errno));
        return -1;
    }

    // write version
    fprintf(fp, "VERSION %d\n", config_version);

    // write name/value pairs
    for (i = 0; config[i].name[0]; i++) {
        fprintf(fp, "%-20s %s\n", config[i].name, config[i].value);
    }

    // close
    fclose(fp);
    return 0;
}

// -----------------  NETWORKING  ----------------------------------------

int getsockaddr(char * node, int port, struct sockaddr_in * ret_addr)
{
    struct addrinfo   hints;
    struct addrinfo * result;
    char              port_str[20];
    int               ret;

    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags    = AI_NUMERICSERV;

    ret = getaddrinfo(node, port_str, &hints, &result);
    if (ret != 0) {
        ERROR("failed to get address of %s, %s\n", node, gai_strerror(ret));
        return -1;
    }
    if (result->ai_addrlen != sizeof(*ret_addr)) {
        ERROR("getaddrinfo result addrlen=%d, expected=%d\n",
            (int)result->ai_addrlen, (int)sizeof(*ret_addr));
        return -1;
    }

    *ret_addr = *(struct sockaddr_in*)result->ai_addr;
    freeaddrinfo(result);
    return 0;
}

char * sock_addr_to_str(char * s, int slen, struct sockaddr * addr)
{
    char addr_str[100];
    int port;

    if (addr->sa_family == AF_INET) {
        inet_ntop(AF_INET,
                  &((struct sockaddr_in*)addr)->sin_addr,
                  addr_str, sizeof(addr_str));
        port = ((struct sockaddr_in*)addr)->sin_port;
    } else if (addr->sa_family == AF_INET6) {
        inet_ntop(AF_INET6,
                  &((struct sockaddr_in6*)addr)->sin6_addr,
                 addr_str, sizeof(addr_str));
        port = ((struct sockaddr_in6*)addr)->sin6_port;
    } else {
        snprintf(s,slen,"Invalid AddrFamily %d", addr->sa_family);
        return s;
    }

    snprintf(s,slen,"%s:%d",addr_str,htons(port));
    return s;
}

int do_recv(int sockfd, void * recv_buff, size_t len)
{
    int ret;
    size_t len_remaining = len;

    while (len_remaining) {
        ret = recv(sockfd, recv_buff, len_remaining, MSG_WAITALL);
        if (ret <= 0) {
            if (ret == 0) {
                errno = ENODATA;
            }
            return -1;
        }

        len_remaining -= ret;
        recv_buff += ret;
    }

    return len;
}

int do_send(int sockfd, void * send_buff, size_t len)
{
    int ret;
    size_t len_remaining = len;

    while (len_remaining) {
        ret = send(sockfd, send_buff, len_remaining, MSG_NOSIGNAL);
        if (ret <= 0) {
            if (ret == 0) {
                errno = ENODATA;
            }
            return -1;
        }

        len_remaining -= ret;
        send_buff += ret;
    }

    return len;
}

// -----------------  RANDOM NUMBERS  ------------------------------------

// return uniformly distributed random numbers in range min to max inclusive
double random_range(double min, double max)
{
    return ((double)random() / RAND_MAX) * (max - min) + min;
}

// return triangular distributed random numbers in range min to max inclusive
// Refer to:
// - http://en.wikipedia.org/wiki/Triangular_distribution
// - http://stackoverflow.com/questions/3510475/generate-random-numbers-according-to-distributions
double random_triangular(double min, double max)
{
    double range = max - min;
    double range_squared_div_2 = range * range / 2;
    double U = (double)random() / RAND_MAX;   // 0 - 1 uniform

    if (U <= 0.5) {
        return min + sqrtf(U * range_squared_div_2);
    } else {
        return max - sqrtf((1.f - U) * range_squared_div_2);
    }
}

// returns a vector whose length equals 'magnitude' and with a random direction
void random_vector(double magnitude, double * x, double * y, double * z)
{
    double x_try, y_try, z_try, hypot, f;

    // compute x/y/z_try within a spherical shell 
    while (true) {
        x_try = random() - (RAND_MAX/2.);
        y_try = random() - (RAND_MAX/2.);
        z_try = random() - (RAND_MAX/2.);
        hypot = hypotenuse(x_try,y_try,z_try);
        if (hypot >= (RAND_MAX/10.) && hypot <= (RAND_MAX/2.)) {
            break;
        }
    }

    // scale the random vector to the caller's specified magnitude
    f = magnitude / hypot;
    *x = x_try * f;
    *y = y_try * f;
    *z = z_try * f;

#if 0
    // verification
    double magnitude_check = hypotenuse(*x, *y, *z);
    if (fabsf(magnitude_check-magnitude) > 2) {
        FATAL("magnitude=%f magnitude_check=%f, xyz=%f %f %f\n",
              magnitude, magnitude_check, *x, *y, *z);
    }
#endif
}

// -----------------  MISC MATH  -----------------------------------------

bool solve_quadratic_equation(double a, double b, double c, double *x1, double *x2)
{
    double discriminant, temp;

    discriminant = b*b - 4*a*c;
    if (discriminant < 0) {
        return false;
    }
    temp = sqrt(discriminant);
    *x1 = (-b + temp) / (2*a);
    *x2 = (-b - temp) / (2*a);
    return true;
}

double hypotenuse(double x, double y, double z)
{
    return sqrtf(x*x + y*y + z*z);
}

// -----------------  SMOOTHING  -----------------------------------------

void basic_exponential_smoothing(double x, double *s, double alpha)
{
    double s_last = *s;
    *s = alpha * x + (1 - alpha) * s_last;
}

void double_exponential_smoothing(double x, double *s, double *b, double alpha, double beta, bool init)
{
    if (init) {
        *s = x;
        *b = 0;
    } else {
        double s_last = *s;
        double b_last = *b;
        *s = alpha * x + (1 - alpha) * (s_last + b_last);
        *b = beta * (*s - s_last) + (1 - beta) * b_last;
    }
}

// -----------------  MOVING AVERAGE  ------------------------------------

double moving_average(double val, ma_t *ma)
{
    int64_t idx;

    idx = (ma->count % ma->max_values);
    ma->sum += (val - ma->values[idx]);
    ma->values[idx] = val;
    ma->count++;
    ma->current = ma->sum / (ma->count <= ma->max_values ? ma->count : ma->max_values);
    return ma->current;
}

double moving_average_query(ma_t *ma)
{
    return ma->current;
}

ma_t * moving_average_alloc(int32_t max_values)
{
    ma_t * ma;
    size_t size = sizeof(ma_t) + max_values * sizeof(ma->values[0]);

    ma = malloc(size);
    assert(ma);

    ma->max_values = max_values;

    moving_average_reset(ma);

    return ma;
}

void moving_average_free(ma_t * ma) 
{
    free(ma);
}

void moving_average_reset(ma_t * ma)
{
    ma->sum = 0;
    ma->count = 0;
    ma->current = NAN;
    memset(ma->values,0,ma->max_values*sizeof(ma->values[0]));
}

// - - - - - - - - - - - - - - - - - - - - 

double timed_moving_average(double val, double time_arg, tma_t *tma)
{
    int64_t idx, i;
    double maq;

    idx = time_arg * (tma->max_bins / tma->time_span);

    if (idx == tma->last_idx || tma->first_call) {
        tma->sum += val;
        tma->count++;
        tma->last_idx = idx;
        tma->first_call = false;
    } else if (idx > tma->last_idx) {
        for (i = tma->last_idx; i < idx; i++) {
            double v;
            if (i == idx-1 && tma->count > 0) {
                v = tma->sum / tma->count;
            } else {
                v = 0;
            }
            moving_average(v,tma->ma);
        }
        tma->sum = val;
        tma->count = 1;
        tma->last_idx = idx;
    } else {
        // time can't go backwards
        assert(0);
    }

    maq = moving_average_query(tma->ma);
    tma->current = (isnan(maq) ? tma->sum/tma->count : maq);

    return tma->current;
}

double timed_moving_average_query(tma_t *tma)
{
    return tma->current;
}

tma_t * timed_moving_average_alloc(double time_span, int64_t max_bins)
{
    tma_t * tma;

    tma = malloc(sizeof(tma_t));
    assert(tma);

    tma->time_span = time_span;
    tma->max_bins  = max_bins;
    tma->ma        = moving_average_alloc(max_bins);

    timed_moving_average_reset(tma);

    return tma;
}

void timed_moving_average_free(tma_t * tma)
{
    if (tma == NULL) {
        return;
    }

    free(tma->ma);
    free(tma);
}

void timed_moving_average_reset(tma_t * tma)
{
    tma->first_call = true;
    tma->last_idx   = 0;
    tma->sum        = 0;
    tma->count      = 0;
    tma->current    = NAN;

    moving_average_reset(tma->ma);
}


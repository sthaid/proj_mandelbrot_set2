#ifndef __UTIL_MISC_H__
#define __UTIL_MISC_H__

#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

// -----------------  GENERAL  -----------------------------------

// show the value of a define
#define SHOW_DEFINE(x) INFO("define %s = %s\n", #x, SHOW_DEFINE_STR(x))
#define SHOW_DEFINE_STR(x) #x

// for use in call to madvise
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096L
#endif
#define ROUND_UP(x,n) (((uint64_t)(x) + ((uint64_t)(n) - 1)) & ~((uint64_t)(n) - 1))  // x must be pwr 2

// -----------------  LOGGING  -----------------------------------

#define INFO(fmt, args...) \
    do { \
        logmsg("INFO", __func__, fmt, ## args); \
    } while (0)
#define WARN(fmt, args...) \
    do { \
        logmsg("WARN", __func__, fmt, ## args); \
    } while (0)
#define ERROR(fmt, args...) \
    do { \
        logmsg("ERROR", __func__, fmt, ## args); \
    } while (0)
#ifdef DEBUG_PRINT_ENABLED
#define DEBUG(fmt, args...) \
    do { \
        if (DEBUG_PRINT_ENABLED) { \
            logmsg("DEBUG", __func__, fmt, ## args); \
        } \
    } while (0)
#else
#define DEBUG(fmt, args...)
#endif

#define FATAL(fmt, args...) \
    do { \
        logmsg("FATAL", __func__, fmt, ## args); \
        exit(1); \
    } while (0)

#define INFO_INTERVAL(us, fmt, args...) \
    do { \
        uint64_t now = microsec_timer(); \
        static uint64_t last; \
        if (now - last > (us)) { \
            INFO(fmt, args); \
            last = now; \
        } \
    } while (0)

#define ERROR_INTERVAL(us, fmt, args...) \
    do { \
        uint64_t now = microsec_timer(); \
        static uint64_t last; \
        if (now - last > (us)) { \
            ERROR(fmt, args); \
            last = now; \
        } \
    } while (0)

#define WARN_INTERVAL(us, fmt, args...) \
    do { \
        uint64_t now = microsec_timer(); \
        static uint64_t last; \
        if (now - last > (us)) { \
            WARN(fmt, args); \
            last = now; \
        } \
    } while (0)

void logmsg(char * lvl, const char * func, char * fmt, ...) __attribute__ ((format (printf, 3, 4)));

// -----------------  ASSET FILE SUPPORT  --------------------------------

bool does_asset_file_exist(char *assetname);
void create_asset_file(char *assetname);
void *read_asset_file(char *assetname, size_t *assetsize);
void write_asset_file(char *assetname, void *data, size_t datalen);

// -----------------  TIME  --------------------------------------

#define MAX_TIME_STR 50

uint64_t tsc_timer(void);
uint64_t microsec_timer(void);
uint64_t get_real_time_us(void);
char * time2str(char * str, int64_t us, bool gmt, bool display_ms, bool display_date);

// -----------------  CONFIG READ/WRITE  --------------------------------------------

#define MAX_CONFIG_VALUE_STR 100

typedef struct {
    const char * name;
    char         value[MAX_CONFIG_VALUE_STR];
} config_t;

int config_read(char * config_filename, config_t * config, int config_version);
int config_write(void);

// -----------------  NETWORKING  ----------------------------------------

int getsockaddr(char * node, int port, struct sockaddr_in * ret_addr);
char * sock_addr_to_str(char * s, int slen, struct sockaddr * addr);
int do_recv(int sockfd, void * recv_buff, size_t len);
int do_send(int sockfd, void * send_buff, size_t len);

// -----------------  RANDOM NUMBERS  ------------------------------------

double random_range(double min, double max);
double random_triangular(double min, double max);
void random_vector(double magnitude, double * x, double * y, double * z);

// -----------------  MISC MATH ------------------------------------------

bool solve_quadratic_equation(double a, double b, double c, double *x1, double *x2);
double hypotenuse(double x, double y, double z);

// -----------------  SMOOTHING  -----------------------------------------

void basic_exponential_smoothing(double x, double *s, double alpha);
void double_exponential_smoothing(double x, double *s, double *b, double alpha, double beta, bool init);

// -----------------  MOVING AVERAGE  ------------------------------------

typedef struct {
    int64_t max_values;
    double sum;
    int64_t count;
    double current;
    double values[0];
} ma_t;
double moving_average(double val, ma_t *ma);
double moving_average_query(ma_t *ma);
ma_t * moving_average_alloc(int32_t max_values);
void moving_average_free(ma_t * ma) ;
void moving_average_reset(ma_t * ma);

typedef struct {
    double time_span;
    int64_t max_bins;
    ma_t * ma;
    bool first_call;
    int64_t last_idx;
    double sum;
    int64_t count;
    double current;
} tma_t;
double timed_moving_average(double val, double time_arg, tma_t *tma);
double timed_moving_average_query(tma_t *tma);
tma_t * timed_moving_average_alloc(double time_span, int64_t max_bins);
void timed_moving_average_free(tma_t * tma);
void timed_moving_average_reset(tma_t * tma);

#endif

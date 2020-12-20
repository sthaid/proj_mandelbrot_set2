#include <common.h>

// -----------------  MANDELBROT SET EVALUATOR  -------------------------

int mandelbrot_set(complex c)
{
    complex z = 0;
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

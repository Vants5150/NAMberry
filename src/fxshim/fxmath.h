//
// fxmath.h - double-precision math functions without libm (Circle has no C library).
// They are needed by the ADEL plugins' DSP engines (same sources used on PiPedal): relative error < 1e-13.
//
#ifndef _fxmath_h
#define _fxmath_h

namespace fxm {

double exp (double x);
double expm1 (double x);
double exp2 (double x);
double log (double x);
double log2 (double x);
double log10 (double x);
double log1p (double x);
double pow (double x, double y);
double sin (double x);
double cos (double x);
double tan (double x);
double atan (double x);
double atan2 (double y, double x);
double tanh (double x);
double fmod (double x, double y);

}

#endif

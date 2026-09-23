#include <stdio.h>
#include <math.h>
#define T(e, expect) do { volatile double v = (e); int neg = signbit(v) != 0; if (v != (expect) || neg != (signbit((double)(expect)) != 0)) printf("BAD %-22s = %g (sign %d)\n", #e, v, neg); } while (0)
int main() {
  volatile double m05 = -0.5, m01 = -0.1, m09=-0.9, mz = -0.0; volatile float f = -0.5f;
  T(trunc(m05), -0.0); T(trunc(m01), -0.0); T(trunc(m09), -0.0); T(trunc(mz), -0.0); T(trunc(-1.5), -1.0);
  T(truncf(f), -0.0f); T(ceil(m05), -0.0); T(ceil(m01), -0.0); T(floor(mz), -0.0);
  T(round(m01), -0.0); T(round(m05), -1.0); T(nearbyint(m01), -0.0); T(rint(m01), -0.0); T(rint(m05), -0.0);
  T(fmod(-1.0, 1.0), -0.0); T(fmod(mz, 1.0), -0.0); T(sin(mz), -0.0); T(tan(mz), -0.0); T(atan(mz), -0.0);
  T(asin(mz), -0.0); T(sinh(mz), -0.0); T(tanh(mz), -0.0); T(cbrt(mz), -0.0); T(expm1(mz), -0.0); T(log1p(mz), -0.0);
  T(pow(mz, 3.0), -0.0); T(pow(-8.0, 1.0/3.0) != pow(-8.0, 1.0/3.0) ? 0 : 1, 0); T(hypot(3.0,4.0), 5.0);
  T(atan2(mz, 1.0), -0.0); T(atan2(0.0, -1.0), M_PI); T(exp(1.0) , 2.718281828459045); T(log(10.0), 2.302585092994046);
  T(pow(10.0, -5.0), 1e-05); T(pow(2.0, 0.5), 1.4142135623730951); T(asinh(mz), -0.0); T(atanh(mz), -0.0);

  printf("done\n"); return 0; }

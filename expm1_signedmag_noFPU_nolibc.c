/*
 * expm1 implementation without FPU or libc.
 *
 * Fixed-point format used throughout this file:
 * - 32 bits total.
 * - [31]:      Sign bit (1 = negative, 0 = positive), signed-magnitude encoding.
 * - [30:16]:   15 bits integer magnitude.
 * - [15:0]:    16 bits fractional magnitude.
 * - This is NOT standard Q16.16 (two's complement).
 *
 * Signed-magnitude encoding is used for easier conversion between float
 * and fixed-point, and vice versa, with fewer instructions than two's complement.
 */

#include <stdio.h>  // for printf
#include <math.h>   // for expm1f()

/* Custom int32_t */
#if defined(__INT32_TYPE__)
    typedef __INT32_TYPE__ int32_t;
#elif defined(_MSC_VER)
    typedef __int32 int32_t;
#else
    typedef int int32_t;
    char int32_t_size_check[sizeof(int32_t) == 4 ? 1 : -1]; // ensure 32-bit
#endif

/* Custom uint32_t */
#if defined(__UINT32_TYPE__)
    typedef __UINT32_TYPE__ uint32_t;
#elif defined(_MSC_VER)
    typedef unsigned __int32 uint32_t;
#else
    typedef unsigned int uint32_t;
    char uint32_t_size_check[sizeof(uint32_t) == 4 ? 1 : -1]; // ensure 32-bit
#endif

/* Custom int64_t */
#if defined(__INT64_TYPE__)
    typedef __INT64_TYPE__ int64_t;
#elif defined(_MSC_VER)
    typedef __int64 int64_t;
#else
    typedef long long int64_t;
    char int64_t_size_check[sizeof(int64_t) == 8 ? 1 : -1]; // ensure 64-bit
#endif


typedef uint32_t fix16_t;
/* Signed-magnitude 1.15.16 format constants */
#define FIX16_ONE 0x00010000U   /* +1.0 */
#define FIX16_e_1 0x0002B7E1U   /* e^1 ≈ 2.7182 */
#define FIX16_PINF 0x7FFFFFFFU 
#define FIX16_NINF 0xFFFFFFFFU 

#define FIX16_exp_PMAX 0x000CB310U // ≈ 12.699463
#define FIX16_exp_NMAX 0x800b1708U // ≈ -11.089966

/* clz32(x):
 *   Returns the number of leading zeros in 32-bit x.
 *   Uses __builtin_clz if available; otherwise falls back to clz().
 */
#if defined(__has_builtin)
  #if __has_builtin(__builtin_clz)
    #define clz32(x) ((x) ? __builtin_clz(x) : 32)
  #else
    #define clz32(x) clz((x), 0)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define clz32(x) ((x) ? __builtin_clz(x) : 32)
#else
  #define clz32(x) clz((x), 0)
#endif
static const int mask[] = {0, 8, 12, 14};
static const int clz_magic[] = {2, 1, 0, 0};

/* clz2(x, c):
 *   Fallback leading-zero count for 32-bit unsigned integers.
 *   Used if __builtin_clz is unavailable.
 */
static inline unsigned clz(uint32_t x, int c)
{
   if (!x && !c)
       return 32;

   uint32_t upper = (x >> (16 >> c));
   uint32_t lower = (x & (0xFFFF >> mask[c]));
   if (c == 3)
       return upper ? clz_magic[upper] : 2 + clz_magic[lower];
   return upper ? clz(upper, c + 1) : (16 >> (c)) + clz(lower, c + 1);
}

/* fix16_to_float(a):
 *   Convert a signed-magnitude 1.15.16 fixed-point value to float.
 *   Handles explicit sign bit; not compatible with two's complement Q16.16.
 */
static inline float fix16_to_float(fix16_t a)
{
    int32_t result_f = a & 0x80000000; // get sign bit
    a &= 0x7fffffff;
    int32_t exp = 15 - clz32(a);
    if (exp >= 0) {
        a >>= exp;
    } else {
        a <<= -1*exp;
    }
    int32_t mantissa = a & 0x0000ffff;

    result_f |= (exp + 127) << 23;
    result_f |= mantissa << 7;

    union { uint32_t u; float f; } conv = { .u = (uint32_t)result_f };
    return conv.f;
}

/* float_to_fix16(a):
 *   Convert IEEE-754 float to signed-magnitude 1.15.16 fixed-point.
 *   Returns signed infinity sentinels on overflow, or 0 on underflow or NaN.
 */
static inline fix16_t float_to_fix16(float a)
{
    if(a != a)  // check if NaN
        return 0;
    if (a == INFINITY)
        return FIX16_PINF;
    if (a == -INFINITY)
        return FIX16_NINF;
    union { float f; int32_t i; } u = { .f = a };
    int32_t sign = u.i & 0x80000000;
    int32_t exp = ((u.i & 0x7f800000) >> 23) - 127;
    int32_t mantissa = u.i & 0x007fffff | 0x00800000;

    if (exp > 15)
        return FIX16_PINF;
    if (exp < -15)
        return 0;

    mantissa >>= 7;
    if (exp >= 0)
        mantissa <<= exp;
    else
        mantissa >>= -1*exp;

    return (mantissa | sign);
}

/* int_to_fix16(a):
 *   Convert integer to signed-magnitude 1.15.16 fixed-point.
 */
static inline fix16_t int_to_fix16(int a)
{
   return (fix16_t)((int64_t)a * FIX16_ONE);
}

/* fix16_mul(x, y):
 *   Multiply two signed-magnitude 1.15.16 fixed-point values.
 *   No two's complement assumptions.
 */
static inline fix16_t fix16_mul(fix16_t x, fix16_t y)
{
   int64_t res = (int64_t) x * y;
   return (fix16_t) (res >> 16);
}

/* fix16_div(a, b):
 *   Divide signed-magnitude 1.15.16 'a' by 'b', return result as signed-magnitude.
 *   Division by zero returns 0.
 */
static inline fix16_t fix16_div(fix16_t a, fix16_t b)
{
   if (b == 0) // division by zero -> defined to return 0 (could be changed to error code)
       return 0;
   fix16_t result = (((int64_t) a) << 16) / ((int64_t) b);
   return result;
}

#define EXP_SERIES_MAX_TERMS 30
#define EXP_TERM_SMALL_THRESHOLD 500
#define EXP_TERM_TINY_THRESHOLD 20
#define EXP_TERM_MIN_ITER 15


/* fix16_expm1(in):
 *   Approximate e^x − 1 for signed-magnitude 1.15.16 input using a Taylor series.
 *   Special cases:
 *     - Zero: returns 0
 *     - +1.0: returns e−2 as signed-magnitude
 *     - Large positive: returns FIX16_PINF
 *     - Large negative: returns signed-magnitude -1 (0x80010000)
 *   Input/output is always signed-magnitude, -0 never happens hence it's exclusion.
 */
fix16_t fix16_expm1(fix16_t in)
{
    if (in == 0)
        return 0;
    if (in == FIX16_ONE)
        return FIX16_e_1 - FIX16_ONE;
    if (in >= FIX16_exp_NMAX)
        return (FIX16_ONE | 0x80000000);  // return -1
    if (in >= FIX16_exp_PMAX && in < 0x80000000)
        return FIX16_PINF;

    int neg = in & 0x80000000;
    if (neg)
        in ^= neg;  // set MSB to 0
    fix16_t result = in;
    fix16_t term = in;
    for (int i = 2; i < EXP_SERIES_MAX_TERMS; i++) {
        term = fix16_mul(term, fix16_div(in, int_to_fix16(i)));
        result += term;
        /* Break early if the term is sufficiently small */
        if ((term < EXP_TERM_SMALL_THRESHOLD) &&
              ((i > EXP_TERM_MIN_ITER) || (EXP_TERM_TINY_THRESHOLD < 20)))
            break;
    }
    if (neg) {
        result = FIX16_ONE - fix16_div(FIX16_ONE, result + FIX16_ONE);
        result |= neg;
    }
    return result;
}


/* my_expm1f(x):
 *   Convenience wrapper: convert float to signed-magnitude fixed-point,
 *   compute expm1, and convert back to float.
 */
float my_expm1f(float x) {
    return fix16_to_float(fix16_expm1(float_to_fix16(x)));
}

/* main():
 *   Simple test: compare my_expm1f() (signed-magnitude fixed-point math)
 *   against standard expm1f() for various input values.
 */
int main(void) {
    float test_vals[] = {
        -16, -10, -5, -2, -1, -0.5f, -0.1f, -0.01f, -0.0001f,
         0.0f, 0.0001f, 0.01f, 0.1f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f
    };
    int n = sizeof(test_vals) / sizeof(test_vals[0]);
    printf("%12s %15s %15s %15s\n", "x", "fix16_expm1", "expm1f", "pct_error(%)");

    for (int i = 0; i < n; ++i) {
        float x = test_vals[i];
        float no_FPU_result = my_expm1f(x);
        float libc_result = expm1f(x);

        float pct_err;
        if (libc_result == 0.0f) {
            printf("%12.6f %15.7f %15.7f %15s\n", x, no_FPU_result, libc_result, "N/A");
        } else {
            pct_err = fabsf(no_FPU_result - libc_result) / fabsf(libc_result) * 100.0f;
            printf("%12.6f %15.7f %15.7f %15.4f\n", x, no_FPU_result, libc_result, pct_err);
        }
    }
    return 0;
}
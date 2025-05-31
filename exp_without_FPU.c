#include <stdint.h>
#include <stdio.h>
#include <math.h>

typedef uint32_t fix16_t;
#define FIX16_ONE 0x00010000  /* 1.0 in fixed-point representation */
#define FIX16_e_1 0x0002B7E1
#define FIX16_PINF 0x7FFFFFFF
#define FIX16_NINF 0xFFFFFFFF

#define FIX16_exp_PMAX 0x000CB310 // ≈ 12.699463
#define FIX16_exp_NMAX 0x800b1708 // ≈ -11.089966


#define clz(x) clz2(x, 0)


static const int mask[] = {0, 8, 12, 14};
static const int magic[] = {2, 1, 0, 0};

unsigned clz2(uint32_t x, int c)
{
   if (!x && !c)
       return 32;

   uint32_t upper = (x >> (16 >> c));
   uint32_t lower = (x & (0xFFFF >> mask[c]));
   if (c == 3)
       return upper ? magic[upper] : 2 + magic[lower];
   return upper ? clz2(upper, c + 1) : (16 >> (c)) + clz2(lower, c + 1);
}


static inline float fix16_to_float(fix16_t a)
{
    // printf("\n[fix to f] old hex: 0x%x\n", a);
    int32_t result_f = a & 0x80000000; // get sign bit
    a &= 0x7fffffff;
    int32_t exp = 15 - clz(a);
    if (exp >= 0) {
        a >>= exp;
    } else {
        a <<= -1*exp;
    }
    int32_t mantissa = a & 0x0000ffff;

    // printf("[fix to f] exp: 0x%x, mantissa: 0x%x\n", exp, mantissa);

    result_f |= (exp + 127) << 23;
    result_f |= mantissa << 7;

    return *(float*)&result_f;
}

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
    // printf("[f to fix] sign: 0x%x, exp: %d, mantissa: 0x%x\n", sign, exp, mantissa);

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

static inline fix16_t int_to_fix16(int a)
{
   return a * FIX16_ONE;
}

static inline fix16_t fix16_mul(fix16_t x, fix16_t y)
{
   int64_t res = (int64_t) x * y;
   return (fix16_t) (res >> 16);
}

static inline fix16_t fix16_div(fix16_t a, fix16_t b)
{
   if (b == 0) /* Avoid division by zero */
       return 0;
   fix16_t result = (((int64_t) a) << 16) / ((int64_t) b);
   return result;
}

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
    for (uint_fast8_t i = 2; i < 30; i++) {
        term = fix16_mul(term, fix16_div(in, int_to_fix16(i)));
        result += term;
        /* Break early if the term is sufficiently small */
        if ((term < 500) && ((i > 15) || (term < 20)))
            break;
    }
    if (neg) {
        result = FIX16_ONE - fix16_div(FIX16_ONE, result + FIX16_ONE);
        result |= neg;
    }
    return result;
}

float my_expm1f(float x) {
    return fix16_to_float(fix16_expm1(float_to_fix16(x)));
}

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

// int main() {
//     float x = -11.09;
//     float y = fix16_to_float(float_to_fix16(x));
//     printf("Float: %f (0x%x)\n", x, *(uint32_t*)&x);
//     printf("After \"round-a-bout\": %f (0x%x)\n", y, *(uint32_t*)&y);
// }

// int main() {
//     printf("around %f in float.\n", fix16_to_float(FIX16_exp_MIN));
// }
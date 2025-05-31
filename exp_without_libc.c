#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define has_mantissa(x) (x & 0x007fffff)
#define has_exponent(x) (x & 0x7f800000)

static inline int round_to_nearest_even(float x) {
    int i = (int)x;
    float frac = x - (float)i;
    
    if (frac > 0.5f || (frac == 0.5f && (i & 1)))
        return i + 1;
    else if (frac < -0.5f || (frac == -0.5f && (i & 1)))
        return i - 1;
    else
        return i;
}


float my_ldexpf(float x, int exp) {
    if (x == 0.0f) 
        return x;

    union { float f; int i; } u = { .f = x };
    
    int old_exp = (u.i >> 23) & 0xff; // extract old expo
    if(old_exp == 0) {
        // could be de-normallized num
        while(has_mantissa(u.i) && !has_exponent(u.i)) {
            u.i <<= 1;
            exp--;
        }
        old_exp = (u.i >> 23) & 0xFF;
    }
    
    exp = exp + old_exp;
    if (exp <= 0) {
        return 0.0f;
    } else if (exp >= 255) {
        u.i = (u.i & 0x80000000) | 0x7f800000;
        return u.f;
    }

    u.i &= ~0x7f800000;
    u.i |= (exp << 23) & 0x7f800000;
    return u.f;
}


// set +-inf making use of IEEE 754 definitions
float P_INF = 1.0f / 0.0f;
float N_INF = -1.0f / 0.0f;
const float ln2_f = 0.693147180559945309417232121458;
const float neg_ln2_f = -0.693147180559945309417232121458;
const float inv_ln2 = 1.4426950408889634073599;
const float neg_ln2_hi = (float) neg_ln2_f;
const float neg_ln2_lo = (float) (neg_ln2_f - (double) neg_ln2_hi);

struct p_const {
    float P0, P1, P2, P3;
};

struct p_const C = {
    0.9999280752600668,   // P0
    1.0001641903948264,   // P1
    0.5049632650961922,   // P2
    0.1656683995499798,   // P3
};


float my_exp(float x) {
    if (x != x) // check if NaN
        return x;
    if (x == P_INF)
        return P_INF;
    if (x == N_INF)
        return 0.0f;
    if (x >= 88.8)
        return P_INF;
    if (x <= -104)
        return 0.0f;

    int k = round_to_nearest_even(x * inv_ln2); // x/ln2, rounding ties to even
    float r = x + k * neg_ln2_hi;
    r = r + k * neg_ln2_lo;
    
    float y = C.P0 + r*(C.P1 + r*(C.P2 + r*C.P3));

    return my_ldexpf(y, k);
}


float my_expm1(float x) {
    if (x != x) // check if NaN
        return x;
    if (x == P_INF)
        return P_INF;
    if (x == N_INF)
        return -1.0f;
    if (x >= 88.8)
        return P_INF;
    if (x <=-104)
        return -1.0f;

    if (x > ln2_f || x < neg_ln2_f)
        return my_exp(x) - 1;
    
    float y = x * (1.0f + x*(0.5f + x*(1.0f/6.0f + x*(1.0f/24.0f + x*(1.0f/120.0f)))));

    return y;
}

int main() {
    float test_vals[] = {
        0.0f,
        1e-6f,
        -1e-6f,
        0.35f,
        -0.35f,
        0.5f,
        ln2_f,
        neg_ln2_f,
        -0.5f,
        0.75f,
       -0.75f,
        1.0f,
       -1.0f,
        5.0f,
       -5.0f,
        10.0f,
        -10.0f,
        20.0f,
        -20.0f
    };
    int n = sizeof(test_vals) / sizeof(test_vals[0]);
    printf("%12s %20s %20s %20s %15s\n", "x", "my_expm1(x)", "expm1f(x)", "my_exp(x)-1","RelError (%)");
    printf("-------------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        float x = test_vals[i];
        float myy = my_expm1(x);
        float ref = expm1f(x);
        float eh = my_exp(x) - 1;

        float relerr = (ref != 0.0f) ? fabsf(myy - ref) / fabsf(ref) * 100.0f : fabsf(myy - ref) * 100.0f;
        printf("%12.6f %20.8f %20.8f %20.8f %15.6f\n", x, myy, ref, eh, relerr);
    }
    return 0;
}
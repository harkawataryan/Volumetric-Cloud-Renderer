// Shared helpers, prepended to every shader after its #version line.

const float PI = 3.14159265359;

float remap(float v, float lo, float hi, float nlo, float nhi) {
    return nlo + (clamp(v, lo, hi) - lo) / max(hi - lo, 1e-5) * (nhi - nlo);
}

// Henyey-Greenstein phase function.
float phaseHG(float cosTheta, float g) {
    float gg = g * g;
    return (1.0 - gg) / (4.0 * PI * pow(1.0 + gg - 2.0 * g * cosTheta, 1.5));
}

// ---- hashing -------------------------------------------------------------

uvec3 pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

vec3 hash3(ivec3 cell, int period) {
    ivec3 c = ((cell % period) + period) % period;
    uvec3 h = pcg3d(uvec3(c) + uvec3(17u, 59u, 131u) * uint(period));
    return vec3(h) * (1.0 / 4294967295.0);
}

// ---- tileable 3D Perlin and Worley -------------------------------------

float perlin3(vec3 p, int period) {
    vec3 i = floor(p);
    vec3 f = p - i;
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float r = 0.0;
    float n[8];
    for (int k = 0; k < 8; ++k) {
        ivec3 o = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        vec3 g = normalize(hash3(ivec3(i) + o, period) * 2.0 - 1.0);
        n[k] = dot(g, f - vec3(o));
    }
    float x0 = mix(n[0], n[1], u.x), x1 = mix(n[2], n[3], u.x);
    float x2 = mix(n[4], n[5], u.x), x3 = mix(n[6], n[7], u.x);
    float y0 = mix(x0, x1, u.y), y1 = mix(x2, x3, u.y);
    r = mix(y0, y1, u.z);
    return r * 0.5 + 0.5;
}

float perlinFbm3(vec3 p, int period, int octaves) {
    float a = 0.0, amp = 0.5, norm = 0.0;
    for (int o = 0; o < octaves; ++o) {
        a += perlin3(p, period) * amp;
        norm += amp;
        p *= 2.0; period *= 2; amp *= 0.5;
    }
    return a / norm;
}

// Inverted Worley: 1 at cell centres, 0 at cell edges.
float worley3(vec3 p, int period) {
    vec3 i = floor(p);
    vec3 f = p - i;
    float d = 1.0;
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        vec3 o = vec3(x, y, z);
        vec3 c = o + hash3(ivec3(i) + ivec3(o), period);
        vec3 r = c - f;
        d = min(d, dot(r, r));
    }
    return 1.0 - sqrt(d);
}

float worleyFbm3(vec3 p, int period) {
    return worley3(p, period) * 0.625 + worley3(p * 2.0, period * 2) * 0.25 + worley3(p * 4.0, period * 4) * 0.125;
}

// ---- 2D ------------------------------------------------------------------

float perlin2(vec2 p, int period) {
    return perlin3(vec3(p, 0.37), period);
}

// Interleaved gradient noise, cheap per-pixel dither that decorrelates over frames.
float ign(vec2 px, uint frame) {
    px += float(frame % 64u) * 5.588238;
    return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
}

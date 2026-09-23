// Quick cost probe for a Giannoulis-style FF compressor kernel (scalar libm, scalar fast-math approx, NEON 4-lane).
#include <arm_neon.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <cstdint>

static inline float fastLog2(float x) { // x > 0
    uint32_t b; std::memcpy(&b, &x, 4);
    float e = (float)((int)((b >> 23) & 255) - 127);
    b = (b & 0x7fffff) | 0x3f800000; float m; std::memcpy(&m, &b, 4); // [1,2)
    // 5th-order minimax-ish poly for log2(m), m in [1,2)
    float t = m - 1.0f;
    float p = t * (1.4425449f + t * (-0.7181452f + t * (0.4575485f + t * (-0.2779042f + t * (0.1217970f - t * 0.0258411f)))));
    return e + p;
}
static inline float fastExp2(float x) {
    x = std::fmax(x, -126.0f);
    float fi = std::floor(x); float f = x - fi;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013333f))));
    int32_t i = (int32_t)fi; uint32_t b; std::memcpy(&b, &p, 4); b += (uint32_t)(i << 23); float r; std::memcpy(&r, &b, 4); return r;
}
static inline float32x4_t vlog2(float32x4_t x) {
    uint32x4_t b = vreinterpretq_u32_f32(x);
    float32x4_t e = vcvtq_f32_s32(vsubq_s32(vreinterpretq_s32_u32(vshrq_n_u32(b, 23)), vdupq_n_s32(127)));
    float32x4_t m = vreinterpretq_f32_u32(vorrq_u32(vandq_u32(b, vdupq_n_u32(0x7fffff)), vdupq_n_u32(0x3f800000)));
    float32x4_t t = vsubq_f32(m, vdupq_n_f32(1.0f));
    float32x4_t p = vdupq_n_f32(-0.0258411f);
    p = vfmaq_f32(vdupq_n_f32(0.1217970f), p, t);
    p = vfmaq_f32(vdupq_n_f32(-0.2779042f), p, t);
    p = vfmaq_f32(vdupq_n_f32(0.4575485f), p, t);
    p = vfmaq_f32(vdupq_n_f32(-0.7181452f), p, t);
    p = vfmaq_f32(vdupq_n_f32(1.4425449f), p, t);
    return vfmaq_f32(e, p, t);
}
static inline float32x4_t vexp2(float32x4_t x) {
    x = vmaxq_f32(x, vdupq_n_f32(-126.0f));
    float32x4_t fi = vrndmq_f32(x); float32x4_t f = vsubq_f32(x, fi);
    float32x4_t p = vdupq_n_f32(0.0013333f);
    p = vfmaq_f32(vdupq_n_f32(0.0096181f), p, f);
    p = vfmaq_f32(vdupq_n_f32(0.0555041f), p, f);
    p = vfmaq_f32(vdupq_n_f32(0.2402265f), p, f);
    p = vfmaq_f32(vdupq_n_f32(0.6931472f), p, f);
    p = vfmaq_f32(vdupq_n_f32(1.0f), p, f);
    int32x4_t i = vshlq_n_s32(vcvtq_s32_f32(fi), 23);
    return vreinterpretq_f32_s32(vaddq_s32(vreinterpretq_s32_f32(p), i));
}

struct Params { float T=-20, W=6, S=0.75f /*1-1/R*/, aA, aR, makeup=1.0f; };

// gain computer in log2 domain units (we keep everything in dB for clarity; 1 log2 unit = 6.0206 dB)
static inline float gcDb(float x, const Params& p) {
    float o = x - p.T;
    if (2.0f * o <= -p.W) return 0.0f;
    if (2.0f * o >= p.W) return -p.S * o;
    float q = o + 0.5f * p.W; return -p.S * q * q / (2.0f * p.W);
}

int main() {
    const double fs = 48000.0; const int N = 48000 * 20; const int bs = 128;
    std::vector<float> L(N), R(N), oL(N), oR(N);
    std::mt19937 rng(1); std::normal_distribution<float> d(0.f, 0.2f);
    for (int n = 0; n < N; ++n) { float env = 0.5f + 0.5f * std::sin(n * 2e-4f); L[n] = d(rng) * env; R[n] = d(rng) * env; }
    Params p; p.aA = std::exp(-1.0f / (0.005f * (float)fs)); p.aR = std::exp(-1.0f / (0.100f * (float)fs));
    auto run = [&](const char* name, auto&& body) {
        for (int w = 0; w < 2; ++w) body();
        auto t0 = std::chrono::high_resolution_clock::now(); int reps = 5; for (int r = 0; r < reps; ++r) body();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / (reps * (double)N * 2.0);
        double chk = 0; for (int n = 0; n < N; n += 997) chk += oL[n] + oR[n];
        std::printf("%-44s %6.2f ns/sample/ch  (chk %.4f)\n", name, ns, chk);
    };
    // 1. scalar libm, per channel independent
    run("scalar libm log10/pow, 2ch", [&] {
        float yL = 0, yR = 0; float* ys[2] = { &yL, &yR }; const float* in[2] = { L.data(), R.data() }; float* out[2] = { oL.data(), oR.data() };
        for (int c = 0; c < 2; ++c) { float y = 0; for (int n = 0; n < N; ++n) {
            float x = in[c][n]; float xd = 20.0f * std::log10(std::fabs(x) + 1e-12f);
            float xl = -gcDb(xd, p);  // positive GR amount
            y = xl > y ? p.aA * y + (1 - p.aA) * xl : p.aR * y + (1 - p.aR) * xl;
            out[c][n] = x * std::pow(10.0f, -y / 20.0f) * p.makeup; } *ys[c] = y; }
    });
    run("scalar fast log2/exp2 approx, 2ch", [&] {
        const float k = 6.0205999f, ik = 1.0f / 6.0205999f; const float* in[2] = { L.data(), R.data() }; float* out[2] = { oL.data(), oR.data() };
        for (int c = 0; c < 2; ++c) { float y = 0; for (int n = 0; n < N; ++n) {
            float x = in[c][n]; float xd = k * fastLog2(std::fabs(x) + 1e-12f);
            float xl = -gcDb(xd, p);
            y = xl > y ? p.aA * y + (1 - p.aA) * xl : p.aR * y + (1 - p.aR) * xl;
            out[c][n] = x * fastExp2(-y * ik) * p.makeup; } }
    });
    run("NEON 4-lane (L,R,-,-) branchless, per sample", [&] {
        const float k = 6.0205999f;
        float32x4_t y = vdupq_n_f32(0), T = vdupq_n_f32(p.T), W = vdupq_n_f32(p.W), hW = vdupq_n_f32(0.5f * p.W), S = vdupq_n_f32(p.S), i2W = vdupq_n_f32(1.0f / (2 * p.W));
        float32x4_t aA = vdupq_n_f32(p.aA), aR = vdupq_n_f32(p.aR), eps = vdupq_n_f32(1e-12f);
        for (int n = 0; n < N; ++n) {
            float tmp[4] = { L[n], R[n], 0, 0 }; float32x4_t x = vld1q_f32(tmp);
            float32x4_t xd = vmulq_f32(vdupq_n_f32(k), vlog2(vaddq_f32(vabsq_f32(x), eps)));
            float32x4_t o = vsubq_f32(xd, T);
            float32x4_t q = vmaxq_f32(vdupq_n_f32(0), vminq_f32(vaddq_f32(o, hW), W)); // clamp to [0,W]
            // knee: S q^2/(2W) for q<W; above: S*o. Unified: GR = S*(q^2/(2W) + max(0, o - W/2))
            float32x4_t gr = vmulq_f32(S, vfmaq_f32(vmaxq_f32(vdupq_n_f32(0), vsubq_f32(o, hW)), vmulq_f32(q, q), i2W));
            uint32x4_t att = vcgtq_f32(gr, y);
            float32x4_t a = vbslq_f32(att, aA, aR);
            y = vfmaq_f32(gr, a, vsubq_f32(y, gr));  // a*y + (1-a)*gr
            float32x4_t g = vexp2(vmulq_f32(y, vdupq_n_f32(-1.0f / k)));
            float32x4_t out = vmulq_f32(x, g); float o4[4]; vst1q_f32(o4, out); oL[n] = o4[0]; oR[n] = o4[1];
        }
    });
    run("NEON 4-lane 2 stereo instances? (4 ch/vec) ", [&] {
        const float k = 6.0205999f;
        float32x4_t y = vdupq_n_f32(0), T = vdupq_n_f32(p.T), W = vdupq_n_f32(p.W), hW = vdupq_n_f32(0.5f * p.W), S = vdupq_n_f32(p.S), i2W = vdupq_n_f32(1.0f / (2 * p.W));
        float32x4_t aA = vdupq_n_f32(p.aA), aR = vdupq_n_f32(p.aR), eps = vdupq_n_f32(1e-12f);
        for (int n = 0; n < N; n += 2) {
            float tmp[4] = { L[n], R[n], L[n+1], R[n+1] }; float32x4_t x = vld1q_f32(tmp); // NOT a real recursion-correct layout; cost proxy for 4 lanes busy
            float32x4_t xd = vmulq_f32(vdupq_n_f32(k), vlog2(vaddq_f32(vabsq_f32(x), eps)));
            float32x4_t o = vsubq_f32(xd, T);
            float32x4_t q = vmaxq_f32(vdupq_n_f32(0), vminq_f32(vaddq_f32(o, hW), W));
            float32x4_t gr = vmulq_f32(S, vfmaq_f32(vmaxq_f32(vdupq_n_f32(0), vsubq_f32(o, hW)), vmulq_f32(q, q), i2W));
            uint32x4_t att = vcgtq_f32(gr, y);
            float32x4_t a = vbslq_f32(att, aA, aR);
            y = vfmaq_f32(gr, a, vsubq_f32(y, gr));
            float32x4_t g = vexp2(vmulq_f32(y, vdupq_n_f32(-1.0f / k)));
            float32x4_t out = vmulq_f32(x, g); float o4[4]; vst1q_f32(o4, out); oL[n] = o4[0]; oR[n] = o4[1]; oL[n+1] = o4[2]; oR[n+1] = o4[3];
        }
    });
    // accuracy of approximations
    double maxLogErr = 0, maxExpErr = 0;
    for (float v = 1e-6f; v < 4.0f; v *= 1.001f) maxLogErr = std::fmax(maxLogErr, std::fabs(fastLog2(v) - std::log2(v)));
    for (float v = -40.0f; v < 4.0f; v += 0.001f) maxExpErr = std::fmax(maxExpErr, std::fabs(fastExp2(v) / std::exp2(v) - 1.0));
    std::printf("fastLog2 max abs err %.3g log2 units = %.3g dB; fastExp2 max rel err %.3g (= %.3g dB)\n", maxLogErr, maxLogErr * 6.0206, maxExpErr, 20*std::log10(1+maxExpErr));
}

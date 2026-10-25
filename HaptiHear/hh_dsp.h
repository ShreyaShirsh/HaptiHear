// =====================================================================
//  hh_dsp.h  -  HaptiHear signal processing + on-device ML (kNN)
//  Pure C++ (no Arduino calls) so the same code also runs on a PC.
//
//  Pipeline (all on the ESP32):
//   16 kHz audio -> 512-sample frames (32 ms) -> Hann window -> FFT
//   -> 16 mel-spaced band energies (dB) per frame
//   -> event detector (adaptive noise floor) finds a sound
//   -> ~1 s window around the sound -> 20-number feature vector
//   -> k-Nearest-Neighbour classifier trained ON THE DEVICE
// =====================================================================
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace hh {

// ------------------------- fixed parameters --------------------------
constexpr int   SR      = 16000;           // sample rate (Hz)
constexpr int   N       = 512;             // FFT / frame size (32 ms)
constexpr int   NBANDS  = 16;              // mel bands
constexpr int   WIN     = 32;              // frames per event window (~1 s)
constexpr int   PRE     = 4;               // frames kept from before the onset
constexpr int   DIM     = NBANDS + 4;      // feature vector length
#ifndef HH_NCLS
#define HH_NCLS 4                          // default; HaptiHear.ino sets its own before including
#endif
constexpr int   NCLS    = HH_NCLS;         // number of trainable classes
constexpr int   MAXS    = 12;              // max training samples per class
constexpr float F_LO    = 100.0f, F_HI = 7900.0f;

// Event detector tuning (dB). INMP441: 0 dBFS ~ 120 dB SPL.
constexpr float ON_DB      = 12.0f;  // sound must be this much above background
constexpr float ACT_DB     = 6.0f;   // frame counts as "active" above floor+6
constexpr float ABS_MIN_DB = -75.0f; // ignore anything quieter (~45 dB SPL)
constexpr float FLOOR_MIN  = -100.0f;

// -------------------------------- FFT --------------------------------
struct FFT {
  float c[N / 2], s[N / 2], win[N];
  uint16_t rev[N];
  void init() {
    const float PI2 = 6.28318530718f;
    for (int k = 0; k < N / 2; k++) { c[k] = cosf(PI2 * k / N); s[k] = sinf(PI2 * k / N); }
    for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * cosf(PI2 * i / (N - 1));
    int bits = 0; while ((1 << bits) < N) bits++;
    for (int i = 0; i < N; i++) {
      int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
      rev[i] = (uint16_t)r;
    }
  }
  // in-place radix-2 FFT (forward)
  void run(float* re, float* im) const {
    for (int i = 0; i < N; i++) { int j = rev[i]; if (j > i) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (int size = 2; size <= N; size <<= 1) {
      int half = size >> 1, step = N / size;
      for (int i = 0; i < N; i += size)
        for (int j = 0; j < half; j++) {
          int k = j * step, a = i + j, b = a + half;
          float tr = re[b] * c[k] + im[b] * s[k];
          float ti = im[b] * c[k] - re[b] * s[k];
          re[b] = re[a] - tr; im[b] = im[a] - ti;
          re[a] += tr;        im[a] += ti;
        }
    }
  }
};

// Per-frame result
struct Frame { float band[NBANDS]; float db; float tonal; };

// ------------------------- frame feature extractor -------------------
struct FrameDSP {
  FFT fft;
  int edge[NBANDS + 1];
  float re[N], im[N];

  void init() {
    fft.init();
    auto mel  = [](float f) { return 2595.0f * log10f(1.0f + f / 700.0f); };
    auto imel = [](float m) { return 700.0f * (powf(10.0f, m / 2595.0f) - 1.0f); };
    float m0 = mel(F_LO), m1 = mel(F_HI);
    for (int b = 0; b <= NBANDS; b++) {
      float f = imel(m0 + (m1 - m0) * b / NBANDS);
      int bin = (int)lroundf(f * N / SR);
      if (b > 0 && bin <= edge[b - 1]) bin = edge[b - 1] + 1;   // >=1 bin per band
      if (bin > N / 2) bin = N / 2;
      edge[b] = bin;
    }
  }

  // x: N samples scaled to [-1, 1)
  void process(const float* x, Frame& out) {
    float mean = 0; for (int i = 0; i < N; i++) mean += x[i]; mean /= N;
    float ms = 0;
    for (int i = 0; i < N; i++) { float v = x[i] - mean; ms += v * v; re[i] = v * fft.win[i]; im[i] = 0; }
    out.db = 10.0f * log10f(ms / N + 1e-12f);
    fft.run(re, im);
    for (int k = 0; k <= N / 2; k++) re[k] = re[k] * re[k] + im[k] * im[k];  // power, reuse re[]
    for (int b = 0; b < NBANDS; b++) {
      float p = 0; for (int k = edge[b]; k < edge[b + 1]; k++) p += re[k];
      out.band[b] = 10.0f * log10f(p + 1e-12f);
    }
    // tonality: peak-to-average power in 300 Hz..4 kHz (beeps/bells are tonal)
    int k0 = 300 * N / SR, k1 = 4000 * N / SR;
    float mx = 0, sum = 0;
    for (int k = k0; k <= k1; k++) { sum += re[k]; if (re[k] > mx) mx = re[k]; }
    out.tonal = 10.0f * log10f((mx + 1e-12f) / (sum / (k1 - k0 + 1) + 1e-12f));
  }
};

// ---------------------- event detector + window ----------------------
struct EventDetector {
  Frame ring[WIN];
  int   head = 0, filled = 0;
  float floorDb = -80.0f;
  bool  floorInit = false;
  int   state = 0;        // 0 idle, 1 capturing, 2 cooldown
  int   count = 0, quiet = 0;
  float levelDb = -100;   // last frame level (for UI)
  float onDb = ON_DB;           // sensitivity: jump above background that starts an event
  float absMinDb = ABS_MIN_DB;  // sensitivity: quietest level that can start an event

  void reset() { head = filled = 0; state = 0; count = quiet = 0; floorInit = false; floorDb = -80; }

  // returns true when a complete event window is ready
  bool push(const Frame& f) {
    ring[head] = f; head = (head + 1) % WIN; if (filled < WIN) filled++;
    levelDb = f.db;
    if (!floorInit) { floorDb = f.db; floorInit = true; }
    bool loud = f.db > floorDb + onDb && f.db > absMinDb;

    if (state != 1) {  // adapt background level (not while capturing)
      if (f.db < floorDb) floorDb = 0.9f * floorDb + 0.1f * f.db;     // fall fast
      else if (!loud)     floorDb += 0.005f * (f.db - floorDb);       // rise slowly
      else                floorDb += 0.001f * (f.db - floorDb);       // very slowly during sustained noise
      if (floorDb < FLOOR_MIN) floorDb = FLOOR_MIN;
    }

    if (state == 0) {
      if (loud && filled >= PRE + 1) { state = 1; count = 0; }
    } else if (state == 1) {
      if (++count >= WIN - PRE - 1) { state = 2; count = 0; quiet = 0; return true; }
    } else {  // cooldown: wait until quiet for 8 frames (~0.25 s), or 2 s max
      quiet = (f.db < floorDb + ACT_DB) ? quiet + 1 : 0;
      if (quiet >= 8 || ++count > 62) state = 0;
    }
    return false;
  }

  // Build the 20-number feature vector from the last WIN frames
  void features(float* v) const {
    const Frame* fr[WIN];
    for (int i = 0; i < WIN; i++) fr[i] = &ring[(head + i) % WIN];  // oldest -> newest
    float thr = floorDb + ACT_DB;
    int nAct = 0, bursts = 0, loudest = 0; bool prev = false;
    float sumDb = 0, tonal = 0, maxDb = -200;
    float shape[NBANDS] = {0};
    for (int i = 0; i < WIN; i++) {
      const Frame& f = *fr[i];
      if (f.db > maxDb) { maxDb = f.db; loudest = i; }
      bool act = f.db > thr;
      if (act && !prev) bursts++;
      prev = act;
      if (!act) continue;
      nAct++; sumDb += f.db; tonal += f.tonal;
      float m = 0; for (int b = 0; b < NBANDS; b++) m += f.band[b]; m /= NBANDS;
      for (int b = 0; b < NBANDS; b++) shape[b] += f.band[b] - m;
    }
    if (nAct == 0) {  // fall back to the single loudest frame
      const Frame& f = *fr[loudest];
      nAct = 1; sumDb = f.db; tonal = f.tonal; bursts = 1;
      float m = 0; for (int b = 0; b < NBANDS; b++) m += f.band[b]; m /= NBANDS;
      for (int b = 0; b < NBANDS; b++) shape[b] = f.band[b] - m;
    }
    for (int b = 0; b < NBANDS; b++) v[b] = shape[b] / nAct;  // spectral shape (loudness-independent)
    v[NBANDS + 0] = (float)nAct / WIN;                         // how long the sound lasts
    v[NBANDS + 1] = maxDb - sumDb / nAct;                      // how "punchy" (impulsive) it is
    v[NBANDS + 2] = (float)bursts;                             // number of separate bursts (beeps/knocks)
    v[NBANDS + 3] = tonal / nAct;                              // how tone-like it is
  }
};

// ------------------------------ kNN model ----------------------------
constexpr uint32_t MODEL_MAGIC = 0x48484B31;  // "HHK1"
struct ModelData {
  uint32_t magic;
  uint8_t  cnt[NCLS];
  float    x[NCLS][MAXS][DIM];
};

enum { RES_UNTRAINED = -2, RES_UNKNOWN = -1 };

struct KNN {
  ModelData m;
  float mu[DIM], sd[DIM], rejectDist = 1e9f;
  float rejFactor = 3.0f, rejOffset = 2.0f;   // unknown threshold = factor * median NN distance + offset
  float rejFixed = 0.0f;                      // > 0: use this fixed unknown threshold instead

  void clear() { memset(&m, 0, sizeof(m)); m.magic = MODEL_MAGIC; finalize(); }
  int  total() const { int t = 0; for (int c = 0; c < NCLS; c++) t += m.cnt[c]; return t; }
  int  classesTrained() const { int t = 0; for (int c = 0; c < NCLS; c++) t += m.cnt[c] > 0; return t; }
  bool valid() const {
    if (m.magic != MODEL_MAGIC) return false;
    for (int c = 0; c < NCLS; c++) if (m.cnt[c] > MAXS) return false;
    return true;
  }
  void clearClass(int c) { m.cnt[c] = 0; }
  bool add(int c, const float* v) {
    if (m.cnt[c] >= MAXS) {  // full: drop oldest
      memmove(m.x[c][0], m.x[c][1], sizeof(float) * DIM * (MAXS - 1));
      m.cnt[c] = MAXS - 1;
    }
    memcpy(m.x[c][m.cnt[c]], v, sizeof(float) * DIM);
    m.cnt[c]++;
    return true;
  }

  static float minSd(int d) {
    if (d < NBANDS) return 2.0f;          // dB
    switch (d - NBANDS) { case 0: return 0.08f; case 1: return 1.5f; case 2: return 0.5f; default: return 1.5f; }
  }
  static float weight(int d) { return d < NBANDS ? 1.0f : 2.0f; }

  float dist(const float* a, const float* b) const {
    float s = 0;
    for (int d = 0; d < DIM; d++) { float z = (a[d] - b[d]) / sd[d]; s += weight(d) * z * z; }
    return sqrtf(s);
  }

  // recompute normalisation + rejection threshold (call after any change)
  void finalize() {
    int n = total();
    for (int d = 0; d < DIM; d++) {
      float s = 0, s2 = 0;
      for (int c = 0; c < NCLS; c++) for (int i = 0; i < m.cnt[c]; i++) { s += m.x[c][i][d]; s2 += m.x[c][i][d] * m.x[c][i][d]; }
      mu[d] = n ? s / n : 0;
      float var = n > 1 ? (s2 / n - mu[d] * mu[d]) : 0;
      float sdv = var > 0 ? sqrtf(var) : 0;
      sd[d] = sdv > minSd(d) ? sdv : minSd(d);
    }
    // typical distance between a sample and its nearest same-class neighbour
    float nn[NCLS * MAXS]; int k = 0;
    for (int c = 0; c < NCLS; c++) {
      if (m.cnt[c] < 2) continue;
      for (int i = 0; i < m.cnt[c]; i++) {
        float best = 1e9f;
        for (int j = 0; j < m.cnt[c]; j++) if (j != i) { float dd = dist(m.x[c][i], m.x[c][j]); if (dd < best) best = dd; }
        nn[k++] = best;
      }
    }
    if (k < 3) { rejectDist = rejFixed > 0 ? rejFixed : 1e9f; return; }  // not enough data for automatic
    for (int i = 1; i < k; i++) { float t = nn[i]; int j = i - 1; while (j >= 0 && nn[j] > t) { nn[j + 1] = nn[j]; j--; } nn[j + 1] = t; }
    float med = nn[k / 2];
    rejectDist = rejFixed > 0 ? rejFixed : rejFactor * med + rejOffset;
  }

  // returns class index, RES_UNKNOWN or RES_UNTRAINED; *outDist = nearest distance
  int classify(const float* v, float* outDist = nullptr) const { return classifyEx(v, -1, -1, outDist); }
  // same, but ignores stored example (skipC, skipI): used for the leave-one-out self-check
  int classifyEx(const float* v, int skipC, int skipI, float* outDist) const {
    if (total() == 0) { if (outDist) *outDist = 0; return RES_UNTRAINED; }
    const int K = 3;
    float bd[K]; int bc[K];
    for (int i = 0; i < K; i++) { bd[i] = 1e9f; bc[i] = -1; }
    for (int c = 0; c < NCLS; c++)
      for (int i = 0; i < m.cnt[c]; i++) {
        if (c == skipC && i == skipI) continue;
        float d = dist(v, m.x[c][i]);
        if (d < bd[K - 1]) {
          int j = K - 1; while (j > 0 && bd[j - 1] > d) { bd[j] = bd[j - 1]; bc[j] = bc[j - 1]; j--; }
          bd[j] = d; bc[j] = c;
        }
      }
    if (outDist) *outDist = bd[0];
    if (bd[0] > rejectDist) return RES_UNKNOWN;
    float score[NCLS] = {0};
    for (int i = 0; i < K; i++) if (bc[i] >= 0) score[bc[i]] += 1.0f / (bd[i] + 0.05f);
    int best = 0; for (int c = 1; c < NCLS; c++) if (score[c] > score[best]) best = c;
    return best;
  }
};

}  // namespace hh

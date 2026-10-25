// Desktop test of hh_dsp.h with synthetic sounds. Build & run from the tools/ folder:
//   g++ -O2 -std=c++17 sim_test.cpp -o sim_test && ./sim_test
// Expected: FFT peak bin 32, 0 events in silence, accuracy 100% on synthetic sounds.
#include "../HaptiHear/hh_dsp.h"
#include <cstdio>
#include <random>
#include <vector>
using namespace hh;
std::mt19937 rng(42);
float U(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
float G() { return std::normal_distribution<float>(0, 1)(rng); }
const float PI = 3.14159265f;

// each generator returns ~1.2 s of audio at amplitude ~1
std::vector<float> doorbell() {
  std::vector<float> y(SR * 1.2, 0); float f1 = U(600, 700), f2 = f1 * U(0.78, 0.84);
  for (int i = 0; i < (int)y.size(); i++) { float t = (float)i / SR;
    float a = t < 0.5f ? expf(-t * 4) : 0, b = t >= 0.45f ? expf(-(t - 0.45f) * 4) : 0;
    y[i] = a * (sinf(2*PI*f1*t) + 0.4f*sinf(2*PI*2.76f*f1*t)) + b * (sinf(2*PI*f2*t) + 0.4f*sinf(2*PI*2.76f*f2*t)); }
  return y; }
std::vector<float> voice() {
  std::vector<float> y(SR * 1.2, 0); float f0 = U(110, 230), ph = 0, syl = U(3, 5);
  float fm1 = U(500, 800), fm2 = U(1200, 2200);
  for (int i = 0; i < (int)y.size(); i++) { float t = (float)i / SR; float f = f0 * (1 + 0.1f * sinf(2*PI*1.3f*t)); ph += 2*PI*f/SR;
    float env = fmaxf(0, sinf(PI * syl * t)); env *= t < 1.0f ? 1 : 0; float s = 0;
    for (int h = 1; h < 30; h++) { float fh = f * h; if (fh > 7000) break;
      float g = expf(-powf((fh - fm1) / 250, 2)) + 0.6f * expf(-powf((fh - fm2) / 350, 2)) + 0.05f; s += g * sinf(h * ph) / h * 3; }
    y[i] = env * (s + 0.05f * G()); }
  return y; }
std::vector<float> alarm() {
  std::vector<float> y(SR * 1.2, 0); float f = U(2700, 3300), rate = U(3, 5);
  for (int i = 0; i < (int)y.size(); i++) { float t = (float)i / SR; float on = fmodf(t * rate, 1.0f) < 0.5f ? 1 : 0; y[i] = on * sinf(2*PI*f*t); }
  return y; }
std::vector<float> knock() {
  std::vector<float> y(SR * 1.2, 0); int n = 2 + rng() % 3; float gap = U(0.15f, 0.25f); float lp = 0;
  for (int k = 0; k < n; k++) { int s0 = (int)(k * gap * SR);
    for (int i = 0; i < SR * 0.08 && s0 + i < (int)y.size(); i++) { float t = (float)i / SR; lp = 0.7f * lp + 0.3f * G();
      y[s0 + i] += 3 * expf(-t * 60) * (lp + 0.8f * sinf(2*PI*U(180, 220)*t)); } }
  return y; }

typedef std::vector<float> (*Gen)();
Gen gens[4] = {doorbell, voice, alarm, knock};
const char* names[4] = {"Doorbell", "Voice", "Alarm", "Knock"};

int main() {
  FrameDSP dsp; dsp.init(); EventDetector det; KNN knn; knn.clear();
  // --- FFT sanity: 1 kHz tone must peak at bin 32
  { float x[N]; for (int i = 0; i < N; i++) x[i] = sinf(2*PI*1000*i/SR); Frame f; dsp.process(x, f);
    int pk = 0; for (int k = 0; k <= N/2; k++) if (dsp.re[k] > dsp.re[pk]) pk = k; printf("FFT peak bin %d (expect 32), frame dB %.1f (expect -3.0)\n", pk, f.db); }
  printf("band edges:"); for (int b = 0; b <= NBANDS; b++) printf(" %d", dsp.edge[b]); printf("\n");

  float bgAmp = powf(10, -80 / 20.0f);  // quiet room noise ~ -80 dBFS
  auto runClip = [&](const std::vector<float>& clip, float gainDb, std::vector<std::vector<float>>& evs, int silenceFrames) {
    float g = powf(10, gainDb / 20); std::vector<float> s;
    for (int i = 0; i < silenceFrames * N; i++) s.push_back(bgAmp * G());
    for (float v : clip) s.push_back(g * v + bgAmp * G());
    for (int i = 0; i < 40 * N; i++) s.push_back(bgAmp * G());
    for (size_t o = 0; o + N <= s.size(); o += N) { Frame f; dsp.process(&s[o], f);
      if (det.push(f)) { std::vector<float> v(DIM); det.features(v.data()); evs.push_back(v); } } };

  // warm-up background
  { std::vector<std::vector<float>> e; runClip({}, 0, e, 100); printf("events in pure silence: %zu (expect 0), floor %.1f dB\n", e.size(), det.floorDb); }

  // TRAIN: 5 examples per class at random loudness
  for (int c = 0; c < 4; c++) for (int r = 0; r < 5; r++) {
    std::vector<std::vector<float>> e; runClip(gens[c](), U(-45, -25), e, 20);
    if (e.size() != 1) printf("  train %s got %zu events\n", names[c], e.size());
    if (!e.empty()) knn.add(c, e[0].data()); }
  knn.finalize();
  printf("trained: %d samples, reject dist %.2f\n", knn.total(), knn.rejectDist);

  // TEST: 40 per class, wider loudness range
  int conf[4][6] = {{0}}; int missed = 0, extra = 0;
  for (int c = 0; c < 4; c++) for (int r = 0; r < 40; r++) {
    std::vector<std::vector<float>> e; runClip(gens[c](), U(-50, -20), e, 20);
    if (e.empty()) { missed++; continue; } if (e.size() > 1) extra += e.size() - 1;
    int p = knn.classify(e[0].data()); conf[c][p >= 0 ? p : 4]++; }
  printf("confusion (rows=true; cols Doorbell Voice Alarm Knock Unknown):\n");
  int ok = 0, tot = 0;
  for (int c = 0; c < 4; c++) { printf("%-9s", names[c]); for (int p = 0; p < 5; p++) printf(" %4d", conf[c][p]); printf("\n"); ok += conf[c][c]; for (int p = 0; p < 5; p++) tot += conf[c][p]; }
  printf("accuracy %.1f%%  missed %d  extra-triggers %d\n", 100.0 * ok / tot, missed, extra);

  // Unknown sound (white-noise hiss) should mostly be rejected or at least not crash
  int unk = 0; for (int r = 0; r < 20; r++) { std::vector<float> clip(SR); for (auto& v : clip) v = G();
    std::vector<std::vector<float>> e; runClip(clip, -35, e, 20); if (!e.empty() && knn.classify(e[0].data()) == RES_UNKNOWN) unk++; }
  printf("white-noise bursts flagged Unknown: %d/20\n", unk);
  printf("sizeof(ModelData)=%zu bytes\n", sizeof(ModelData));
}

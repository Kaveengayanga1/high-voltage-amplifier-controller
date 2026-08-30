// analyzer_types.h — rev 3 pin map, 2026-08-30
// Custom types in a header so Arduino's auto-generated prototypes can see them.
#ifndef ANALYZER_TYPES_H
#define ANALYZER_TYPES_H

enum WaveType { W_NOSIG, W_SINE, W_SQUARE, W_TRIANGLE, W_RAMP, W_OTHER };

struct Features {
  float mean, vpp, rms, crest, sym;
  uint16_t vmin, vmax;
};

#endif

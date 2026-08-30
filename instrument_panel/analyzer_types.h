#ifndef ANALYZER_TYPES_H
#define ANALYZER_TYPES_H

// Custom types in a header so Arduino's auto-generated prototypes
// (inserted before the sketch body) can see them.

enum WaveType { W_NOSIG, W_SINE, W_SQUARE, W_TRIANGLE, W_RAMP, W_OTHER };

struct Features {
  float mean, vpp, rms, crest, sym;
  uint16_t vmin, vmax;
};

#endif

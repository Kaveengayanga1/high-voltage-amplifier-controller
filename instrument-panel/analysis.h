#ifndef ANALYSIS_H
#define ANALYSIS_H

/* Signal analysis result — lives in a header so the Arduino builder's
 * auto-generated prototypes (inserted above the sketch body) can see it. */
struct Analysis {
  float mean, vpp, rms, crest, posFrac, freq;
  int   crossings;
  bool  periodsOK;
  const char *type;
};

#endif

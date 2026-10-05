/* A PC speaker: a queue of square-wave tones, like the original's Sound / Delay / NoSound. */
#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_resume(void);
void audio_beep(int hz, int ms);
/* n tones from hz, each `step` Hz higher (or lower), `ms` long */
void audio_sweep(int hz, int n, int step, int ms);

#endif

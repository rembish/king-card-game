/* A PC speaker: a queue of square-wave tones, like the original's Sound / Delay / NoSound. */
#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_resume(void);
void audio_beep(int hz, int ms);

#endif

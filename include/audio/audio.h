#ifndef _AUDIO_H
#define _AUDIO_H

int audio_init(const char *device); // Select openal audio device, etc.
void audio_render();
void audio_close();

#endif // _AUDIO_H
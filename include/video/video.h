#ifndef _VIDEO_H
#define _VIDEO_H

int video_init(int window_w, int window_h, int fullscreen, int vsync); // Create window etc.
void video_render();
void video_close();

#endif // _VIDEO_H
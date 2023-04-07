#pragma once
#include <sdl2/SDL.h>
#include <opencv2/opencv.hpp>

void matToTexture(SDL_Texture* texture, const cv::Mat mat) {

	unsigned char* texture_data = NULL;
	int texture_pitch = 0;

	SDL_LockTexture(texture, NULL, (void**)&texture_data, &texture_pitch);
	memcpy(texture_data, mat.data, mat.total() * mat.elemSize());

	SDL_UnlockTexture(texture);


}

#include "pch.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
// behind the SDL_opengl.h
#include <glad/glad.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <tinyxml2/tinyxml2.h>

#include "console.h"
#include "strings.h"
#include "utils.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#define NANOSVG_IMPLEMENTATION	// Expands implementation
#include <nanosvg/nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvgrast.h>

#include <sdl2/SDL.h>
#include <sdl2/SDL_image.h>

//#include <sdl2/SDL_opengl.h>

#include <gfx/SDL2_gfxPrimitives.h>

#include <filesystem>


SDL_Window* gWindow = NULL;
SDL_Renderer* gRenderer = NULL;

namespace fs = std::filesystem;

void putpixel(int x, int y) {
	//SDL_RenderDrawPoint(gRenderer, x, y);
}

void drawCircle(int x0, int y0, int radius)
{
	int x = radius - 1;
	int y = 0;
	int dx = 1;
	int dy = 1;
	int err = dx - (radius << 1);

	while (x >= y)
	{
		putpixel(x0 + x, y0 + y);
		putpixel(x0 + y, y0 + x);
		putpixel(x0 - y, y0 + x);
		putpixel(x0 - x, y0 + y);
		putpixel(x0 - x, y0 - y);
		putpixel(x0 - y, y0 - x);
		putpixel(x0 + y, y0 - x);
		putpixel(x0 + x, y0 - y);

		if (err <= 0)
		{
			y++;
			err += dy;
			dy += 2;
		}

		if (err > 0)
		{
			x--;
			dx += 2;
			err += dx - (radius << 1);
		}
	}
}



int getImageData(char* dataPtr, int width, int height, unsigned char* imgRGBAData) {

	NSVGimage* g_image = nullptr;
	NSVGrasterizer* rast = nsvgCreateRasterizer();
	g_image = nsvgParse(dataPtr, "px", 96);

	if (g_image == nullptr) {
		std::cout << "Read SVG Error." << std::endl;
		nsvgDeleteRasterizer(rast);
		nsvgDelete(g_image);
		return -1;
	}

	int pixelsCount = width * height * 4;
	// Width scan
	// RGBA
	nsvgRasterize(rast
		, g_image
		, 0
		, 0
		, 1
		, imgRGBAData
		, width
		, height
		, width * 4);

	nsvgDeleteRasterizer(rast);
	nsvgDelete(g_image);
	return 0;
}


void blendBg(const cv::Mat& bgMask, const cv::Mat& overlay, cv::Mat& output) {
	for (int i = 0; i < bgMask.rows; i++) {
		for (int j = 0; j < bgMask.cols; j++) {
			// None transparent area.
			float alpha_src = overlay.at<cv::Vec4b>(i, j)[3] / 255.0;
			float alpha_dst = 1.0 - alpha_src;
			output.at<cv::Vec4b>(i, j)[0] = bgMask.at<cv::Vec4b>(i, j)[0] * alpha_dst + overlay.at<cv::Vec4b>(i, j)[0] * alpha_src;
			output.at<cv::Vec4b>(i, j)[1] = bgMask.at<cv::Vec4b>(i, j)[1] * alpha_dst + overlay.at<cv::Vec4b>(i, j)[1] * alpha_src;
			output.at<cv::Vec4b>(i, j)[2] = bgMask.at<cv::Vec4b>(i, j)[2] * alpha_dst + overlay.at<cv::Vec4b>(i, j)[2] * alpha_src;
			output.at<cv::Vec4b>(i, j)[3] = bgMask.at<cv::Vec4b>(i, j)[3] * alpha_dst + overlay.at<cv::Vec4b>(i, j)[3] * alpha_src;
		}
	}
}

void normalizePadding(int scale, const cv::Mat src, cv::Mat& dst) {

	cv::Mat imgMat = src;
	cv::cvtColor(src, imgMat, cv::COLOR_RGBA2BGRA);

	cv::Scalar bgColor = cv::Scalar(247, 89, 10, 255);
	//cv::Scalar bgColor = cv::Scalar(0, 0, 0, 0.2 * 255);

	//(24vp * 8) target ==> 192 * 192, 22vp as basic unit
	//22 * 8 = |176 - 192| = 16vp
	int targetSize = 24 * scale;

	//int scale = targetSize / 24;

	int contentSize = 22 * scale;

	cv::resize(imgMat, imgMat, cv::Size(contentSize, contentSize));

	cv::Mat padded;
	int padding = 1 * scale;// Factor, 24vp * 3 = 72vp, 66 + 3*2 = 72
	padded.create(imgMat.rows + 2 * padding, imgMat.cols + 2 * padding, imgMat.type());
	padded.setTo(cv::Scalar::all(0));
	// RGBA 10, 89, 247, 1 ==> BGRA 247 89 10 1
	imgMat.copyTo(padded(cv::Rect(padding, padding, imgMat.cols, imgMat.rows)));

	cv::Mat mask;
	mask.create(imgMat.rows + 2 * padding, imgMat.cols + 2 * padding, imgMat.type());
	mask.setTo(cv::Scalar::all(0));

	// RGBA 10, 89, 247, 1 ==> BGRA 247 89 10 1
	//cv::circle(mask, cv::Point(96, 96), 96, cv::Scalar(247, 89, 10, 1.0 * 255), cv::FILLED);

	cv::Mat bgMask;
	bgMask.create(imgMat.rows + 2 * padding, imgMat.cols + 2 * padding, imgMat.type());
	bgMask.setTo(cv::Scalar::all(0));
	//cv::rectangle(bgMask, cv::Rect(0, 0, targetSize, targetSize), cv::Scalar(247, 89, 10, 255), cv::FILLED);
	cv::circle(bgMask,
		cv::Point(targetSize / 2, targetSize / 2),
		targetSize / 2 - 1,
		bgColor,//BGRA
		cv::FILLED);

	blendBg(bgMask, padded, dst);
}

void grayscale(const cv::Mat src, cv::Mat& dst) {
	dst = src.clone();
	//// BGRA 0,1,2,3
	for (int i = 0; i < src.rows; i++) {
		for (int j = 0; j < src.cols; j++) {
			float grayValue = src.at<cv::Vec4b>(i, j)[0] * 0.229 // B
				+ src.at<cv::Vec4b>(i, j)[1] * 0.587 // G
				+ src.at<cv::Vec4b>(i, j)[2] * 0.114; // R
			dst.at<cv::Vec4b>(i, j)[0] = grayValue;
			dst.at<cv::Vec4b>(i, j)[1] = grayValue;
			dst.at<cv::Vec4b>(i, j)[2] = grayValue;
		}
	}
}

void destroy() {
	SDL_DestroyRenderer(gRenderer);
	SDL_DestroyWindow(gWindow);
	SDL_Quit();
}

int init() {

	/*
	* init SDL/OpenGL
	*/
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0) {
		SDL_Log("Init SDL error: %s\n", SDL_GetError());

		return EXIT_FAILURE;
	}

	atexit(SDL_Quit);

	SDL_GL_LoadLibrary(NULL);

	//const char* glsl_version = "#version 130";
	const char* glsl_version = "#version 460";//OpenGL 460 Core
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");


}

void initWindow(int winWidth, int winHeight) {
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

	gWindow = SDL_CreateWindow(
		"Normalize Icon",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		winWidth,
		winHeight,
		window_flags
	);

	SDL_GLContext gl_context = SDL_GL_CreateContext(gWindow);
	SDL_GL_MakeCurrent(gWindow, gl_context);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED);

	//SDL_GL_DeleteContext(gl_context);
	// INITIALIZE GLAD(must init before SDL_GL_MakeCurrent(...)
	if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
		Console::setColor(WhiteFore, RedBack);
		printf("Failed to initialize GLAD\n");
		Console::setColor();
	}
}


void preview(int winWidth, int winHeight, cv::Mat destMat, cv::Mat grayMat, int scale) {

	bool quit = false;
	SDL_Event event;

	IMG_Init(IMG_INIT_PNG);

	std::cout << IMG_GetError() << std::endl;

	//SDL_RWops* rwOps = SDL_RWFromFile("result.png", "r+");
	//SDL_RWops* rwOps = SDL_RWFromMem(destMat.data, destMat.total() * destMat.elemSize());

	//SDL_RWseek(rwOps, 0, RW_SEEK_SET);

	SDL_SetRenderDrawColor(gRenderer, 0x0, 0x0, 0xFF, 0xFF);

	SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);

	//************************************Get Render info***************************
	SDL_RendererInfo info;
	SDL_GetRendererInfo(gRenderer, &info);
	Console::setColor(YellowFore);
	std::cout << "**************************************Renderer Info*************************** " << std::endl;
	std::cout << "Renderer name: " << info.name << std::endl;
	std::cout << "GL Version: " << glGetString(GL_VERSION) << std::endl;
	std::cout << "Texture support formats: " << std::endl;
	for (Uint32 i = 0; i < info.num_texture_formats; i++)
	{
		std::cout << SDL_GetPixelFormatName(info.texture_formats[i]) << std::endl;
	}
	std::cout << "**************************************Renderer Info*************************** " << std::endl;
	Console::setColor();
	//************************************Get Render info***************************

	//SDL_Surface* imgSur = IMG_LoadPNG_RW(rwOps);

	std::cout << IMG_GetError() << std::endl;

	//SDL_Texture* texture = SDL_CreateTextureFromSurface(render, imgSur);

	int rmask, gmask, bmask, amask;
	rmask = 0xFF000000;
	gmask = 0x00FF0000;
	bmask = 0x0000FF00;
	amask = 0x000000FF;
	int pitch = destMat.rows * 4;
	int depth = 4 * 8;
	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
		destMat.data,
		destMat.cols,
		destMat.rows,
		depth,
		pitch,
		rmask,
		gmask,
		bmask,
		amask
	);

	//SDL_Texture* texture = SDL_CreateTextureFromSurface(render, surface);

	SDL_Texture* texture = SDL_CreateTexture(gRenderer,
		SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STREAMING,
		destMat.cols,
		destMat.rows
	);

	SDL_Texture* grayTexture = SDL_CreateTexture(gRenderer,
		SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STREAMING,
		grayMat.cols,
		grayMat.rows
	);


	// if set SDL_BLENDMODE_NONE, alpha channel will be loss(replace with black).
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(grayTexture, SDL_BLENDMODE_BLEND);

	cv::Mat bg;
	bg.create(winWidth, winHeight, CV_8UC3);
	bg.setTo(cv::Scalar(255, 248, 248, 0));

	SDL_Texture* backgroundTexture = SDL_CreateTexture(gRenderer,
		SDL_PIXELFORMAT_BGR24,
		SDL_TEXTUREACCESS_STREAMING,
		winWidth,
		winHeight
	);

	//SDL_FreeSurface(imgSur);

	int targetSize = 24 * scale;;

	while (!quit) {

		if (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				quit = true;
				break;
			case SDL_WINDOWEVENT:
			{
				if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
					winWidth = event.window.data1;
					winHeight = event.window.data2;
				}
				break;
			}
			case SDL_MOUSEMOTION: {
				int x = event.motion.x;
				int y = event.motion.y;
				//std::cout << x << " " << y << " " << std::endl;
				break;
			}
			case SDL_MOUSEBUTTONDOWN: {
				switch (event.button.button) {
				case SDL_BUTTON_LEFT: {
					int x = event.motion.x;
					int y = event.motion.y;
					std::cout << x << " " << y << " " << std::endl;
					//SDL_ShowSimpleMessageBox(0, "Mouse", "Left button was pressed!", gWindow);
					break;
				}
				case SDL_BUTTON_RIGHT:
					SDL_ShowSimpleMessageBox(0, "Mouse", "Right button was pressed!", gWindow);
					break;
				default:
					break;
				}
				break;
			}
			case SDL_MOUSEBUTTONUP: {
				switch (event.button.button) {
				case SDL_BUTTON_LEFT: {
					int x = event.motion.x;
					int y = event.motion.y;
					std::cout << x << " " << y << " " << std::endl;
					break;
				}
				default:
					break;
				}
				break;
			}
			}
		}

		matToTexture(texture, destMat);

		matToTexture(grayTexture, grayMat);

		matToTexture(backgroundTexture, bg);

		//SDL_UpdateTexture(texture, NULL, destMat.data, destMat.cols);

		SDL_RenderClear(gRenderer);

		SDL_Rect rect;
		rect.w = destMat.cols;
		rect.h = destMat.rows;
		rect.x = 100;
		rect.y = 100;

		//SDL_SetRenderTarget(render, texture);

		int targetCenterX = 100 + targetSize / 2;
		int targetCenterY = 100 + targetSize / 2;

		SDL_RenderCopy(gRenderer, backgroundTexture, NULL, NULL);

		// draw grid lines
		SDL_SetRenderDrawColor(gRenderer, 217, 217, 217, 255 * 0.4);
		for (int i = 0; i < winWidth; i += 8) {
			SDL_RenderDrawLine(gRenderer, i, 0, i, winHeight);
		}
		for (int i = 0; i < winHeight; i += 8) {
			SDL_RenderDrawLine(gRenderer, 0, i, winWidth, i);
		}

		// draw main xAxis, yAxis
		SDL_SetRenderDrawColor(gRenderer, 230, 69, 102, 255);
		SDL_RenderDrawLine(gRenderer, 0, targetCenterY, winWidth, targetCenterX);
		SDL_RenderDrawLine(gRenderer, targetCenterX, 0, targetCenterX, winHeight);
		// draw center Circle
		SDL_SetRenderDrawColor(gRenderer, 217, 217, 217, 255 * 0.4);
		aacircleRGBA(gRenderer, targetCenterX - 1, targetCenterY - 1, targetSize / 2 - 1, 217, 217, 217, 255);
		SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
		//drawCircle(targetCenterX, targetCenterY, targetSize / 2);

		// draw conent mask(pink)
		SDL_SetRenderDrawColor(gRenderer, 230, 69, 102, 255 * 0.2);
		SDL_Rect bgRect = rect;
		bgRect.w = destMat.cols - scale;
		bgRect.h = destMat.rows - scale;
		bgRect.x = rect.x + scale / 2;
		bgRect.y = rect.y + scale / 2;
		roundedBoxRGBA(gRenderer, bgRect.x + bgRect.w, bgRect.y, bgRect.x, bgRect.y + bgRect.h, 8, 230, 69, 102, 255 * 0.2);

		// draw outbounding rect in red color.
		SDL_SetRenderDrawColor(gRenderer, 230, 69, 102, 255);
		SDL_Rect boundRect = rect;
		boundRect.w = destMat.cols + 1;
		boundRect.h = destMat.rows + 1;
		boundRect.x = rect.x - 1;
		boundRect.y = rect.y - 1;
		roundedRectangleRGBA(gRenderer, boundRect.x + boundRect.w, boundRect.y, boundRect.x, boundRect.y + boundRect.h, 16, 230, 69, 102, 255);

		//aalineRGBA(gRenderer, 0, 0, 100, 100, 255, 0, 0, 255);

		SDL_Rect grayRect = rect;
		grayRect.x = rect.x + rect.w + 64;

		SDL_SetRenderDrawColor(gRenderer, 178, 223, 238, 255);
		SDL_Rect grayRectBoud = grayRect;
		grayRectBoud.w = grayMat.cols + 1;
		grayRectBoud.h = grayMat.rows + 1;
		grayRectBoud.x = grayRect.x - 1;
		grayRectBoud.y = grayRect.y - 1;
		SDL_RenderDrawRect(gRenderer, &grayRectBoud);

		// copy to renderer
		SDL_RenderCopy(gRenderer, texture, NULL, &rect);

		SDL_RenderCopy(gRenderer, grayTexture, NULL, &grayRect);

		//glViewport(0, 0, winWidth, winHeight);
		//glClearColor(1.f, 0.f, 1.f, 0.f);
		//glClear(GL_COLOR_BUFFER_BIT);

		//display
		SDL_RenderPresent(gRenderer);
		//SDL_GL_SwapWindow(gWindow);
	}// while
	SDL_DestroyTexture(texture);
	SDL_DestroyTexture(backgroundTexture);
	IMG_Quit();
}

int main(int argc, char** argv)
{

	Console::setColor(ConsoleColors::GrayFore);
	std::cout << "Welcome to Normalize Icon Tool.\n";
	Console::setColor();
	Console::newLine();
	cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);

	int winWidth = 840;
	int winHeight = 480;

	init();
	initWindow(winWidth, winHeight);

	int ICON_WIDTH = 200;

	int ICON_HEIGHT = 200;


	unsigned char* imgData = NULL;

	imgData = (unsigned char*)malloc(ICON_WIDTH * ICON_HEIGHT * 4);

	Console::newLine();

	for (const auto& entry : fs::directory_iterator(".\\svgs")) {

		memset(imgData, 0, ICON_WIDTH * ICON_HEIGHT * 4);

		Console::setColor(YellowFore);
		std::cout << "  Processing: ";
		Console::setColor(GreenFore);
		std::cout << entry.path().filename() << std::endl;
		Console::setColor();

		if (entry.is_directory()) continue;

		std::ifstream file(entry.path(), std::ios::binary | std::ios::ate);
		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<char> buffer(size);

		if (!file.read(buffer.data(), size))
		{
			Console::setColor(RedFore, WhiteBack);
			std::cout << "Error to read: " << entry.path() << std::endl;
			Console::setColor();
		}

		// PNG file data
		getImageData(buffer.data(), ICON_WIDTH, ICON_HEIGHT, imgData);

		cv::Mat destMat;
		cv::Mat imgMat = cv::Mat(ICON_HEIGHT, ICON_WIDTH, CV_8UC4, imgData, ICON_WIDTH * 4);

		int scale = 8;

		normalizePadding(scale, imgMat, destMat);

		std::wstring newName = entry.path().filename();
		std::string filePath(".\\svgs\\png\\");
		std::transform(newName.begin(), newName.end(), std::back_inserter(filePath), [](wchar_t c) {
			return (char)c;
			});

		std::string nameWithExt = filePath.substr(filePath.find_last_of('\\') + 1, -1);
		std::string nameWithoutExt = nameWithExt.substr(0, nameWithExt.find_last_of('.'));
		filePath = filePath.substr(0, filePath.find_last_of('\\') + 1);

		//std::string finalName("ic");
		std::string finalName(nameWithoutExt);
		//finalName += "_";
		//finalName += nameWithoutExt;
		finalName += "_";

		cv::imwrite((filePath + finalName + "on.png").c_str(), destMat);

		cv::Mat grayMat;

		grayscale(destMat, grayMat);

		cv::imwrite((filePath + finalName + "off.png").c_str(), grayMat);

		cv::cvtColor(destMat, destMat, cv::COLOR_BGRA2RGBA);
		cv::cvtColor(grayMat, grayMat, cv::COLOR_BGRA2RGBA);

		bool isPreview = true;
		if (isPreview) {
			preview(winWidth, winHeight, destMat, grayMat, scale);
		}

	}

	free(imgData);

	destroy();

	return 0;
}

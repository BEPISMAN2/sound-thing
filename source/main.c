#include <string.h>
#include <stdio.h>

#include <3ds.h>
#include "sound.h"

int main(int argc, char **argv) {

	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);
	
	wavFile wav;
	Result ret;
	
	ret = loadWav("/test.wav", &wav, 2);
	if (ret != 0)
		goto exit;
	
	printWav(&wav);
	deleteWav(&wav);

	exit:
	printf("Test. Press the START button to exit.\n");
	
	// Main loop
	while (aptMainLoop()) {

		gspWaitForVBlank();
		hidScanInput();

		// Your code goes here

		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break; // break in order to return to hbmenu

		// Flush and swap framebuffers
		gfxFlushBuffers();
		gfxSwapBuffers();
	}

	gfxExit();
	return 0;
}

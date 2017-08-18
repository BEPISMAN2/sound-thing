#include <string.h>
#include <stdio.h>

#include <3ds.h>
#include "sound.h"

void waitForInput() {
	while (aptMainLoop()) {	
		hidScanInput();
		if (hidKeysUp() & KEY_START)
			break;
		
		gfxFlushBuffers();
		gfxSwapBuffers();
	}
}

int main(int argc, char **argv) {

	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);
	
	Result ret = soundInit();
	if (ret != 0)
		goto exit;
	
	wavFile wav;
	
	ret = loadWav("/test.wav", &wav, 0.1);
	if (ret != 0) {
		fprintf(stderr, "error: could not load file\n");
		goto exit;
	}
	
	printWav(&wav);
	printf("Press the START button to start playback.\n");
	waitForInput();
	
	ret = playWav(&wav, 0, false);
	if (ret != 0) {
		fprintf(stderr, "error: could not play file, result: %x\n", ret);
		deleteWav(&wav);
		goto exit;
	} 
	
	printf("Playing back WAV...\n");
	

	exit:
	printf("Test. Press the START button to exit.\n");
	waitForInput();
	
	if (ret != 0) {
		stopWav(0);
		deleteWav(&wav);
	}
	
	soundExit();
	gfxExit();
	return 0;
}

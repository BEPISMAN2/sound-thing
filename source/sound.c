#include <stdio.h>
#include <string.h>
#include <math.h>
#include <3ds.h>

#include "sound.h"

Result loadWav(const char *path, wavFile *wav, int streamChunkSize) {
	u16 bytePerBlock;
	u16 bytePerSample;
	char magic[4];
	
	int ckSize;
	int format;
	
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "error: could not open %s\n", path);
		return -1;
	}
	
	// -- some file checks --
	
	// read magic RIFF
	fseek(fp, 0, SEEK_SET);
	fread(&magic, 4, 1, fp);
	if (strcmp(magic, "RIFF") != 0) {
		fprintf(stderr, "error: file invalid (RIFF)\n");
		return -1;
	}

	// read magic WAVE
	fseek(fp, 8, SEEK_SET);
	fread(&magic, 4, 1, fp);
	if (strcmp(magic, "WAVE") != 0) {
		fprintf(stderr, "error: file invalid (WAVE)\n");
		return -1;
	}
	
	// check fmt marker
	fseek(fp, 12, SEEK_SET);
	fread(&magic, 4, 1, fp);
	if (strcmp(magic, "fmt\0") != 0) {
		fprintf(stderr, "error: file invalid (fmt)\n");
		return -1;
	}
	
	// check chunk size
	fseek(fp, 16, SEEK_SET);
	fread(&ckSize, 4, 1, fp);
	if (ckSize != 16) {
		fprintf(stderr, "error: ckSize must be 16 for PCM\n");
		return -1;
	}
	
	// check format
	fseek(fp, 20, SEEK_SET);
	fread(&format, 2, 1, fp);
	if (format != 0x0001) {
		fprintf(stderr, "error: invalid format\n");
		return -1;
	}
	
	// -- now we actually get to do shit --
	
	// get number of audio channels (stereo/mono)
	fseek(fp, 22, SEEK_SET);
	fread(&wav->channels, 2, 1, fp);
	
	// get sample rate
	fseek(fp, 24, SEEK_SET);
	fread(&wav->rate, 4, 1, fp);
	
	// get byte per block
	fseek(fp, 32, SEEK_SET);
	fread(&bytePerBlock, 2, 1, fp);
	
	// get bits per sample, convert to byte per sample
	fseek(fp, 24, SEEK_SET);
	fread(&bytePerSample, 2, 1, fp);
	bytePerSample /= 8;
	
	// get size of data
	fseek(fp, 40, SEEK_SET);
	fread(&wav->size, 4, 1, fp);
	
	
	// -- do a few calculations --
	wav->nSamples = wav->size / bytePerBlock;
	wav->bytePerSample = bytePerSample / wav->channels;
	if (bytePerSample == 8)
		wav->encoding = NDSP_ENCODING_PCM8;
	else if (bytePerSample== 16)
		wav->encoding = NDSP_ENCODING_PCM16;
	else {
		fprintf(stderr, "error: invalid encoding; must be either PCM8 or PCM16\n");
		return -1;
	}
	
	// calculate chunk size for streaming
	if (streamChunkSize < 0) {
		wav->chunkNSamples = wav->nSamples;
		wav->chunkSize = wav->size;
	} else {
		wav->chunkNSamples = fmin(	round(streamChunkSize * wav->rate),
									wav->nSamples);
		wav->chunkSize = wav->chunkNSamples * wav->channels * wav->bytePerSample;
	}
	
	// allocate memory
	if (linearSpaceFree() < wav->chunkSize) {
		fprintf(stderr, "error: not enough linear memory available\n");
		return -1;
	}
	wav->data = (char*)linearAlloc(wav->chunkSize);
	
	// read the actual audio data into memory
	fseek(fp, 44, SEEK_SET);
	fread(wav->data, wav->chunkSize, 1, fp);
	
	// get file current position
	wav->filePos = ftell(fp);
	
	// get file size
	fseek(fp, 0, SEEK_END);
	wav->fileSize = ftell(fp);
	
	wav->file = fp;
	
	return 0;
}
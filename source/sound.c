#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <3ds.h>

#include "sound.h"

#define STACKSIZE (32 * 1024) // taken from ctrmus

bool runSoundThread = false;
bool isInitialized = false;

typedef struct {
	Thread thread;
	Handle mutex;
} threadStruct;


threadStruct* tRef;

Result soundInit() {
	// initialize NDSP
	if (isInitialized)
		return 0;
	
	Result ret = ndspInit();
	if (ret) {
		fprintf(stderr, "error: could not initialize NDSP. 0x%x\n", ret);
		return ret;
	}
	
	threadStruct *t = malloc(sizeof(threadStruct));
	
	svcCreateMutex(&t->mutex, false);
	
	runSoundThread = true;
	
	s32 prio;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	printf("main thread prio: %d\n", prio);
	
	t->thread = threadCreate(updateChannels, (void*) t, STACKSIZE, prio - 1, -2, false);
	printf("thread created: %p\n", t);
	
	
	isInitialized = true;
	tRef = t;
	
	return 0;
}

void soundExit() {
	if (isInitialized)
		return;
	
	svcWaitSynchronization(tRef->mutex, U64_MAX);
	threadJoin(tRef->thread, U64_MAX);
	threadFree(tRef->thread);
	
	svcCloseHandle(tRef->mutex);
	ndspExit();
}

Result loadWav(const char *path, wavFile *wav, double streamChunkSize) {
	u16 bytePerBlock;
	u16 bytePerSample;
	char magic[4];
	
	int ckSize;
	int format;
	u32 rate;
	u32 size;
	
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "error: could not open %s\n", path);
		return -1;
	}
	
	// -- some file checks --
	
	// read magic RIFF
	fseek(fp, 0, SEEK_SET);
	fread(&magic, 4, 1, fp);
	if (strncmp(magic, "RIFF", 4) != 0) {
		fprintf(stderr, "error: file invalid (RIFF)\n");
		return -1;
	}

	// read magic WAVE
	fseek(fp, 8, SEEK_SET);
	fread(&magic, 4, 1, fp);
	if (strncmp(magic, "WAVE", 4) != 0) {
		fprintf(stderr, "error: file invalid (WAVE)\n");
		return -1;
	}
	
	// check fmt marker
	fseek(fp, 12, SEEK_SET);
	fread(&magic, 4, 1, fp);
	if (strncmp(magic, "fmt", 3) != 0) {
		fprintf(stderr, "error: file invalid (fmt)\n");
		return -1;
	}
	
	// check chunk size
	fseek(fp, 16, SEEK_SET);
	fread(&magic, 4, 1, fp);
	if (*magic != 16) {
		fprintf(stderr, "error: ckSize must be 16 for PCM\n");
		return -1;
	}
	
	// check format
	fseek(fp, 20, SEEK_SET);
	fread(&magic, 2, 1, fp);
	if (*magic != 0x0001) {
		fprintf(stderr, "error: invalid format\n");
		return -1;
	}
	
	// -- now we actually get to do shit --
	
	// get number of audio channels (stereo/mono)
	u16 channels;
	fseek(fp, 22, SEEK_SET);
	fread(&channels, 2, 1, fp);
	if (channels > 2 || channels < 1) {
		fprintf(stderr, "error: must have 1 or 2 channels, channels detected: %d\n", channels);
		return -1;
	}
	wav->channels = channels;
	
	// get sample rate
	fseek(fp, 24, SEEK_SET);
	fread(&rate, 4, 1, fp);
	wav->rate = rate;
	
	// get byte per block
	fseek(fp, 32, SEEK_SET);
	fread(&bytePerBlock, 2, 1, fp);
	
	// get bits per sample, convert to byte per sample
	fseek(fp, 34, SEEK_SET);
	fread(&bytePerSample, 2, 1, fp);
	bytePerSample /= 8;
	
	// get size of data
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	wav->size = size;
	
	// -- do a few calculations --
	wav->nSamples = wav->size / bytePerBlock;
	wav->bytePerSample = bytePerSample / wav->channels;
	if (bytePerSample == 1)
		wav->encoding = NDSP_ENCODING_PCM8;
	else if (bytePerSample== 2)
		wav->encoding = NDSP_ENCODING_PCM16;
	else {
		fprintf(stderr, "error: invalid encoding; must be either PCM8 or PCM16, bytePerSample = %d\n", bytePerSample);
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

void deleteWav(wavFile *wav) {
	linearFree(wav->data);
	fclose(wav->file);
}

Result playWav(wavFile *wav, int channel, bool loop) {
	ndspWaveBuf *wbuf;
	
	stopWav(channel);
	ndspChnReset(channel);
	ndspChnSetRate(channel, wav->rate);
	ndspChnInitParams(channel);
	ndspChnSetFormat(channel, NDSP_CHANNELS(wav->channels) | NDSP_ENCODING(wav->encoding));
	
	wbuf = calloc(1, sizeof(ndspWaveBuf));
	
	wbuf->data_vaddr = wav->data;
	wbuf->nsamples = wav->chunkNSamples;
	wbuf->looping = (wav->chunkSize < wav->size) ? false : loop;
	
	Result ret = DSP_FlushDataCache((u32*)wav->data, wav->chunkSize);
	ndspChnWaveBufAdd(channel, wbuf);
	
	channels[channel] = wav;

	// free previous stream
	if (streaming[channel] != NULL) {
		free(streaming[channel]);
		streaming[channel] = NULL;
	}
	
	// create new stream
	if (wav->chunkSize < wav->fileSize) { 
		audioStream *stream = calloc(1, sizeof(audioStream));
		stream->loop = loop;
		stream->audio = wav;
		stream->nextWaveBuf = wbuf;
	
		if (linearSpaceFree() < wav->chunkSize * 2) {
			fprintf(stderr, "error: not enough linear memory available\n");
			return -1;
		}
	
		stream->nextData = linearAlloc(wav->chunkSize);
		stream->prevData = linearAlloc(wav->chunkSize);
		stream->filePos = wav->filePos;
		stream->done = false;
		streaming[channel] = stream;
	}
	
	return ret;
}

void stopWav(int channel) {
	ndspChnWaveBufClear(channel);
}

void printWav(wavFile *wav) {
	printf(" -- WAV FILE --\n");
	printf("Sample Rate: %f\n", wav->rate);
	printf("Number of Channels: %d\n", wav->channels);
	
	switch (wav->encoding) {
		case NDSP_ENCODING_PCM8:
			printf("Encoding: NDSP_ENCODING _PCM8\n");
			break;
		case NDSP_ENCODING_PCM16:
			printf("Encoding: NDSP_ENCODING_PCM16\n");
			break;
		default:
			printf("!! Uncrecognized Encoding !!\n");
			break;
	};
	
	printf("Number of Samples: %d\n", wav->nSamples);
	printf("Audio data size: %d\n", wav->size);
	printf("Byte Per Sample: %d\n", wav->bytePerSample);
	printf("Stream Chunk Size: %d\n", wav->chunkSize);
	printf("Number of Samples per Chunk: %d\n", wav->chunkNSamples);
	printf("File Size: %d\n", wav->fileSize);
	printf("-- END PRINT --\n\n");
}

void updateChannels(void *arg) {
	threadStruct *t = (threadStruct*) arg;
	printf("thread is running\n");
	while (runSoundThread) {
		
	
	
	svcSleepThread(100 * 10000);
	svcWaitSynchronization(t->mutex, U64_MAX);
		
	for (int i = 0; i < 24; i++) {
		if (streaming[i] == NULL) {
			continue;
		}
		
		//printf("channel %d is not null\n", i);
		
		audioStream *stream = streaming[i];
		wavFile *wav = stream->audio;
		if (stream->done == true) {
			//printf("stream %d is done\n", i);
			continue;
		}
		
		//printf("wavebuf seq: %d, nextwavebuf seq: %d\n", ndspChnGetWaveBufSeq(i), stream->nextWaveBuf->sequence_id); 
		
		// next chunk has started to play, load it into memory
		if (stream->nextWaveBuf != NULL && ndspChnGetWaveBufSeq(i) == stream->nextWaveBuf->sequence_id) {
			stream->prevStartTime = stream->prevStartTime + (double) (wav->chunkNSamples) / wav->rate;
			
			if (!stream->eof) {
				//printf("loading next chunk into memory...\n");
				// swap these buffers, set nextData (about to play) to prevData (playing now)
				char *nextData = stream->nextData;
				char *prevData = stream->prevData;
				stream->prevData = nextData;
				stream->nextData = prevData;
				stream->prevWaveBuf = stream->nextWaveBuf;
				
				// recalculate chunk size and number of samples
				u32 chunkSize = fmin(wav->fileSize - stream->filePos, wav->chunkSize);
				u32 chunkNSamples = chunkSize / wav->channels / wav->bytePerSample;
				
				// read next chunk into memory (will play after this chunk is done playing)
				fseek(wav->file, stream->filePos, SEEK_SET);
				fread(stream->nextData, chunkSize, 1, wav->file);
				stream->filePos = ftell(wav->file);
				if (stream->filePos == wav->fileSize) {
					stream->eof = true;
				}
				
				//printf("filepos: %d\n", stream->filePos);
				
				// create new wavebuf
				ndspWaveBuf *wbuf = calloc(1, sizeof(ndspWaveBuf));
				wbuf->data_vaddr = stream->nextData;
				wbuf->nsamples = chunkNSamples;
				wbuf->looping = false;
				
				// flush that shit
				DSP_FlushDataCache((u32*)stream->nextData, chunkSize);
				ndspChnWaveBufAdd(i, wbuf);
				
				// set next wave buf
				stream->nextWaveBuf = wbuf;
				//printf("Next chunk loaded into memory.\n");
			}
		}
		
		//printf("EOF: %d\n", stream->eof);
		//if (stream->eof)
		//	svcSleepThread(2000000000);
		
		// it's NULL and not playing anymore, free this
		if (stream->prevWaveBuf != NULL && ndspChnGetWaveBufSeq(i) != stream->prevWaveBuf->sequence_id) {
			printf("Freeing old waveBuf...\n");
			free(stream->prevWaveBuf);
			stream->prevWaveBuf = NULL;
		}
		
		// we have reached the end of the file
		if (stream->prevWaveBuf == NULL && stream->nextWaveBuf != NULL && ndspChnGetWaveBufSeq(i) != stream->nextWaveBuf->sequence_id && stream->eof == true) {
			printf("sound is done playing\n");
			free(stream->nextWaveBuf);
			stream->nextWaveBuf = NULL;
			
			if (!stream->loop) {
				linearFree(stream->prevData);
				stream->prevData = NULL;
				
				linearFree(stream->nextData);
				stream->nextData = NULL;
				
				stream->done = true;
				printf("stream is now done\n");
			} else {
				ndspWaveBuf *wbuf = calloc(1, sizeof(ndspWaveBuf));
				
				wbuf->data_vaddr = wav->data;
				wbuf->nsamples = wav->chunkNSamples;
				wbuf->looping = false;
				
				DSP_FlushDataCache((u32*)wav->data, wav->chunkSize);
				ndspChnWaveBufAdd(i, wbuf);
				
				stream->prevStartTime = 0;
				stream->eof = false;
				stream->filePos = wav->filePos;
				stream->nextWaveBuf = wbuf;
			}
		}
		
		//printf("EOF not reached...\n");
	}
	
	svcReleaseMutex(t->mutex);
	
	}
}
#ifndef SOUND_H
#define SOUND_H

typedef struct {
	FILE* file;
	long fileSize;
	int filePos;
	
	float rate;
	u32 channels;
	u32 encoding;
	
	u32 nSamples;
	u32 size;
	char *data;
	
	u16 bytePerSample;
	u32 chunkSize;
	u32 chunkNSamples;
} wavFile;

typedef struct {
	wavFile *audio;
	bool loop;
	long filePos;
	
	double prevStartTime;
	bool eof;
	bool done;
	
	char *nextData;
	ndspWaveBuf *nextWaveBuf;
	
	char *prevData;
	ndspWaveBuf *prevWaveBuf;
} audioStream;

wavFile *channels[24];
audioStream *streaming[24];

Result loadWav(const char *path, wavFile *file, int streamChunkSize);
Result playWav(wavFile *file, int channel);

Result stopWav(int channel);
Result stopAll();

#endif
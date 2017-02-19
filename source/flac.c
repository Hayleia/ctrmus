#include <3ds.h>

#define DR_FLAC_IMPLEMENTATION
#include <./dr_libs/dr_flac.h>
#include "all.h"
#include "flac.h"

static drflac*		pFlac;
//static const int	buffSize = 16 * 1024;
static const int	buffSize = 8 * 1024;

/**
 * Set decoder parameters for flac.
 *
 * \param	decoder Structure to store parameters.
 */
void setFlac(struct decoder_fn* decoder)
{
	decoder->init = &initFlac;
	decoder->rate = &rateFlac;
	decoder->channels = &channelFlac;
	decoder->buffSize = buffSize;
	decoder->decode = &decodeFlac;
	decoder->exit = &exitFlac;
}

/**
 * Initialise Flac decoder.
 *
 * \param	file	Location of flac file to play.
 * \return			0 on success, else failure.
 */
int initFlac(const char* file)
{
	pFlac = drflac_open_file(file);

	return pFlac == NULL ? -1 : 0;
}

/**
 * Get sampling rate of Flac file.
 *
 * \return	Sampling rate.
 */
uint32_t rateFlac(void)
{
	return pFlac->sampleRate;
}

/**
 * Get number of channels of Flac file.
 *
 * \return	Number of channels for opened file.
 */
uint8_t channelFlac(void)
{
	return pFlac->channels;
}

/**
 * Decode part of open Flac file.
 *
 * \param buffer	Decoded output.
 * \return			Samples read for each channel.
 */
uint64_t decodeFlac(void* buffer)
{
	return drflac_read_s16(pFlac, buffSize, buffer);
}

/**
 * Free Flac decoder.
 */
void exitFlac(void)
{
	drflac_close(pFlac);
}

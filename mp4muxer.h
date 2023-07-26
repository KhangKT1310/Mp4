/*
 *  ============================================================================
 * File Name    : mp4_record.h
 * Project      : RTS3916
 * Author       : khangkt
 * Version      :
 * File Created : 06-02-2023  08:58:56:AM
 * AuthorEmail  : khangkt@fpt.com.vn
 * Copyright    : Your copyright notice
 *  ============================================================================
 */

#ifndef __MP4_RECORD_H__
#define __MP4_RECORD_H__

#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>

#define MTCE_MP4_FOTMAT_SAVE "%s/%s/%d/%02d/%02d/%02d"
#define MP4_H265 1
#define MP4_H264 0

typedef struct
{
	uint32_t rate;
	uint32_t bitrate;
	uint32_t format;

} mtce_mp4AudioAttr_t;

typedef struct
{
	int enc_type;
	int width;
	int height;
	int num_chn;
	int fps;
	uint32_t bitrate;
	int gop;

} mtce_mp4VideoAttr_t;


int mtce_mp4Init(char *rootPath);
int mtce_mp4SetVideoAttr(mtce_mp4VideoAttr_t *video_attr);
int mtce_mp4SetAudioAttr(mtce_mp4AudioAttr_t *audio_attr);
int mtce_mp4Start();
int mtce_mp4WriteAudio(uint8_t *data, int size, uint64_t timestamp);
int mtce_mp4WriteVideo(uint8_t *data, int size, uint64_t timestamp);
int mtce_mp4Stop();
int mtce_mp4Release();

#endif // __H__MP4_RECORD__
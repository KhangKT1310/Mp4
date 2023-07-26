

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <rtsvideo.h>

#include <pthread.h>

#include "mp4muxer.h"

bool enable_motion = false;

typedef struct
{
	char rootPath[128];
	char path[256];
	char newPath[512];
	char mkdirPath[512];

	int state;
} mtce_record_t;

typedef struct
{
	int time_count;
	struct tm *tm_info;
	time_t timer;

} mtce_TimeHandel_t;

typedef struct
{
	mtce_mp4AudioAttr_t audio;
	mtce_mp4VideoAttr_t video;
} mtce_mp4Attr_t;

typedef struct
{
	mtce_record_t record;
	AVBSFContext *bsf;

	AVFormatContext *ofmt_ctx;
	pthread_mutex_t mutex;
	int64_t v_start_time;
	int64_t a_start_time;
	int wait_IDR;
	AVRational timebase;

} mtce_mp4Handle_t;

typedef struct
{
	int mp4Record_chn;

	mtce_TimeHandel_t handel;
	mtce_mp4Attr_t attr;
	mtce_mp4Handle_t hdn;
} mtce_mp4Context_t;

enum
{
	MP4_STATE_NONE = -1,
	MP4_STATE_INIT,
	MP4_STATE_START,
	MP4_STATE_OPEN,
	MP4_STATE_CLOSE,
	MP4_STATE_RELEASE,
};

static mtce_mp4Context_t this;

/* audio codec */
static int _acodecInit(AVCodecContext *codec);
static int _acodecCopy(AVCodecContext *i_codec, AVStream *stream);
static AVStream *get_audio_stream(AVFormatContext *ctx);

/* video codec */
static int _vcodecInit(AVCodecContext *codec);
static int _vcodecCopy(AVCodecContext *i_codec, AVStream *stream);
static AVStream *get_video_stream(AVFormatContext *ctx);

static int _mp4Codec_Init(AVCodecContext *v_codec, AVCodecContext *a_codec);
static int _mp4CreateFile();
static int _mp4Open();
static int _mp4Write(AVPacket *packet, enum AVMediaType codec_type);

int mtce_mp4Init(char *rootPath)
{
	memset(&this, 0, sizeof(this));
	this.hdn.record.state = MP4_STATE_NONE;

	if (rootPath)
	{
		memset(this.hdn.record.rootPath, 0, sizeof(this.hdn.record.rootPath));
		memcpy(this.hdn.record.rootPath, rootPath, strlen(rootPath));
	}

	this.hdn.record.state = MP4_STATE_INIT;

	return 0;
}

int mtce_mp4Start()
{

	if (this.hdn.record.state == MP4_STATE_CLOSE || this.hdn.record.state == MP4_STATE_INIT)
	{
		// Input AVFormatContext and Output AVFormatContext

		int ret = 0;
		this.handel.time_count = 0;
		this.hdn.timebase = AV_TIME_BASE_Q;
		AVCodecContext v_codec, a_codec;

		// printf("audio rate %d \n", this.attr.audio.rate);
		// printf("audio bitrate %d \n", this.attr.audio.bitrate);
		// printf("audio FM %d \n", this.attr.audio.format);

		// printf("video enc type %d \n", this.attr.video.enc_type);
		// printf("video w %d \n", this.attr.video.width);
		// printf("video h %d \n", this.attr.video.height);
		// printf("video fps %d \n", this.attr.video.fps);
		// printf("video num chnn %d \n", this.attr.video.num_chn);
		// printf("video BR %d \n", this.attr.video.bitrate);
		// printf("video GOP %d \n", this.attr.video.gop);

		ret = _acodecInit(&a_codec);
		if (ret != 0)
		{
			printf("_acodecInit failed\n");
		}

		ret = _vcodecInit(&v_codec);
		if (ret != 0)
		{
			printf("_vcodecInit failed\n");
		}
		ret = _mp4Codec_Init(&v_codec, &a_codec);
		if (ret != 0)
		{
			printf("_mp4Codec_Init failed\n");
		}

		this.hdn.record.state = MP4_STATE_START;
		return 0;
	}

	return -1;
}

int mtce_mp4Stop()
{
	if (this.hdn.record.state == MP4_STATE_OPEN)
	{
		if (this.hdn.ofmt_ctx == NULL)
			return 0;

		pthread_mutex_lock(&this.hdn.mutex);

		if (this.hdn.ofmt_ctx && this.hdn.ofmt_ctx->pb)
		{
			av_write_trailer(this.hdn.ofmt_ctx);
			avio_close(this.hdn.ofmt_ctx->pb);
			this.hdn.ofmt_ctx->pb = NULL;
		}
		pthread_mutex_unlock(&this.hdn.mutex);
		this.hdn.record.state = MP4_STATE_CLOSE;
	}
	return 0;
}

int mtce_mp4Release()
{
	if (this.hdn.ofmt_ctx == NULL)
		return 0;

	pthread_mutex_lock(&this.hdn.mutex);

	if (this.hdn.bsf)
		av_bsf_free(&this.hdn.bsf);

	this.hdn.bsf = NULL;

	if (this.hdn.ofmt_ctx)
	{
		avformat_free_context(this.hdn.ofmt_ctx);
		this.hdn.ofmt_ctx = NULL;
	}
	pthread_mutex_unlock(&this.hdn.mutex);
	pthread_mutex_destroy(&this.hdn.mutex);

	this.hdn.record.state = MP4_STATE_RELEASE;
	printf("Release mp4 \n");

	return 0;
}

int mtce_mp4WriteAudio(uint8_t *data, int size, uint64_t timestamp)
{
	int ret = 0;
	AVPacket packet;
	av_init_packet(&packet);
	packet.data = data;
	packet.pts = timestamp;
	packet.dts = timestamp;
	packet.size = size;

	ret = _mp4Write(&packet, AVMEDIA_TYPE_AUDIO);
	if (ret < 0)
	{
		printf("write audio failed\n");
	}

	av_free_packet(&packet);
	return 0;
}

int mtce_mp4WriteVideo(uint8_t *data, int size, uint64_t timestamp)
{
	int ret = 0;
	AVPacket packet;

	if (this.hdn.record.state == MP4_STATE_START)
	{
		ret = _mp4CreateFile();
		if (ret != 0)
		{
			printf("Error: mp4 create file failed.\n");

			return ret;
		}
		ret = _mp4Open();
		if (ret != 0)
		{
			printf("Error: mp4 open file failed.\n");
			mtce_mp4Release();
			return ret;
		}
	}

	av_init_packet(&packet);
	packet.data = data;
	packet.pts = timestamp;
	packet.dts = timestamp;
	packet.size = size;
	packet.flags |= AV_PKT_FLAG_KEY;

	ret = _mp4Write(&packet, AVMEDIA_TYPE_VIDEO);
	if (ret < 0)
	{
		printf("write video failed\n");
	}

	av_free_packet(&packet);

	return ret;
}

int mtce_mp4SetAudioAttr(mtce_mp4AudioAttr_t *audio_attr)
{
	memset(&this.attr.audio, 0, sizeof(this.attr.audio));
	/* audio */
	this.attr.audio.rate = audio_attr->rate;
	this.attr.audio.format = audio_attr->format;
	this.attr.audio.bitrate = audio_attr->bitrate;

	return 0;
}

int mtce_mp4SetVideoAttr(mtce_mp4VideoAttr_t *video_attr)
{
	memset(&this.attr.video, 0, sizeof(this.attr.video));
	/* video */
	this.attr.video.width = video_attr->width;
	this.attr.video.height = video_attr->height;
	this.attr.video.fps = video_attr->fps;
	this.attr.video.bitrate = video_attr->bitrate;
	this.attr.video.enc_type = video_attr->enc_type;
	this.attr.video.num_chn = video_attr->num_chn;
	this.attr.video.gop = video_attr->gop;
	return 0;
}
/**********************************************************************************************************************************
 *private function
 *
 ***********************************************************************************************************************************/

static int _mp4Codec_Init(AVCodecContext *v_codec, AVCodecContext *a_codec)
{
	// Input AVFormatContext and Output AVFormatContext
	int ret;
	AVFormatContext *ofmt_ctx = NULL;
	AVOutputFormat *oformat = NULL;
	AVStream *stream = NULL;
	const char *muxer = "mp4";
	this.hdn.bsf = NULL;
	this.hdn.ofmt_ctx = NULL;
	this.hdn.v_start_time = AV_NOPTS_VALUE;
	this.hdn.a_start_time = AV_NOPTS_VALUE;
	this.hdn.wait_IDR = 1;
	if (v_codec == NULL && a_codec == NULL)
		return -1;
	avcodec_register_all();
	av_register_all();
	oformat = av_guess_format(muxer, NULL, "");
	if (!oformat)
	{
		printf("cat not get muxer for:%s\n", muxer);
		return -1;
	}
	ret = avformat_alloc_output_context2(&ofmt_ctx, oformat,
										 NULL, NULL);

	if (ret < 0)
		goto fail;
	do
	{
		if (!v_codec)
			break;
		/* codec may be NULL */
		stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!stream)
		{
			printf("alloc stream fail\n");
			goto fail;
		}
		stream->id = ofmt_ctx->nb_streams - 1;
		ret = _vcodecCopy(v_codec, stream);
		stream->time_base = v_codec->time_base;
		stream->avg_frame_rate.num = v_codec->framerate.num;
		stream->avg_frame_rate.den = 1;
		stream->r_frame_rate = stream->avg_frame_rate;
		if (this.attr.video.enc_type == MP4_H265)
		{
			ofmt_ctx->video_codec_id = AV_CODEC_ID_H265;
		}
		else if (this.attr.video.enc_type == MP4_H264)
		{
			ofmt_ctx->video_codec_id = AV_CODEC_ID_H264;
		}
		if (ret)
		{
			printf("copy codec params fail\n");
			goto fail;
		}
	} while (0);

	do
	{
		if (!a_codec)
			break;
		/* codec may be NULL */
		stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!stream)
		{
			printf("alloc stream fail\n");
			goto fail;
		}
		stream->id = ofmt_ctx->nb_streams - 1;
		ret = _acodecCopy(a_codec, stream);
		if (ret)
		{
			printf("copy codec params fail\n");
			goto fail;
		}
		stream->time_base = v_codec->time_base;
		ofmt_ctx->audio_codec_id = AV_CODEC_ID_AAC;
		AVBitStreamFilter *aac_bsf = (AVBitStreamFilter *)av_bsf_get_by_name("aac_adtstoasc");
		if (!aac_bsf)
		{
			printf("get bsf aac_adtstoasc fail\n");
			goto fail;
		}
		ret = av_bsf_alloc(aac_bsf, &this.hdn.bsf);
		if (ret)
		{
			printf("alloc bsf fail\n");
			goto fail;
		}
	} while (0);
	do
	{
		int64_t fflags = 0;
		av_opt_get_int(ofmt_ctx, "fflags", 0, &fflags);
		fflags &= ~((int64_t)AVFMT_FLAG_FLUSH_PACKETS);
		av_opt_set_int(ofmt_ctx, "fflags", fflags, 0);
	} while (0);
	this.hdn.ofmt_ctx = ofmt_ctx;
	return 0;
fail:

	if (this.hdn.bsf)
		av_bsf_free(&this.hdn.bsf);
	this.hdn.bsf = NULL;

	if (ofmt_ctx)
		avformat_free_context(ofmt_ctx);

	return -1;
}

static int _mp4CreateFile()
{
	// int status = -1;
	// char *option;
	// char file_ext[] = ".mp4";

	// if (enable_motion)
	// {
	// 	option = "Motion";
	// }
	// else
	// {
	// 	option = "Regular";
	// }
	// snprintf(this.hdn.record.path, sizeof(this.hdn.record.path), MTCE_MP4_FOTMAT_SAVE, this.hdn.record.rootPath, option,
	// 		 1900 + this.handel.tm_info->tm_year, this.handel.tm_info->tm_mon + 1, this.handel.tm_info->tm_mday,
	// 		 this.handel.tm_info->tm_hour);
	// printf("[%s]",this.hdn.record.path);
	// snprintf(this.hdn.record.mkdirPath, sizeof(this.hdn.record.mkdirPath), "mkdir -p %s", this.hdn.record.path);

	// if (this.handel.tm_info->tm_min != this.handel.time_count)
	// {
	// 	status = system(this.hdn.record.mkdirPath);
	// 	if (status == 0)
	// 	{
	// 		printf("\n************************************ Mp4 Information ************************************\n");
	// 	}
	// 	else
	// 	{
	// 		printf("Unable to create directory.\n");
	// 		return -1;
	// 	}
	// 	snprintf(this.hdn.record.newPath, sizeof(this.hdn.record.newPath), "%s/%02d_%02d%s", this.hdn.record.path,
	// 			 this.handel.tm_info->tm_min, this.handel.tm_info->tm_sec, file_ext);

	// 	this.handel.time_count = this.handel.tm_info->tm_min;
	// }
	memset(this.hdn.record.newPath, 0, sizeof(this.hdn.record.newPath));
	strcpy(this.hdn.record.newPath, "/tmp/nfs-client/khangkt/file.mp4");
	printf("File name: %s\n", this.hdn.record.newPath);

	return 0;
}

static int _mp4Open()
{
	int ret = 0;

	av_dump_format(this.hdn.ofmt_ctx, 0, 0, 1);
	// Open output file
	if (!(this.hdn.ofmt_ctx->flags & AVFMT_NOFILE))
	{
		if (avio_open(&this.hdn.ofmt_ctx->pb, this.hdn.record.newPath, AVIO_FLAG_WRITE) < 0)
		{
			printf("Could not open output file '%s'", this.hdn.record.newPath);
			this.hdn.record.state = MP4_STATE_CLOSE;
			goto fail;
		}
	}
	// // Write file header
	if ((ret = avformat_write_header(this.hdn.ofmt_ctx, NULL)) < 0)
	{
		printf("Error occurred when opening output file %d\n", ret);
		this.hdn.record.state = MP4_STATE_CLOSE;
		goto fail;
	}
	// printf("*****************************************************************************************\n\n");

	pthread_mutex_init(&this.hdn.mutex, NULL);
	this.hdn.record.state = MP4_STATE_OPEN;
	return 0;
fail:
	// 	if (this.hdn.ofmt_ctx && this.hdn.ofmt_ctx->pb)
	// 		avio_close(this.hdn.ofmt_ctx->pb);

	// 	if (this.hdn.bsf)
	// 		av_bsf_free(&this.hdn.bsf);
	// 	this.hdn.bsf = NULL;

	// 	if (this.hdn.ofmt_ctx)
	// 		avformat_free_context(this.hdn.ofmt_ctx);

	return -1;
}

static int _mp4Write(AVPacket *packet, enum AVMediaType codec_type)
{
	int ret = 0;
	if (this.hdn.record.state == MP4_STATE_OPEN)
	{
		pthread_mutex_lock(&this.hdn.mutex);

		AVStream *out_stream = NULL;

		if (this.hdn.ofmt_ctx == NULL)
		{
			pthread_mutex_unlock(&this.hdn.mutex);
			return -2;
		}

		if (codec_type == AVMEDIA_TYPE_VIDEO)
		{
			out_stream = get_video_stream(this.hdn.ofmt_ctx);

			if (AV_NOPTS_VALUE == this.hdn.v_start_time)
			{
				if (AV_NOPTS_VALUE != packet->pts)
				{
					this.hdn.v_start_time = packet->pts;
				}
				else if (AV_NOPTS_VALUE != packet->dts)
				{
					this.hdn.v_start_time = packet->dts;
				}
				else
				{
					printf("Invalid PTS and DTS\n");
					return -3;
				}
			}

			packet->pts -= this.hdn.v_start_time;
			packet->pts = av_rescale_q(packet->pts,
									   this.hdn.timebase, out_stream->time_base);
			packet->dts = packet->pts;

			packet->stream_index = out_stream->id;

			if (!this.hdn.wait_IDR)
			{
				ret = av_interleaved_write_frame(this.hdn.ofmt_ctx, packet);
			}
			else
			{
				if (packet->flags & AV_PKT_FLAG_KEY)
				{
					this.hdn.wait_IDR = 0;
					ret = av_interleaved_write_frame(this.hdn.ofmt_ctx, packet);
				}
			}
		}
		else
		{
			out_stream = get_audio_stream(this.hdn.ofmt_ctx);

			if (AV_NOPTS_VALUE == this.hdn.a_start_time)
			{
				if (AV_NOPTS_VALUE != packet->pts)
				{
					this.hdn.a_start_time = packet->pts;
				}
				else if (AV_NOPTS_VALUE != packet->dts)
				{
					this.hdn.a_start_time = packet->dts;
				}
				else
				{
					printf("Invalid PTS and DTS\n");
					return -4;
				}
			}
			packet->pts -= this.hdn.a_start_time;
			packet->pts = av_rescale_q(packet->pts,
									   this.hdn.timebase, out_stream->time_base);
			packet->dts = packet->pts;
			packet->stream_index = out_stream->id;

			if (this.hdn.bsf)
			{
				ret = av_bsf_send_packet(this.hdn.bsf, packet);
				if (ret < 0)
				{
					printf("bsf send fail\n");
					return -5;
				}

				ret = av_bsf_receive_packet(this.hdn.bsf, packet);
				if (ret < 0)
				{
					printf("bsf receive fail\n");
					return -6;
				}
			}
			ret = av_interleaved_write_frame(this.hdn.ofmt_ctx, packet);
		}

		pthread_mutex_unlock(&this.hdn.mutex);
	}
	return ret;
}

static int _acodecInit(AVCodecContext *codec)
{
	memset(codec, 0, sizeof(*codec));
	codec->codec_type = AVMEDIA_TYPE_AUDIO;
	codec->sample_rate = this.attr.audio.rate;
	codec->codec_id = AV_CODEC_ID_AAC;
	codec->channels = 1;
	codec->extradata = NULL;
	codec->extradata_size = 0;
	codec->time_base = this.hdn.timebase;
	codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	codec->sample_fmt = 1;
	codec->frame_size = 1024;
	codec->delay = 22;
	return 0;
}

static AVStream *get_audio_stream(AVFormatContext *ctx)
{
	int i = 0;

	for (i = 0; i < ctx->nb_streams; i++)
	{
		if (AVMEDIA_TYPE_AUDIO == ctx->streams[i]->codecpar->codec_type)
			return ctx->streams[i];
	}

	return NULL;
}

static int _acodecCopy(AVCodecContext *i_codec, AVStream *stream)
{

	AVCodecParameters *o_codec = stream->codecpar;
	uint64_t extra_size = (uint64_t)i_codec->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE;

	o_codec->codec_id = i_codec->codec_id;
	o_codec->codec_type = i_codec->codec_type;
	o_codec->bit_rate = i_codec->bit_rate;
	o_codec->extradata = av_mallocz(extra_size);
	if (!(o_codec->extradata))
		return -ENOMEM;
	if (i_codec->extradata_size > 0)
		memcpy(o_codec->extradata, i_codec->extradata, i_codec->extradata_size);
	o_codec->extradata_size = i_codec->extradata_size;
	o_codec->bits_per_coded_sample = i_codec->bits_per_coded_sample;
	o_codec->bits_per_raw_sample = i_codec->bits_per_raw_sample;
	o_codec->channel_layout = i_codec->channel_layout;
	o_codec->sample_rate = i_codec->sample_rate;
	o_codec->channels = i_codec->channels;
	o_codec->frame_size = i_codec->frame_size;
	o_codec->block_align = i_codec->block_align;

	return 0;
}

static int _vcodecInit(AVCodecContext *codec)
{
	char *extradata = NULL;
	int extradata_size = 0;
	memset(codec, 0, sizeof(*codec));

	if (this.attr.video.enc_type == MP4_H264)
	{
		struct rts_h264_info h264_info;
		rts_av_get_h264_mediainfo(this.attr.video.num_chn, &h264_info);
		extradata_size = h264_info.sps_pps_len;
		extradata = h264_info.sps_pps;
		printf("H264 extradata_size %d\n", extradata_size);
		printf("H264 extradata %s\n", extradata);
		codec->codec_id = AV_CODEC_ID_H264;
	}
	else if (this.attr.video.enc_type == MP4_H265)
	{
		struct rts_h265_info h265_info;
		rts_av_get_h265_mediainfo(this.attr.video.num_chn, &h265_info);
		extradata_size = h265_info.vps_sps_pps_len;
		extradata = h265_info.vps_sps_pps;
		printf("H265 extradata_size %d\n",
		
		 extradata_size);
		printf("H265 extradata %s\n", extradata);
		codec->codec_id = AV_CODEC_ID_H265;
	}

	codec->codec_type = AVMEDIA_TYPE_VIDEO;
	codec->width = this.attr.video.width;
	codec->height = this.attr.video.height;
	codec->framerate.num = this.attr.video.fps;
	codec->bit_rate = this.attr.video.bitrate;
	codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	/* emit one intra frame every twelve frames at most */
	codec->gop_size = this.attr.video.gop;
	codec->qmin = 20;
	codec->qmax = 50;

	codec->time_base = this.hdn.timebase;
	codec->extradata = NULL;
	codec->extradata_size = 0;

	codec->extradata = av_malloc(extradata_size);
	memcpy(codec->extradata,
		   extradata,
		   extradata_size);
	codec->extradata_size = extradata_size;

	return 0;
}

int _vcodecCopy(AVCodecContext *i_codec, AVStream *stream)
{

	AVCodecParameters *o_codec = stream->codecpar;
	uint64_t extra_size = (uint64_t)i_codec->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE;

	o_codec->codec_id = i_codec->codec_id;
	o_codec->codec_type = i_codec->codec_type;
	o_codec->bit_rate = i_codec->bit_rate;
	o_codec->extradata = av_mallocz(extra_size);
	if (!(o_codec->extradata))
		return -ENOMEM;
	if (i_codec->extradata_size > 0)
		memcpy(o_codec->extradata, i_codec->extradata, i_codec->extradata_size);
	o_codec->extradata_size = i_codec->extradata_size;
	o_codec->bits_per_coded_sample = i_codec->bits_per_coded_sample;
	o_codec->width = i_codec->width;
	o_codec->height = i_codec->height;
	return 0;
}

static AVStream *get_video_stream(AVFormatContext *ctx)
{
	int i = 0;

	for (i = 0; i < ctx->nb_streams; i++)
	{
		if (AVMEDIA_TYPE_VIDEO == ctx->streams[i]->codecpar->codec_type)
			return ctx->streams[i];
	}

	return NULL;
}

//
// Created by 4dage on 2020/3/16.
//
#include "log.h"
#include "AndroidBuf.h"
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"

/**
 * @data 数据开始指针
 * @length 数据长度
 * @width 视频宽度
 * @height 视频高度
 */
typedef void (*YUVDataHandle)(char *data, int length, int width, int height);
YUVDataHandle _Android_YuvDataHandle;

typedef void (*LiveErrorCallback)(int code);
LiveErrorCallback recorder_callback = NULL;

static int ERROR_OUTPUT = -1;

static AVFormatContext *i_fmt_ctx = NULL;
static AVStream *i_video_stream = NULL;

static AVFormatContext *o_fmt_ctx;
static AVStream *o_video_stream;
static AVOutputFormat *oformat;

static FILE *fp = NULL;
static bool shouldRecording = false; // 是否录制视频流
static bool shouldStopDecode = false; // 是否继续解码解码标志位
static bool isMp4Recording = true; // 是否编码为mp4视频流
static bool isH264Replay = false; // 是否H264视频流回放
static bool isH264Encoding = false; // 是否H264视频流回放

void checkWriteError(size_t writeCount) {
    if (writeCount == 0) {
        if (shouldRecording) {
            recorder_callback(4);
        }
        shouldRecording = false;
    }
}

void endRecordMp4() {
    av_write_trailer(o_fmt_ctx);
    av_freep(&o_fmt_ctx->streams[0]->codecpar);
    av_freep(&o_fmt_ctx->streams[0]);

    avio_close(o_fmt_ctx->pb);
    av_free(o_fmt_ctx);
}

void
live_recording_start(const char *file_name, int archive, LiveErrorCallback live_error_callback) {
    recorder_callback = live_error_callback;
    isMp4Recording = archive == 1;
    if (!isMp4Recording) {
        fp = fopen(file_name, "wb");
        shouldRecording = true;
        return;
    }

    oformat = av_guess_format(NULL, file_name, NULL);

    if (avformat_alloc_output_context2(&o_fmt_ctx, oformat, NULL, file_name) != 0) {
        LOGW("初始化o_fmt_ctx结构体失败\n");
        recorder_callback(ERROR_OUTPUT);
        return;
    }

    /*
    * since all input files are supposed to be identical (framerate, dimension, color format, ...)
    * we can safely set output codec values from first input file
    */
    o_video_stream = avformat_new_stream(o_fmt_ctx, NULL);
    {
        AVCodecParameters *c;
        c = o_video_stream->codecpar;
        c->bit_rate = 400000;
        c->codec_id = i_video_stream->codecpar->codec_id;
        c->codec_type = i_video_stream->codecpar->codec_type;

        c->sample_aspect_ratio.num = i_video_stream->sample_aspect_ratio.num;
        c->sample_aspect_ratio.den = i_video_stream->sample_aspect_ratio.den;

        c->extradata = i_video_stream->codecpar->extradata;
        c->extradata_size = i_video_stream->codecpar->extradata_size;

        c->width = i_video_stream->codecpar->width;
        c->height = i_video_stream->codecpar->height;
        c->format = i_video_stream->codecpar->format;

        o_video_stream->r_frame_rate = i_video_stream->r_frame_rate;
    }

    // 列出输出文件的相关流信息
    LOGW("------------------- 输出文件信息 ------------------\n");
    av_dump_format(o_fmt_ctx, 0, file_name, 1);
    LOGW("-------------------------------------------------\n");

    avio_open(&o_fmt_ctx->pb, file_name, AVIO_FLAG_WRITE);

    if (!(o_fmt_ctx->flags & AVFMT_NOFILE)) {

    }

    if (!o_fmt_ctx->nb_streams) {
        LOGW("output file dose not contain any stream\n");
        recorder_callback(ERROR_OUTPUT);
        return;
    }

    // 根据文件名的后缀写相应格式的文件头
    if (avformat_write_header(o_fmt_ctx, NULL) < 0) {
        LOGW("Could not write header for output file\n");
        recorder_callback(ERROR_OUTPUT);
        return;
    }

    shouldRecording = true;
    LOGW("------------------- 开始录视频 ------------------\n");
}

void live_recording_end() {
    if (shouldRecording) {

        shouldRecording = false;
        if (isMp4Recording) {
            endRecordMp4();
        } else {
            fclose(fp);
            fp = NULL;
        }
    }
}

void
start_decode(const char *file_name, YUVDataHandle callback) {
    _Android_YuvDataHandle = callback;

    shouldStopDecode = false;
    std::cout << "FfmpegTag start play, shouldStopDecode: " << shouldStopDecode << std::endl;

    LOGD("FfmpegTag start playvideo... url, %s", file_name);
    av_register_all();

    i_fmt_ctx = avformat_alloc_context();

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);   //采用tcp传输
    //    av_dict_set(&opts, "stimeout", "10000000", 0);   //设置超时10秒

    // Open video file
    if (int err_code = avformat_open_input(&i_fmt_ctx, file_name, NULL, &opts) != 0) {
        LOGE("FfmpegTag err_code : %d\n", err_code);
        LOGE("FfmpegTag Couldn't open file : %s \n", file_name);
        return; // Couldn't open file
    }

    i_fmt_ctx->probesize = 1 * 1024; // 缩减探测数据尺寸，减少探测时间，优化首开延迟
    i_fmt_ctx->max_analyze_duration = 1 * AV_TIME_BASE;

    // Retrieve stream information
    if (avformat_find_stream_info(i_fmt_ctx, NULL) < 0) {
        LOGE("FfmpegTag Couldn't find stream information.");
        return;
    }

    // Find the first video stream
    int videoStream = -1, i;
    for (i = 0; i < i_fmt_ctx->nb_streams; i++) {
        if (i_fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO
            && videoStream < 0) {
            videoStream = i;
            i_video_stream = i_fmt_ctx->streams[i];
        }
    }
    if (videoStream == -1) {
        LOGE("FfmpegTag Didn't find a video stream.");
        return; // Didn't find a video stream
    }

    // Get a pointer to the codec context for the video stream
    AVCodecContext *pCodecCtx = i_fmt_ctx->streams[videoStream]->codec;

    // Find the decoder for the video stream
    AVCodec *pCodec = avcodec_find_decoder(i_fmt_ctx->streams[videoStream]->codecpar->codec_id);
    //    AVCodec *pCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
    std::cout << "FfmpegTag codec_id : " << pCodecCtx->codec_id << std::endl;
    std::cout << "FfmpegTag AVCodec name : " << pCodec->name << std::endl;


    if (pCodec == NULL) {
        LOGE("Codec not found.");
        return; // Codec not found
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE("Could not open codec.");
        return; // Could not open codec
    }

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;

    std::cout << "videoWidth : " << videoWidth << std::endl;
    std::cout << "videoHeight : " << videoHeight << std::endl;

    AVFrame *pFrame = av_frame_alloc();

    // 用于渲染
    //    AVFrame *pFrameRGBA = av_frame_alloc();
    AVFrame *pFrameYUV = av_frame_alloc();
    if (pFrameYUV == NULL || pFrame == NULL) {
        LOGE("Could not allocate video frame.");
        return;
    }

    // Determine required buffer size and allocate buffer
    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P,
                         pCodecCtx->width, pCodecCtx->height, 1);
    //AV_PIX_FMT_RGBA

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width,
                                                pCodecCtx->height,
                                                pCodecCtx->pix_fmt,
                                                pCodecCtx->width,
                                                pCodecCtx->height,
                                                AV_PIX_FMT_YUV420P,
                                                SWS_BILINEAR,
                                                NULL,
                                                NULL,
                                                NULL);

    LOGD("AV_PIX_FMT_YUV420P SWS_BILINEAR");
    int frameFinished;
    AVPacket packet;
    AVPacket *p;

    int last_pts = 0;
    int last_dts = 0;

    int64_t pts, dts;

    int retryDecodeCount = 10;
    int tryCount = 0;
    size_t writeCount = 0;
    int read_frame_result = av_read_frame(i_fmt_ctx, &packet);
    while (read_frame_result >= 0 && tryCount < retryDecodeCount) {
        if (shouldRecording) {
            if (isMp4Recording) {
                p = av_packet_clone(&packet);
                /*
                 * pts and dts should increase monotonically
                 * pts should be >= dts
                 */
                p->flags |= AV_PKT_FLAG_KEY;
                pts = p->pts;
                p->pts += last_pts;
                dts = p->dts;
                p->dts += last_dts;
                p->stream_index = 0;
                static int num = 1;
                LOGD("write frame %d\n", num++);

                av_interleaved_write_frame(o_fmt_ctx, p);
            } else {
                if (fp != NULL) {
                    fwrite((&packet)->data, 1, (p)->size, fp);
                    //LOGD("packet->size : %d", (&packet)->size);
                }
            }

        }

        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {

            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // 并不是decode一次就可解码出一帧
            if (frameFinished) {

                // 格式转换
                sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height,
                          pFrameYUV->data, pFrameYUV->linesize);

                int pic_size = pCodecCtx->width * pCodecCtx->height;


                int newSize = pic_size * 1.5;

                char *s = new char[newSize];
                //写入数据
                memcpy(s, pFrame->data[0], pic_size); // 写入Y
                memcpy(s + pic_size, pFrame->data[1], pic_size / 4); // 写入U
                memcpy(s + pic_size * 5 / 4, pFrame->data[2], pic_size / 4); // 写入V

                _Android_YuvDataHandle(s, newSize, videoWidth, videoHeight);
                if (strlen(s) > 0) {}

                delete[] s;
                if (shouldStopDecode) {
                    break;
                }
            }

        }
        av_packet_unref(&packet);
        read_frame_result = av_read_frame(i_fmt_ctx, &packet);
        if (read_frame_result < 0) {
            tryCount++;
            sleep(500);
        } else if (tryCount != 0) {
            tryCount = 0;
        }

    }

    last_dts += dts;
    last_pts += pts;

    av_free(buffer);
    av_free(pFrameYUV);

    // Free the YUV frame
    av_free(pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);

    // Close the video file
    avformat_close_input(&i_fmt_ctx);

    if (shouldRecording) {
        if (isMp4Recording) {
            endRecordMp4();
        } else {
            fclose(fp);
        }
    }

    shouldStopDecode = false;
    std::cout << "FfmpegTag shouldStopDecode : " << shouldStopDecode << std::endl;
    std::cout << "FfmpegTag stop play." << std::endl;
}

void end_decode() {
    shouldStopDecode = true;
}

//回放H264视频流
void start_rebroadcast(const char *file , YUVDataHandle callback){
    isH264Replay = true;
    isH264Encoding = true;
    start_decode(file, callback);
}

void pause_rebroadcast(){
    isH264Encoding = false;
}

void resume_rebroadcast(){
    isH264Encoding = true;
}

void stop_rebroadcast(){
    end_decode();
    isH264Replay = false;
    isH264Encoding = false;
}

}

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

    static bool shouldStopDecode = false; // 是否继续解码解码标志位

    typedef void (*YUVDataHandle)(char *data, int length, int width, int height);
    YUVDataHandle _Android_YuvDataHandle;

    void
    start_decode(const char *file_name, YUVDataHandle callback) {
        _Android_YuvDataHandle = callback;

        shouldStopDecode = false;
        std::cout << "FfmpegTag start play, shouldStopDecode: " << shouldStopDecode << std::endl;

        LOGD("FfmpegTag start playvideo... url, %s", file_name);
        av_register_all();

        AVFormatContext *pFormatCtx = avformat_alloc_context();

        AVDictionary *opts = NULL;
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);   //采用tcp传输
    //    av_dict_set(&opts, "stimeout", "10000000", 0);   //设置超时10秒

        // Open video file
        if (int err_code = avformat_open_input(&pFormatCtx, file_name, NULL, &opts) != 0) {
            LOGE("FfmpegTag err_code : %d\n", err_code);
            LOGE("FfmpegTag Couldn't open file : %s \n", file_name);
            return; // Couldn't open file
        }

        pFormatCtx->probesize = 1 * 1024; // 缩减探测数据尺寸，减少探测时间，优化首开延迟
        pFormatCtx->max_analyze_duration = 1 * AV_TIME_BASE;

        // Retrieve stream information
        if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
            LOGE("FfmpegTag Couldn't find stream information.");
            return;
        }

        // Find the first video stream
        int videoStream = -1, i;
        for (i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO
                && videoStream < 0) {
                videoStream = i;
            }
        }
        if (videoStream == -1) {
            LOGE("FfmpegTag Didn't find a video stream.");
            return; // Didn't find a video stream
        }

        // Get a pointer to the codec context for the video stream
        AVCodecContext *pCodecCtx = pFormatCtx->streams[videoStream]->codec;

        // Find the decoder for the video stream
        AVCodec *pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
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
        int retryDecodeCount = 10;
        int tryCount = 0;
        int read_frame_result = av_read_frame(pFormatCtx, &packet);
        while (read_frame_result >= 0 && tryCount < retryDecodeCount) {
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
            read_frame_result = av_read_frame(pFormatCtx, &packet);
            if (read_frame_result < 0) {
                tryCount++;
                sleep(500);
            } else if (tryCount != 0) {
                tryCount = 0;
            }

        }

        av_free(buffer);
        av_free(pFrameYUV);

        // Free the YUV frame
        av_free(pFrame);

        // Close the codecs
        avcodec_close(pCodecCtx);

        // Close the video file
        avformat_close_input(&pFormatCtx);

        shouldStopDecode = false;
        std::cout << "FfmpegTag shouldStopDecode : " << shouldStopDecode << std::endl;
        std::cout << "FfmpegTag stop play." << std::endl;
    }

    void end_decode() {
        shouldStopDecode = true;
    }
}

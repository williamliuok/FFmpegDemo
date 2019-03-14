#include "log.h"
#include "AndroidBuf.h"
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <iostream>

#ifdef __cplusplus             //告诉编译器，这部分代码按C语言的格式进行编译，而不是C++的
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#ifdef __cplusplus
}
#endif

const char *METHOD_UPDATE_FRAME_DATA = "updateFrameData";
const char *METHOD_SIG_UPDATE_FRAME_DATA = "([B)V";

static bool shouldStopDecode = false; // 是否继续解码解码标志位

void
callNative(JNIEnv *env, jobject thiz, const char *methodName, const char *methodSig, char *bytes,
           int length) {
    jclass clazz = env->GetObjectClass(thiz);//获取该对象的类
    jmethodID m_mid = env->GetMethodID(clazz, methodName, methodSig);//获取JAVA方法的ID
    jbyteArray RtnArr = env->NewByteArray(length);
    env->SetByteArrayRegion(RtnArr, 0, length, (jbyte *) bytes);
    env->CallVoidMethod(thiz, m_mid, RtnArr);
    env->DeleteLocalRef(RtnArr); // 清除LocalRef中的引用，防止native memory的内存泄漏
    env->DeleteLocalRef(clazz); // 清除LocalRef中的引用，防止native memory的内存泄漏
}

extern "C"
JNIEXPORT void JNICALL
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_stopFFmpegDecode(JNIEnv *env, jobject instance) {
    shouldStopDecode = true;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_nativeInit(JNIEnv *env, jclass type) {
    std::cout.rdbuf(new AndroidBuf);// 打印输出流重定向至Android打印流
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_playVideo(JNIEnv *env, jobject instance, jstring url_
//                                                    jobject surface
) {
    shouldStopDecode = false;
    std::cout << "FfmpegTag start play, shouldStopDecode: " << shouldStopDecode << std::endl;
    const char *file_name = env->GetStringUTFChars(url_, 0);

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
        return -1; // Couldn't open file
    }

    pFormatCtx->probesize = 1 * 1024; // 缩减探测数据尺寸，减少探测时间，优化首开延迟
    pFormatCtx->max_analyze_duration = 1 * AV_TIME_BASE;

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("FfmpegTag Couldn't find stream information.");
        return -1;
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
        return -1; // Didn't find a video stream
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
        return -1; // Codec not found
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE("Could not open codec.");
        return -1; // Could not open codec
    }

    // 获取native window
//    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;
    std::cout << "videoWidth : " << videoWidth << std::endl;
    std::cout << "videoHeight : " << videoHeight << std::endl;

    // 设置native window的buffer大小,可自动拉伸
//    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight,
//                                     WINDOW_FORMAT_RGBA_8888);
//    ANativeWindow_Buffer windowBuffer;

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE("Could not open codec.");
        return -1; // Could not open codec
    }

    // Allocate video frame
    AVFrame *pFrame = av_frame_alloc();

    // 用于渲染
//    AVFrame *pFrameRGBA = av_frame_alloc();
    AVFrame *pFrameYUV = av_frame_alloc();
    if (pFrameYUV == NULL || pFrame == NULL) {
        LOGE("Could not allocate video frame.");
        return -1;
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
    LOGD("AV_PIX_FMT_RGBA SWS_BILINEAR");
    int frameFinished;
    AVPacket packet;
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {

            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // 并不是decode一次就可解码出一帧
            if (frameFinished) {
                // lock native window buffer
//                ANativeWindow_lock(nativeWindow, &windowBuffer, 0);

                // 格式转换
                sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height,
                          pFrameYUV->data, pFrameYUV->linesize);

                // 获取stride
//                uint8_t *dst = static_cast<uint8_t *>(windowBuffer.bits);
//                int dstStride = windowBuffer.stride * 4;
//                uint8_t *src = (uint8_t *) (pFrameRGBA->data[0]);
//                int srcStride = pFrameRGBA->linesize[0];

                int pic_size = pCodecCtx->width * pCodecCtx->height;
                int newSize = pic_size * 1.5;

                unsigned char *s = new unsigned char[newSize];
                //写入数据
                memcpy(s, pFrame->data[0], pic_size); // 写入Y
                memcpy(s + pic_size, pFrame->data[1], pic_size / 4); // 写入U
                memcpy(s + pic_size * 5 / 4, pFrame->data[2], pic_size / 4); // 写入V
//                int a = 0, i;
//                for (i = 0; i < videoHeight; i++) {
//                    memcpy(s + a, pFrame->data[0] + i * pFrame->linesize[0], videoWidth);
//                    a += videoWidth;
//                }
//                for (i = 0; i < videoHeight / 2; i++) {
//                    memcpy(s + a, pFrame->data[1] + i * pFrame->linesize[1], videoWidth / 2);
//                    a += videoWidth / 2;
//                }
//                for (i = 0; i < videoHeight / 2; i++) {
//                    memcpy(s + a, pFrame->data[2] + i * pFrame->linesize[2], videoWidth / 2);
//                    a += videoWidth / 2;
//                }
                callNative(env, instance, METHOD_UPDATE_FRAME_DATA, METHOD_SIG_UPDATE_FRAME_DATA,
                           reinterpret_cast<char *>(s), newSize);
                delete[] s;
                if (shouldStopDecode) {
                    break;
                }
                // 由于window的stride和帧的stride不同,因此需要逐行复制
//                int h;
//                for (h = 0; h < videoHeight; h++) {
//                    memcpy(dst + h * dstStride, src + h * srcStride, srcStride);
//                }
//                ANativeWindow_unlockAndPost(nativeWindow);
            }

        }
        av_packet_unref(&packet);
    }

    av_free(buffer);
    av_free(pFrameYUV);

    // Free the YUV frame
    av_free(pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    env->ReleaseStringUTFChars(url_, file_name);
    shouldStopDecode = false;
    std::cout << "FfmpegTag shouldStopDecode : " << shouldStopDecode << std::endl;
    std::cout << "FfmpegTag stop play." << std::endl;
    return 0;
}


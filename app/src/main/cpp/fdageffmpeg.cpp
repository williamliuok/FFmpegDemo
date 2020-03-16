#include "log.h"
#include "AndroidBuf.h"
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
const char *METHOD_SIG_UPDATE_FRAME_DATA = "(II[B)V";

const char *METHOD_UPDATE_VIDEO_SIZE = "updateVideoSize";
const char *METHOD_SIG_UPDATE_VIDEO_SIZE = "(II)V";

static bool shouldStopDecode = false; // 是否继续解码解码标志位

// 全局常量定义
const char *base64char = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char padding_char = '=';

static FILE *fp = NULL;

/*编码代码
* const unsigned char * sourcedata， 源数组
* char * base64 ，码字保存
*/
int base64_encode(const unsigned char *sourcedata, char *base64) {
    int i = 0, j = 0;
    unsigned char trans_index = 0;    // 索引是8位，但是高两位都为0
    const int datalength = strlen((const char *) sourcedata);
    for (; i < datalength; i += 3) {
        // 每三个一组，进行编码
        // 要编码的数字的第一个
        trans_index = ((sourcedata[i] >> 2) & 0x3f);
        base64[j++] = base64char[(int) trans_index];
        // 第二个
        trans_index = ((sourcedata[i] << 4) & 0x30);
        if (i + 1 < datalength) {
            trans_index |= ((sourcedata[i + 1] >> 4) & 0x0f);
            base64[j++] = base64char[(int) trans_index];
        } else {
            base64[j++] = base64char[(int) trans_index];

            base64[j++] = padding_char;

            base64[j++] = padding_char;

            break;   // 超出总长度，可以直接break
        }
        // 第三个
        trans_index = ((sourcedata[i + 1] << 2) & 0x3c);
        if (i + 2 < datalength) { // 有的话需要编码2个
            trans_index |= ((sourcedata[i + 2] >> 6) & 0x03);
            base64[j++] = base64char[(int) trans_index];

            trans_index = sourcedata[i + 2] & 0x3f;
            base64[j++] = base64char[(int) trans_index];
        } else {
            base64[j++] = base64char[(int) trans_index];

            base64[j++] = padding_char;

            break;
        }
    }

    base64[j] = '\0';

    return 0;
}

void
callNativeUpdateFrame(JNIEnv *env, jobject thiz, int videoWidth, int videoHeight, char *bytes,
                      int length) {
    jclass clazz = env->GetObjectClass(thiz);//获取该对象的类
    jmethodID m_mid = env->GetMethodID(clazz, METHOD_UPDATE_FRAME_DATA,
                                       METHOD_SIG_UPDATE_FRAME_DATA);//获取JAVA方法的ID
    jbyteArray RtnArr = env->NewByteArray(length);
    env->SetByteArrayRegion(RtnArr, 0, length, (jbyte *) bytes);
    env->CallVoidMethod(thiz, m_mid, videoWidth, videoHeight, RtnArr);
    env->DeleteLocalRef(RtnArr); // 清除LocalRef中的引用，防止native memory的内存泄漏
    env->DeleteLocalRef(clazz); // 清除LocalRef中的引用，防止native memory的内存泄漏
}

void
callNativeUpdateVideoSize(JNIEnv *env, jobject thiz, int width, int height) {
    jclass clazz = env->GetObjectClass(thiz);//获取该对象的类
    jmethodID m_mid = env->GetMethodID(clazz, METHOD_UPDATE_VIDEO_SIZE,
                                       METHOD_SIG_UPDATE_VIDEO_SIZE);//获取JAVA方法的ID
    env->CallVoidMethod(thiz, m_mid, width, height);
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
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_playVideo(JNIEnv *env, jobject instance, jstring url_) {
    const char *file_name = env->GetStringUTFChars(url_, 0);
    shouldStopDecode = false;
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

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;
    callNativeUpdateVideoSize(env, instance, videoWidth, videoHeight);
    std::cout << "videoWidth : " << videoWidth << std::endl;
    std::cout << "videoHeight : " << videoHeight << std::endl;

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
    int frameFinished;
    AVPacket packet;

    while (av_read_frame(pFormatCtx, &packet) >= 0 && !shouldStopDecode) {
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

                unsigned char *s = new unsigned char[newSize];
                //写入数据
                memcpy(s, pFrame->data[0], pic_size); // 写入Y
                memcpy(s + pic_size, pFrame->data[1], pic_size / 4); // 写入U
                memcpy(s + pic_size * 5 / 4, pFrame->data[2], pic_size / 4); // 写入V

                callNativeUpdateFrame(env, instance, videoWidth, videoHeight,
                                      reinterpret_cast<char *>(s), newSize);
                delete[] s;
            }
        }
        av_packet_unref(&packet);
    }
    shouldStopDecode = false;

    av_free(buffer);
    av_free(pFrameYUV);

    // Free the YUV frame
    av_free(pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);

    env->ReleaseStringUTFChars(url_, file_name);
    std::cout << "FfmpegTag shouldStopDecode : " << shouldStopDecode << std::endl;
    std::cout << "FfmpegTag stop play." << std::endl;
    return 0;
}

#include <stdio.h>

void wirteToLocal(char *msg) {
    FILE *fp;
    if ((fp = fopen("/storage/emulated/0/ffmpeg.txt", "ab+")) == NULL) {
        printf("cannot open file!\n");
        return;
    }
    fwrite(msg, sizeof(char), sizeof(msg), fp);
    fclose(fp);
}

extern "C" {
void stop_decode() {
    shouldStopDecode = true;
}
}

extern "C" {
typedef void (*YUVDataHandle)(char *data, int length, int width, int height);
typedef void (*StopHandle)();
YUVDataHandle _Android_YuvDataHandle;
StopHandle _Android_StopHandle;
void start_decode(const char *file_name, YUVDataHandle callback, StopHandle stopHandle) {
    _Android_YuvDataHandle = callback;
    _Android_StopHandle = stopHandle;

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

    if (read_frame_result < 0) {
        _Android_StopHandle();
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
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_startPlay(JNIEnv *env, jobject instance, jstring url_,
                                                    jobject surface) {
    shouldStopDecode = false;
    //LOGD("start playvideo... url");

    char *file_name = const_cast<char *>(env->GetStringUTFChars(url_, JNI_FALSE));

    // 视频url
    LOGD("start playvideo... url, %s", file_name);
    av_register_all();

    AVFormatContext *pFormatCtx = avformat_alloc_context();

    // Open video file
    if (avformat_open_input(&pFormatCtx, file_name, NULL, NULL) != 0) {
        LOGE("Couldn't open file:%s\n", file_name);
        return -1; // Couldn't open file
    }

    pFormatCtx->probesize = 1 * 32; // 缩减探测数据尺寸，减少探测时间，优化首开延迟
    //pFormatCtx->max_analyze_duration = 1 * AV_TIME_BASE;
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("Couldn't find stream information.");
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
        LOGE("Didn't find a video stream.");
        return -1; // Didn't find a video stream
    }

    // Get a pointer to the codec context for the video stream
    AVCodecContext *pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    // Find the decoder for the video stream
    AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL) {
        LOGE("Codec not found.");
        return -1; // Codec not found
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE("Could not open codec.");
        return -1; // Could not open codec
    }

    // 获取native window
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;

    // 设置native window的buffer大小,可自动拉伸
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight,
                                     WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer windowBuffer;

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE("Could not open codec.");
        return -1; // Could not open codec
    }

    // Allocate video frame
    AVFrame *pFrame = av_frame_alloc();

    // 用于渲染
    AVFrame *pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == NULL || pFrame == NULL) {
        LOGE("Could not allocate video frame.");
        return -1;
    }

    // Determine required buffer size and allocate buffer
    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pCodecCtx->width, pCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width,
                                                pCodecCtx->height,
                                                pCodecCtx->pix_fmt,
                                                pCodecCtx->width,
                                                pCodecCtx->height,
                                                AV_PIX_FMT_RGBA,
                                                SWS_FAST_BILINEAR,
                                                NULL,
                                                NULL,
                                                NULL);

    int frameFinished;
    AVPacket packet;

    if (fp == NULL) {
        fp = fopen("/sdcard/debug.h264", "wb");
    }
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        if (fp != NULL) {
            fwrite((&packet)->data, 1, (&packet)->size, fp);
            LOGD("packet->size : %d", (&packet)->size);
        }

        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            if (shouldStopDecode) {
                break;
            }
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // 并不是decode一次就可解码出一帧
            if (frameFinished) {

                // lock native window buffer
                ANativeWindow_lock(nativeWindow, &windowBuffer, 0);

                // 格式转换
                sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height,
                          pFrameRGBA->data, pFrameRGBA->linesize);

                // 获取stride
                uint8_t *dst = static_cast<uint8_t *>(windowBuffer.bits);
                int dstStride = windowBuffer.stride * 4;
                uint8_t *src = pFrameRGBA->data[0];
                int srcStride = pFrameRGBA->linesize[0];

                // 由于window的stride和帧的stride不同,因此需要逐行复制
                int h;
                for (h = 0; h < videoHeight; h++) {
                    memcpy(dst + h * dstStride, src + h * srcStride, srcStride);
                }


                ANativeWindow_unlockAndPost(nativeWindow);
            }

        }
        av_packet_unref(&packet);
    }

    av_free(buffer);
    av_free(pFrameRGBA);

    // Free the YUV frame
    av_free(pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);

    // Close the video file
    avformat_close_input(&pFormatCtx);
    env->ReleaseStringUTFChars(url_, file_name);
    return 0;

}
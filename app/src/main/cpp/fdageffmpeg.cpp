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

static AVFormatContext *i_fmt_ctx = NULL;
static AVStream *i_video_stream = NULL;

static AVFormatContext *o_fmt_ctx;
static AVStream *o_video_stream;
static AVOutputFormat *oformat;

static AVCodecContext* encodeContext;

static int fps = 0;
static struct timeval startPreviewTime;
static int writeCount = 0;
static int64_t last_pts = 0;

static FILE *fp = NULL;
static bool shouldRecording = false; // 是否录制mp4
static bool isMp4Recording = true; // 是否编码为mp4视频流
static bool isH264Replay = false; // 是否H264视频流回放
static bool isH264Encoding = false; // 是否H264视频流回放

void endRecordMp4();

//void writeAvPacket(int last_pts, int last_dts, AVPacket *p, int64_t &pts, int64_t &dts);

void writeAvPacket(int64_t last_pts, int64_t last_dts, AVPacket *p) {
    static int num = 0;

    p->flags |= AV_PKT_FLAG_KEY;
    p->pts += 14850;
    p->dts += 14850;
    p->stream_index = 0;
    LOGE("writeAvPacket[%d] last_pts:%d, last_dts, p.pts:%d, p.dts\n", num++, last_pts, last_dts, p->pts, p->dts);
    //p->pts = p->dts = num * (encodeContext->time_base.den) /encodeContext->time_base.num / 3000;
    int ret = av_interleaved_write_frame(o_fmt_ctx, p);
    //LOGE("av_interleaved_write_frame ret = %d\n", ret);
    av_packet_unref(p);

}

void checkWriteError(size_t writeCount) {
    if (writeCount == 0) {
        //shouldRecording = false;
    }
}

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

    AVFormatContext *avFormatContext = avformat_alloc_context();

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);   //采用tcp传输
    //    av_dict_set(&opts, "stimeout", "10000000", 0);   //设置超时10秒

    // Open video file
    if (int err_code = avformat_open_input(&avFormatContext, file_name, NULL, &opts) != 0) {
        LOGE("FfmpegTag err_code : %d\n", err_code);
        LOGE("FfmpegTag Couldn't open file : %s \n", file_name);
        return -1; // Couldn't open file
    }

    avFormatContext->probesize = 1 * 1024; // 缩减探测数据尺寸，减少探测时间，优化首开延迟
    avFormatContext->max_analyze_duration = 1 * AV_TIME_BASE;

    if (avformat_find_stream_info(avFormatContext, &opts) < 0) {
        LOGE("FfmpegTag Couldn't find stream information.");
        return -1;
    }



    // Find the first video stream
    int videoStream = -1, i;
    for (i = 0; i < avFormatContext->nb_streams; i++) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && videoStream < 0) {
            videoStream = i;
        }
    }
    if (videoStream == -1) {
        LOGE("FfmpegTag Didn't find a video stream.");
        return -1; // Didn't find a video stream
    }

    // Get a pointer to the codec context for the video stream
    AVCodecContext *pCodecCtx = avFormatContext->streams[videoStream]->codec;

    // Find the decoder for the video stream
    AVCodec *pCodec = avcodec_find_decoder(avFormatContext->streams[videoStream]->codecpar->codec_id);
    //std::cout << "FfmpegTag codec_id : " << pCodecCtx->codec_id << std::endl;
    //std::cout << "FfmpegTag AVCodec name : " << pCodec->name << std::endl;


    if (pCodec == NULL) {
        LOGE("Codec not found.");
        return -1; // Codec not found
    }

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;

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

    while (av_read_frame(avFormatContext, &packet) >= 0 && !shouldStopDecode) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
//            avcodec_send_packet(pCodecCtx, &packet);
//            avcodec_receive_frame(pCodecCtx, pFrame);
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

                callNativeUpdateFrame(env,instance,videoWidth, videoHeight,reinterpret_cast<char *>(s),newSize);

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
    avformat_close_input(&avFormatContext);

    env->ReleaseStringUTFChars(url_, file_name);
    //std::cout << "FfmpegTag shouldStopDecode : " << shouldStopDecode << std::endl;
    //std::cout << "FfmpegTag stop play." << std::endl;
    return 0;
}



extern "C"
JNIEXPORT jint JNICALL
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_startPlay(JNIEnv *env, jobject instance, jstring url_,
                                                    jobject surface) {
    shouldStopDecode = false;
    //LOGD("start playvideo... url");

    char *file_name = const_cast<char *>(env->GetStringUTFChars(url_, JNI_FALSE));

    // 视频url
    LOGW("start playvideo... url, %s", file_name);
    av_register_all();

    i_fmt_ctx = avformat_alloc_context();

    AVDictionary *format_opts = NULL;
//    av_dict_set(&format_opts, "stimeout", std::to_string( 2* 1000000).c_str(), 0); //设置链接超时时间（us）
    av_dict_set(&format_opts, "rtsp_transport",  "tcp", 0); //设置推流的方式，默认udp。

    // Open video file
    if (avformat_open_input(&i_fmt_ctx, file_name, NULL, &format_opts) != 0) {
        LOGE("Couldn't open file:%s\n", file_name);
        return -1; // Couldn't open file
    }

    i_fmt_ctx->probesize = 1 * 32; // 缩减探测数据尺寸，减少探测时间，优化首开延迟
    i_fmt_ctx->max_analyze_duration = 1 * AV_TIME_BASE;
    // Retrieve stream information
    if (avformat_find_stream_info(i_fmt_ctx, NULL) < 0) {
        LOGE("Couldn't find stream information.");
        return -1;
    }

    // Find the first video stream
    int videoStream = -1, i;
    for (i = 0; i < i_fmt_ctx->nb_streams; i++) {
        if (i_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && videoStream < 0) {
            videoStream = i;
            i_video_stream = i_fmt_ctx->streams[i];
            LOGE("num:%d, den:%d.", i_video_stream->r_frame_rate.num,
                 i_video_stream->r_frame_rate.den);
        }
    }
    if (videoStream == -1) {
        LOGE("Didn't find a video stream.");
        return -1; // Didn't find a video stream
    }

    // Get a pointer to the codec context for the video stream
    AVCodecContext *pCodecCtx = i_fmt_ctx->streams[videoStream]->codec;

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

    int ret;
    AVPacket packet;
    AVPacket *lastKeyPacket = av_packet_alloc();
    AVFrame *lastFrame = av_frame_alloc();



    gettimeofday(&startPreviewTime, NULL );
    //LOGW("startPreviewTime: %d", startPreviewTime);
    while (av_read_frame(i_fmt_ctx, &packet) >= 0) {

        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {

            if (shouldStopDecode) {
                break;
            }
            // Decode video frame
            //avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            avcodec_send_packet(pCodecCtx, &packet);
            ret = avcodec_receive_frame(pCodecCtx, pFrame);

            // 并不是decode一次就可解码出一帧
            if (ret == 0) {
                writeCount++;
                lastFrame = av_frame_clone(pFrame);
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


            if (shouldRecording) {
                ret = avcodec_send_frame(encodeContext, lastFrame);
                //LOGE("writeCount:%d, ret1:%d", writeCount, ret);
                ret = avcodec_receive_packet(encodeContext, lastKeyPacket);
                //LOGE("writeCount:%d, ret2:%d", writeCount, ret);
                if (ret == 0) {
                    if (isMp4Recording) {
                        /*
                        * pts and dts should increase monotonically
                        * pts should be >= dts
                        */
                        //AVPacket *p = av_packet_clone(lastKeyPacket);
                        //LOGE("isMp4Recording----------------------------");
                        static int num = 0;
                        //lastKeyPacket->pts = av_rescale_q(lastKeyPacket->pts, encodeContext->time_base, o_video_stream->time_base);
                        lastKeyPacket->pts = lastKeyPacket->dts = (lastKeyPacket->pts - last_pts)/fps;
                        //lastKeyPacket->stream_index = 0;

                        LOGE("writeAvPacket[%d] pts:%d, dts:%d\n", num++, lastKeyPacket->pts, lastKeyPacket->dts);
                        int ret = av_interleaved_write_frame(o_fmt_ctx, lastKeyPacket);
                        //LOGE("av_interleaved_write_frame ret = %d\n", ret);
                        av_packet_unref(lastKeyPacket);
                        //writeAvPacket(last_pts, last_dts, lastKeyPacket);

                    } else {
                        //LOGE("isH264Recording----------------------------");
                        if (fp != NULL) {
                            fwrite((&packet)->data, 1, lastKeyPacket->size, fp);
                            //LOGD("packet->size : %d", (&packet)->size);
                        }
                    }
                }
            }else{
                last_pts = packet.pts;
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
    avformat_close_input(&i_fmt_ctx);
    if (shouldRecording) {
        if (isMp4Recording) {
            endRecordMp4();
        } else {
            fclose(fp);
        }
    }
    env->ReleaseStringUTFChars(url_, file_name);
    return 0;

}


void endRecordMp4() {
    //av_interleaved_write_frame(o_fmt_ctx, lastKeyPacket);
    av_write_trailer(o_fmt_ctx);
    av_freep(&o_fmt_ctx->streams[0]->codecpar);
    av_freep(&o_fmt_ctx->streams[0]);

    avio_close(o_fmt_ctx->pb);
    av_free(o_fmt_ctx);
}

static void log_callback_test2(void *ptr, int level, const char *fmt, va_list vl)
{
    va_list vl2;
    char *line = static_cast<char *>(malloc(128 * sizeof(char)));
    static int print_prefix = 1;
    va_copy(vl2, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, 128, &print_prefix);
    va_end(vl2);
    line[127] = '\0';
    LOGE("%s", line);
    free(line);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_startRecord(JNIEnv *env, jobject thiz, jstring outfile,
                                                      jboolean is_mp4) {
    LOGW("startRecord : entering-----------------------");
    char *file_name = const_cast<char *>(env->GetStringUTFChars(outfile, JNI_FALSE));
    isMp4Recording = is_mp4;
    LOGW("startRecord : %s", file_name);
    if (!isMp4Recording) {
        fp = fopen(file_name, "wb");
        if(fp == NULL){
            LOGW("打开文件失败\n");
            return -1;
        }
        shouldRecording = true;
        return 0;
    }

    //av_log_set_callback(log_callback_test2);

    oformat = av_guess_format(NULL, file_name, NULL);
    int ret = 0;
    o_fmt_ctx = avformat_alloc_context();
    ret = avformat_alloc_output_context2(&o_fmt_ctx, oformat, NULL, file_name);
    if (ret != 0) {
        LOGW("初始化o_fmt_ctx结构体失败, ret: %d\n",ret);
        return -1;
    }
    ret = avio_open(&o_fmt_ctx->pb, file_name, AVIO_FLAG_READ_WRITE);
    if (ret != 0) {
        LOGW("打开输出文件失败, ret: %d\\n\",ret");
        return -1;
    }
    /*
    * since all input files are supposed to be identical (framerate, dimension, color format, ...)
    * we can safely set output codec values from first input file
    */
    o_video_stream = avformat_new_stream(o_fmt_ctx, NULL);
    struct timeval now;
    gettimeofday(&now, NULL );
    long offset = (1000000 * ( now.tv_sec - startPreviewTime.tv_sec ) + now.tv_usec - startPreviewTime.tv_usec);
    LOGW("time: %d, writeCount: %d, last_pts: %d", offset, writeCount, last_pts);
    fps = static_cast<int>(last_pts*1000/offset);
    LOGW("fps: %d", fps);

    encodeContext = o_video_stream->codec;
    encodeContext->time_base.den = 10;
    encodeContext->time_base.num = 1;
    encodeContext->bit_rate = 400000;
    encodeContext->codec_id = oformat->video_codec;
    encodeContext->codec_type = AVMEDIA_TYPE_VIDEO;
    encodeContext->pix_fmt = AV_PIX_FMT_YUV420P;

    encodeContext->width=i_video_stream->codecpar->width;
    encodeContext->height=i_video_stream->codecpar->height;

    encodeContext->sample_aspect_ratio.num = i_video_stream->sample_aspect_ratio.num;
    encodeContext->sample_aspect_ratio.den = i_video_stream->sample_aspect_ratio.den;

    encodeContext->extradata = i_video_stream->codecpar->extradata;
    encodeContext->extradata_size = i_video_stream->codecpar->extradata_size;

    AVCodecParameters *c = o_video_stream->codecpar;
    c->format = i_video_stream->codecpar->format;

    o_video_stream->r_frame_rate = i_video_stream->r_frame_rate;

    if(encodeContext->codec_id == AV_CODEC_ID_H264)
    {
        LOGW("codec_id =  AV_CODEC_ID_H264");
    }else{
        LOGW("codec_id !=  AV_CODEC_ID_H264");
    }

    if(encodeContext->codec_id == AV_CODEC_ID_MPEG4)
    {
        LOGW("codec_id =  AV_CODEC_ID_MPEG4");
    }else{
        LOGW("codec_id !=  AV_CODEC_ID_MPEG4");
    }

    // 列出输出文件的相关流信息
    LOGW("------------------- 输出文件信息 ------------------\n");
    av_dump_format(o_fmt_ctx, 0, "", 1);
    LOGW("-------------------------------------------------\n");

    /*{
        AVCodecParameters *c;
        c = o_video_stream->codecpar;
        c->bit_rate = 400000;
        c->codec_id = AV_CODEC_ID_H264;
        c->codec_type = AVMEDIA_TYPE_VIDEO;

        c->sample_aspect_ratio.num = i_video_stream->sample_aspect_ratio.num;
        c->sample_aspect_ratio.den = i_video_stream->sample_aspect_ratio.den;

        c->extradata = i_video_stream->codecpar->extradata;
        c->extradata_size = i_video_stream->codecpar->extradata_size;

        c->width = i_video_stream->codecpar->width;
        c->height = i_video_stream->codecpar->height;
        c->format = i_video_stream->codecpar->format;
        LOGW("width: %d, height: %d\n", c->width , c->height );

        o_video_stream->r_frame_rate = i_video_stream->r_frame_rate;
    }*/


    encodeContext = o_video_stream->codec;
    AVCodec* pCodec = avcodec_find_encoder(encodeContext->codec_id);
    ret = avcodec_open2(encodeContext, pCodec, NULL);
    if(ret != 0){
        LOGW("avcodec_open2 failed, ret: %s\n",av_err2str(ret));
        return -1;
    }



    if (!(o_fmt_ctx->flags & AVFMT_NOFILE)) {

    }

    if (!o_fmt_ctx->nb_streams) {
        LOGW("output file dose not contain any stream\n");
        return -1;
    }

    ret = avcodec_parameters_from_context(o_video_stream->codecpar, encodeContext);
    if(ret!=0){
        LOGW("avcodec_parameters_from_context failed, ret: %d\n",ret);
        return -1;
    }

    // 根据文件名的后缀写相应格式的文件头

    AVDictionary* opt = NULL;
    av_dict_set_int(&opt, "video_track_timescale", 25, 0);

    if (avformat_write_header(o_fmt_ctx, NULL) < 0) {
        LOGW("Could not write header for output file\n");
        return -1;
    }
    //av_interleaved_write_frame(o_fmt_ctx, lastKeyPacket);
    shouldRecording = true;
    LOGW("------------------- 开始录视频 ------------------\n");
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_fdage_ffmpegdecode_Ffmpegdecoder_endRecord(JNIEnv *env, jobject thiz) {
    if (shouldRecording) {
        shouldRecording = false;
        if (isMp4Recording) {
            endRecordMp4();
        } else {
            fclose(fp);
            fp = NULL;
        }
    }
    return 0;
}

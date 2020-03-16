package com.fdage.ffmpegdecode;


import android.view.Surface;


import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import static com.fdage.ffmpegdecode.CommonUtil.Method_Init_Yuv;
import static com.fdage.ffmpegdecode.CommonUtil.Method_Show_Yuv;

/**
 * @Author William Liu
 * @Time 16:27
 * @Package com.fdage.ffmpegdecode
 * @Description
 */
public class Ffmpegdecoder {

    private volatile byte[] mByte; // 存储每一帧解码数据YUV
    private boolean needCallUnity = true;
    private final int retryCount = 10; //　解码失败时尝试重试次数
    private final int retrySleepTime = 500; //　解码失败时尝试时休眠间隔时长
    private boolean shouldStart = false;

    public Ffmpegdecoder() {

    }

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("fdageffmpeg");
        nativeInit();
    }

    public static native void nativeInit();

    /**
     * Unity调用开始解码
     *
     * @param url 需要解码的数据流地址
     */
    public int StartDecode(String url) {
        shouldStart = true;
        int retry = 0;
        int result = playVideo(url);
        while (shouldStart && result != 0 && retry++ < retryCount) {
            LogUtil.d("Ffmpegdecoder", "Ffmpegdecoder play failed, retry time : " + retry);
            try {
                Thread.sleep(retrySleepTime);
                result = playVideo(url);
            } catch (InterruptedException e) {

            }
        }
        return result;
    }

    /**
     * Unity调用开始解码
     *
     * @param url 需要解码的数据流地址
     */
    public int StartDecode(String url, Surface surface) {
        shouldStart = true;
        int retry = 0;
        int result = startPlay(url, surface);
        while (shouldStart && result != 0 && retry++ < retryCount) {
            LogUtil.d("Ffmpegdecoder", "Ffmpegdecoder play failed, retry time : " + retry);
            try {
                Thread.sleep(retrySleepTime);
                result = startPlay(url, surface);
            } catch (InterruptedException e) {

            }
        }
        return result;
    }

    /**
     * Unity调用停止解码
     */
    public void StopDecode() {
        shouldStart = false;
        stopFFmpegDecode();
    }

    public synchronized byte[] GetDecodeByte() {
        LogUtil.d("Ffmpegdecoder", "GetDecodeByte..............");
        if (null != mByte) {
            return mByte;
        }
        return null;
    }

    /**
     * native解码时时更新java层帧数据
     */
    public void updateVideoSize(int width, int height) {
        StringBuilder wh = new StringBuilder().append(width).append(",").append(height);
        CommonUtil.callUnity(wh.toString(), Method_Init_Yuv);
        LogUtil.d("Ffmpegdecoder", "Ffmpegdecoder video size : " + wh);
    }

    /**
     * native解码时时更新java层帧数据
     */
    public void updateFrameData(int width, int height, byte[] bytes) {
        if (null != listener) {
            listener.onDecodeFrame(width, height, bytes);
            return;
        }
//        if (null == mByte) {
//            mByte = bytes.clone();
//        } else {
//            System.arraycopy(bytes, 0, mByte, 0, bytes.length);
//        }
//        if (needCallUnity) {
//            String s = Base64.encodeToString(bytes, Base64.DEFAULT);
//            CommonUtil.callUnity(s, Method_Show_Yuv);
//            LogUtil.d("Ffmpegdecoder", "Ffmpegdecoder bytes length : " + bytes.length);
//            LogUtil.d("Ffmpegdecoder", "Ffmpegdecoder String length : " + s.length());
//        }
    }

    public native int playVideo(String url);

    public native void stopFFmpegDecode();

    private native int startPlay(String url, Surface surface);

    private DecodeListener listener;

    public void setListener(DecodeListener listener) {
        this.listener = listener;
    }

    public interface DecodeListener {
        void onDecodeFrame(int width, int height, byte[] data);
    }
}

package com.fdage.ffmpegdecode;

import android.app.Activity;
import android.util.Base64;

import java.io.UnsupportedEncodingException;

/**
 * @Author William Liu
 * @Time 16:27
 * @Package com.fdage.ffmpegdecode
 * @Description
 */
public class Ffmpegdecoder {

    private Activity activity;
    private volatile byte[] mByte; // 存储每一帧解码数据YUV
    private boolean needCallUnity = true;
    private final int retryCount = 10; //　解码失败时尝试重试次数
    private final int retrySleepTime = 500; //　解码失败时尝试时休眠间隔时长

    public Ffmpegdecoder() {
    }

    public Ffmpegdecoder(Activity activity) {
        this.activity = activity;
    }

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("lxffmpeg");
        nativeInit();
    }

    public static native void nativeInit();

    /**
     * Unity调用开始解码
     *
     * @param url 需要解码的数据流地址
     */
    public int StartDecode(String url) {
        int retry = 0;
        int result = playVideo(url);
        while (result != 0 && retry++ < retryCount) {
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
     * Unity调用停止解码
     */
    public void StopDecode() {
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
    public void updateFrameData(byte[] bytes) {
        if (null == mByte) {
            mByte = bytes.clone();
        } else {
            System.arraycopy(bytes, 0, mByte, 0, bytes.length);
        }
        if (needCallUnity) {
//                String s = new String(bytes, "US-ASCII");
            String s = Base64.encodeToString(bytes, Base64.DEFAULT);
            CommonUtil.callUnity(s);
            LogUtil.d("Ffmpegdecoder", "Ffmpegdecoder bytes length : " + bytes.length);
            LogUtil.d("Ffmpegdecoder", "Ffmpegdecoder String length : " + s.length());
        }
    }

    public native int playVideo(String url);

    public native void stopFFmpegDecode();


}

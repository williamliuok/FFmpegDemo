package com.fdage.ffmpegdecode;

import android.app.Activity;


import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * 通用工具类
 * Created by Administrator on 2018/9/4.
 */

public class CommonUtil {

    private static Activity mActivity;

    public static Activity getActivity() {
        if (null == mActivity) {
            try {
                Class<?> classtype = Class.forName("com.unity3d.player.UnityPlayer");
                Activity activity = (Activity) classtype.getDeclaredField("currentActivity").get(classtype);
                mActivity = activity;
            } catch (ClassNotFoundException e) {
                LogUtil.e("TAG", "getActivity: " + e.getMessage());
            } catch (IllegalAccessException e) {
                LogUtil.e("TAG", "getActivity: " + e.getMessage());
            } catch (NoSuchFieldException e) {
                LogUtil.e("TAG", "getActivity: " + e.getMessage());
            }
        }
        return mActivity;
    }

    static String gameObjectName;
    static String functionName;

    /**
     * 调用Unity的方法
     *
     * @param args 参数
     * @return 调用是否成功
     */
    public static boolean callUnity(String args) {
        if (gameObjectName == null || functionName == null) {
            gameObjectName = "AndroidHelper";
            functionName = "ShowYUV";
        }
        try {
            Class<?> classtype = Class.forName("com.unity3d.player.UnityPlayer");
            Method method = classtype.getMethod("UnitySendMessage", String.class, String.class, String.class);
            method.invoke(classtype, gameObjectName, functionName, args);
            return true;
        } catch (ClassNotFoundException e) {
            LogUtil.e("TAG", "getActivity: " + e.getMessage());
        } catch (NoSuchMethodException e) {
            LogUtil.e("TAG", "getActivity: " + e.getMessage());
        } catch (IllegalAccessException e) {
            LogUtil.e("TAG", "getActivity: " + e.getMessage());
        } catch (InvocationTargetException e) {
            LogUtil.e("TAG", "getActivity: " + e.getMessage());
        }
        return false;
    }


}

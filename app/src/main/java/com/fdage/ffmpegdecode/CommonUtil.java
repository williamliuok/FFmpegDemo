package com.fdage.ffmpegdecode;

import android.app.Activity;
import android.text.TextUtils;


import java.lang.ref.WeakReference;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * 通用工具类
 * Created by Administrator on 2018/9/4.
 */

public class CommonUtil {

    private static WeakReference<Activity> mActivity;
    private static Class<?> classtype;

    public static Activity getActivity() {
        if (null == mActivity || null == mActivity.get()) {
            try {
                if (null == classtype) {
                    classtype = Class.forName("com.unity3d.player.UnityPlayer");
                }
                Activity activity = (Activity) classtype.getDeclaredField("currentActivity").get(classtype);
                mActivity =  new WeakReference<>(activity);
            } catch (ClassNotFoundException e) {
                LogUtil.e("TAG", "getActivity: " + e.getMessage());
            } catch (IllegalAccessException e) {
                LogUtil.e("TAG", "getActivity: " + e.getMessage());
            } catch (NoSuchFieldException e) {
                LogUtil.e("TAG", "getActivity: " + e.getMessage());
            }
        }
        return mActivity.get();
    }

    private static final String gameObjectName = "AndroidHelper";
    public static String Method_Init_Yuv = "InitYUV";
    public static String Method_Show_Yuv = "ShowYUV";


    public static boolean callUnity(String args, String methodName) {
        if (TextUtils.isEmpty(methodName)) {
            return false;
        }
        try {
            if (null == classtype) {
                classtype = Class.forName("com.unity3d.player.UnityPlayer");
            }
            Method method = classtype.getMethod("UnitySendMessage", String.class, String.class, String.class);
            method.invoke(classtype, gameObjectName, methodName, args);
            return true;
        } catch (ClassNotFoundException | NoSuchMethodException | IllegalAccessException | InvocationTargetException e) {
            LogUtil.e("TAG", "getActivity: " + e.getMessage());
        }
        return false;
    }


}

package com.fdage.ffmpegdemo;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import com.fdage.ffmpegdecode.Ffmpegdecoder;

import java.io.File;
import java.lang.ref.WeakReference;

public class MainActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    private SurfaceView surfaceView;
    private SurfaceHolder mSurfaceHolder;
    private Ffmpegdecoder ffmpegdecoder = new Ffmpegdecoder(this);
    private ImageView iv;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        surfaceView = findViewById(R.id.surface_view);
        iv = findViewById(R.id.iv);
        mSurfaceHolder = surfaceView.getHolder();
        mSurfaceHolder.addCallback(this);
        // Example of a call to a native method
//        TextView tv = findViewById(R.id.sample_text);
//        tv.setText(stringFromJNI());
    }

    public void show(final String msg) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(MainActivity.this, msg, Toast.LENGTH_SHORT).show();
            }
        });
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
//        new Thread(new Runnable() {
//            @Override
//            public void run() {
//                String url = "/storage/emulated/0/DCIM/Camera/VID_20190312_121130.mp4";
//                String url = "rtsp://192.168.10.1:554/ucast/12";
//                String url = "rtmp://58.200.131.2:1935/livetv/hunantv";
                String url = "rtmp://live.hkstv.hk.lxdns.com/live/hks1";
//                String url = "rtsp://184.72.239.149/vod/mp4://BigBuckBunny_175k.mov";
//                ffmpegdecoder.playVideo(url,holder.getSurface());
//            }
//        }).start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {

    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {

    }


}

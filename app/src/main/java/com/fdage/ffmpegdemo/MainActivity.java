package com.fdage.ffmpegdemo;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.ImageView;

import com.fdage.ffmpegdecode.Ffmpegdecoder;

import java.io.File;

public class MainActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    private SurfaceView surfaceView;
    private SurfaceHolder mSurfaceHolder;
    private Button btn_start;
    private Button btn_end;
    private Ffmpegdecoder ffmpegdecoder = new Ffmpegdecoder();
    private ImageView iv;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        surfaceView = findViewById(R.id.surface_view);
        btn_start = findViewById(R.id.btn_start);
        btn_end = findViewById(R.id.btn_end);
        iv = findViewById(R.id.iv);
        mSurfaceHolder = surfaceView.getHolder();
        mSurfaceHolder.addCallback(this);
        // Example of a call to a native method
//        TextView tv = findViewById(R.id.sample_text);
//        tv.setText(stringFromJNI());

        btn_start.setOnClickListener(v -> {
            new Thread(() -> {
//                String url = "/storage/emulated/0/DCIM/Camera/VID_20190312_121130.mp4";
//                String url = getExternalFilesDir("") + File.separator + "20191029142000.h264";
//                String url = getExternalFilesDir("") + File.separator + "2019_10_30 11_13_25.mp4";
                String url = "rtsp://192.168.10.1:554/ucast/12";
//                String url = "rtmp://58.200.131.2:1935/livetv/hunantv";
//                String url = "rtmp://live.hkstv.hk.lxdns.com/live/hks1";
//                String url = "rtsp://184.72.239.149/vod/mp4://BigBuckBunny_175k.mov";
                ffmpegdecoder.StartDecode(url, mSurfaceHolder.getSurface());
            }).start();
        });
        btn_end.setOnClickListener(v -> {
            ffmpegdecoder.stopFFmpegDecode();
        });


    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {

    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {

    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {

    }


}

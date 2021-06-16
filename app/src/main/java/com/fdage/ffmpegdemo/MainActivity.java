package com.fdage.ffmpegdemo;

import android.os.Environment;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.TextureView;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.RadioGroup;

import com.fdage.ffmpegdecode.Ffmpegdecoder;

import java.io.File;
import java.io.IOException;

public class MainActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    public static final int MODE_RECORD_MP4 = 0;
    public static final int MODE_RECORD_H264 = 1;
    public static final int MODE_REPLAY_MP4 = 2;
    public static final int MODE_REPLAY_H264 = 3;

    private TextureView surfaceView;
//    private SurfaceView surfaceView;
    private SurfaceHolder mSurfaceHolder;
    private Button btn_start;
    private Button btn_start_record;
    private Button btn_end;
    private Button btn_stop_record;
    private RadioGroup rg_options;
    private Ffmpegdecoder ffmpegdecoder = new Ffmpegdecoder();

    private int mode = MODE_RECORD_MP4; //启动模式

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        surfaceView = findViewById(R.id.surface_view);
        btn_start = findViewById(R.id.btn_start);
        btn_start_record = findViewById(R.id.btn_start_record);
        btn_end = findViewById(R.id.btn_end);
        btn_stop_record = findViewById(R.id.btn_end_record);

        rg_options = findViewById(R.id.rg_options);
        rg_options.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(RadioGroup group, int checkedId) {
                if(checkedId == R.id.rb_record_mp4){
                    btn_start.setText("开始预览");
                    btn_end.setText("结束预览");
                    btn_start_record.setText("开始录制");
                    btn_stop_record.setText("结束录制");
                    mode = MODE_RECORD_MP4;
                }else if(checkedId == R.id.rb_record_h264){
                    btn_start.setText("开始预览");
                    btn_end.setText("结束预览");
                    btn_start_record.setText("开始录制");
                    btn_stop_record.setText("结束录制");
                    mode = MODE_RECORD_H264;
                }else if(checkedId == R.id.rb_replay_mp4){
                    btn_start.setText("开始回放");
                    btn_end.setText("结束回放");
                    btn_start_record.setText("暂停回放");
                    btn_stop_record.setText("继续回放");
                    mode = MODE_REPLAY_MP4;
                }else if(checkedId == R.id.rb_replay_h264){
                    btn_start.setText("开始回放");
                    btn_end.setText("结束回放");
                    btn_start_record.setText("暂停回放");
                    btn_stop_record.setText("继续回放");
                    mode = MODE_REPLAY_H264;
                }
            }
        });
//        mSurfaceHolder = surfaceView.getHolder();
//        mSurfaceHolder.addCallback(this);
        // Example of a call to a native method

        surfaceView.setRotation(-90f);
        btn_start.setOnClickListener(v -> {
            new Thread(() -> {
//                String url = "/storage/emulated/0/DCIM/Camera/VID_20190312_121130.mp4";
//                String url = getExternalFilesDir("") + File.separator + "20191029142000.h264";
//                String url = getExternalFilesDir("") + File.separator + "2019_10_30 11_13_25.mp4";
                String url;
                if(mode < 2) {
                    url = "rtsp://192.168.42.1/live";
//                    url = "rtsp://192.168.10.1:554/ucast/12";
                    ffmpegdecoder.StartDecode(url, new Surface(surfaceView.getSurfaceTexture()));
                }else {
                    if(mode == MODE_REPLAY_MP4) {
                        url = Environment.getExternalStorageDirectory().getAbsolutePath() + "/test.mp4";
                    } else{
                        url = Environment.getExternalStorageDirectory().getAbsolutePath() + "/test.h264";
                    }
                    ffmpegdecoder.playBackH264(url, mSurfaceHolder.getSurface());
                }
//                String url = "rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_175k.mov";
//                String url = "rtmp://58.200.131.2:1935/livetv/hunantv";
//                String url = "rtmp://live.hkstv.hk.lxdns.com/live/hks1";
//                String url = "rtsp://184.72.239.149/vod/mp4://BigBuckBunny_175k.mov";
            }).start();
        });
        btn_end.setOnClickListener(v -> {
            if(mode < 2) {
                ffmpegdecoder.stopFFmpegDecode();
            }else{
                if(mode == MODE_REPLAY_MP4) {

                } else{

                }
            }
        });

        btn_start_record.setOnClickListener(v -> {
            File file;
            if(mode == MODE_RECORD_MP4) {
                file = new File(getExternalFilesDir("").getAbsolutePath() + "/test.mp4");
            }else {
                file = new File(getExternalFilesDir("").getAbsolutePath() + "/test.h264");
            }
            if(!file.exists()){
                try {
                    file.createNewFile();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
            if(mode == MODE_RECORD_MP4) {
                ffmpegdecoder.StartRecord(file.toString(), true);
            }else if(mode == MODE_RECORD_H264) {
                ffmpegdecoder.StartRecord(file.toString(), false);
            }

        });
        btn_stop_record.setOnClickListener(v -> ffmpegdecoder.StopRecord());
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

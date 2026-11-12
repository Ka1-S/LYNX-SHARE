package com.ka1_s.lynx;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

public class LynxActivity extends Activity
{
    static
    {
        System.loadLibrary("LYNX");
    }

    private static final String TAG = "LynxActivity";
    private static final int PERMISSION_REQUEST_AUDIO = 1;
    private static final int PERMISSION_REQUEST_STORAGE = 2;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        
        // Keep screen on for audio apps
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        
        // Request permissions
        requestPermissionsIfNeeded();
        
        // Notify native code
        onNativeCreate();
        
        Log.i(TAG, "onCreate");
    }

    @Override
    protected void onStart()
    {
        super.onStart();
        onNativeStart();
        Log.i(TAG, "onStart");
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        onNativeResume();
        Log.i(TAG, "onResume");
    }

    @Override
    protected void onPause()
    {
        super.onPause();
        onNativePause();
        Log.i(TAG, "onPause");
    }

    @Override
    protected void onStop()
    {
        super.onStop();
        onNativeStop();
        Log.i(TAG, "onStop");
    }

    @Override
    protected void onDestroy()
    {
        super.onDestroy();
        onNativeDestroy();
        Log.i(TAG, "onDestroy");
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration newConfig)
    {
        super.onConfigurationChanged(newConfig);
        onNativeConfigurationChanged();
        Log.i(TAG, "onConfigurationChanged");
    }

    @Override
    public void onLowMemory()
    {
        super.onLowMemory();
        onNativeLowMemory();
        Log.i(TAG, "onLowMemory");
    }

    @Override
    public void onTrimMemory(int level)
    {
        super.onTrimMemory(level);
        onNativeTrimMemory(level);
        Log.i(TAG, "onTrimMemory: " + level);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus)
    {
        super.onWindowFocusChanged(hasFocus);
        onNativeWindowFocusChanged(hasFocus);
        Log.i(TAG, "onWindowFocusChanged: " + hasFocus);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults)
    {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        onNativeRequestPermissionsResult(requestCode, permissions, grantResults);
        Log.i(TAG, "onRequestPermissionsResult: " + requestCode);
    }

    private void requestPermissionsIfNeeded()
    {
        // Request audio recording permission
        if (ContextCompat.checkSelfPermission(this, android.Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
        {
            ActivityCompat.requestPermissions(this, new String[]{android.Manifest.permission.RECORD_AUDIO}, PERMISSION_REQUEST_AUDIO);
        }

        // Request storage permission for Android 10 and below
        if (android.os.Build.VERSION.SDK_INT <= android.os.Build.VERSION_CODES.Q)
        {
            if (ContextCompat.checkSelfPermission(this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED)
            {
                ActivityCompat.requestPermissions(this, new String[]{android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, PERMISSION_REQUEST_STORAGE);
            }
        }
    }

    // Native methods
    private native void onNativeCreate();
    private native void onNativeStart();
    private native void onNativeResume();
    private native void onNativePause();
    private native void onNativeStop();
    private native void onNativeDestroy();
    private native void onNativeConfigurationChanged();
    private native void onNativeLowMemory();
    private native void onNativeTrimMemory(int level);
    private native void onNativeWindowFocusChanged(boolean hasFocus);
    private native void onNativeRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults);
}
package com.ka1_s.lynx;

import android.app.Application;
import android.content.Context;
import android.util.Log;

public class LynxApplication extends Application
{
    private static final String TAG = "LynxApplication";
    private static Context applicationContext;

    @Override
    public void onCreate()
    {
        super.onCreate();
        applicationContext = this;
        Log.i(TAG, "Application created");
    }

    @Override
    public void onTerminate()
    {
        super.onTerminate();
        Log.i(TAG, "Application terminated");
    }

    @Override
    public void onLowMemory()
    {
        super.onLowMemory();
        Log.i(TAG, "onLowMemory");
    }

    @Override
    public void onTrimMemory(int level)
    {
        super.onTrimMemory(level);
        Log.i(TAG, "onTrimMemory: " + level);
    }

    public static Context getContext()
    {
        return applicationContext;
    }
}
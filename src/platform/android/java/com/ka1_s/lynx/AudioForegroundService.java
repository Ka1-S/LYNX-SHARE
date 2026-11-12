package com.ka1_s.lynx;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import androidx.core.app.NotificationCompat;

public class AudioForegroundService extends Service
{
    private static final String TAG = "AudioForegroundService";
    private static final String CHANNEL_ID = "lynx_audio_channel";
    private static final int NOTIFICATION_ID = 1;

    @Override
    public void onCreate()
    {
        super.onCreate();
        createNotificationChannel();
        Log.i(TAG, "Service created");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId)
    {
        Notification notification = createNotification();
        startForeground(NOTIFICATION_ID, notification);
        Log.i(TAG, "Service started");
        return START_STICKY;
    }

    @Override
    public void onDestroy()
    {
        super.onDestroy();
        stopForeground(true);
        Log.i(TAG, "Service destroyed");
    }

    @Override
    public IBinder onBind(Intent intent)
    {
        return null;
    }

    private void createNotificationChannel()
    {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
        {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "LYNX Audio Playback",
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("Audio playback and recording for LYNX SHARE");
            NotificationManager manager = getSystemService(NotificationManager.class);
            manager.createNotificationChannel(channel);
        }
    }

    private Notification createNotification()
    {
        Intent intent = new Intent(this, LynxActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_IMMUTABLE);

        return new NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("LYNX SHARE")
            .setContentText("Audio engine running")
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .build();
    }
}
package com.bittenos.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager

/**
 * Keeps the app alive with its screen closed, so a remote button set to "A sound" still plays one.
 * Only runs when the person switches it on in the soundboard; voice keeps the app alive by itself.
 */
class SoundService : Service() {
    companion object {
        private const val CH = "sounds"
        private const val STOP = "stop"
        fun start(ctx: Context) {
            try { ctx.startForegroundService(Intent(ctx, SoundService::class.java)) } catch (e: Exception) {}
        }
        fun stop(ctx: Context) { ctx.stopService(Intent(ctx, SoundService::class.java)) }
    }

    private var wifi: WifiManager.WifiLock? = null
    private var wake: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == STOP) {
            Soundboard.setBackground(false)
            stopSelf()
            return START_NOT_STICKY
        }
        MaskLink.start(this)
        Soundboard.init(this)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CH, "Soundboard", NotificationManager.IMPORTANCE_MIN).apply {
            description = "Shown while the remote can play sounds with the app closed"
        })
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 2, Intent(this, SoundService::class.java).setAction(STOP),
            PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CH)
            .setSmallIcon(android.R.drawable.ic_lock_silent_mode_off)
            .setContentTitle("Soundboard is listening")
            .setContentText("Remote buttons set to a sound play it on this phone")
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(2, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        else startForeground(2, n)
        if (wifi == null) {
            @Suppress("DEPRECATION")
            wifi = getSystemService(WifiManager::class.java).createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "bittenos:sounds")
                .apply { setReferenceCounted(false); acquire() }
            wake = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "bittenos:sounds")
                .apply { setReferenceCounted(false); acquire() }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        wifi?.let { if (it.isHeld) it.release() }
        wake?.let { if (it.isHeld) it.release() }
        super.onDestroy()
    }
}

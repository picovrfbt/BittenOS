package com.bittenos.app

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.provider.Settings

/**
 * Keeps the whole app working with the screen off: the link to the mask, remote buttons that play
 * sounds, settings waiting to be sent, and the voice. Without it Android lets the WiFi doze and
 * pauses the app a minute or so after the screen goes off. On unless switched off in Network & app.
 */
class AwakeService : Service() {
    companion object {
        private const val CH = "awake"
        private const val STOP = "stop"

        fun start(ctx: Context) {
            try { ctx.startForegroundService(Intent(ctx, AwakeService::class.java)) } catch (e: Exception) {}
        }
        fun stop(ctx: Context) { ctx.stopService(Intent(ctx, AwakeService::class.java)) }

        /** Battery saving can still stop a foreground service on some phones (Samsung especially). */
        fun batteryFree(ctx: Context) =
            ctx.getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(ctx.packageName)

        /** Android's own "let this app run in the background" question. */
        @SuppressLint("BatteryLife")
        fun askBatteryFree(ctx: Context) {
            val i = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:" + ctx.packageName))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            try { ctx.startActivity(i) } catch (e: Exception) {
                try { ctx.startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
                catch (e2: Exception) {}
            }
        }
    }

    private var wifiLock: WifiManager.WifiLock? = null
    private var wake: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == STOP) {
            AppPrefs.setScreenOff(applicationContext, false)
            stopSelf()
            return START_NOT_STICKY
        }
        // Android can restart this on its own after killing the app, with no screen to start the rest
        AppPrefs.init(this)
        MaskLink.start(this)
        Cfg.init()
        Soundboard.init(this)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CH, "Working with the screen off", NotificationManager.IMPORTANCE_MIN).apply {
            description = "Shown while BittenOS keeps talking to the mask with the screen off"
        })
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 2, Intent(this, AwakeService::class.java).setAction(STOP),
            PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CH)
            .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
            .setContentTitle("BittenOS is running")
            .setContentText("Stays linked to the mask with the screen off")
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Turn off", stop).build())
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(2, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        else startForeground(2, n)
        if (wake == null) {
            // The high-perf lock is the one that holds with the screen off: without it the WiFi
            // only wakes every few hundred ms and the mask's packets pile up or time out.
            @Suppress("DEPRECATION")
            wifiLock = getSystemService(WifiManager::class.java).createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "bittenos:awake")
                .apply { setReferenceCounted(false); acquire() }
            wake = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "bittenos:awake")
                .apply { setReferenceCounted(false); acquire() }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        wifiLock?.let { if (it.isHeld) it.release() }
        wake?.let { if (it.isHeld) it.release() }
        wake = null
        super.onDestroy()
    }
}

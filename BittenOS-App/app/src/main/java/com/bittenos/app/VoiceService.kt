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
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Keeps the voice playing with the screen off and the phone in a pocket - the thing a browser tab
 * cannot do. A foreground service, so Android shows a notification and leaves it alone.
 */
class VoiceService : Service() {
    companion object {
        private const val CH = "voice"
        private const val STOP = "stop"
        private val _on = MutableStateFlow(false)
        val on: StateFlow<Boolean> = _on

        fun start(ctx: Context) {
            ctx.startForegroundService(Intent(ctx, VoiceService::class.java))
        }
        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, VoiceService::class.java).setAction(STOP))
        }

        fun smoothingMs(ctx: Context) =
            ctx.getSharedPreferences("voice", Context.MODE_PRIVATE).getInt("smooth", 40)
        fun setSmoothingMs(ctx: Context, ms: Int) {
            ctx.getSharedPreferences("voice", Context.MODE_PRIVATE).edit().putInt("smooth", ms).apply()
            if (_on.value) restart?.invoke()
        }
        fun output(ctx: Context): VoiceEngine.Output =
            ctx.getSharedPreferences("voice", Context.MODE_PRIVATE).getString("output", null)
                ?.let { n -> VoiceEngine.Output.entries.firstOrNull { it.name == n } } ?: VoiceEngine.Output.Aux
        fun setOutput(ctx: Context, o: VoiceEngine.Output) {
            ctx.getSharedPreferences("voice", Context.MODE_PRIVATE).edit().putString("output", o.name).apply()
            if (_on.value) restart?.invoke()
        }
        private var restart: (() -> Unit)? = null
    }

    private var engine: VoiceEngine? = null
    private var engineHost: String? = null
    private var wifiLocks = listOf<WifiManager.WifiLock>()
    private var wake: PowerManager.WakeLock? = null
    private val scope = CoroutineScope(Dispatchers.Main)
    private var watch: Job? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == STOP) {
            stopSelf()
            return START_NOT_STICKY
        }
        if (_on.value) return START_NOT_STICKY
        goForeground()
        _on.value = true
        holdLocks()
        restart = { engineHost = null; follow(MaskLink.state.value) }
        // Follows the connection: plays while the mask is there, and picks up again by itself if
        // the WiFi drops and comes back.
        watch = scope.launch { MaskLink.state.collect { follow(it) } }
        return START_NOT_STICKY
    }

    private fun follow(s: MaskLink.State) {
        val h = (s as? MaskLink.State.Connected)?.host
        if (h == engineHost && engine != null) return
        engine?.stop()
        engine = null
        engineHost = h
        if (h != null) {
            engine = VoiceEngine(this, h, MaskLink.network, smoothingMs(this), output(this)).also { it.start() }
        }
    }

    private fun holdLocks() {
        val wm = applicationContext.getSystemService(WifiManager::class.java)
        val locks = mutableListOf<WifiManager.WifiLock>()
        // WiFi power saving batches packets for up to ~100 ms at a time - fatal for this. The
        // low-latency lock only applies with the screen on; the high-perf one covers the pocket.
        if (Build.VERSION.SDK_INT >= 29) {
            locks += wm.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "bittenos:voice-ll")
        }
        @Suppress("DEPRECATION")
        locks += wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "bittenos:voice")
        locks.forEach { it.setReferenceCounted(false); it.acquire() }
        wifiLocks = locks
        wake = getSystemService(PowerManager::class.java)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "bittenos:voice")
            .apply { setReferenceCounted(false); acquire() }
    }

    private fun goForeground() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CH, "Voice", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Shown while the mask's mic is playing through this phone"
            })
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1,
            Intent(this, VoiceService::class.java).setAction(STOP), PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CH)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentTitle("Voice is on")
            .setContentText("The mask's mic is playing through this phone")
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        else startForeground(1, n)
    }

    override fun onDestroy() {
        watch?.cancel()
        restart = null
        engine?.stop()
        engine = null
        engineHost = null
        wifiLocks.forEach { if (it.isHeld) it.release() }
        wake?.let { if (it.isHeld) it.release() }
        _on.value = false
        super.onDestroy()
    }
}

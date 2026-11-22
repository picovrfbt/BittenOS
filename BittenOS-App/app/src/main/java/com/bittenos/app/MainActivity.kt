package com.bittenos.app

import android.Manifest
import android.content.pm.PackageManager
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.lifecycleScope
import com.bittenos.app.ui.AppScreen
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AppPrefs.init(this)
        MaskLink.start(this)              // first: the others read what it kept from last time
        Cfg.init()
        Faces.init(this)
        Soundboard.init(this)
        volumeControlStream = AudioManager.STREAM_MUSIC     // the volume buttons set the voice level
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        lifecycleScope.launch {
            AppPrefs.keepOn.collect { on ->
                if (on) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
        // The app stays alive with the screen off, so a fresh open would land on the last screen: start at home
        if (savedInstanceState == null) com.bittenos.app.ui.Nav.home()
        enableEdgeToEdge()
        hideBars()
        setContent { AppScreen() }
    }

    /** Full screen: the phone's status bar and navigation bar hide; a swipe from the edge shows them for a moment. */
    private fun hideBars() {
        if (Build.VERSION.SDK_INT >= 28) window.attributes = window.attributes.also {
            it.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        WindowCompat.getInsetsController(window, window.decorView).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }

    // Dialogs, the keyboard and other apps can bring the bars back: hide them again on return
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideBars()
    }

    override fun onStart() {
        super.onStart()
        // Started while the app is on screen: Android won't allow it from the background.
        if (AppPrefs.screenOff.value) AwakeService.start(this)
        lifecycleScope.launch { Updater.check(this@MainActivity, quiet = true) }   // at most hourly
    }

    override fun onResume() {
        super.onResume()
        MaskLink.kick()           // back from WiFi settings: look straight away
    }
}

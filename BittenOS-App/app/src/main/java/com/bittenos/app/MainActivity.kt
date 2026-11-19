package com.bittenos.app

import android.Manifest
import android.content.pm.PackageManager
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.bittenos.app.ui.AppScreen
import com.bittenos.app.ui.BittenTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        MaskLink.start(this)
        volumeControlStream = AudioManager.STREAM_MUSIC     // the volume buttons set the voice level
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        enableEdgeToEdge()
        setContent { BittenTheme { AppScreen() } }
    }

    override fun onResume() {
        super.onResume()
        MaskLink.kick()           // back from WiFi settings: look straight away
    }
}

package com.bittenos.app.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Placeholder look until the big UI change: dark, with the visor's cyan as the accent.
val Accent = Color(0xFF3DDCFF)
val Good = Color(0xFF4ADE80)
val Warn = Color(0xFFFBBF24)
val Muted = Color(0xFF8A93A6)

private val scheme = darkColorScheme(
    primary = Accent,
    onPrimary = Color(0xFF00222B),
    secondary = Accent,
    background = Color(0xFF0B0E14),
    surface = Color(0xFF0B0E14),
    surfaceVariant = Color(0xFF161B26),
    surfaceContainer = Color(0xFF121722),
    onSurface = Color(0xFFE6E9F0),
    onSurfaceVariant = Muted,
)

@Composable
fun BittenTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = scheme, content = content)
}

package com.bittenos.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bedtime
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.bittenos.app.Cfg
import com.bittenos.app.Faces
import com.bittenos.app.MaskLink
import com.bittenos.app.Soundboard
import kotlinx.coroutines.launch

// ---- the header ------------------------------------------------------------------------------

@Composable
fun HudHeader(onMenu: () -> Unit, onRave: () -> Unit, rave: Boolean, sleeping: Boolean, display: Boolean,
              online: Boolean, onSleep: () -> Unit, onPower: () -> Unit) {
    val a = LocalAccent.current
    Row(Modifier.fillMaxWidth().statusBarsPadding().padding(start = Hud.Edge, end = Hud.Edge, top = 14.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(Modifier.height(44.dp).background(accentDeep(a), RoundedCornerShape(2.dp))
            .border(1.dp, a, RoundedCornerShape(2.dp)).clickable(onClick = onMenu).padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Icon(Icons.Filled.Menu, null, Modifier.size(16.dp), tint = a)
            Caps("MENU", color = a, size = 11.sp, weight = FontWeight.Bold)
        }
        Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
            Text("BITTEN·OS", Modifier.padding(bottom = 6.dp).drawBehind {
                val w = 34.dp.toPx()
                drawLine(a, Offset(size.width / 2 - w / 2, size.height + 4.dp.toPx()),
                    Offset(size.width / 2 + w / 2, size.height + 4.dp.toPx()), 2.dp.toPx())
            }, color = Hud.Text, fontWeight = FontWeight.Bold, fontSize = 14.sp, letterSpacing = 0.34.em, maxLines = 1)
        }
        IconBtn(Icons.Filled.GraphicEq, "Rave mode", onRave, if (rave) Hud.Pink else Hud.Pink, lit = rave, border = Color(0xFF5A1F37))
        IconBtn(Icons.Filled.Bedtime, if (sleeping) "Wake up" else "Sleeping face", onSleep, if (sleeping) a else Hud.Text2,
            lit = sleeping, enabled = online)
        IconBtn(Icons.Filled.PowerSettingsNew, if (display) "Panels off" else "Panels on", onPower,
            if (display) a else Hud.Faint, lit = display, enabled = online)
    }
}

@Composable
fun IconBtn(icon: ImageVector, desc: String, onClick: () -> Unit, tint: Color, lit: Boolean = false,
            enabled: Boolean = true, border: Color = Hud.Line2) {
    Box(Modifier.size(44.dp).background(if (lit) lerpColor(Hud.Bg, tint, 0.13f) else Hud.PanelBg, RoundedCornerShape(2.dp))
        .border(1.dp, if (lit) tint else border, RoundedCornerShape(2.dp))
        .clickable(enabled = enabled, onClick = onClick), contentAlignment = Alignment.Center) {
        Icon(icon, desc, Modifier.size(18.dp), tint = if (enabled) tint else Hud.Ghost)
    }
}

// ---- the mask link bar ---------------------------------------------------------------------------

@Composable
fun LinkPanel(link: MaskLink.State, onWifi: () -> Unit) {
    val a = LocalAccent.current
    Column(Modifier.padding(horizontal = Hud.Edge).padding(top = 6.dp, bottom = 16.dp).fillMaxWidth()
        .background(Hud.FieldBg, RoundedCornerShape(2.dp)).border(1.dp, Hud.Line, RoundedCornerShape(2.dp))
        .padding(horizontal = 12.dp, vertical = 10.dp)) {
        Caps("MASK LINK", Modifier.fillMaxWidth(), color = when (link) {
            is MaskLink.State.Connected -> if (link.ownWifi) a else Hud.Cyan
            MaskLink.State.Searching -> Hud.Faint
            else -> Hud.Amber
        }, size = 10.sp, weight = FontWeight.Bold, spacing = 0.32.em, align = androidx.compose.ui.text.style.TextAlign.Center)
        Spacer(Modifier.height(6.dp))
        when (link) {
            is MaskLink.State.Connected -> {
                val c = if (link.ownWifi) a else Hud.Cyan
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    LinkNode("PHONE", c, dotFirst = true)
                    if (link.ownWifi) Wire(Modifier.weight(1f), c)
                    else {
                        Wire(Modifier.weight(1f), c); LinkNode("ROUTER", Hud.Line2); Wire(Modifier.weight(1f), c, delayMs = 400)
                    }
                    LinkNode("MASK", c)
                }
                val s = link.stats
                val phones = s.optInt("wifiClients", 0)
                Row(Modifier.fillMaxWidth().padding(top = 7.dp)) {
                    Mono((if (link.ownWifi) "ON MASK WIFI" else "SAME NETWORK"), color = c, size = 10.sp)
                    Mono(" · " + link.host, Modifier.weight(1f), color = Hud.Dim, size = 10.sp)
                    val right = mutableListOf<String>()
                    if (link.ownWifi) right += if (phones == 1) "1 PHONE" else "$phones PHONES"
                    else s.optString("mdns").takeIf { it.isNotBlank() }?.let { right += "${it.uppercase()}.LOCAL" }
                    if (s.has("tempC")) right += "%.0f°C".format(s.optDouble("tempC"))
                    Mono(right.joinToString(" · "), color = Hud.Dim, size = 10.sp)
                }
            }
            MaskLink.State.Searching -> {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    LinkNode("PHONE", Hud.Faint, dotFirst = true); Wire(Modifier.weight(1f), Hud.Faint); LinkNode("MASK ?", Hud.Line2)
                }
                Mono("SEARCHING · 192.168.4.1 · MDNS · LAST", Modifier.padding(top = 7.dp), color = Hud.Dim, size = 10.sp)
            }
            else -> {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    LinkNode("PHONE", Hud.Amber, dotFirst = true)
                    Box(Modifier.weight(1f), contentAlignment = Alignment.Center) { Caps("NO MASK HERE", color = Hud.Amber, size = 9.sp) }
                    LinkNode("MASK", Hud.Line2)
                }
                Row(Modifier.fillMaxWidth().padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Mono(if (link is MaskLink.State.NoWifi) "NOT ON WIFI" else "NOT ON MASK WIFI", Modifier.weight(1f), color = Hud.Amber, size = 10.sp)
                    HudButton("Open WiFi picker", onWifi, kind = BtnKind.Amber, height = 34.dp, size = 9.sp)
                }
            }
        }
    }
}

// ---- settings tied to config.json ----------------------------------------------------------------

/** Whether controls that need the mask can be used right now. */
@Composable
fun online(): Boolean {
    val st by MaskLink.state.collectAsState()
    return st is MaskLink.State.Connected
}

@Composable
fun CfgToggle(key: String, title: String, hint: String? = null, top: Boolean = true, enabled: Boolean = true,
              after: (Boolean) -> Unit = {}) {
    val cfg by Cfg.data.collectAsState()
    val on = online()
    val scope = rememberCoroutineScope()
    @Suppress("UNUSED_EXPRESSION") cfg
    SetRow(title, hint, enabled = enabled && on, top = top) {
        HudToggle(Cfg.bool(key), { v -> scope.launch { if (Cfg.set(key to v)) after(v) } }, enabled = enabled && on)
    }
}

@Composable
fun CfgStep(key: String, title: String, hint: String? = null, min: Double, max: Double, step: Double,
            unit: String = "", decimals: Int = 0, scale: Double = 1.0, enabled: Boolean = true, top: Boolean = true) {
    val cfg by Cfg.data.collectAsState()
    val on = online()
    val scope = rememberCoroutineScope()
    @Suppress("UNUSED_EXPRESSION") cfg
    val v = Cfg.dbl(key)
    SetRow(title, hint, enabled = enabled && on, top = top) {
        NumStepper(v, min, max, step, { x -> "%.${decimals}f".format(x / scale) + unit }, enabled && on) { x ->
            scope.launch { Cfg.set(key to (if (decimals == 0 && scale == 1.0) x.toLong() else x)) }
        }
    }
}

@Composable
fun <T> CfgPick(key: String, title: String, hint: String? = null, options: List<Pair<T, String>>,
                read: (String) -> T, write: (T) -> Any, enabled: Boolean = true, top: Boolean = true) {
    val cfg by Cfg.data.collectAsState()
    val on = online()
    val scope = rememberCoroutineScope()
    @Suppress("UNUSED_EXPRESSION") cfg
    SetRow(title, hint, enabled = enabled && on, top = top) {
        Picker(read(key), options, { v -> scope.launch { Cfg.set(key to write(v)) } }, title, enabled = enabled && on)
    }
}

@Composable
fun CfgStrPick(key: String, title: String, hint: String? = null, options: List<Pair<String, String>>,
               enabled: Boolean = true, top: Boolean = true) =
    CfgPick(key, title, hint, options, { Cfg.str(it) }, { it }, enabled, top)

@Composable
fun CfgFacePick(key: String, title: String, hint: String? = null, enabled: Boolean = true, top: Boolean = true) {
    val faces by Faces.list.collectAsState()
    CfgStrPick(key, title, hint, faces.map { it to Faces.label(it) }, enabled, top)
}

/** A segmented row for a setting with a few named values. */
@Composable
fun <T> CfgSeg(key: String, options: List<Pair<T, String>>, read: (String) -> T, write: (T) -> Any,
               enabled: Boolean = true, cols: Int = options.size) {
    val cfg by Cfg.data.collectAsState()
    val on = online()
    val scope = rememberCoroutineScope()
    @Suppress("UNUSED_EXPRESSION") cfg
    Seg(options, read(key), { v -> scope.launch { Cfg.set(key to write(v)) } }, enabled = enabled && on, cols = cols)
}

/** A slider for a setting: follows the finger, saves on release. */
@Composable
fun CfgSlider(key: String, title: String, min: Float, max: Float, step: Float, fmt: (Float) -> String,
              enabled: Boolean = true, hint: String? = null) {
    val cfg by Cfg.data.collectAsState()
    val on = online()
    val scope = rememberCoroutineScope()
    var v by remember(cfg) { mutableFloatStateOf(Cfg.dbl(key).toFloat()) }
    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
        LabVal(title, fmt(v))
        if (hint != null) Hint(hint, Modifier.padding(top = 2.dp))
        val steps = (((max - min) / step).toInt() - 1).coerceIn(0, 200)
        HudSlider(v, { v = (Math.round(it / step) * step).coerceIn(min, max) }, min..max, steps = if (steps > 60) 0 else steps,
            enabled = enabled && on, onDone = { scope.launch { Cfg.set(key to (if (step >= 1f) v.toLong() else v.toDouble())) } })
    }
}

// ---- things several screens list ----------------------------------------------------------------

/** Kept in step with doButtonAction() in the firmware. "sound" plays on this phone's soundboard. */
val BTN_ACTIONS = listOf(
    "none" to "Nothing", "nextFace" to "Next face", "prevFace" to "Previous face", "displays" to "Displays on/off",
    "sleep" to "Sleep / wake", "face" to "A chosen face", "phrase" to "A quick phrase", "sound" to "A sound (this phone)",
    "screen" to "Next screen page", "slots" to "Slot machine", "dino" to "Dino game", "boot" to "Boot sequence",
    "shutdown" to "Fake low battery")

fun phraseOptions(): List<Pair<Int, String>> = (0 until 6).map { i ->
    val t = Cfg.phrase(i)
    i to ("${i + 1}" + if (t.isNotEmpty()) ": $t" else " (empty)")
}

fun soundOptions(): List<Pair<Int, String>> =
    Soundboard.sounds.value.map { it.slot to "${it.slot + 1}: ${it.name}" }.ifEmpty { listOf(0 to "No sounds yet · add them in Soundboard") }

/**
 * An action picker plus the second picker its action needs (which face, phrase or sound), for the
 * mask's button and each remote gesture. [key] is the action key; the face key is key+"Face" and
 * the number key key+"Phrase", the way config.json names them (btnShort → btnShortFace).
 */
@Composable
fun ActionRows(key: String, title: String, hint: String? = null, top: Boolean = true) {
    val cfg by Cfg.data.collectAsState()
    val sounds by Soundboard.sounds.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    @Suppress("UNUSED_EXPRESSION") sounds
    CfgStrPick(key, title, hint, BTN_ACTIONS, top = top)
    when (Cfg.str(key)) {
        "face" -> CfgFacePick(key + "Face", "$title · which face")
        "phrase" -> CfgPick(key + "Phrase", "$title · which phrase", null, phraseOptions(), { Cfg.int(it) }, { it })
        "sound" -> CfgPick(key + "Phrase", "$title · which sound", "Plays on this phone, through the voice output",
            soundOptions(), { Cfg.int(it) }, { it })
    }
}

/** The face picture in a tile. */
@Composable
fun FacePic(name: String, modifier: Modifier = Modifier) {
    val ver by Faces.version.collectAsState()
    val a = LocalAccent.current
    @Suppress("UNUSED_EXPRESSION") ver
    val lit = android.graphics.Color.rgb((a.red * 255).toInt(), (a.green * 255).toInt(), (a.blue * 255).toInt())
    val off = android.graphics.Color.rgb(0x14, 0x1C, 0x22)
    val pic = remember(name, ver, a) { Faces.preview(name, lit, off) }
    Box(modifier.background(Color(0xFF07090B)), contentAlignment = Alignment.Center) {
        if (pic != null) androidx.compose.foundation.Image(pic, null, Modifier.fillMaxWidth(),
            contentScale = androidx.compose.ui.layout.ContentScale.Fit, filterQuality = androidx.compose.ui.graphics.FilterQuality.None)
        else Box(Modifier.fillMaxWidth().height(30.dp))
    }
}

/** A label/value cell for status grids. */
@Composable
fun Stat(lab: String, value: String, modifier: Modifier = Modifier, color: Color = Hud.Text) {
    Column(modifier.background(Hud.FieldBg, RoundedCornerShape(2.dp)).border(1.dp, Hud.Line, RoundedCornerShape(2.dp))
        .padding(horizontal = 10.dp, vertical = 8.dp)) {
        Caps(lab, size = 9.sp)
        Spacer(Modifier.height(3.dp))
        Mono(value, color = color, size = 13.sp, maxLines = 2)
    }
}

@Composable
fun Dot(color: Color) = Box(Modifier.size(7.dp).background(color, androidx.compose.foundation.shape.CircleShape))

@Composable
fun Gap(h: Int = 8) = Spacer(Modifier.height(h.dp))

@Composable
fun HGap(w: Int = 8) = Spacer(Modifier.width(w.dp))

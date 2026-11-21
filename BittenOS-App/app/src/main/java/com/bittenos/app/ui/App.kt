package com.bittenos.app.ui

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.os.Build
import android.provider.Settings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.bittenos.app.AppPrefs
import com.bittenos.app.Cfg
import com.bittenos.app.Faces
import com.bittenos.app.MaskLink
import com.bittenos.app.Pending
import com.bittenos.app.Soundboard
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject

/** Where the app is: a stack of screens, so Back walks out the way you came in. */
object Nav {
    val stack = androidx.compose.runtime.mutableStateListOf("home")
    fun go(r: String) { stack.add(r) }
    fun back() { if (stack.size > 1) stack.removeAt(stack.size - 1) }
    fun home() { while (stack.size > 1) stack.removeAt(stack.size - 1) }
    /** A page from the mask itself: the face maker, the screen designer, the guide. */
    fun web(path: String) = go("web:$path")
}

@Composable
fun AppScreen() {
    val accent by AppPrefs.accent.collectAsState()
    HudTheme(accent.color) {
        val link by MaskLink.state.collectAsState()
        val route = Nav.stack.last()
        val host = (link as? MaskLink.State.Connected)?.host
        LaunchedEffect(host) {
            // A new connection: read the real settings and the faces once
            if (host != null) { Cfg.load(); Faces.refresh() }
        }
        BackHandler(enabled = Nav.stack.size > 1) { Nav.back() }
        Box(Modifier.fillMaxSize().stripes().imePadding()) {
            if (route.startsWith("web:")) WebPage(route.removePrefix("web:"))
            else Column(Modifier.fillMaxSize()) {
                Top(link)
                WaitingBar(link is MaskLink.State.Connected)
                Box(Modifier.weight(1f)) {
                    when (route) {
                        "home" -> HomeScreen(link)
                        "menu" -> MenuScreen(link)
                        "voice" -> VoiceScreen(link)
                        "mic" -> MicScreen()
                        "lights" -> LightsScreen()
                        "sensors" -> SensorsScreen()
                        "behaviour" -> BehaviourScreen()
                        "buttons" -> ButtonsScreen()
                        "create" -> CreateScreen()
                        "network" -> NetworkScreen()
                        "system" -> SystemScreen()
                        "update" -> UpdateScreen()
                        "rave" -> RaveScreen()
                        "sounds" -> SoundsScreen()
                        else -> HomeScreen(link)
                    }
                }
            }
            ToastHost(Modifier.align(Alignment.BottomCenter).padding(bottom = 18.dp))
        }
        ConflictSheet()
    }
}

@Composable
private fun Top(link: MaskLink.State) {
    val conn = link as? MaskLink.State.Connected
    val s = conn?.stats ?: MaskLink.lastStats ?: JSONObject()
    val scope = rememberCoroutineScope()
    HudHeader(
        onMenu = { if (Nav.stack.last() == "menu") Nav.back() else Nav.go("menu") },
        onRave = { if (Nav.stack.last() != "rave") Nav.go("rave") },
        rave = conn != null && s.optBoolean("rave"),
        sleeping = conn != null && s.optBoolean("sleeping"),
        display = s.optBoolean("display", true),
        online = conn != null,
        onSleep = { scope.launch {
            val want = !s.optBoolean("sleeping")
            val t = MaskLink.sleep(want)?.trim()
            if (t == "on" || t == "off") Toast.ok(if (t == "on") "Sleeping" else "Awake") else Toast.bad(t ?: "No mask")
        } },
        onPower = { scope.launch {
            val want = !s.optBoolean("display", true)
            val t = MaskLink.panels(want)?.trim()
            if (t == "on" || t == "off") Toast.ok(if (t == "on") "Displays on" else "Displays off") else Toast.bad(t ?: "No mask")
        } },
    )
}

fun openWifiSettings(ctx: Context) {
    val i = if (Build.VERSION.SDK_INT >= 29) Intent(Settings.Panel.ACTION_WIFI) else Intent(Settings.ACTION_WIFI_SETTINGS)
    ctx.startActivity(i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
}

// ---- home -----------------------------------------------------------------------------------

@Composable
fun HomeScreen(link: MaskLink.State) {
    val ctx = LocalContext.current
    val conn = link as? MaskLink.State.Connected
    val s = conn?.stats ?: MaskLink.lastStats ?: JSONObject()
    HudColumn {
        LinkPanel(link) { openWifiSettings(ctx) }
        UpdateBanner()
        FacesPanel(conn, s)
        BrightnessPanel(conn, s)
        SayPanel(conn)
        SoundsPanel()
        Row(Modifier.fillMaxWidth().padding(horizontal = Hud.Edge + 2.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Mono(if (s.has("uptime")) "UP " + fmtUptime(s.optLong("uptime")) else "—", color = Hud.Faint, size = 9.5.sp)
            Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                Caps("BITTEN·OS", color = Hud.Text2, size = 10.sp, spacing = 0.2.em)
                Caps("FURBITTEN 14-PANEL", color = LocalAccent.current, size = 8.5.sp, spacing = 0.24.em)
            }
            Mono(if (s.has("loopHz")) "LOOP ${s.optInt("loopHz")} HZ" else "—", color = Hud.Faint, size = 9.5.sp)
        }
    }
}

fun fmtUptime(sec: Long): String {
    val d = sec / 86400; val h = sec % 86400 / 3600; val m = sec % 3600 / 60
    return if (d > 0) "${d}D ${h}H" else "%02d:%02d:%02d".format(h, m, sec % 60)
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun FacesPanel(conn: MaskLink.State.Connected?, s: JSONObject) {
    val faces by Faces.list.collectAsState()
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    val scope = rememberCoroutineScope()
    val current = s.optString("anim")
    var held by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf<String?>(null) }
    Panel("01", "Faces", trailing = { Mono("HOLD A FACE FOR OPTIONS", color = Hud.Faint, size = 9.sp) }) {
        if (faces.isEmpty()) Note(if (conn == null) "No faces seen yet. Join the mask's WiFi and they appear here." else "Loading faces…")
        faces.chunked(4).forEach { row ->
            Row(Modifier.fillMaxWidth().padding(bottom = Hud.TileGap), horizontalArrangement = Arrangement.spacedBy(Hud.TileGap)) {
                row.forEach { f ->
                    val on = f == current
                    val a = LocalAccent.current
                    Column(Modifier.weight(1f).background(if (on) accentDeep(a) else Hud.FieldBg, RoundedCornerShape(2.dp))
                        .border(1.dp, if (held == f) Hud.Text else if (on) a else Hud.Line, RoundedCornerShape(2.dp))
                        .combinedClickable(onLongClick = { held = f }) {
                            if (conn == null) { Toast.bad("Faces need the mask in reach"); return@combinedClickable }
                            busy = f
                            scope.launch {
                                val (ok, why) = MaskLink.okRedirect("/change?anim=" + MaskLink.enc(f))
                                if (!ok) Toast.bad(why)
                                busy = null
                            }
                        }.padding(start = 4.dp, end = 4.dp, top = 4.dp)) {
                        FacePic(f, Modifier.fillMaxWidth())
                        Row(Modifier.fillMaxWidth().height(20.dp), verticalAlignment = Alignment.CenterVertically) {
                            Caps(Faces.label(f) + if (busy == f) " …" else "", Modifier.weight(1f), color = if (on) a else Color(0xFF9AA6AF),
                                size = 8.5.sp, weight = FontWeight.Bold, spacing = 0.1.em)
                            if (on) Box(Modifier.background(a, RoundedCornerShape(1.dp)).padding(horizontal = 3.dp)) {
                                Mono("ON", color = onAccent(a), size = 8.sp)
                            }
                        }
                    }
                }
                repeat(4 - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
        Box(Modifier.fillMaxWidth().height(34.dp).border(1.dp, lerpColor(Hud.Bg, LocalAccent.current, 0.5f), RoundedCornerShape(2.dp))
            .clickable(enabled = conn != null) { Nav.web("/animator.html") }, contentAlignment = Alignment.Center) {
            Caps("+ New face", color = LocalAccent.current, size = 9.5.sp, weight = FontWeight.Bold)
        }
        if (conn != null && current.endsWith(".json") && Faces.canBlink(current)) {
            Gap(10)
            Field {
                LabVal("Blinks · " + Faces.label(current), when (Cfg.str("blinkMode")) {
                    "blink" -> "MASK BLINKS"; "still" -> "NO BLINKING"; else -> "FACE'S OWN" })
                CfgSeg("blinkMode", listOf("face" to "Face's own", "blink" to "Mask", "still" to "None"), { Cfg.str(it) }, { it })
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Caps("Double blink", Modifier.weight(1f))
                    NumStepper(Cfg.dbl("blinkDoublePct"), 0.0, 100.0, 5.0, { "${it.toInt()}%" }) { v ->
                        scope.launch { Cfg.set("blinkDoublePct" to v.toInt()) }
                    }
                }
            }
        }
    }
    held?.let { FaceSheet(it, current, conn != null) { held = null } }
}

@Composable
private fun FaceSheet(name: String, current: String, online: Boolean, close: () -> Unit) {
    val scope = rememberCoroutineScope()
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    var renaming by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf(false) }
    val label = Faces.label(name)
    val used = Faces.usedAs(name)
    val onNow = name == current
    val boot = name == "default.json"
    val skip = Cfg.str("faceSkip").split(';').contains(name)
    Sheet(label, close) {
        Mono("$name · ${Faces.frames(name)} frame${if (Faces.frames(name) == 1) "" else "s"} · %.1f KB".format(Faces.bytes(name) / 1024.0),
            color = Hud.Dim, size = 11.sp)
        if (used.isNotEmpty()) Caps("Used as " + used.joinToString(", "), Modifier.padding(top = 6.dp), color = Hud.Amber, size = 9.sp, maxLines = 3)
        if (onNow) Caps("On the mask now", Modifier.padding(top = 6.dp), color = LocalAccent.current, size = 9.sp)
        Gap(12)
        Column(verticalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Show on the mask", {
                scope.launch { val (ok, why) = MaskLink.okRedirect("/change?anim=" + MaskLink.enc(name)); if (!ok) Toast.bad(why) }
                close()
            }, Modifier.fillMaxWidth(), BtnKind.Fill, enabled = online && !onNow)
            HudButton("Edit in face maker", { AppPrefs.setLastEdited(name); close(); Nav.web("/animator.html?face=" + MaskLink.enc(name)) },
                Modifier.fillMaxWidth(), enabled = online)
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                HudButton("Rename", { renaming = true }, Modifier.weight(1f), enabled = online)
                HudButton(if (onNow) "Delete · switch face first" else if (boot) "Delete · startup face" else "Delete · asks first",
                    { deleting = true }, Modifier.weight(1f), BtnKind.Danger, enabled = online && !onNow && !boot)
            }
            HudButton(if (skip) "Next face skips it · tap to include" else "In the Next face cycle · tap to skip", {
                val list = Cfg.str("faceSkip").split(';').filter { it.isNotEmpty() }.toMutableList()
                if (skip) list.remove(name) else list.add(name)
                scope.launch { Cfg.set("faceSkip" to list.joinToString(";"), ok = if (skip) "$label back in the cycle" else "Next face skips $label") }
            }, Modifier.fillMaxWidth(), if (skip) BtnKind.Plain else BtnKind.On, enabled = online)
        }
        if (onNow || boot) Hint(if (onNow) "Delete is locked while this face is on the mask." else "default is the face the mask loads at startup, so it stays.",
            Modifier.padding(top = 8.dp))
    }
    if (renaming) RenameSheet(name, onNow) { renaming = false; close() }
    if (deleting) Confirm("Delete $label?",
        (if (used.isNotEmpty()) "Used as ${used.joinToString(", ")}. Those settings will need a new face. " else "") +
            "This can't be undone. Download every face first (Create) if you want a copy.", "Delete", {
            scope.launch {
                val (ok, why) = Faces.delete(name)
                if (ok) { Faces.refresh(); Toast.ok("$label deleted") } else Toast.bad(why)
            }
            close()
        }, { deleting = false })
}

@Composable
private fun RenameSheet(name: String, onNow: Boolean, close: () -> Unit) {
    val scope = rememberCoroutineScope()
    val label = Faces.label(name)
    var typed by remember { mutableStateOf(label) }
    val faces by Faces.list.collectAsState()
    val valid = Regex("^[A-Za-z0-9_-]+$").matches(typed)
    val taken = typed != label && faces.contains("$typed.json")
    Sheet("Rename face", close) {
        Mono("$name · ${Faces.frames(name)} frames", color = Hud.Dim, size = 11.sp)
        Gap(10)
        Caps("New name")
        Gap(6)
        HudInput(typed, { typed = it.trim() }, "name", Modifier.fillMaxWidth(), maxLen = 24)
        Hint(when {
            !valid -> "Letters, numbers, - and _ only."
            taken -> "A face called $typed already exists."
            else -> "Settings that use this face keep the old name, so re-pick it there after."
        }, Modifier.padding(top = 6.dp), color = if (!valid || taken) Hud.Red else Hud.Faint)
        Gap(12)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Cancel", close, Modifier.weight(1f))
            HudButton("Rename", {
                scope.launch {
                    val err = Faces.rename(name, typed, onNow)
                    if (err == null) { Faces.refresh(); Toast.ok("Renamed to $typed") } else Toast.bad(err)
                }
                close()
            }, Modifier.weight(1f), BtnKind.Fill, enabled = valid && !taken && typed != label)
        }
    }
}

@Composable
private fun BrightnessPanel(conn: MaskLink.State.Connected?, s: JSONObject) {
    val scope = rememberCoroutineScope()
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    val waiting by Pending.list.collectAsState()
    val queued = waiting.firstOrNull { it.key == Pending.Key.Brightness }?.value
    val auto = Cfg.bool("autoBriEna")
    var bri by remember { mutableFloatStateOf((queued ?: s.optInt("bright", 5)).toFloat()) }
    var dragging by remember { mutableStateOf(false) }
    var lastSent by remember { mutableLongStateOf(0L) }
    LaunchedEffect(s.optInt("bright", -1), queued) {
        if (!dragging) (queued ?: s.optInt("bright", -1).takeIf { it >= 0 })?.let { bri = it.toFloat() }
    }
    Panel("02", "Brightness", trailing = {
        Caps("Auto", color = Hud.Dim)
        HGap(8)
        HudToggle(auto, { v -> scope.launch { Cfg.set("autoBriEna" to v) } }, enabled = conn != null)
    }) {
        LabVal("Visor panels", (if (auto) "AUTO · " else "") + "%02d / 15".format(bri.toInt()) +
            if (queued != null && conn == null) " · WAITING" else "")
        HudSlider(bri, {
            bri = it; dragging = true
            val now = System.currentTimeMillis()
            if (conn != null && now - lastSent > 120) { lastSent = now; scope.launch { MaskLink.brightness(it.toInt(), false) } }
        }, 0f..15f, steps = 14, enabled = !auto, onDone = {
            dragging = false
            if (conn != null) scope.launch { MaskLink.brightness(bri.toInt(), true) }
            else Pending.put(Pending.Key.Brightness, bri.toInt(), s.optInt("bright", -1).takeIf { it >= 0 })
        })
        if (auto) Hint("The light sensor sets this. Its range is in Lights & screen.")
        else if (conn == null) Hint("The mask is away: a change here waits and goes across when it's back.")
    }
}

private val SPEEDS = listOf(160 to "V.slow", 90 to "Slow", 55 to "Normal", 30 to "Fast")

@Composable
private fun SayPanel(conn: MaskLink.State.Connected?) {
    val scope = rememberCoroutineScope()
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    var text by rememberSaveable { mutableStateOf("") }
    var speed by rememberSaveable { mutableIntStateOf(55) }
    var once by rememberSaveable { mutableStateOf(false) }
    val online = conn != null
    fun send() {
        val t = text.trim()
        if (t.isEmpty()) { Toast.bad("Type a message first"); return }
        val cur = (MaskLink.state.value as? MaskLink.State.Connected)?.stats?.optString("anim")?.takeIf { it.endsWith(".json") }
        val back = cur ?: "default.json"
        val anim = Faces.textAnim(t, speed, if (once) back else null, cur) ?: run { Toast.bad("No panel layout yet"); return }
        scope.launch {
            val err = Faces.showBuilt(anim)
            if (err == null) Toast.ok(if (once) "Showing your text once" else "Showing your text") else Toast.bad(err)
        }
    }
    Panel("03", "Say") {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            HudInput(text, { text = it }, "Type a message", Modifier.weight(1f), onDone = { send() })
            HudButton("Show", { send() }, kind = BtnKind.Out, enabled = online)
        }
        Gap(6)
        Seg(SPEEDS, speed, { speed = it })
        Gap(4)
        Seg(listOf(false to "Repeat", true to "Play once"), once, { once = it })
        Row(Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Caps("Quick phrases", Modifier.weight(1f))
            LinkBtn("Edit ›", { Nav.go("create") })
        }
        val filled = (0 until 6).filter { Cfg.phrase(it).isNotEmpty() }
        if (filled.isEmpty()) Note("No phrases saved yet. Edit adds them.")
        filled.chunked(2).forEach { row ->
            Row(Modifier.fillMaxWidth().padding(bottom = Hud.SegGap), horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                row.forEach { i ->
                    HudButton(Cfg.phrase(i), {
                        scope.launch {
                            val t = MaskLink.fetch("/phrase?slot=$i")?.trim()
                            if (t == "ok") Toast.ok("Showing \"${Cfg.phrase(i)}\"") else Toast.bad(t ?: "No mask")
                        }
                    }, Modifier.weight(1f), enabled = online)
                }
                if (row.size == 1) Spacer(Modifier.weight(1f))
            }
        }
        Hint("Scrolls across the mouth while the eyes keep the face. Repeat runs until you tap a face.")
    }
}

@Composable
private fun SoundsPanel() {
    val sounds by Soundboard.sounds.collectAsState()
    val playing by Soundboard.playing.collectAsState()
    Panel("04", "Soundboard", titleColor = Hud.Cyan, trailing = { LinkBtn("All ›", { Nav.go("sounds") }, Hud.Cyan) }) {
        if (sounds.isEmpty()) {
            Note("Sounds that play from this phone, with a tap or a remote button.")
            Gap(8)
            HudButton("+ Add sounds", { Nav.go("sounds") }, Modifier.fillMaxWidth(), BtnKind.Cyan)
        } else {
            sounds.take(6).chunked(3).forEach { row ->
                Row(Modifier.fillMaxWidth().padding(bottom = Hud.SegGap), horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                    row.forEach { snd ->
                        HudButton(snd.name, { if (playing == snd.slot) Soundboard.stop() else Soundboard.play(snd.slot) },
                            Modifier.weight(1f), if (playing == snd.slot) BtnKind.Cyan else BtnKind.Plain,
                            sub = if (playing == snd.slot) "■ STOP" else "%02d".format(snd.slot + 1))
                    }
                    repeat(3 - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
        }
    }
}

// ---- menu -----------------------------------------------------------------------------------

private val MENU = listOf(
    Triple("home", "Faces & say", "Tiles · blinks · brightness · text"),
    Triple("voice", "Voice & mic", "Through the phone · mouth mic"),
    Triple("lights", "Lights & screen", "Cheeks · auto-bright · status screen"),
    Triple("sensors", "Sensors", "Boop · boop counter · tilt"),
    Triple("behaviour", "Behaviour", "Face changes · sleep · startup"),
    Triple("buttons", "Buttons & remote", "Presses · next face order · ESP-NOW remote"),
    Triple("create", "Create", "Face maker · screen designer · files · phrases"),
    Triple("network", "Network & app", "Mask WiFi · join a network · this app"),
    Triple("system", "System", "Status · easter eggs · firmware"),
    Triple("rave", "Spectrum rave", "VU bar · spectrum · cheeks"),
    Triple("sounds", "Soundboard", "Sounds from this phone · remote buttons"),
)

@Composable
fun MenuScreen(link: MaskLink.State) {
    val a = LocalAccent.current
    val voice by com.bittenos.app.VoiceService.on.collectAsState()
    HudColumn {
        Crumb("000", "Menu // navigation")
        MENU.forEachIndexed { i, (r, t, sub) ->
            val c = if (r == "rave") Hud.Pink else if (r == "sounds") Hud.Cyan else a
            Row(Modifier.padding(horizontal = Hud.Edge).padding(bottom = 6.dp).fillMaxWidth().heightIn(min = 58.dp)
                .background(Hud.PanelBg, RoundedCornerShape(2.dp)).border(1.dp, Hud.Line, RoundedCornerShape(2.dp))
                .clickable { if (r == "home") Nav.home() else { Nav.back(); Nav.go(r) } }.padding(horizontal = 14.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Caps(t, color = Hud.Text, size = 12.sp, weight = FontWeight.Bold, spacing = 0.14.em)
                    Caps(sub, color = Hud.Faint, size = 9.sp, spacing = 0.08.em)
                }
                Mono("%03d".format(i + 1), color = c, size = 11.sp)
                HGap(8)
                Text("›", color = c, fontSize = 20.sp)
            }
        }
        Gap(8)
        val conn = link as? MaskLink.State.Connected
        Row(Modifier.padding(horizontal = Hud.Edge + 2.dp).fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Dot(if (conn != null) a else Hud.Amber)
            HGap(6)
            Mono(if (conn == null) "NO MASK" else if (conn.ownWifi) "ON MASK WIFI" else "SAME NETWORK · ${conn.host}",
                Modifier.weight(1f), color = if (conn != null) a else Hud.Amber, size = 10.sp)
            Mono((if (voice) "VOICE ON" else "VOICE OFF") + (conn?.stats?.let { " · ${it.optInt("wifiClients")} PHONES" } ?: ""),
                color = Hud.Dim, size = 10.sp)
        }
    }
}

// ---- changes waiting for the mask ---------------------------------------------------------------

@Composable
private fun WaitingBar(connected: Boolean) {
    val list by Pending.list.collectAsState()
    val note by Pending.note.collectAsState()
    val asking by Pending.conflicts.collectAsState()
    var open by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(note) { if (note != null) { delay(4000); Pending.clearNote() } }
    note?.let {
        Row(Modifier.padding(horizontal = Hud.Edge).padding(bottom = 8.dp).fillMaxWidth()
            .background(accentDeep(), RoundedCornerShape(2.dp)).border(1.dp, LocalAccent.current, RoundedCornerShape(2.dp))
            .padding(10.dp)) { Caps("✓ $it", color = LocalAccent.current, weight = FontWeight.Bold) }
    }
    if (list.isEmpty()) return
    Column(Modifier.padding(horizontal = Hud.Edge).padding(bottom = 10.dp).fillMaxWidth()
        .background(Color(0xFF160F05), RoundedCornerShape(2.dp)).border(1.dp, Color(0xFF6A4E17), RoundedCornerShape(2.dp))) {
        Row(Modifier.fillMaxWidth().heightIn(min = 40.dp).clickable { open = !open }.padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Caps("${list.size} waiting", color = Hud.Amber, weight = FontWeight.Bold)
            Mono(if (asking.isNotEmpty()) " · NEEDS YOUR ANSWER" else if (connected) " · SENDING…" else " · GO ACROSS WHEN THE MASK IS BACK",
                Modifier.weight(1f), color = Hud.Amber, size = 10.sp)
            Caps(if (open) "Hide" else "See", color = Hud.Amber, weight = FontWeight.Bold)
        }
        if (open) list.forEach { c ->
            Row(Modifier.fillMaxWidth().border(1.dp, Color(0xFF2A2010)).padding(horizontal = 10.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Caps(c.key.label, color = Hud.Text, size = 10.5.sp, weight = FontWeight.Bold)
                    c.base?.let { Mono("was $it${c.key.unit} on the mask", color = Color(0xFF8B7A55), size = 9.5.sp) }
                }
                Mono("${c.value}${c.key.unit}", color = Hud.Amber, size = 11.sp)
                HGap(8)
                Box(Modifier.border(1.dp, Color(0xFF3A2E14), RoundedCornerShape(2.dp)).clickable { Pending.discard(c.key) }
                    .padding(horizontal = 10.dp, vertical = 8.dp)) { Caps("Discard", color = Color(0xFFC9A55A), size = 9.sp, weight = FontWeight.Bold) }
            }
        }
    }
}

@Composable
private fun ConflictSheet() {
    val conflicts by Pending.conflicts.collectAsState()
    val cf = conflicts.firstOrNull() ?: return
    val scope = rememberCoroutineScope()
    val k = cf.change.key
    Sheet("${k.label} changed on the mask", {}, titleColor = Hud.Amber) {
        Note("While the app was away it was set on the mask itself (the web page, the button or the remote). Which one should it keep?",
            color = Hud.Text2)
        Gap(10)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Stat("On the mask", "${cf.maskNow}${k.unit}", Modifier.weight(1f))
            Stat("Yours", "${cf.change.value}${k.unit}", Modifier.weight(1f), Hud.Amber)
        }
        Gap(12)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("The mask's", { scope.launch { Pending.resolve(k, keepMine = false) } }, Modifier.weight(1f))
            HudButton("Mine", { scope.launch { Pending.resolve(k, keepMine = true) } }, Modifier.weight(1f), BtnKind.Amber)
        }
    }
}

// ---- the mask's own pages, inside the app --------------------------------------------------------

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun WebPage(path: String) {
    val host = MaskLink.host
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = Hud.Edge, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            HudButton("‹ Back", { Nav.back() }, kind = BtnKind.On, height = 38.dp)
            Caps(when {
                path.startsWith("/animator") -> "007 // Face maker"
                path.startsWith("/screen") -> "007 // Screen designer"
                path.startsWith("/docs") -> "009 // How to use"
                else -> "The web page"
            }, Modifier.weight(1f), color = LocalAccent.current, size = 11.sp, weight = FontWeight.Bold)
        }
        if (host == null) {
            Note("This page is stored on the mask, so it needs the mask in reach.", Modifier.padding(Hud.Edge))
            return
        }
        AndroidView(factory = { c ->
            WebView(c).apply {
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                webViewClient = object : WebViewClient() {
                    override fun onPageFinished(view: WebView?, url: String?) {
                        // Leaving the face maker: the faces may have changed
                        kotlinx.coroutines.MainScope().launch { Faces.refresh() }
                    }
                }
                setBackgroundColor(android.graphics.Color.rgb(0x0B, 0x0F, 0x13))
                loadUrl("http://$host$path")
            }
        }, modifier = Modifier.fillMaxSize())
    }
}


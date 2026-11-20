package com.bittenos.app.ui

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Face
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MoreHoriz
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.bittenos.app.MaskLink
import com.bittenos.app.Pending
import com.bittenos.app.VoiceEngine
import com.bittenos.app.VoiceService
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject

private enum class Tab(val label: String) { Faces("Faces"), Controls("Controls"), Voice("Voice"), More("More") }

@Composable
fun AppScreen() {
    val link by MaskLink.state.collectAsState()
    var tab by rememberSaveable { mutableStateOf(Tab.Faces) }
    var webOpen by rememberSaveable { mutableStateOf(false) }
    val conn = link as? MaskLink.State.Connected

    if (webOpen && conn != null) {
        WebPage(conn.host) { webOpen = false }
        return
    }

    Scaffold(
        topBar = { Column { ConnectionBanner(link); WaitingBar(conn != null) } },
        bottomBar = {
            NavigationBar {
                Tab.entries.forEach { t ->
                    NavigationBarItem(
                        selected = tab == t,
                        onClick = { tab = t },
                        icon = {
                            Icon(when (t) {
                                Tab.Faces -> Icons.Filled.Face
                                Tab.Controls -> Icons.Filled.Tune
                                Tab.Voice -> Icons.Filled.Mic
                                Tab.More -> Icons.Filled.MoreHoriz
                            }, contentDescription = null)
                        },
                        label = { Text(t.label) },
                    )
                }
            }
        },
    ) { pad ->
        Box(Modifier.padding(pad).fillMaxSize()) {
            when (tab) {
                Tab.Faces -> if (conn != null) FacesTab(conn) else OfflineFaces()
                Tab.Controls -> ControlsTab(conn)
                Tab.Voice -> VoiceTab(conn)
                Tab.More -> MoreTab(conn) { webOpen = true }
            }
        }
    }
    ConflictDialog()
}

// ---- changes waiting for the mask --------------------------------------------------------

@Composable
private fun WaitingBar(connected: Boolean) {
    val list by Pending.list.collectAsState()
    val note by Pending.note.collectAsState()
    val asking by Pending.conflicts.collectAsState()
    var open by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(note) { if (note != null) { delay(4000); Pending.clearNote() } }

    note?.let {
        Row(Modifier.fillMaxWidth().background(Good.copy(alpha = 0.14f)).padding(horizontal = 16.dp, vertical = 8.dp)) {
            Text("\u2713  $it", color = Good, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        }
    }
    if (list.isEmpty()) return
    Column(Modifier.fillMaxWidth().background(Warn.copy(alpha = 0.12f))) {
        Row(Modifier.fillMaxWidth().clickable { open = !open }.padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text((if (list.size == 1) "1 change waiting" else "${list.size} changes waiting") +
                    if (asking.isNotEmpty()) "  \u00b7  needs your answer" else if (connected) "  \u00b7  sending\u2026" else "  \u00b7  goes across when the mask is back",
                Modifier.weight(1f), color = Warn, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
            Text(if (open) "Hide" else "See", color = Warn, fontSize = 13.sp)
        }
        if (open) list.forEach { c ->
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("${c.key.label}  \u2192  ${c.value}${c.key.unit}", fontSize = 14.sp)
                    c.base?.let { Text("was $it${c.key.unit} on the mask", color = Muted, fontSize = 12.sp) }
                }
                TextButton(onClick = { Pending.discard(c.key) }) { Text("Discard") }
            }
        }
    }
}

@Composable
private fun ConflictDialog() {
    val conflicts by Pending.conflicts.collectAsState()
    val cf = conflicts.firstOrNull() ?: return
    val scope = rememberCoroutineScope()
    val k = cf.change.key
    AlertDialog(
        onDismissRequest = {},
        title = { Text("${k.label} changed on the mask") },
        text = {
            Text("While the app was away, it was set to ${cf.maskNow}${k.unit} on the mask. " +
                 "You set ${cf.change.value}${k.unit} here. Which one should it keep?")
        },
        confirmButton = {
            TextButton(onClick = { scope.launch { Pending.resolve(k, keepMine = true) } }) {
                Text("Mine (${cf.change.value}${k.unit})")
            }
        },
        dismissButton = {
            TextButton(onClick = { scope.launch { Pending.resolve(k, keepMine = false) } }) {
                Text("The mask's (${cf.maskNow}${k.unit})")
            }
        },
    )
}

// ---- the banner: the point of "knows when it is on the WiFi" ------------------------------

@Composable
private fun ConnectionBanner(link: MaskLink.State) {
    val ctx = LocalContext.current
    val (dot, title, sub) = when (link) {
        is MaskLink.State.Connected -> {
            val s = link.stats
            val phones = s.optInt("wifiClients", 0)
            val bits = mutableListOf<String>()
            if (link.ownWifi) {
                bits += if (phones == 1) "1 phone on it" else "$phones phones on it"
            } else {
                bits += link.host
                s.optString("mdns").takeIf { it.isNotBlank() }?.let { bits += "$it.local" }
            }
            if (s.has("tempC")) bits += "%.0f°C".format(s.optDouble("tempC"))
            Triple(Good, if (link.ownWifi) "On the mask's WiFi" else "Mask found on this network",
                bits.joinToString("  ·  "))
        }
        MaskLink.State.Searching -> Triple(Muted, "Looking for the mask…", "")
        MaskLink.State.NotFound -> Triple(Warn, "Not on the mask's WiFi", "Join the mask's network and this finds it by itself")
        MaskLink.State.NoWifi -> Triple(Warn, "Not on WiFi", "Join the mask's network and this finds it by itself")
    }
    Row(
        Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .statusBarsPadding()
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(10.dp).background(dot, CircleShape))
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(title, fontWeight = FontWeight.SemiBold, fontSize = 16.sp)
            if (sub.isNotEmpty()) Text(sub, color = Muted, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        if (link !is MaskLink.State.Connected) {
            OutlinedButton(onClick = { openWifiSettings(ctx) }) { Text("WiFi") }
        }
    }
}

private fun openWifiSettings(ctx: Context) {
    val i = if (Build.VERSION.SDK_INT >= 29) Intent(Settings.Panel.ACTION_WIFI) else Intent(Settings.ACTION_WIFI_SETTINGS)
    ctx.startActivity(i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
}

@Composable
private fun NotConnected() {
    Column(Modifier.fillMaxSize().padding(32.dp), verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally) {
        Text("No mask yet", fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
        Spacer(Modifier.height(8.dp))
        Text("Join the mask's WiFi, or the same network it is on. The app notices by itself.",
            color = Muted)
    }
}

@Composable
private fun OfflineFaces() {
    val faces = remember { MaskLink.lastFaces }
    if (faces.isEmpty()) { NotConnected(); return }
    Column(Modifier.fillMaxSize()) {
        Text("Faces change straight away, so they need the mask in reach. Settings on Controls and Voice wait for it.",
            Modifier.padding(16.dp), color = Muted, fontSize = 13.sp)
        LazyVerticalGrid(GridCells.Adaptive(150.dp), contentPadding = PaddingValues(12.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(faces) { f ->
                Box(Modifier.fillMaxWidth().height(64.dp)
                        .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f), RoundedCornerShape(14.dp))
                        .padding(horizontal = 14.dp),
                    contentAlignment = Alignment.CenterStart) {
                    Text(faceName(f), color = Muted, maxLines = 2, overflow = TextOverflow.Ellipsis)
                }
            }
        }
    }
}

// ---- faces -------------------------------------------------------------------------------

private fun faceName(f: String) = f.removeSuffix(".json").replace('_', ' ')

@Composable
private fun FacesTab(conn: MaskLink.State.Connected) {
    val scope = rememberCoroutineScope()
    var faces by remember { mutableStateOf<List<String>?>(null) }
    var pending by remember { mutableStateOf<String?>(null) }
    var reload by remember { mutableIntStateOf(0) }
    LaunchedEffect(conn.host, reload) { faces = MaskLink.faces() }
    val current = conn.stats.optString("anim")

    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 8.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text("Showing: " + faceName(current).ifEmpty { "—" }, Modifier.weight(1f), color = Muted)
            IconButton(onClick = { faces = null; reload++ }) { Icon(Icons.Filled.Refresh, "Reload the faces") }
        }
        val list = faces
        if (list == null) {
            Text("Loading faces…", Modifier.padding(16.dp), color = Muted)
            return
        }
        LazyVerticalGrid(GridCells.Adaptive(150.dp), contentPadding = PaddingValues(12.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(list) { f ->
                val on = f == current || f.removeSuffix(".json") == current.removeSuffix(".json")
                Box(
                    Modifier
                        .fillMaxWidth()
                        .height(64.dp)
                        .background(if (on) Accent.copy(alpha = 0.16f) else MaterialTheme.colorScheme.surfaceVariant,
                            RoundedCornerShape(14.dp))
                        .border(1.dp, if (on) Accent else Color.Transparent, RoundedCornerShape(14.dp))
                        .clickable {
                            pending = f
                            scope.launch { MaskLink.showFace(f); pending = null }
                        }
                        .padding(horizontal = 14.dp),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    Text(faceName(f) + if (pending == f) "  …" else "",
                        fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal,
                        maxLines = 2, overflow = TextOverflow.Ellipsis)
                }
            }
        }
    }
}

// ---- controls ----------------------------------------------------------------------------

@Composable
private fun ControlsTab(conn: MaskLink.State.Connected?) {
    val s = conn?.stats ?: MaskLink.lastStats ?: JSONObject()
    val online = conn != null
    val scope = rememberCoroutineScope()
    val waiting by Pending.list.collectAsState()
    val queuedBri = waiting.firstOrNull { it.key == Pending.Key.Brightness }?.value
    var bri by remember { mutableFloatStateOf((queuedBri ?: s.optInt("bright", 8)).toFloat()) }
    var dragging by remember { mutableStateOf(false) }
    var lastSent by remember { mutableLongStateOf(0L) }
    LaunchedEffect(s.optInt("bright", -1), queuedBri) {
        if (!dragging) (queuedBri ?: s.optInt("bright", -1).takeIf { it >= 0 })?.let { bri = it.toFloat() }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (!online) Text(
            if (s.length() == 0) "The mask is away. Brightness changes here wait and go across when it's back."
            else "The mask is away: this is what it had last time. Brightness changes wait and go across when it's back.",
            color = Muted, fontSize = 13.sp)
        Panel {
            Text("Brightness", fontWeight = FontWeight.SemiBold)
            Text("${bri.toInt()} of 15" + if (queuedBri != null && !online) "  \u00b7  waiting" else "", color = Muted, fontSize = 13.sp)
            Slider(
                value = bri, valueRange = 0f..15f, steps = 14,
                onValueChange = {
                    bri = it; dragging = true
                    val now = System.currentTimeMillis()
                    if (online && now - lastSent > 120) {              // live while dragging, but not a flood
                        lastSent = now
                        scope.launch { MaskLink.brightness(it.toInt(), false) }
                    }
                },
                onValueChangeFinished = {
                    dragging = false
                    if (online) scope.launch { MaskLink.brightness(bri.toInt(), true) }
                    else Pending.put(Pending.Key.Brightness, bri.toInt(), s.optInt("bright", -1).takeIf { it >= 0 })
                },
            )
        }
        Panel {
            ToggleRow("Panels on", "Blank the visor and keep everything else running", s.optBoolean("display", true), online) {
                scope.launch { MaskLink.panels(it) }
            }
            HorizontalDivider(Modifier.padding(vertical = 4.dp))
            ToggleRow("Sleeping face", "The dozing face, as if it had gone quiet", s.optBoolean("sleeping"), online) {
                scope.launch { MaskLink.sleep(it) }
            }
            HorizontalDivider(Modifier.padding(vertical = 4.dp))
            ToggleRow("Rave", "The mouth becomes a music meter", s.optBoolean("rave"), online) {
                scope.launch { MaskLink.rave(it) }
            }
        }
        Panel {
            Text("Boops", fontWeight = FontWeight.SemiBold)
            Text(s.optInt("boops").toString(), fontSize = 28.sp, color = Accent)
        }
        Text("Everything else is still on the full page, under More.", color = Muted, fontSize = 13.sp)
    }
}

@Composable
private fun ToggleRow(title: String, sub: String, checked: Boolean, enabled: Boolean = true, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(title, fontWeight = FontWeight.SemiBold)
            Text(sub, color = Muted, fontSize = 13.sp)
        }
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled)
    }
}

@Composable
private fun Panel(content: @Composable () -> Unit) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        shape = RoundedCornerShape(16.dp)) {
        Column(Modifier.fillMaxWidth().padding(16.dp)) { content() }
    }
}

// ---- voice -------------------------------------------------------------------------------

@Composable
private fun VoiceTab(conn: MaskLink.State.Connected?) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val on by VoiceService.on.collectAsState()
    val v by VoiceEngine.stats.collectAsState()
    var smooth by remember { mutableIntStateOf(VoiceService.smoothingMs(ctx)) }
    var mic by remember { mutableStateOf<JSONObject?>(null) }
    val waiting by Pending.list.collectAsState()
    val queuedGain = waiting.firstOrNull { it.key == Pending.Key.VoiceGain }?.value
    var gain by remember { mutableFloatStateOf((queuedGain ?: MaskLink.lastGain ?: 500).toFloat()) }
    LaunchedEffect(conn?.host) {
        if (conn != null) MaskLink.micStream()?.let { mic = it; gain = it.optInt("gain", 500).toFloat() }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Panel {
            Text("Your voice through this phone", fontWeight = FontWeight.SemiBold, fontSize = 17.sp)
            Text("Plug the speaker's own cable into the headphone socket. Keeps playing with the screen off.",
                color = Muted, fontSize = 13.sp)
            Spacer(Modifier.height(12.dp))
            Button(
                onClick = { if (on) VoiceService.stop(ctx) else VoiceService.start(ctx) },
                enabled = on || conn != null,
                modifier = Modifier.fillMaxWidth().height(52.dp),
                colors = if (on) ButtonDefaults.buttonColors(containerColor = Warn) else ButtonDefaults.buttonColors(),
            ) { Text(if (on) "Stop" else "Start listening", fontSize = 16.sp) }
            if (mic?.optBoolean("ready", true) == false) {
                Spacer(Modifier.height(8.dp))
                Text("The mask says its mic isn't ready (type: ${mic?.optString("type")}).", color = Warn, fontSize = 13.sp)
            }
        }

        if (on) Panel {
            val status = when {
                conn == null -> "Waiting for the mask…"
                !v.receiving -> "Connected, waiting for sound… (older firmware has no UDP voice)"
                else -> "Playing"
            }
            Text(status, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
            LinearProgressIndicator(progress = { v.level.coerceIn(0f, 1f) }, Modifier.fillMaxWidth())
            Spacer(Modifier.height(12.dp))
            Text(if (v.delayMs < 0) "Delay: measuring…" else "Delay: about ${v.delayMs} ms",
                fontSize = 22.sp, color = Accent, fontWeight = FontWeight.SemiBold)
            Text("buffer ${v.bufferMs} ms  ·  phone output ${maxOf(v.outputMs, 0)} ms  ·  mask packet 10 ms",
                color = Muted, fontSize = 13.sp)
            Text(if (v.lowLatencyPath) "Using Android's fast audio path (${v.outputRate} Hz)"
                 else "Android refused the fast audio path: expect more delay",
                color = if (v.lowLatencyPath) Muted else Warn, fontSize = 13.sp)
            if (v.dryOuts > 0 || v.lostPackets > 0)
                Text("Crackles: ${v.dryOuts}  ·  packets lost: ${v.lostPackets}", color = Muted, fontSize = 13.sp)
        }

        Panel {
            Text("Smoothing", fontWeight = FontWeight.SemiBold)
            Text("Audio held back to ride over WiFi hiccups. Less is quicker but crackles sooner.",
                color = Muted, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
            val opts = listOf(20 to "Least", 40 to "Normal", 80 to "Most")
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                opts.forEachIndexed { i, (ms, label) ->
                    SegmentedButton(
                        selected = smooth == ms,
                        onClick = { smooth = ms; VoiceService.setSmoothingMs(ctx, ms) },
                        shape = SegmentedButtonDefaults.itemShape(i, opts.size),
                    ) { Text(label) }
                }
            }
        }

        Panel {
            Text("Voice gain", fontWeight = FontWeight.SemiBold)
            Text("Set on the mask. Turn it up until speech is clear, then use the phone's volume.",
                color = Muted, fontSize = 13.sp)
            Text("${gain.toInt()}%" + if (conn == null && queuedGain != null) "  \u00b7  waiting for the mask" else "",
                color = Muted, fontSize = 13.sp)
            Slider(value = gain, valueRange = 25f..2000f,
                onValueChange = { gain = it },
                onValueChangeFinished = {
                    if (conn != null) scope.launch { MaskLink.micStream(gain.toInt()) }
                    else Pending.put(Pending.Key.VoiceGain, gain.toInt(), MaskLink.lastGain)
                })
        }

        Text("Feedback comes before latency: point the speaker away from your head, and keep the gain no higher than you need.",
            color = Muted, fontSize = 13.sp)
    }
}

// ---- more --------------------------------------------------------------------------------

@Composable
private fun MoreTab(conn: MaskLink.State.Connected?, openWeb: () -> Unit) {
    val ctx = LocalContext.current
    var addr by remember { mutableStateOf(MaskLink.manualHost ?: "") }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Panel {
            Text("The full control page", fontWeight = FontWeight.SemiBold)
            Text("Every setting the mask has. Still there for anyone without the app.", color = Muted, fontSize = 13.sp)
            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = openWeb, enabled = conn != null) { Text("Open here") }
                OutlinedButton(onClick = {
                    conn?.let { ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("http://${it.host}/"))) }
                }, enabled = conn != null) { Text("Open in browser") }
            }
        }
        Panel {
            Text("Mask address", fontWeight = FontWeight.SemiBold)
            Text("Only needed if it is on a network where it can't be found by itself. On the mask's own WiFi it is always ${MaskLink.AP_HOST}.",
                color = Muted, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(addr, { addr = it }, Modifier.fillMaxWidth(), singleLine = true,
                placeholder = { Text("e.g. 192.168.1.42") })
            Spacer(Modifier.height(8.dp))
            Button(onClick = { MaskLink.manualHost = addr }) { Text("Save") }
        }
        Panel {
            Text("How it finds the mask", fontWeight = FontWeight.SemiBold)
            Text("It asks rather than reading the WiFi name: whatever answers like BittenOS is the mask. " +
                 "That needs no location permission and still works if the network is renamed. " +
                 "It checks the mask's own address, the last one that worked, and anything announcing itself on the network.",
                color = Muted, fontSize = 13.sp)
        }
    }
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun WebPage(host: String, close: () -> Unit) {
    BackHandler(onBack = close)
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surfaceContainer).statusBarsPadding(),
            verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = close) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back to the app") }
            Text("Full control page", fontWeight = FontWeight.SemiBold)
        }
        AndroidView(factory = { c ->
            WebView(c).apply {
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                webViewClient = WebViewClient()
                loadUrl("http://$host/")
            }
        }, modifier = Modifier.fillMaxSize())
    }
}

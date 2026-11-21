package com.bittenos.app.ui

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bittenos.app.AppPrefs
import com.bittenos.app.Cfg
import com.bittenos.app.Faces
import com.bittenos.app.MaskLink
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

// ---- 008 network & app ---------------------------------------------------------------------------

@Composable
fun NetworkScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val cfg by Cfg.data.collectAsState()
    val link by MaskLink.state.collectAsState()
    val s = (link as? MaskLink.State.Connected)?.stats ?: JSONObject()
    val on = link is MaskLink.State.Connected
    val accent by AppPrefs.accent.collectAsState()
    val keepOn by AppPrefs.keepOn.collectAsState()
    val screenOff by AppPrefs.screenOff.collectAsState()
    var batteryFree by remember { mutableStateOf(com.bittenos.app.AwakeService.batteryFree(ctx)) }
    // Back from Android's battery question: look again
    val owner = androidx.compose.ui.platform.LocalLifecycleOwner.current
    DisposableEffect(owner) {
        val o = androidx.lifecycle.LifecycleEventObserver { _, e ->
            if (e == androidx.lifecycle.Lifecycle.Event.ON_RESUME) batteryFree = com.bittenos.app.AwakeService.batteryFree(ctx)
        }
        owner.lifecycle.addObserver(o)
        onDispose { owner.lifecycle.removeObserver(o) }
    }
    var wifiName by remember(cfg) { mutableStateOf(Cfg.str("wifiName")) }
    var wifiPass by remember(cfg) { mutableStateOf(Cfg.str("wifiPass")) }
    var staName by remember(cfg) { mutableStateOf(Cfg.str("staName")) }
    var staPass by remember(cfg) { mutableStateOf(Cfg.str("staPass")) }
    var mdns by remember(cfg) { mutableStateOf(Cfg.str("mdnsName")) }
    var addr by remember { mutableStateOf(MaskLink.manualHost ?: "") }
    var wifiAsk by remember { mutableStateOf(false) }
    HudColumn {
        Crumb("008", "Network & app")
        Panel("01", "The mask's own WiFi") {
            Note("Always on, so you can't be locked out. Changing it drops this connection: rejoin the new name after.")
            Gap(10)
            Caps("Network name"); Gap(4)
            HudInput(wifiName, { wifiName = it }, "9+ characters", Modifier.fillMaxWidth(), maxLen = 32)
            Gap(8)
            Caps("Password"); Gap(4)
            HudInput(wifiPass, { wifiPass = it }, "9+ characters", Modifier.fillMaxWidth(), password = true, maxLen = 63)
            Gap(10)
            HudButton("Save WiFi", {
                if (wifiName.trim().length < 9 || wifiPass.length < 9) Toast.bad("Name and password need 9 characters or more")
                else wifiAsk = true
            }, Modifier.fillMaxWidth(), BtnKind.Out, enabled = on)
        }
        Panel("02", "Join a network") {
            CfgToggle("staEna", "Also join a network", "Your phone's hotspot or home WiFi, so the phone keeps its internet", top = false)
            Field {
                LabVal("Status", when {
                    !s.has("staOn") -> "—"
                    !s.optBoolean("staOn") -> "NOT JOINING"
                    s.optBoolean("staJoined") -> s.optString("staIp") + (s.optInt("staRssi").takeIf { it != 0 }?.let { " · $it dBm" } ?: "")
                    s.optString("staName").isNotEmpty() -> "TRYING ${s.optString("staName").uppercase()}…"
                    else -> "NO NETWORK SET"
                })
            }
            Gap(8)
            Caps("Network to join"); Gap(4)
            HudInput(staName, { staName = it }, "Hotspot or home WiFi", Modifier.fillMaxWidth(), maxLen = 32)
            Gap(8)
            Caps("Its password"); Gap(4)
            HudInput(staPass, { staPass = it }, "Password", Modifier.fillMaxWidth(), password = true, maxLen = 63)
            Gap(8)
            Caps("Name on that network"); Gap(4)
            HudInput(mdns, { mdns = it.lowercase().replace(Regex("[^a-z0-9-]"), "") }, "protogen", Modifier.fillMaxWidth(), maxLen = 24)
            Hint("Reach it as ${mdns.ifEmpty { "protogen" }}.local from a computer on the same network.", Modifier.padding(top = 4.dp))
            Gap(10)
            HudButton("Join", {
                if (staName.isBlank()) { Toast.bad("Give it a network name to join"); return@HudButton }
                scope.launch {
                    Cfg.set("staName" to staName.trim(), "staPass" to staPass, "mdnsName" to mdns.ifEmpty { "protogen" },
                        ok = "Joining ${staName.trim()}…")
                }
            }, Modifier.fillMaxWidth(), BtnKind.Out, enabled = on)
        }
        Panel("03", "Old BLE remote") {
            CfgToggle("bleEna", "BLE handheld remote", "The older Bluetooth remote. Starts at the next restart.", top = false)
        }
        Panel("04", "This app") {
            Caps("Accent"); Gap(6)
            Seg(com.bittenos.app.ui.AccentChoice.entries.map { it to it.label }, accent,
                { AppPrefs.setAccent(it); Faces.clearPictures() }, cols = 5, height = 38.dp)
            SetRow("Work with the screen off", "Stays linked to the mask with the phone locked: remote sounds, voice, " +
                "and changes waiting to send. Shows a small notification.") {
                HudToggle(screenOff, { AppPrefs.setScreenOff(ctx, it) })
            }
            if (screenOff) SetRow("Battery saving", if (batteryFree) "Left out, so Android won't cut the link"
                else "Android may still pause the app after a while. Let it run to stop that.") {
                if (batteryFree) Mono("OK", color = LocalAccent.current, size = 10.sp)
                else LinkBtn("Let it run ›", { com.bittenos.app.AwakeService.askBatteryFree(ctx) }, Hud.Amber)
            }
            SetRow("Keep the screen on", "While the app is open") { HudToggle(keepOn, { AppPrefs.setKeepOn(it) }) }
            SetRow("Mask address", "Only if it isn't found by itself, e.g. 192.168.1.40") {}
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                HudInput(addr, { addr = it.trim() }, "Automatic", Modifier.weight(1f), maxLen = 40)
                HudButton("Set", { MaskLink.manualHost = addr.ifEmpty { null }; Toast.ok(if (addr.isEmpty()) "Finding it by itself" else "Trying $addr") })
            }
            Gap(10)
            HudButton("Open the full web page", { Nav.web("/") }, Modifier.fillMaxWidth(), enabled = on)
            Hint("Everything the app does is also on the mask's own page, for any phone or computer.", Modifier.padding(top = 6.dp))
        }
    }
    if (wifiAsk) Confirm("Save the new WiFi?", "You'll need to reconnect to \"${wifiName.trim()}\" afterwards.", "Save", {
        scope.launch { Cfg.set("wifiName" to wifiName.trim(), "wifiPass" to wifiPass, ok = "WiFi saved · rejoin the new name") }
    }, { wifiAsk = false }, danger = false)
    @Suppress("UNUSED_EXPRESSION") ctx
}

// ---- 009 system ---------------------------------------------------------------------------------

/** Settings a loaded config must not change: the WiFi would drop this connection, the rest belong to this mask. */
private val CFG_KEEP = setOf("wifiName", "wifiPass", "staName", "staPass", "mdnsName", "boopCount", "boopFloor", "sndNames")

@Composable
fun SystemScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val link by MaskLink.state.collectAsState()
    val s = (link as? MaskLink.State.Connected)?.stats ?: JSONObject()
    val on = link is MaskLink.State.Connected
    var slots by remember { mutableStateOf<JSONObject?>(null) }
    var dino by remember { mutableStateOf<JSONObject?>(null) }
    var loadAsk by remember { mutableStateOf<JSONObject?>(null) }
    val saveCfg = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
        if (uri != null) scope.launch { saveConfigTo(ctx, uri) }
    }
    val loadCfg = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) scope.launch {
            val o = withContext(Dispatchers.IO) {
                try { ctx.contentResolver.openInputStream(uri)?.use { JSONObject(it.readBytes().decodeToString()) } } catch (e: Exception) { null }
            }
            if (o == null || !o.has("wifiName")) Toast.bad("That doesn't look like a config file") else loadAsk = o
        }
    }
    LaunchedEffect(on) { if (on) { slots = MaskLink.json("/slots"); dino = MaskLink.json("/dino") } }
    LaunchedEffect(slots?.optBoolean("on")) { while (slots?.optBoolean("on") == true) { delay(500); slots = MaskLink.json("/slots") } }
    LaunchedEffect(dino?.optBoolean("on")) { while (dino?.optBoolean("on") == true) { delay(700); dino = MaskLink.json("/dino") } }
    HudColumn {
        Crumb("009", "System")
        Panel("01", "Status") {
            fun mb(b: Long) = "%.1f MB".format(b / 1048576.0)
            val rows = listOf(
                "Firmware" to s.optString("fw", "—"), "Up" to (if (s.has("uptime")) fmtUptime(s.optLong("uptime")) else "—"),
                "Loop" to (if (s.has("loopHz")) "${s.optInt("loopHz")} Hz" else "—"), "Chip" to (if (s.has("tempC")) "%.0f °C".format(s.optDouble("tempC")) else "—"),
                "Memory free" to (if (s.has("heapFree")) "${s.optLong("heapFree") / 1024} KB" else "—"),
                "PSRAM free" to (if (s.has("psramFree")) mb(s.optLong("psramFree")) else "—"),
                "Files" to (if (s.has("fsUsed")) "${mb(s.optLong("fsUsed"))} / ${mb(s.optLong("fsTotal"))}" else "—"),
                "Phones" to (if (s.has("wifiClients")) "${s.optInt("wifiClients")}" else "—"),
                "Face" to Faces.label(s.optString("anim", "—")), "Frame" to "${s.optInt("frame")} / ${s.optInt("frames")}",
            )
            rows.chunked(2).forEach { r ->
                Row(Modifier.padding(bottom = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    r.forEach { (l, v) -> Stat(l, v, Modifier.weight(1f)) }
                }
            }
            if (s.has("i2c")) Hint("I2C: " + s.optString("i2c"))
        }
        Panel("02", "Easter eggs", titleColor = Hud.Pink) {
            SetRow("Slot machine", slots?.let { if (it.optInt("spins") > 0) (if (it.optInt("outcome") == 1) "Triple sevens! " else "") +
                "Won ${it.optInt("wins")} of ${it.optInt("spins")}" else null }, top = false) {
                HudButton(if (slots?.optBoolean("on") == true) "Stop" else "Spin",
                    { scope.launch { slots = MaskLink.json("/slots?" + if (slots?.optBoolean("on") == true) "stop=1" else "go=1") } },
                    kind = BtnKind.Pink, enabled = on, height = 38.dp)
            }
            SetRow("Dino game", dino?.let { "Score ${it.optInt("score")}" + if (it.optInt("best") > 0) " · best ${it.optInt("best")}" else "" }) {
                if (dino?.optBoolean("on") == true) HudButton("Jump", { scope.launch { dino = MaskLink.json("/dino?jump=1") } },
                    kind = BtnKind.Fill, height = 38.dp)
                HudButton(if (dino?.optBoolean("on") == true) "Stop" else "Start",
                    { scope.launch { dino = MaskLink.json("/dino?" + if (dino?.optBoolean("on") == true) "stop=1" else "go=1") } },
                    kind = BtnKind.Pink, enabled = on, height = 38.dp)
            }
            SetRow("Fake death", "The panels die out like a flat battery") {
                HudButton(if (s.optBoolean("death")) "Revive" else "Die", {
                    scope.launch { MaskLink.fetch("/death?" + if (s.optBoolean("death")) "stop=1" else "go=1"); MaskLink.kick() }
                }, kind = BtnKind.Pink, enabled = on, height = 38.dp)
            }
            CfgToggle("deathHoldEna", "Stay dead", "Off means it comes back by itself a moment after going dark")
        }
        Panel("03", "Settings file") {
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                HudButton("Save config", { saveCfg.launch("config.json") }, Modifier.weight(1f), enabled = on, sub = "TO THIS PHONE")
                HudButton("Load config", { loadCfg.launch(arrayOf("application/json", "*/*")) }, Modifier.weight(1f), enabled = on, sub = "FROM A FILE")
            }
            Hint("Loading applies everything in the file apart from the WiFi, the boop total and the resting level.", Modifier.padding(top = 8.dp))
        }
        Panel("04", "Firmware & help") {
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                HudButton("Update", { Nav.go("update") }, Modifier.weight(1f), BtnKind.Out, enabled = on)
                HudButton("How to use", { Nav.web("/docs.html") }, Modifier.weight(1f), enabled = on)
            }
        }
    }
    loadAsk?.let { o ->
        val keys = o.keys().asSequence().filter { k -> k !in CFG_KEEP && o.get(k).let { it is Number || it is Boolean || it is String } }.toList()
        Confirm("Load ${keys.size} settings?", "They replace what's on the mask now. The WiFi stays as it is.", "Load", {
            scope.launch {
                var ok = true
                keys.chunked(12).forEach { part ->
                    val (done, why) = MaskLink.saveConfig(part.map { k ->
                        k to o.get(k).let { v -> if (v is Boolean) (if (v) "true" else "false") else v.toString() }
                    })
                    if (!done) { ok = false; Toast.bad(why.ifBlank { "Not all saved" }) }
                }
                Cfg.load()
                if (ok) Toast.ok("Config loaded")
            }
        }, { loadAsk = null }, danger = false)
    }
}

private suspend fun saveConfigTo(ctx: Context, uri: Uri) {
    val t = MaskLink.fetch("/config.json", 5000) ?: return Toast.bad("No mask")
    try {
        withContext(Dispatchers.IO) { ctx.contentResolver.openOutputStream(uri)?.use { it.write(t.toByteArray()) } }
        Toast.ok("Config saved")
    } catch (e: Exception) { Toast.bad("Couldn't write the file") }
}

// ---- firmware update -------------------------------------------------------------------------------

@Composable
fun UpdateScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val on = online()
    var info by remember { mutableStateOf<JSONObject?>(null) }
    var file by remember { mutableStateOf<Pair<String, ByteArray>?>(null) }
    var kind by remember { mutableStateOf<String?>(null) }
    var progress by remember { mutableFloatStateOf(-1f) }
    var msg by remember { mutableStateOf<Pair<String, Boolean>?>(null) }
    var ask by remember { mutableStateOf(false) }
    LaunchedEffect(on) { if (on) info = MaskLink.json("/otainfo") }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) scope.launch {
            val b = withContext(Dispatchers.IO) { try { ctx.contentResolver.openInputStream(uri)?.use { it.readBytes() } } catch (e: Exception) { null } }
            if (b == null) { Toast.bad("Couldn't read that file"); return@launch }
            val name = uri.lastPathSegment?.substringAfterLast('/') ?: "firmware.bin"
            file = name to b
            kind = when {
                b.isNotEmpty() && b[0] == 0xE9.toByte() -> "app"
                b.size >= 16 && String(b, 8, 8) == "littlefs" -> "fs"
                else -> null
            }
            val i = info
            msg = when {
                kind == null -> "That isn't a firmware or filesystem image, so it won't be sent." to true
                kind == "app" && i != null && b.size > i.optLong("appSlot") -> "Too big for the firmware slot." to true
                kind == "fs" && i != null && !i.optBoolean("staging") -> "This mask has no staging area for a filesystem image." to true
                kind == "fs" && i != null && b.size.toLong() != i.optLong("fsSize") -> "Wrong size for this mask's filesystem." to true
                kind == "fs" -> "A filesystem image replaces the faces and the settings. Save config first (System)." to false
                else -> null
            }
        }
    }
    HudColumn {
        Crumb("009.B", "Update")
        Panel("01", "On the mask") {
            fun mb(n: Long) = "%.2f MB".format(n / 1048576.0)
            val i = info
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Stat("Running", i?.let { it.optString("running") + if (!it.optBoolean("confirmed", true)) " (probation)" else "" } ?: "—", Modifier.weight(1f))
                Stat("Built", i?.optString("built") ?: "—", Modifier.weight(1f))
            }
            Gap(6)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Stat("Firmware", i?.let { mb(it.optLong("appSize")) + " / " + mb(it.optLong("appSlot")) } ?: "—", Modifier.weight(1f))
                Stat("Filesystem", i?.let { mb(it.optLong("fsSize")) + if (!it.optBoolean("staging")) " · no staging" else "" } ?: "—", Modifier.weight(1f))
            }
        }
        Panel("02", "Send an update") {
            HudButton(file?.let { "${it.first} · %.2f MB".format(it.second.size / 1048576.0) } ?: "Pick a .bin file",
                { pick.launch(arrayOf("application/octet-stream", "*/*")) }, Modifier.fillMaxWidth(), enabled = on && progress < 0,
                sub = when (kind) { "app" -> "FIRMWARE"; "fs" -> "FILESYSTEM"; null -> if (file != null) "UNKNOWN" else null; else -> null })
            if (progress >= 0) { Gap(10); Gauge(progress) }
            msg?.let { (t, bad) -> Note(t, Modifier.padding(top = 8.dp), if (bad) Hud.Red else Hud.Amber) }
            Gap(10)
            HudButton("Send to the mask", { ask = true }, Modifier.fillMaxWidth(), BtnKind.Fill,
                enabled = on && file != null && kind != null && msg?.second != true && progress < 0)
            Hint("The mask checks the image before it switches over, and goes back to the old one if the new one doesn't start.",
                Modifier.padding(top = 8.dp))
        }
    }
    if (ask) Confirm("Send ${if (kind == "fs") "the filesystem" else "the firmware"}?", "The mask restarts when it's done. Keep this phone close.", "Send", {
        val f = file ?: return@Confirm
        progress = 0f; msg = null
        scope.launch {
            val r = MaskLink.post("/ota", listOf(Triple("file", f.first, f.second)), timeout = 120_000) { p -> progress = p }
            if (r == null || r.first !in 200..299) {
                progress = -1f
                msg = (r?.second?.ifBlank { null } ?: "The connection dropped before the upload finished. Nothing was changed.") to true
                return@launch
            }
            progress = 1f
            msg = "Accepted. The mask is restarting…" to false
            repeat(60) {
                delay(2000)
                MaskLink.kick()
                val i = MaskLink.json("/otainfo")
                if (i != null) { info = i; msg = "Done. The mask is back, running from ${i.optString("running")}." to false; progress = -1f; file = null; return@launch }
            }
            msg = "The mask hasn't come back after two minutes. Check it's on and rejoin its WiFi." to true
            progress = -1f
        }
    }, { ask = false }, danger = false)
}

// ---- 010 spectrum rave ------------------------------------------------------------------------------

@Composable
fun RaveScreen() {
    val scope = rememberCoroutineScope()
    val link by MaskLink.state.collectAsState()
    val s = (link as? MaskLink.State.Connected)?.stats ?: JSONObject()
    val on = link is MaskLink.State.Connected
    val mode by AppPrefs.raveMode.collectAsState()
    val mic by AppPrefs.raveMic.collectAsState()
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    val running = s.optBoolean("rave")
    fun q(extra: String) = "/rave?mode=$mode&mic=${if (mic) 1 else 0}$extra"
    HudColumn {
        Crumb("010", "Spectrum rave")
        Panel("01", "Rave", titleColor = Hud.Pink, trailing = { Mono(if (running) "● RUNNING" else "OFF", color = if (running) Hud.Pink else Hud.Faint, size = 10.sp) }) {
            Caps("Look"); Gap(6)
            Seg(listOf(0 to "VU bar", 1 to "Spectrum"), mode, { v -> AppPrefs.setRaveMode(v); if (running) scope.launch { MaskLink.fetch("/rave?mode=$v") } },
                subs = listOf("ONE BAR", "BANDS"))
            SetRow("Use the mic", "Off shows a demo pattern without listening") {
                HudToggle(mic, { v -> AppPrefs.setRaveMic(v); if (running) scope.launch { MaskLink.fetch("/rave?mic=${if (v) 1 else 0}") } }, color = Hud.Pink)
            }
            SetRow("Cheeks join in", "The cheek strip shows the level too") {
                HudToggle(Cfg.bool("stripRaveEna"), { v -> scope.launch {
                    if (MaskLink.fetch("/rave?cheeks=${if (v) 1 else 0}") != null) Cfg.data.value.put("stripRaveEna", v).also { Cfg.load() }
                } }, enabled = on, color = Hud.Pink)
            }
            if (Cfg.bool("stripRaveEna")) CfgSlider("stripRaveGain", "Cheek meter gain", 25f, 400f, 5f, { "${it.toInt()}%" })
            Gap(10)
            HudButton(if (running) "Stop the rave" else "Start the rave", {
                scope.launch {
                    val r = MaskLink.fetch(if (running) "/rave?stop=1" else q("&go=1"))
                    if (r == null) Toast.bad("No mask") else if (!r.trim().startsWith("{")) Toast.bad(r.trim())
                    MaskLink.kick()
                }
            }, Modifier.fillMaxWidth(), if (running) BtnKind.Plain else BtnKind.Pink, enabled = on, height = 52.dp, size = 12.sp)
            Hint("Tap a face or the RAVE button again to leave it.", Modifier.padding(top = 8.dp))
        }
    }
}

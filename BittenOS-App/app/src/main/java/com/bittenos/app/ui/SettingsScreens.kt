package com.bittenos.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bittenos.app.Cfg
import com.bittenos.app.MaskLink
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject

@Composable
private fun stats(): JSONObject {
    val link by MaskLink.state.collectAsState()
    return (link as? MaskLink.State.Connected)?.stats ?: JSONObject()
}

@Composable
private fun watchCfg() { val c by Cfg.data.collectAsState(); @Suppress("UNUSED_EXPRESSION") c }

fun parseColor(hex: String): Color = try { Color(android.graphics.Color.parseColor(hex)) } catch (e: Exception) { Color(0xFFFF2D96) }

// ---- 003 lights & screen -----------------------------------------------------------------------

@Composable
fun LightsScreen() {
    val s = stats()
    watchCfg()
    val scope = rememberCoroutineScope()
    val on = online()
    HudColumn {
        Crumb("003", "Lights & screen")
        Panel("01", "Auto-brightness") {
            CfgToggle("autoBriEna", "Follow the room light", "The light sensor sets the panel brightness", top = false)
            Field {
                LabVal("Panels now", if (s.has("bright")) "${s.optInt("bright")} / 15" else "—")
                Gauge(s.optInt("bright") / 15f, idle = !Cfg.bool("autoBriEna"))
                Mono("LIGHT ${s.optInt("ambient", -1).takeIf { it >= 0 } ?: "—"}" +
                    (s.optInt("ambientLive", -1).takeIf { it >= 0 }?.let { " · LIVE $it" } ?: ""), color = Hud.Faint, size = 9.5.sp)
            }
            Gap(8)
            HudButton("Measure now", {
                scope.launch {
                    MaskLink.fetch("/autobri?measure=1") ?: return@launch Toast.bad("No mask")
                    Toast.ok(if (Cfg.str("autoBriMode") == "live") "Reading the light" else "Measuring · the panels may blink")
                }
            }, Modifier.fillMaxWidth(), enabled = on)
            CfgStrPick("autoBriMode", "Method", "Blank: panels go dark to read · Live: reads while lit", listOf(
                "blank" to "Blank", "hybrid" to "Hybrid", "live" to "Live"))
            CfgPick("autoBriSecs", "How often it looks", null, listOf(10 to "10 seconds", 30 to "30 seconds", 60 to "1 minute",
                120 to "2 minutes", 300 to "5 minutes", 600 to "10 minutes"), { Cfg.int(it) }, { it })
            CfgStep("autoBriMin", "Lowest", "In the dark", 0.0, 15.0, 1.0)
            CfgStep("autoBriMax", "Highest", "In bright light. High settings draw a lot of power.", 0.0, 15.0, 1.0)
            LightPoint("autoBriDark", "Dark point", "This reading and below give Lowest", 0.0, 5000.0, 5.0, s)
            LightPoint("autoBriBright", "Bright point", "This reading and above give Highest", 1.0, 20000.0, 25.0, s)
        }
        StripPanel(s)
        Panel("03", "Status screen") {
            CfgToggle("oledEna", "Inside screen", if (Cfg.bool("oledEna") && s.has("oled") && !s.optBoolean("oled"))
                "Switched on but not found: check wiring, address 0x3C" else "0.96\" SSD1306 OLED inside the helmet", top = false)
            CfgPick("bOled", "Brightness", null, listOf(0 to "Dimmest", 1 to "Dim", 2 to "Medium", 3 to "Bright", 4 to "Brightest"),
                { Cfg.int(it) }, { it })
            CfgToggle("oledFlip", "Turn the picture round", "For how the screen is mounted")
            Gap(4)
            HudButton("Screen designer", { Nav.web("/screen.html") }, Modifier.fillMaxWidth(), BtnKind.Out, enabled = on)
        }
    }
}

@Composable
private fun LightPoint(key: String, title: String, hint: String, min: Double, max: Double, step: Double, s: JSONObject) {
    val scope = rememberCoroutineScope()
    CfgStep(key, title, hint, min, max, step)
    Row(Modifier.fillMaxWidth().padding(bottom = 4.dp), horizontalArrangement = Arrangement.End) {
        LinkBtn("Use the reading now ›", {
            val v = s.optInt("ambient", -1).takeIf { it >= 0 } ?: s.optInt("ambientLive", -1)
            if (v < 0) Toast.bad("No light reading yet · press Measure first")
            else scope.launch { Cfg.set(key to v, ok = "$title set to $v") }
        })
    }
}

private val BLUSH = listOf("#ff2d96", "#ff5a7a", "#ff8fb8", "#ff3d3d", "#b45cff", "#3dd9f5", "#ffb53d", "#ffffff")

@Composable
private fun StripPanel(s: JSONObject) {
    val scope = rememberCoroutineScope()
    val on = online()
    var bri by remember { mutableFloatStateOf(Cfg.dbl("bStrip").toFloat()) }
    val cfgV by Cfg.data.collectAsState()
    LaunchedEffect(cfgV) { bri = Cfg.dbl("bStrip").toFloat() }
    var last by remember { mutableLongStateOf(0L) }
    var mapOpen by remember { mutableStateOf(false) }
    var wiring by remember { mutableStateOf(false) }
    fun cmd(q: String, msg: String) = scope.launch {
        if (MaskLink.fetch("/strip?$q") != null) Toast.ok(msg) else Toast.bad("No mask")
    }
    Panel("02", "Cheek strip", titleColor = Hud.Pink) {
        CfgToggle("stripEna", "Cheek LEDs", "The WS2812 chain down both cheeks", top = false)
        LabVal("Brightness", "${(bri / 255 * 100).toInt()}%", Hud.Pink)
        HudSlider(bri, {
            bri = it
            val now = System.currentTimeMillis()
            if (now - last > 120) { last = now; scope.launch { MaskLink.fetch("/strip?bri=${it.toInt()}") } }
        }, 0f..255f, color = Hud.Pink, enabled = on, onDone = { scope.launch {
            if (MaskLink.fetch("/strip?bri=${bri.toInt()}&save=1") != null) Cfg.data.value.put("bStrip", bri.toInt())
        } })
        CfgToggle("stripAutoEna", "Follow the room light", "Dim indoors, brighter outside. Needs Auto-brightness on.")
        if (Cfg.bool("stripAutoEna")) CfgSlider("stripAutoMin", "Lowest in the dark", 1f, 100f, 1f, { "${it.toInt()}%" })
        CfgToggle("stripProxEna", "Glow as a paw comes close", "Raise the boop line and it starts from further out")
        Rule(Modifier.padding(vertical = 6.dp))
        CfgToggle("stripBlushEna", "Blush on a boop", "A flare runs out from the snout, then the cheeks stay pink", top = false)
        CfgToggle("stripBlushHold", "Stay while booped", "Held as long as the paw is there, then fades")
        CfgToggle("stripFlareEna", "Flare first", "A pale sweep runs out before the blush settles")
        val col = Cfg.str("stripBlush")
        SetRow("Blush colour") {
            Box(Modifier.size(28.dp).background(parseColor(col), RoundedCornerShape(2.dp)).border(1.dp, Hud.Line2, RoundedCornerShape(2.dp)))
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            BLUSH.forEach { h ->
                Box(Modifier.weight(1f).height(30.dp).background(parseColor(h), RoundedCornerShape(2.dp))
                    .border(if (h.equals(col, true)) 2.dp else 1.dp, if (h.equals(col, true)) Hud.Text else Hud.Line, RoundedCornerShape(2.dp))
                    .clickable(enabled = on) { scope.launch { Cfg.set("stripBlush" to h) } })
            }
        }
        CfgStep("stripBlushMs", "Blush time", "With Stay while booped on, the fade after the paw lifts", 500.0, 10000.0, 250.0,
            unit = " s", decimals = 2, scale = 1000.0)
        CfgToggle("stripIdleEna", "Idle glow", "A soft blush the rest of the time, breathing slowly")
        CfgToggle("stripRaveEna", "Rave meter", "An audio level up both cheeks while rave mode runs")
        if (Cfg.bool("stripRaveEna")) CfgSlider("stripRaveGain", "Rave meter gain", 25f, 400f, 5f, { "${it.toInt()}%" })
        Rule(Modifier.padding(vertical = 8.dp))
        Caps("Try it")
        Gap(6)
        Seg(listOf("boop=1" to "Blush", "test=1" to "Rainbow", "test=2" to "Dot", "test=0" to "Normal"), null,
            { q -> cmd(q, when (q) { "boop=1" -> "Blushing"; "test=1" -> "Rainbow"; "test=2" -> "Walking dot"; else -> "Back to normal" }) },
            enabled = on)
        Gap(8)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Which LEDs blush", { mapOpen = true }, Modifier.weight(1f), enabled = on)
            HudButton("Wiring", { wiring = true }, Modifier.weight(1f), enabled = on)
        }
        if (s.optInt("stripTest") != 0) Hint("A test pattern is showing. Normal puts it back.", Modifier.padding(top = 6.dp), Hud.Amber)
    }
    if (mapOpen) BlushMapSheet { mapOpen = false }
    if (wiring) Sheet("How the chain is laid out", { wiring = false }, Hud.Pink) {
        CfgStep("stripNum", "LEDs", "Both sides added together", 0.0, 64.0, 1.0, top = false)
        CfgStep("stripSplit", "Split", "Where the chain crosses to the other cheek", 0.0, 64.0, 1.0)
        CfgToggle("stripRevA", "Reverse first side")
        CfgToggle("stripRevB", "Reverse second side")
        Gap(6)
        HudButton("Show ends", { cmd("test=5", "Green at the snout, red at the ear") }, Modifier.fillMaxWidth())
        Hint("Green should be at the snout and red at the ear on both sides. If not, flip that side.", Modifier.padding(top = 6.dp))
    }
}

@Composable
private fun BlushMapSheet(close: () -> Unit) {
    val scope = rememberCoroutineScope()
    val n = Cfg.int("stripNum").coerceIn(0, 64)
    val split = Cfg.int("stripSplit").coerceIn(0, n)
    var map by remember { mutableStateOf(Cfg.str("stripBlushMap").padEnd(n, '0').take(n)) }
    val col = parseColor(Cfg.str("stripBlush"))
    Sheet("Which LEDs blush", close, Hud.Pink) {
        Note("Tap an LED to switch it. Each tap lights that one white on the mask so you can see which it is.")
        Gap(10)
        listOf(0 until split to "First side", split until n to "Second side").forEach { (r, lab) ->
            if (r.isEmpty()) return@forEach
            Caps(lab, size = 9.sp)
            Gap(4)
            r.chunked(8).forEach { row ->
                Row(Modifier.fillMaxWidth().padding(bottom = 4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    row.forEach { i ->
                        val lit = map.getOrNull(i) == '1'
                        Box(Modifier.weight(1f).height(30.dp).background(if (lit) col else Hud.FieldBg, RoundedCornerShape(2.dp))
                            .border(1.dp, if (lit) col else Hud.Line2, RoundedCornerShape(2.dp))
                            .clickable {
                                map = map.substring(0, i) + (if (lit) '0' else '1') + map.substring(i + 1)
                                scope.launch { MaskLink.fetch("/strip?id=$i") }
                            }, contentAlignment = Alignment.Center) {
                            Mono("${i + 1}", color = if (lit) Color.Black else Hud.Faint, size = 9.sp)
                        }
                    }
                    repeat(8 - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
            Gap(6)
        }
        Mono("${map.count { it == '1' }} OF $n", color = Hud.Pink, size = 10.sp)
        Gap(10)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("All", { map = "1".repeat(n) }, Modifier.weight(1f))
            HudButton("None", { map = "0".repeat(n) }, Modifier.weight(1f))
            HudButton("Save", { scope.launch { if (Cfg.set("stripBlushMap" to map, ok = "Blush LEDs saved")) close() } },
                Modifier.weight(1f), BtnKind.Pink)
        }
    }
}

// ---- 004 sensors ------------------------------------------------------------------------------------

@Composable
fun SensorsScreen() {
    val s = stats()
    watchCfg()
    val scope = rememberCoroutineScope()
    val on = online()
    var calAsk by remember { mutableStateOf(false) }
    var resetAsk by remember { mutableStateOf(false) }
    HudColumn {
        Crumb("004", "Sensors")
        Panel("01", "Boop") {
            CfgToggle("boopEna", "Boop face", "Change face when something touches the snout", top = false)
            val sig = s.optInt("boopSig"); val th = Cfg.int("boopThresh")
            Field {
                LabVal("Closeness", if (!s.optBoolean("boop", true)) "NO SENSOR" else "$sig" + if (s.optBoolean("booping")) " · BOOP" else "")
                Gauge(sig / 250f, th / 250f, over = sig >= th)
                Mono("RAW ${s.optInt("boopRaw")} · RESTING ${s.optInt("boopBase")} · SHADE ${s.optInt("shade")}%", color = Hud.Faint, size = 9.5.sp)
            }
            CfgStep("boopThresh", "How close counts", "The yellow line on Closeness", 1.0, 250.0, 1.0)
            CfgStep("boopConfirm", "Readings in a row", "Lower is quicker, higher is steadier", 2.0, 10.0, 1.0)
            CfgToggle("boopLightEna", "Must shade the sensor", "Stops random boops")
            if (Cfg.bool("boopLightEna")) CfgSlider("boopShade", "Shade needed", 5f, 90f, 5f, { "${it.toInt()}%" })
            CfgFacePick("aBoop", "Boop face")
            Gap(6)
            HudButton("Measure resting level", { calAsk = true }, Modifier.fillMaxWidth(), enabled = on)
        }
        Panel("02", "Boop counter", trailing = { Mono("${s.optInt("boops")} BOOPS", size = 10.sp) }) {
            CfgToggle("boopCountEna", "Show the count", "Pops up on the nose after each boop", top = false)
            CfgStep("boopMilestone", "Milestone every", "A special face on every this many boops. 0 turns it off.", 0.0, 1000.0, 10.0)
            CfgFacePick("aMilestone", "Milestone face")
            CfgStep("sassBoops", "Sass after", "Boops in quick succession before it pulls a face. 0 turns it off.", 0.0, 10.0, 1.0)
            CfgStep("sassSecs", "Within", "How close together those boops land", 2.0, 60.0, 1.0, unit = " s")
            CfgFacePick("aSass", "Sass face")
            Gap(6)
            HudButton("Reset the count", { resetAsk = true }, Modifier.fillMaxWidth(), BtnKind.Danger, enabled = on)
        }
        Panel("03", "Tilt") {
            CfgToggle("tiltEna", "Tilt faces", "Change face when you tilt your head", top = false)
            CfgStep("tiltTrig", "How far to lean", "1.0 is as far as you taught it; lower triggers sooner", 0.2, 1.5, 0.05, decimals = 2)
            CfgFacePick("aTilt", "Tilt left (wearing it)")
            CfgFacePick("aTilt2", "Tilt right")
            Gap(8)
            Caps("Teach positions")
            Hint("Tilt your head that way, hold still, and press. Saves straight away.", Modifier.padding(vertical = 4.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                listOf("neutral" to "Idle", "tilt" to "Left", "tilt2" to "Right").forEach { (p, lab) ->
                    val xyz = listOf("X", "Y", "Z").map { Cfg.data.value.optDouble(p + it, Double.NaN) }
                    HudButton(lab, {
                        scope.launch {
                            val t = MaskLink.fetch("/gyro")?.trim() ?: return@launch Toast.bad("No mask")
                            val v = t.split(';').mapNotNull { it.toDoubleOrNull() }
                            if (v.size != 3) return@launch Toast.bad(t)
                            Cfg.set("${p}X" to v[0], "${p}Y" to v[1], "${p}Z" to v[2], ok = "$lab taught")
                        }
                    }, Modifier.weight(1f), enabled = on,
                        sub = if (xyz.any { it.isNaN() }) "not set" else xyz.joinToString(" ") { "%.2f".format(it) })
                }
            }
            if (!s.optBoolean("tilt", true)) Hint("Tilt sensor (BMI270) not found.", Modifier.padding(top = 6.dp), Hud.Amber)
        }
    }
    if (calAsk) Confirm("Measure the resting level?", "Keep everything away from the visor for a second.", "Measure", {
        scope.launch { if (MaskLink.fetch("/boopcal") != null) Toast.ok("Measuring… keep clear") else Toast.bad("No mask") }
    }, { calAsk = false }, danger = false)
    if (resetAsk) Confirm("Reset the boop counter?", "It goes back to zero.", "Reset", {
        scope.launch { if (Cfg.set("boopCount" to 0, ok = "Boop counter reset")) { delay(300); MaskLink.kick() } }
    }, { resetAsk = false })
}

// ---- 005 behaviour ----------------------------------------------------------------------------------

@Composable
fun BehaviourScreen() {
    watchCfg()
    val scope = rememberCoroutineScope()
    val on = online()
    HudColumn {
        Crumb("005", "Behaviour")
        Panel("01", "Face changes") {
            CfgToggle("transEna", "Dip between faces", "Dim away and back when the face changes", top = false)
            if (Cfg.bool("transEna")) CfgSlider("transMs", "Dip time", 20f, 400f, 10f, { "${it.toInt()} ms" })
            CfgToggle("bobEna", "Mouth bob", "The mouth lifts a pixel every second or so")
        }
        Panel("02", "Sleep") {
            CfgToggle("sleepEna", "Sleep when still", "Sleeping face when you've been still a while", top = false)
            CfgStep("sleepMins", "Minutes of stillness", null, 1.0, 120.0, 1.0, unit = " min")
            CfgStep("sleepTol", "Wake sensitivity", "Lower wakes with less movement", 0.02, 1.0, 0.02, decimals = 2)
            CfgFacePick("aSleep", "Sleeping face")
            CfgToggle("sleepDeepEna", "Deep sleep", "After a long sleep, turn the panels off entirely")
            if (Cfg.bool("sleepDeepEna")) CfgStep("sleepDeepMins", "Minutes asleep first", null, 1.0, 240.0, 5.0, unit = " min")
        }
        Panel("03", "Startup & warm-up") {
            CfgToggle("wakeFlickEna", "Warm-up flicker", "After a deep sleep the panels stutter back on one at a time", top = false)
            CfgToggle("wakeFlickBoot", "Also at switch-on")
            CfgToggle("bootBarEna", "Loading bar", "A bar fills the maw first, then the flicker brings the face in")
            if (Cfg.bool("bootBarEna")) CfgStep("bootBarMs", "Bar time", null, 300.0, 8000.0, 100.0, unit = " s", decimals = 1, scale = 1000.0)
            CfgStrPick("wakeFlickStyle", "Flicker style", null, listOf("crt" to "Old CRT · slow, heavy", "tube" to "Tube · fast, twitchy"))
            CfgStep("wakeFlickMs", "Flicker time", "Until every panel is back", 300.0, 8000.0, 100.0, unit = " s", decimals = 1, scale = 1000.0)
            CfgPick("wakeFlickGlitch", "Glitch", "Scrambled rows and columns while it warms up",
                listOf(0 to "None", 1 to "A little", 2 to "A lot"), { Cfg.int(it) }, { it })
            Gap(6)
            HudButton("Try the warm-up", {
                scope.launch {
                    val t = MaskLink.fetch("/wakeflick")?.trim()
                    if (t == "ok") Toast.ok("Warming up…") else Toast.bad(t ?: "No mask")
                }
            }, Modifier.fillMaxWidth(), BtnKind.Out, enabled = on)
        }
    }
}

package com.bittenos.app.ui

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bittenos.app.Cfg
import com.bittenos.app.MaskLink
import com.bittenos.app.Pending
import com.bittenos.app.VoiceEngine
import com.bittenos.app.VoiceService
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject

// ---- 002 voice through the phone -------------------------------------------------------------------

@Composable
fun VoiceScreen(link: MaskLink.State) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val conn = link as? MaskLink.State.Connected
    val on by VoiceService.on.collectAsState()
    val v by VoiceEngine.stats.collectAsState()
    var smooth by remember { mutableIntStateOf(VoiceService.smoothingMs(ctx)) }
    var output by remember { mutableStateOf(VoiceService.output(ctx)) }
    var mic by remember { mutableStateOf<JSONObject?>(null) }
    val waiting by Pending.list.collectAsState()
    val queuedGain = waiting.firstOrNull { it.key == Pending.Key.VoiceGain }?.value
    var gain by remember { mutableFloatStateOf((queuedGain ?: MaskLink.lastGain ?: 500).toFloat()) }
    LaunchedEffect(conn?.host) {
        if (conn != null) MaskLink.micStream()?.let { mic = it; gain = it.optInt("gain", 500).toFloat() }
    }
    HudColumn {
        Crumb("002", "Voice & mic")
        Panel("01", "Through this phone", titleColor = Hud.Cyan, trailing = {
            Mono(if (on) "● LIVE" else "OFF", color = if (on) Hud.Cyan else Hud.Faint, size = 10.sp)
        }) {
            Note(if (output == VoiceEngine.Output.Aux) "Plug the speaker's cable into the headphone socket. Keeps playing with the screen off."
                 else "Pair the speaker with this phone first. Keeps playing with the screen off.")
            Gap(12)
            HudButton(if (on) "Stop" else "Start listening", { if (on) VoiceService.stop(ctx) else VoiceService.start(ctx) },
                Modifier.fillMaxWidth(), if (on) BtnKind.Amber else BtnKind.Cyan, enabled = on || conn != null, height = 52.dp, size = 12.sp)
            if (mic?.optBoolean("ready", true) == false)
                Hint("The mask says its mic isn't ready (type ${mic?.optString("type")}).", Modifier.padding(top = 8.dp), Hud.Amber)
            if (on) {
                Gap(14)
                LabVal("Level", when {
                    conn == null -> "WAITING FOR THE MASK"
                    !v.receiving -> "WAITING FOR SOUND"
                    else -> "PLAYING"
                }, Hud.Cyan)
                Gap(6)
                Meter(v.level, Hud.Cyan)
                Gap(12)
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Stat("Delay", if (v.delayMs < 0) "…" else "${v.delayMs} ms", Modifier.weight(1f), Hud.Cyan)
                    Stat("Buffer", "${v.bufferMs} ms", Modifier.weight(1f))
                    Stat("Output", "${maxOf(v.outputMs, 0)} ms", Modifier.weight(1f))
                }
                if (v.routedTo.isNotEmpty()) Hint("Playing through " + v.routedTo, Modifier.padding(top = 8.dp),
                    if (v.onChosen) Hud.Faint else Hud.Amber)
                if (!v.onChosen) Hint(if (v.output == VoiceEngine.Output.Bluetooth) "No Bluetooth speaker connected yet. It moves over by itself once one is."
                    else "Nothing in the headphone socket yet. It moves over by itself once the cable is in.", color = Hud.Amber)
                if (v.output == VoiceEngine.Output.Aux) Hint(if (v.lowLatencyPath) "Android's fast audio path (${v.outputRate} Hz)"
                    else "Android refused the fast audio path: expect more delay", color = if (v.lowLatencyPath) Hud.Faint else Hud.Amber)
                if (v.dryOuts > 0 || v.lostPackets > 0) Hint("Crackles ${v.dryOuts} · packets lost ${v.lostPackets}")
            }
        }
        Panel("02", "Output") {
            Seg(VoiceEngine.Output.entries.map { it to if (it == VoiceEngine.Output.Aux) "Aux cable" else "Bluetooth" }, output,
                { output = it; VoiceService.setOutput(ctx, it) })
            Hint(if (output == VoiceEngine.Output.Aux) "The quickest way out: no radio between the phone and the speaker."
                 else "Bluetooth adds its own delay, usually 150-250 ms on older speakers. Around 200 ms is the hardest to talk over.",
                Modifier.padding(top = 8.dp))
            Hint("The soundboard plays through the same output.", Modifier.padding(top = 4.dp))
        }
        Panel("03", "Smoothing") {
            Seg(listOf(20 to "Least", 40 to "Normal", 80 to "Most"), smooth, { smooth = it; VoiceService.setSmoothingMs(ctx, it) })
            Hint("Audio held back to ride over WiFi hiccups. Less is quicker but crackles sooner.", Modifier.padding(top = 8.dp))
        }
        Panel("04", "Voice gain") {
            LabVal("Set on the mask", "${gain.toInt()}%" + if (conn == null && queuedGain != null) " · WAITING" else "", Hud.Cyan)
            HudSlider(gain, { gain = (Math.round(it / 25f) * 25f) }, 25f..4000f, color = Hud.Cyan, onDone = {
                if (conn != null) scope.launch { MaskLink.micStream(gain.toInt()) }
                else Pending.put(Pending.Key.VoiceGain, gain.toInt(), MaskLink.lastGain)
            })
            Hint("Turn it up until speech is clear, then use the phone's volume. Point the speaker away from your head.")
        }
        Panel("05", "Mouth mic", trailing = { LinkBtn("Set up ›", { Nav.go("mic") }) }) {
            Note("The mouth moving with your voice: on/off, the meter, the quiet and loud levels.")
        }
    }
}

// ---- the mouth mic -------------------------------------------------------------------------------

@Composable
fun MicScreen() {
    val link by MaskLink.state.collectAsState()
    val s = (link as? MaskLink.State.Connected)?.stats ?: JSONObject()
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    val scope = rememberCoroutineScope()
    val on = online()
    HudColumn {
        Crumb("002.B", "Mouth mic")
        Panel("01", "Microphone") {
            CfgToggle("speechEna", "Mouth moves with your voice", "From the mic on GPIO 4", top = false)
            val lvl = s.optInt("mic", -1)
            Field {
                LabVal("Level", if (lvl < 0) "MIC OFF" else "$lvl" + if (s.optBoolean("speaking")) " · TALKING" else "")
                Gauge(maxOf(lvl, 0) / 100f, Cfg.dbl("spTrig").toFloat() / 100f, idle = lvl < 0)
                Mono("SWING ${s.optInt("micSwing")} · ${s.optString("micType", "?")}" +
                    if (!s.optBoolean("micReady", true)) " · NOT READY" else "", color = Hud.Faint, size = 9.5.sp)
            }
            Gap(10)
            val phase = s.optInt("micTest", 0)
            HudButton(when (phase) { 1 -> "Keep quiet…"; 2 -> "Now talk…"; else -> "Test the mic" }, {
                scope.launch {
                    val t = MaskLink.fetch("/mictest")?.trim()
                    if (t == "ok") Toast.ok("Keep quiet for two seconds…") else Toast.bad(t ?: "No mask")
                }
            }, Modifier.fillMaxWidth(), if (phase in 1..2) BtnKind.On else BtnKind.Plain, enabled = on && phase !in 1..2)
            if (phase == 3) Hint("Quiet ${"%.0f".format(s.optDouble("micTestQuiet"))} · talking ${"%.0f".format(s.optDouble("micTestLoud"))}. " +
                "Talking should be well above quiet; if they're close, turn the pot on the mic board up.", Modifier.padding(top = 6.dp))
        }
        Panel("02", "Levels") {
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                HudButton("Measure quiet", {
                    scope.launch {
                        MaskLink.fetch("/miccal") ?: return@launch Toast.bad("No mask")
                        Toast.ok("Listening, keep quiet for a second")
                        delay(1800); Cfg.load(); Toast.ok("Quiet level measured: ${Cfg.int("spMin")}")
                    }
                }, Modifier.weight(1f), enabled = on)
                HudButton("Measure talking", {
                    scope.launch {
                        val t = MaskLink.fetch("/micloud")?.trim()
                        if (t != "ok") return@launch Toast.bad(t ?: "No mask")
                        Toast.ok("Talk normally for three seconds…")
                        delay(3600); Cfg.load(); Toast.ok("Range set to ${Cfg.int("spMax")}")
                    }
                }, Modifier.weight(1f), enabled = on)
            }
            CfgStep("spMin", "Quiet room", "The swing of a quiet room, taken off what it hears", 0.0, 2000.0, 5.0)
            CfgStep("spMax", "Range", "How loud counts as full volume. Default 500.", 20.0, 4000.0, 10.0)
            CfgStep("spTrig", "Talking line", "Level that counts as talking: the line on the meter. Default 10.", 2.0, 100.0, 1.0)
            CfgStrPick("mouthMode", "Mouth movement", null, listOf("shapes" to "Shapes", "flap" to "Flap"))
        }
        Hint("If the mouth doesn't move: check Use mic is on, measure quiet with the room silent, then lower the talking line.",
            Modifier.padding(horizontal = Hud.Edge + 2.dp))
    }
}

@Suppress("unused") private val keepAlign = Alignment.Center
@Suppress("unused") private val keepWeight = FontWeight.Bold

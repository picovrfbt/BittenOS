package com.bittenos.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bittenos.app.Cfg
import com.bittenos.app.Faces
import com.bittenos.app.MaskLink
import com.bittenos.app.Soundboard
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject

private val REMOTE = listOf("Top", "Middle", "Bottom")

@Composable
fun ButtonsScreen() {
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    var ordering by remember { mutableStateOf(false) }
    HudColumn {
        Crumb("006", "Buttons & remote")
        Panel("01", "Mask button") {
            ActionRows("btnShort", "Short press", top = false)
            ActionRows("btnDouble", "Double press", "Off, so a single press acts the moment you let go")
            ActionRows("btnHold", "Hold")
            CfgStep("btnHoldMs", "How long is a hold", null, 300.0, 5000.0, 100.0, unit = " s", decimals = 1, scale = 1000.0)
            CfgStrPick("eggAction", "Quick presses", "Press quickly this many times in a row for an easter egg",
                listOf("slots" to "The slot machine", "dino" to "The dino game", "shutdown" to "The fake low battery", "none" to "Nothing"))
            CfgStep("eggPresses", "Presses", "Each within about half a second of the last", 3.0, 10.0, 1.0)
        }
        Panel("02", "Next face order", trailing = { LinkBtn("Edit ›", { ordering = true }) }) {
            val order = faceOrder()
            val skip = Cfg.str("faceSkip").split(';').filter { it.isNotEmpty() }.toSet()
            Note(order.filter { it !in skip }.joinToString(" › ") { Faces.label(it) }.ifEmpty { "No faces yet" })
            if (skip.isNotEmpty()) Hint("Skipped: " + skip.joinToString(", ") { Faces.label(it) }, Modifier.padding(top = 6.dp))
        }
        RemotePanel()
    }
    if (ordering) OrderSheet { ordering = false }
}

/** The faces in Next face order: the saved order first, then any new faces in list order. */
fun faceOrder(): List<String> {
    val all = Faces.list.value
    val saved = Cfg.str("faceOrder").split(';').filter { it in all }
    return saved + all.filter { it !in saved }
}

@Composable
private fun OrderSheet(close: () -> Unit) {
    val scope = rememberCoroutineScope()
    val order = remember { faceOrder().toMutableStateList() }
    val skip = remember { Cfg.str("faceSkip").split(';').filter { it.isNotEmpty() }.toMutableStateList() }
    Sheet("Next face order", close) {
        Note("The order Next face and Previous face walk through. Skipped faces stay on the tiles but the cycle passes them by.")
        Gap(10)
        order.forEachIndexed { i, f ->
            val skipped = f in skip
            Row(Modifier.fillMaxWidth().padding(bottom = 4.dp).background(Hud.FieldBg, RoundedCornerShape(2.dp))
                .border(1.dp, Hud.Line, RoundedCornerShape(2.dp)).padding(start = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                Mono("%02d".format(i + 1), color = if (skipped) Hud.Ghost else LocalAccent.current, size = 10.sp)
                HGap(10)
                Caps(Faces.label(f), Modifier.weight(1f), color = if (skipped) Hud.Ghost else Hud.Text, size = 10.5.sp)
                LinkBtn(if (skipped) "Skipped" else "In", { if (skipped) skip.remove(f) else skip.add(f) },
                    if (skipped) Hud.Faint else LocalAccent.current)
                Box(Modifier.size(40.dp).clickable(enabled = i > 0) { order.removeAt(i); order.add(i - 1, f) },
                    contentAlignment = Alignment.Center) { Text("▲", color = if (i > 0) Hud.Dim else Hud.Ghost) }
                Box(Modifier.size(40.dp).clickable(enabled = i < order.size - 1) { order.removeAt(i); order.add(i + 1, f) },
                    contentAlignment = Alignment.Center) { Text("▼", color = if (i < order.size - 1) Hud.Dim else Hud.Ghost) }
            }
        }
        Gap(10)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Cancel", close, Modifier.weight(1f))
            HudButton("Save", {
                scope.launch {
                    if (Cfg.set("faceOrder" to order.joinToString(";"), "faceSkip" to skip.joinToString(";"), ok = "Order saved")) close()
                }
            }, Modifier.weight(1f), BtnKind.Fill)
        }
    }
}

@Composable
private fun RemotePanel() {
    val scope = rememberCoroutineScope()
    val link by MaskLink.state.collectAsState()
    val s = (link as? MaskLink.State.Connected)?.stats ?: JSONObject()
    val on = link is MaskLink.State.Connected
    var pair by remember { mutableStateOf<JSONObject?>(null) }
    var tab by rememberSaveableInt(0)
    var forgetAsk by remember { mutableStateOf(false) }
    val sounds by Soundboard.sounds.collectAsState()
    val lastRemote by Soundboard.lastRemote.collectAsState()
    suspend fun send(q: String) { pair = MaskLink.json("/pair" + if (q.isEmpty()) "" else "?$q") }
    LaunchedEffect(on) { if (on) send("") }
    LaunchedEffect(pair?.optBoolean("pairing")) {
        while (pair?.optBoolean("pairing") == true) { delay(1000); send("") }
    }
    val pairing = pair?.optBoolean("pairing") == true
    val mac = pair?.optString("mac").orEmpty()
    val ago = s.optLong("rmtAgoMs", -1)
    val heard = ago in 0..15_000
    Panel("03", "ESP-NOW remote", trailing = {
        Dot(if (heard) LocalAccent.current else Hud.Faint)
        HGap(6)
        Mono(when {
            !s.optBoolean("rmtPaired", mac.isNotEmpty()) -> "NOT PAIRED"
            heard -> "${s.optInt("rmtRssi")} dBm"
            else -> "NOT HEARD"
        }, size = 10.sp)
    }) {
        Field {
            LabVal("Pairing", if (pairing) "LISTENING · ${pair?.optInt("secsLeft")} s" else if (mac.isNotEmpty()) mac.uppercase() else "NOTHING PAIRED")
            if (pairing) Hint("Hold all three buttons on the remote for three seconds.")
            if (ago >= 0) {
                val btn = REMOTE.getOrNull(s.optInt("rmtBtn", -1))
                Mono("LAST ${btn?.uppercase() ?: "—"} ${listOf("PRESS", "HOLD", "DOUBLE").getOrElse(s.optInt("rmtEvt")) { "" }} · ${ago / 1000} S AGO · HEARD ${s.optInt("rmtHeard")} · LOST ${s.optInt("rmtLost")}",
                    color = Hud.Faint, size = 9.5.sp)
            }
        }
        Gap(8)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton(if (pairing) "Stop" else if (mac.isNotEmpty()) "Pair again" else "Pair",
                { scope.launch { send(if (pairing) "stop=1" else "go=1") } }, Modifier.weight(1f),
                if (pairing) BtnKind.On else BtnKind.Plain, enabled = on)
            if (mac.isNotEmpty()) HudButton("Forget", { forgetAsk = true }, Modifier.weight(1f), BtnKind.Danger, enabled = on)
        }
        Gap(14)
        Seg(REMOTE.indices.map { it to REMOTE[it] }, tab, { tab = it })
        Gap(4)
        val b = tab
        ActionRows("rmt$b", "${REMOTE[b]} press", top = false)
        ActionRows("rmt${b}H", "${REMOTE[b]} hold")
        ActionRows("rmt${b}D", "${REMOTE[b]} double tap")
        Rule(Modifier.padding(vertical = 8.dp))
        Hint(if (sounds.isEmpty()) "Pick \"A sound (this phone)\" to play one from the soundboard. Add sounds in Soundboard first."
             else "\"A sound (this phone)\" plays from the soundboard here, through the voice output, even with the screen " +
                "off as long as Work with the screen off is on in Network & app.")
        lastRemote?.let { Mono("LAST SOUND · ${it.uppercase()}", Modifier.padding(top = 6.dp), color = Hud.Cyan, size = 9.5.sp) }
    }
    if (forgetAsk) Confirm("Forget the remote?", "It stops working until it's paired again.", "Forget",
        { scope.launch { send("forget=1") } }, { forgetAsk = false })
}

@Composable
fun rememberSaveableInt(v: Int): MutableIntState = androidx.compose.runtime.saveable.rememberSaveable { mutableIntStateOf(v) }

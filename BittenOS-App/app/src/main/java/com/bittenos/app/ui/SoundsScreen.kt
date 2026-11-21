package com.bittenos.app.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bittenos.app.Cfg
import com.bittenos.app.Soundboard
import com.bittenos.app.VoiceEngine
import com.bittenos.app.VoiceService
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.launch

/** 011: sounds that play from this phone, with a tap or a remote button. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SoundsScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val sounds by Soundboard.sounds.collectAsState()
    val playing by Soundboard.playing.collectAsState()
    val one by Soundboard.oneAtATime.collectAsState()
    val bg by Soundboard.background.collectAsState()
    val lastRemote by Soundboard.lastRemote.collectAsState()
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    var held by remember { mutableStateOf<Int?>(null) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) scope.launch {
            val free = Soundboard.SLOTS - Soundboard.sounds.value.size
            val n = Soundboard.import(uris)
            when {
                n == 0 -> Toast.bad("Nothing added · those files can't be played")
                n < uris.size -> Toast.ok("$n added" + if (uris.size > free) " · the board is full" else " · some couldn't be played")
                else -> Toast.ok(if (n == 1) "Sound added" else "$n sounds added")
            }
        }
    }
    val out = VoiceService.output(ctx)
    HudColumn {
        Crumb("011", "Soundboard")
        Panel("01", "Sounds", titleColor = Hud.Cyan, trailing = { Mono("${sounds.size} / ${Soundboard.SLOTS}", color = Hud.Cyan, size = 10.sp) }) {
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                HudButton("+ Import audio", { pick.launch(arrayOf("audio/*")) }, Modifier.weight(1f), BtnKind.Cyan,
                    enabled = sounds.size < Soundboard.SLOTS, sub = "MP3 · WAV · OGG · M4A")
                HudButton("Stop", { Soundboard.stop() }, Modifier.weight(0.6f), enabled = playing >= 0)
            }
            Gap(12)
            if (sounds.isEmpty()) Note("No sounds yet. Import a few from this phone's files; you can name them after.")
            sounds.chunked(3).forEach { row ->
                Row(Modifier.fillMaxWidth().padding(bottom = Hud.TileGap), horizontalArrangement = Arrangement.spacedBy(Hud.TileGap)) {
                    row.forEach { snd ->
                        val lit = playing == snd.slot
                        Column(Modifier.weight(1f).heightIn(min = 70.dp)
                            .background(if (lit) lerpColor(Hud.Bg, Hud.Cyan, 0.16f) else Hud.FieldBg, RoundedCornerShape(2.dp))
                            .border(1.dp, if (held == snd.slot) Hud.Text else if (lit) Hud.Cyan else Hud.Line2, RoundedCornerShape(2.dp))
                            .combinedClickable(onLongClick = { held = snd.slot }) {
                                if (lit) Soundboard.stop() else Soundboard.play(snd.slot)
                            }.padding(8.dp)) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Mono("%02d".format(snd.slot + 1), Modifier.weight(1f), color = if (lit) Hud.Cyan else Hud.Faint, size = 9.sp)
                                Mono(if (lit) "■" else "▶", color = if (lit) Hud.Cyan else Hud.Dim, size = 10.sp)
                            }
                            Spacer(Modifier.weight(1f))
                            Caps(snd.name, color = if (lit) Hud.Cyan else Hud.Text, size = 10.sp, weight = FontWeight.Bold, maxLines = 2,
                                spacing = 0.08.sp)
                            Mono(Soundboard.fmtLen(snd.ms) + if (snd.volume < 0.99f) " · ${(snd.volume * 100).toInt()}%" else "",
                                color = Hud.Faint, size = 8.5.sp)
                        }
                    }
                    repeat(3 - row.size) { Spacer(Modifier.weight(1f)) }
                }
            }
            if (sounds.isNotEmpty()) Hint("Hold a sound to rename it, set its volume, move it or delete it.")
        }
        Panel("02", "Playing") {
            SetRow("One at a time", "A new sound cuts off the one playing. Off plays them over each other.", top = false) {
                HudToggle(one, { Soundboard.setOneAtATime(it) }, color = Hud.Cyan)
            }
            SetRow("Listen with the app closed", "Keeps a small notification so remote buttons still play sounds") {
                HudToggle(bg, { Soundboard.setBackground(it) }, color = Hud.Cyan)
            }
            SetRow("Comes out of", "The same output as the voice · change it in Voice & mic") {
                Mono(if (out == VoiceEngine.Output.Aux) "AUX CABLE" else "BLUETOOTH", color = Hud.Cyan, size = 10.sp)
            }
        }
        Panel("03", "Remote", trailing = { LinkBtn("Set buttons ›", { Nav.back(); Nav.go("buttons") }) }) {
            Note("In Buttons & remote, set a press, hold or double tap to \"A sound (this phone)\" and pick one. " +
                "The mask passes it to this phone over WiFi, so the phone has to be on the mask's network.")
            val used = remoteUses()
            if (used.isNotEmpty()) {
                Gap(8)
                used.forEach { (what, slot) ->
                    LabVal(what, Soundboard.name(slot)?.uppercase() ?: "SLOT ${slot + 1} · EMPTY",
                        if (Soundboard.name(slot) != null) Hud.Cyan else Hud.Amber)
                }
            }
            lastRemote?.let { Mono("LAST · ${it.uppercase()}", Modifier.padding(top = 8.dp), color = Hud.Cyan, size = 9.5.sp) }
        }
    }
    held?.let { slot -> SoundSheet(slot) { held = null } }
}

/** Which buttons are set to play a sound, for the list on this screen. */
private fun remoteUses(): List<Pair<String, Int>> {
    val out = mutableListOf<Pair<String, Int>>()
    val names = listOf("Top", "Middle", "Bottom")
    for (b in 0..2) for ((suf, what) in listOf("" to "press", "H" to "hold", "D" to "double tap")) {
        val k = "rmt$b$suf"
        if (Cfg.str(k) == "sound") out += "${names[b]} $what" to Cfg.int(k + "Phrase")
    }
    for ((k, what) in listOf("btnShort" to "Mask button press", "btnDouble" to "Mask button double", "btnHold" to "Mask button hold"))
        if (Cfg.str(k) == "sound") out += what to Cfg.int(k + "Phrase")
    return out
}

@Composable
private fun SoundSheet(slot: Int, close: () -> Unit) {
    val sounds by Soundboard.sounds.collectAsState()
    val snd = sounds.firstOrNull { it.slot == slot } ?: run { close(); return }
    var name by remember { mutableStateOf(snd.name) }
    var vol by remember { mutableFloatStateOf(snd.volume) }
    var del by remember { mutableStateOf(false) }
    var moving by remember { mutableStateOf(false) }
    Sheet("Sound ${slot + 1}", close, Hud.Cyan) {
        Mono(Soundboard.fmtLen(snd.ms), color = Hud.Dim, size = 11.sp)
        Gap(10)
        Caps("Name"); Gap(4)
        HudInput(name, { name = it }, "Name", Modifier.fillMaxWidth(), maxLen = 24)
        Gap(10)
        LabVal("Volume", "${(vol * 100).toInt()}%", Hud.Cyan)
        HudSlider(vol, { vol = it }, 0f..1f, color = Hud.Cyan, onDone = { Soundboard.setVolume(slot, vol) })
        Gap(8)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Play", { Soundboard.setVolume(slot, vol); Soundboard.play(slot) }, Modifier.weight(1f), BtnKind.Cyan)
            HudButton("Move to…", { moving = true }, Modifier.weight(1f), sub = "SLOT ${slot + 1}")
        }
        Gap(5)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Delete", { del = true }, Modifier.weight(1f), BtnKind.Danger)
            HudButton("Save", { Soundboard.rename(slot, name); Soundboard.setVolume(slot, vol); Toast.ok("Saved"); close() },
                Modifier.weight(1f), BtnKind.Fill, enabled = name.isNotBlank())
        }
        Hint("Buttons point at the slot number, so moving a sound moves it off whatever button plays that slot.", Modifier.padding(top = 8.dp))
    }
    if (moving) Sheet("Move to slot", { moving = false }, Hud.Cyan) {
        (0 until Soundboard.SLOTS).chunked(6).forEach { row ->
            Row(Modifier.fillMaxWidth().padding(bottom = Hud.SegGap), horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                row.forEach { s ->
                    val other = sounds.firstOrNull { it.slot == s }
                    HudButton("${s + 1}", { Soundboard.moveTo(slot, s); moving = false; close(); Toast.ok("Moved to slot ${s + 1}") },
                        Modifier.weight(1f), if (s == slot) BtnKind.Cyan else BtnKind.Plain, enabled = s != slot,
                        sub = other?.name?.take(5) ?: "—", height = 46.dp)
                }
            }
        }
        Hint("A slot with a sound in it swaps places.")
    }
    if (del) Confirm("Delete ${snd.name}?", "The copy in this app goes. The original file on the phone stays.", "Delete",
        { Soundboard.delete(slot); close() }, { del = false })
}

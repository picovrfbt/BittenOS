package com.bittenos.app.ui

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bittenos.app.MaskUpdate
import kotlinx.coroutines.launch
import org.json.JSONObject

/** Updating the mask from the copy this app carries. [info] is the mask's /otainfo, or null while it's away. */
@Composable
fun MaskUpdatePanel(code: String, info: JSONObject?, on: Boolean, refresh: () -> Unit) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val st by MaskUpdate.state.collectAsState()
    val b = remember { MaskUpdate.bundled(ctx) }
    var ask by remember { mutableStateOf(false) }
    LaunchedEffect(st is MaskUpdate.State.Done) { if (st is MaskUpdate.State.Done) refresh() }
    val behind = MaskUpdate.behind(info, b)
    val sending = st is MaskUpdate.State.Sending
    val maskFw = info?.optInt("fw", 0) ?: 0
    Panel(code, "Update from this app", trailing = {
        Mono(when {
            sending -> "SENDING"
            behind -> "UPDATE"
            info != null && b != null && maskFw > b.code -> "MASK IS NEWER"
            info != null && b != null -> "UP TO DATE"
            else -> ""
        }, color = if (behind || sending) Hud.Amber else Hud.Faint, size = 10.sp)
    }) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Stat("On the mask", when {
                info == null -> "—"
                info.has("fw") -> "v${info.optString("fwName")} · $maskFw"
                else -> "Before versions"
            }, Modifier.weight(1f))
            Stat("In this app", b?.let { "v${it.name} · ${it.code}" } ?: "None", Modifier.weight(1f), if (behind) Hud.Amber else Hud.Text)
        }
        Gap(10)
        when (val s = st) {
            is MaskUpdate.State.Sending -> {
                LabVal(s.step, "${(s.progress * 100).toInt()}%", Hud.Amber); Gap(6); Gauge(s.progress, color = Hud.Amber)
                Hint("Keep the phone close and this screen open.", Modifier.padding(top = 6.dp), Hud.Amber)
            }
            else -> {
                if (s is MaskUpdate.State.Done) Note(s.note, Modifier.padding(bottom = 8.dp), LocalAccent.current)
                if (s is MaskUpdate.State.Failed) Note(s.why, Modifier.padding(bottom = 8.dp), Hud.Red)
                when {
                    b == null -> Note("This copy of the app was built without a mask update.", color = Hud.Dim)
                    !on -> Note("Join the mask's WiFi to check it.", color = Hud.Dim)
                    behind -> HudButton(if (maskFw >= b.code) "Finish sending the web pages" else "Update the mask to v${b.name}",
                        { ask = true }, Modifier.fillMaxWidth(), BtnKind.Fill, sub = "NO INTERNET NEEDED")
                    maskFw > b.code -> Note("The mask has newer firmware than this app carries (flashed from the PC), so there's nothing to send.",
                        color = Hud.Dim)
                    else -> Note("The mask matches this app.", color = Hud.Dim)
                }
            }
        }
        Hint("A backup for when the PC isn't to hand. Firmware first, then the web pages and any new built-in faces. " +
            "Settings, the screen layout and your faces are never replaced. If new firmware doesn't start, the mask goes back to the old one.",
            Modifier.padding(top = 8.dp))
    }
    if (ask && info != null && b != null) Confirm("Update the mask to v${b.name}?",
        "It restarts partway through. Keep the phone close and the app open until it says done.", "Update", {
            scope.launch { MaskUpdate.send(ctx, info) }
        }, { ask = false }, danger = false)
}

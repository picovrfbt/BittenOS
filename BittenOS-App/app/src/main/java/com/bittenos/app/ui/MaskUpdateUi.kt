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

/** Updating the mask from GitHub, through this phone. [info] is the mask's /otainfo, or null while it's away. */
@Composable
fun MaskUpdatePanel(code: String, info: JSONObject?, on: Boolean, refresh: () -> Unit) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val st by MaskUpdate.state.collectAsState()
    var ask by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { if (st is MaskUpdate.State.Idle) MaskUpdate.check(ctx) }
    LaunchedEffect(st is MaskUpdate.State.Done) { if (st is MaskUpdate.State.Done) refresh() }
    val r = when (val s = st) {
        is MaskUpdate.State.Found -> s.r
        is MaskUpdate.State.Downloading -> s.r
        is MaskUpdate.State.Sending -> s.r
        is MaskUpdate.State.Done -> s.r
        is MaskUpdate.State.Failed -> s.r
        else -> null
    }
    val behind = r != null && MaskUpdate.behind(info, r)
    Panel(code, "Update from GitHub", trailing = {
        Mono(when {
            st is MaskUpdate.State.Sending -> "SENDING"
            behind -> "UPDATE"
            r != null && info != null -> "UP TO DATE"
            else -> ""
        }, color = if (behind || st is MaskUpdate.State.Sending) Hud.Amber else Hud.Faint, size = 10.sp)
    }) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Stat("On the mask", when {
                info == null -> "—"
                info.has("fw") -> "v${info.optString("fwName")} · ${info.optInt("fw")}"
                else -> "Before versions"
            }, Modifier.weight(1f))
            Stat("On GitHub", r?.let { "v${it.name} · ${it.code}" } ?: "—", Modifier.weight(1f), if (behind) Hud.Amber else Hud.Text)
        }
        if (info != null && info.has("fw") && info.optInt("web", 0) < info.optInt("fw"))
            Hint("Its web pages are from " + (info.optInt("web", 0).takeIf { it > 0 }?.let { "release $it" } ?: "before versions") + ".",
                Modifier.padding(top = 6.dp), Hud.Amber)
        Gap(10)
        when (val s = st) {
            is MaskUpdate.State.Idle, is MaskUpdate.State.Checking -> HudButton(
                if (s is MaskUpdate.State.Checking) "Checking GitHub…" else "Check GitHub",
                { scope.launch { MaskUpdate.check(ctx) } }, Modifier.fillMaxWidth(), enabled = s !is MaskUpdate.State.Checking)
            is MaskUpdate.State.Failed -> {
                Note(s.why, Modifier.padding(bottom = 8.dp), Hud.Red)
                HudButton("Check again", { scope.launch { MaskUpdate.check(ctx) } }, Modifier.fillMaxWidth())
            }
            is MaskUpdate.State.Found -> {
                if (s.r.notes.isNotBlank()) Note(s.r.notes, Modifier.padding(bottom = 8.dp), Hud.Text2)
                when {
                    !s.have -> HudButton("Download v${s.r.name}", { scope.launch { MaskUpdate.download(ctx, s.r) } },
                        Modifier.fillMaxWidth(), BtnKind.Fill, sub = "ABOUT 2 MB · NEEDS INTERNET")
                    !on -> Note("Downloaded to this phone. Join the mask's WiFi to send it.", color = LocalAccent.current)
                    behind -> HudButton("Send to the mask", { ask = true }, Modifier.fillMaxWidth(), BtnKind.Fill, sub = "DOWNLOADED")
                    else -> Note("The mask already has this version.", color = Hud.Dim)
                }
            }
            is MaskUpdate.State.Downloading -> {
                LabVal("Downloading", "${(s.progress * 100).toInt()}%"); Gap(6); Gauge(s.progress)
            }
            is MaskUpdate.State.Sending -> {
                LabVal(s.step, "${(s.progress * 100).toInt()}%", Hud.Amber); Gap(6); Gauge(s.progress, color = Hud.Amber)
                Hint("Keep the phone close and this screen open.", Modifier.padding(top = 6.dp), Hud.Amber)
            }
            is MaskUpdate.State.Done -> {
                Note(s.note, Modifier.padding(bottom = 8.dp), LocalAccent.current)
                HudButton("Check again", { scope.launch { MaskUpdate.check(ctx) } }, Modifier.fillMaxWidth())
            }
        }
        Hint("Firmware first, then the web pages and any new built-in faces. Your settings, screen layout and " +
            "faces are never replaced. If new firmware doesn't start, the mask goes back to the old one.",
            Modifier.padding(top = 8.dp))
    }
    if (ask && r != null && info != null) Confirm("Update the mask to v${r.name}?",
        "It restarts partway through. Keep the phone close and the app open until it says done.", "Update", {
            scope.launch { MaskUpdate.send(ctx, r, info) }
        }, { ask = false }, danger = false)
}

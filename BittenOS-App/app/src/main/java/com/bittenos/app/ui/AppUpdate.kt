package com.bittenos.app.ui

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.bittenos.app.Updater
import kotlinx.coroutines.launch

/** Updating this app from the GitHub repo: the version, what changed, download and install. */
@Composable
fun AppUpdatePanel(code: String) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val st by Updater.state.collectAsState()
    val (cur, curName) = remember { Updater.current(ctx) }
    var canInstall by remember { mutableStateOf(Updater.canInstall(ctx)) }
    // Back from Android's "install unknown apps" switch: look again
    val owner = androidx.compose.ui.platform.LocalLifecycleOwner.current
    DisposableEffect(owner) {
        val o = LifecycleEventObserver { _, e -> if (e == Lifecycle.Event.ON_RESUME) canInstall = Updater.canInstall(ctx) }
        owner.lifecycle.addObserver(o)
        onDispose { owner.lifecycle.removeObserver(o) }
    }
    LaunchedEffect(Unit) { Updater.check(ctx, quiet = true) }
    Panel(code, "This app", trailing = {
        Mono(when (st) {
            is Updater.State.Available, is Updater.State.Downloading, is Updater.State.Ready -> "UPDATE"
            is Updater.State.Latest -> "UP TO DATE"
            else -> "v$curName"
        }, color = if (st is Updater.State.Latest || st is Updater.State.Idle) Hud.Faint else Hud.Amber, size = 10.sp)
    }) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Stat("Installed", "v$curName · $cur", Modifier.weight(1f))
            val r = when (val s = st) {
                is Updater.State.Latest -> s.r; is Updater.State.Available -> s.r
                is Updater.State.Downloading -> s.r; is Updater.State.Ready -> s.r; else -> null
            }
            Stat("On GitHub", r?.let { "v${it.name} · ${it.code}" } ?: "—", Modifier.weight(1f),
                if (r != null && r.code > cur) Hud.Amber else Hud.Text)
        }
        Gap(10)
        when (val s = st) {
            is Updater.State.Idle, is Updater.State.Latest, is Updater.State.Failed, is Updater.State.Checking -> {
                if (s is Updater.State.Failed) Note(s.why, Modifier.padding(bottom = 8.dp), Hud.Red)
                HudButton(if (s is Updater.State.Checking) "Checking GitHub…" else "Check for an update",
                    { scope.launch { Updater.check(ctx) } }, Modifier.fillMaxWidth(), enabled = s !is Updater.State.Checking)
            }
            is Updater.State.Available -> {
                if (s.r.notes.isNotBlank()) Note(s.r.notes, Modifier.padding(bottom = 8.dp), Hud.Text2)
                HudButton("Download v${s.r.name}", { scope.launch { Updater.download(ctx, s.r) } }, Modifier.fillMaxWidth(),
                    BtnKind.Fill, sub = "ABOUT 16 MB")
            }
            is Updater.State.Downloading -> {
                LabVal("Downloading", "${(s.progress * 100).toInt()}%")
                Gap(6); Gauge(s.progress)
            }
            is Updater.State.Ready -> {
                if (!canInstall) {
                    Note("Android needs to allow BittenOS to install apps, once. Switch it on, then come back.",
                        Modifier.padding(bottom = 8.dp), Hud.Amber)
                    HudButton("Allow installing", { Updater.allowInstalls(ctx) }, Modifier.fillMaxWidth(), BtnKind.Amber)
                } else HudButton("Install v${s.r.name}", { Updater.install(ctx, s.file) }, Modifier.fillMaxWidth(), BtnKind.Fill,
                    sub = "ANDROID ASKS FIRST")
            }
        }
        val changes = when (val s = st) {
            is Updater.State.Latest -> s.r.changes; is Updater.State.Available -> s.r.changes
            is Updater.State.Downloading -> s.r.changes; is Updater.State.Ready -> s.r.changes; else -> emptyList()
        }
        if (changes.isNotEmpty()) {
            Gap(12)
            Caps("Recent changes in the repo"); Gap(4)
            changes.take(8).forEach { c ->
                Row(Modifier.padding(vertical = 3.dp)) {
                    Mono(c.date.drop(5), color = Hud.Faint, size = 9.sp)
                    HGap(8)
                    Note(c.title, Modifier.weight(1f))
                }
            }
        }
        Hint("Reads github.com/${Updater.REPO}. Uses mobile data or a WiFi with internet, never the mask's. " +
            "Your sounds and settings stay.", Modifier.padding(top = 8.dp))
    }
}

/** A strip on the home screen when GitHub has a newer app. */
@Composable
fun UpdateBanner() {
    val st by Updater.state.collectAsState()
    val ready = st is Updater.State.Available || st is Updater.State.Ready
    if (!ready) return
    Row(Modifier.fillMaxWidth().padding(horizontal = Hud.Edge, vertical = 4.dp)) {
        HudButton("App update available", { Nav.go("update") }, Modifier.fillMaxWidth(), BtnKind.Amber, sub = "TAP TO SEE WHAT'S NEW")
    }
}

package com.bittenos.app.ui

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bittenos.app.AppPrefs
import com.bittenos.app.Cfg
import com.bittenos.app.Faces
import com.bittenos.app.MaskLink
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/** Progress for an upload or download, shown in a sheet. */
private data class Job(val title: String, val done: Int, val total: Int, val note: String, val finished: Boolean = false)

@Composable
fun CreateScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val on = online()
    val faces by Faces.list.collectAsState()
    val last by AppPrefs.lastEdited.collectAsState()
    var job by remember { mutableStateOf<Job?>(null) }
    val pickFaces = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) scope.launch { uploadFaces(ctx, uris) { job = it } }
    }
    val saveZip = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
        if (uri != null) scope.launch { downloadFaces(ctx, uri) { job = it } }
    }
    HudColumn {
        Crumb("007", "Create")
        Panel("01", "Face maker") {
            Note("Draw faces frame by frame on the mask's own editor.")
            Gap(10)
            val cont = last?.takeIf { it in faces }
            if (cont != null) HudButton("Continue · ${Faces.label(cont)}", { Nav.web("/animator.html?face=" + MaskLink.enc(cont)) },
                Modifier.fillMaxWidth(), BtnKind.Fill, enabled = on)
            if (cont != null) Gap(5)
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                HudButton("New face", { Nav.web("/animator.html") }, Modifier.weight(1f), if (cont == null) BtnKind.Fill else BtnKind.Plain, enabled = on)
                HudButton("Open one", { Nav.go("home") }, Modifier.weight(1f), enabled = on, sub = "HOLD A TILE")
            }
        }
        Panel("02", "Face files", trailing = { Mono("${faces.size} ON THE MASK", size = 10.sp) }) {
            Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                HudButton("Upload", { pickFaces.launch(arrayOf("application/json", "application/zip", "application/octet-stream", "*/*")) },
                    Modifier.weight(1f), enabled = on, sub = ".JSON OR .ZIP")
                HudButton("Download all", { saveZip.launch("bittenos-faces.zip") }, Modifier.weight(1f), enabled = on && faces.isNotEmpty(),
                    sub = ".ZIP")
            }
            Hint("A face with the same name is replaced. Download first if you want a copy.", Modifier.padding(top = 8.dp))
        }
        PhrasesPanel()
        Panel("04", "Screen designer") {
            Note("Lay out the little status screen inside the helmet.")
            Gap(10)
            HudButton("Open the designer", { Nav.web("/screen.html") }, Modifier.fillMaxWidth(), BtnKind.Out, enabled = on)
        }
    }
    job?.let { j ->
        Sheet(j.title, { if (j.finished) job = null }) {
            LabVal(if (j.finished) "Done" else "Working…", "${j.done} / ${j.total}")
            Gap(6)
            Gauge(if (j.total == 0) 1f else j.done.toFloat() / j.total)
            Gap(8)
            Note(j.note)
            if (j.finished) { Gap(12); HudButton("Close", { job = null }, Modifier.fillMaxWidth()) }
        }
    }
}

private fun nameOf(ctx: Context, u: Uri): String = try {
    ctx.contentResolver.query(u, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
        if (c.moveToFirst()) c.getString(0) else null
    } ?: (u.lastPathSegment ?: "face")
} catch (e: Exception) { u.lastPathSegment ?: "face" }

private fun cleanLabel(n: String) = n.substringAfterLast('/').removeSuffix(".json").replace(Regex("[^A-Za-z0-9_-]"), "_").take(24)

private suspend fun uploadFaces(ctx: Context, uris: List<Uri>, show: (Job) -> Unit) {
    // Gather every face first, from plain files and from inside zips
    val todo = mutableListOf<Pair<String, String>>()
    withContext(Dispatchers.IO) {
        for (u in uris) {
            val name = nameOf(ctx, u)
            try {
                ctx.contentResolver.openInputStream(u)?.use { input ->
                    if (name.endsWith(".zip", true)) ZipInputStream(input).use { z ->
                        while (true) {
                            val e = z.nextEntry ?: break
                            if (!e.isDirectory && e.name.endsWith(".json", true)) todo += cleanLabel(e.name) to z.readBytes().decodeToString()
                        }
                    } else todo += cleanLabel(name) to input.readBytes().decodeToString()
                }
            } catch (e: Exception) {}
        }
    }
    var ok = 0
    val bad = mutableListOf<String>()
    todo.forEachIndexed { i, (label, text) ->
        show(Job("Uploading faces", i, todo.size, label))
        val valid = try { org.json.JSONObject(text).has("visor") } catch (e: Exception) { false }
        if (valid && Faces.upload(label, text)) ok++ else bad += label
    }
    Faces.refresh()
    show(Job("Uploading faces", todo.size, todo.size,
        "$ok uploaded" + if (bad.isNotEmpty()) ". Not faces, or refused: ${bad.joinToString(", ")}" else "", true))
}

private suspend fun downloadFaces(ctx: Context, uri: Uri, show: (Job) -> Unit) {
    val names = Faces.list.value
    var ok = 0
    try {
        withContext(Dispatchers.IO) {
            ctx.contentResolver.openOutputStream(uri)?.use { out ->
                ZipOutputStream(out).use { z ->
                    names.forEachIndexed { i, n ->
                        withContext(Dispatchers.Main) { show(Job("Downloading faces", i, names.size, n)) }
                        val t = MaskLink.fetch("/anims/" + MaskLink.enc(n).replace("+", "%20"), 6000) ?: return@forEachIndexed
                        z.putNextEntry(ZipEntry(n)); z.write(t.toByteArray()); z.closeEntry()
                        ok++
                    }
                }
            }
        }
        show(Job("Downloading faces", names.size, names.size, "$ok of ${names.size} faces saved to the zip.", true))
    } catch (e: Exception) {
        show(Job("Downloading faces", ok, names.size, "Couldn't write the file: ${e.message}", true))
    }
}

private val SPEED_OPTS = listOf(160 to "V.slow", 90 to "Slow", 55 to "Normal", 30 to "Fast")

@Composable
private fun PhrasesPanel() {
    val cfg by Cfg.data.collectAsState()
    @Suppress("UNUSED_EXPRESSION") cfg
    var open by remember { mutableIntStateOf(-1) }
    Panel("03", "Quick phrases", trailing = { Mono("6 SLOTS", size = 10.sp) }) {
        (0 until 6).forEach { i ->
            val t = Cfg.phrase(i)
            SetRow("${i + 1} · " + t.ifEmpty { "empty" }, if (t.isEmpty()) null else
                (SPEED_OPTS.firstOrNull { it.first == Cfg.phraseSpeed(i) }?.second ?: "${Cfg.phraseSpeed(i)} ms") +
                    if (Cfg.phraseOnce(i)) " · plays once" else " · repeats", top = i > 0) {
                LinkBtn("Edit ›", { open = i })
            }
        }
        Hint("Phrases show from the Say panel, the mask button or the remote.")
    }
    if (open >= 0) PhraseSheet(open) { open = -1 }
}

@Composable
private fun PhraseSheet(i: Int, close: () -> Unit) {
    val scope = rememberCoroutineScope()
    var text by remember { mutableStateOf(Cfg.phrase(i)) }
    var speed by remember { mutableIntStateOf(Cfg.phraseSpeed(i).let { s -> SPEED_OPTS.minBy { Math.abs(it.first - s) }.first }) }
    var once by remember { mutableStateOf(Cfg.phraseOnce(i)) }
    suspend fun save(t: String, sp: Int, o: Boolean): Boolean {
        val (ok, why) = MaskLink.saveConfig(listOf("phrase$i" to t, "phraseSpeed$i" to "$sp", "phraseOnce$i" to if (o) "true" else "false"))
        if (ok) Cfg.putPhraseLocal(i, t, sp, o) else Toast.bad(why.ifBlank { "Not saved" })
        return ok
    }
    Sheet("Phrase ${i + 1}", close) {
        HudInput(text, { text = it }, "Phrase text", Modifier.fillMaxWidth(), maxLen = 60)
        Gap(8)
        Seg(SPEED_OPTS, speed, { speed = it })
        Gap(5)
        Seg(listOf(false to "Repeat", true to "Play once"), once, { once = it })
        Gap(8)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Hint("Try it first from the Say panel.", Modifier.weight(1f))
        }
        Gap(12)
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Clear", { scope.launch { if (save("", 55, false)) { Toast.ok("Phrase ${i + 1} cleared"); close() } } },
                Modifier.weight(1f), BtnKind.Danger, enabled = Cfg.phrase(i).isNotEmpty())
            HudButton("Save", { scope.launch { if (save(text.trim(), speed, once)) { Toast.ok("Phrase ${i + 1} saved"); close() } } },
                Modifier.weight(1f), BtnKind.Fill, enabled = text.isNotBlank())
        }
    }
}

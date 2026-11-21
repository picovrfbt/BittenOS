package com.bittenos.app

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.zip.ZipInputStream

/**
 * Updates the mask from the GitHub repo through this phone. Each mask version is a release tagged
 * mask-<n> with firmware.bin and web.zip. The phone downloads them over the internet (they're kept,
 * so this can happen at home), then on the mask's WiFi sends:
 *  1. the firmware to /ota, and waits for the mask to restart and report the new version
 *  2. each web page to /webfile, then /webdone to note the version
 *  3. any built-in face the mask doesn't have yet (faces it has are never overwritten)
 * Settings, the screen layout and the person's own faces are never sent or replaced.
 */
object MaskUpdate {
    data class Release(val code: Int, val name: String, val notes: String, val fwUrl: String, val webUrl: String?)

    sealed interface State {
        data object Idle : State
        data object Checking : State
        /** [have] is true once both files are downloaded and on this phone. */
        data class Found(val r: Release, val have: Boolean) : State
        data class Downloading(val r: Release, val progress: Float) : State
        data class Sending(val r: Release, val step: String, val progress: Float) : State
        data class Done(val r: Release, val note: String) : State
        data class Failed(val why: String, val r: Release? = null) : State
    }

    val state = MutableStateFlow<State>(State.Idle)

    private fun dir(ctx: Context, code: Int) = File(ctx.filesDir, "mask/$code")
    private fun fw(ctx: Context, code: Int) = File(dir(ctx, code), "firmware.bin")
    private fun web(ctx: Context, code: Int) = File(dir(ctx, code), "web.zip")
    fun have(ctx: Context, r: Release) = fw(ctx, r.code).isFile && (r.webUrl == null || web(ctx, r.code).isFile)

    /** The highest mask-<n> release on GitHub. */
    suspend fun check(ctx: Context) {
        val s = state.value
        if (s is State.Checking || s is State.Downloading || s is State.Sending) return
        state.value = State.Checking
        state.value = withContext(Dispatchers.IO) {
            try {
                val net = Updater.internet(ctx) ?: return@withContext offline(ctx)
                val rels = JSONArray(Updater.text(net, "https://api.github.com/repos/${Updater.REPO}/releases?per_page=30"))
                val best = (0 until rels.length()).map { rels.getJSONObject(it) }
                    .filter { !it.optBoolean("draft") && Regex("mask-[0-9]+").matches(it.optString("tag_name")) }
                    .maxByOrNull { it.getString("tag_name").removePrefix("mask-").toInt() }
                    ?: return@withContext State.Failed("No mask releases on GitHub yet")
                val assets = best.getJSONArray("assets")
                fun url(n: String) = (0 until assets.length()).map { assets.getJSONObject(it) }
                    .firstOrNull { it.optString("name") == n }?.getString("browser_download_url")
                val fwUrl = url("firmware.bin") ?: return@withContext State.Failed("The newest mask release has no firmware.bin")
                val r = Release(best.getString("tag_name").removePrefix("mask-").toInt(),
                    best.optString("name").removePrefix("BittenOS mask ").removePrefix("v"),
                    best.optString("body").trim(), fwUrl, url("web.zip"))
                remember(ctx, r)
                State.Found(r, have(ctx, r))
            } catch (e: Exception) {
                State.Failed(e.message?.takeIf { it.length < 120 } ?: "Couldn't reach GitHub")
            }
        }
    }

    /** No internet (on the mask's WiFi with mobile data off): offer what was downloaded before. */
    private fun offline(ctx: Context): State {
        val r = remembered(ctx) ?: return State.Failed("No internet. Turn on mobile data, or check at home first.")
        return State.Found(r, have(ctx, r))
    }

    private fun remember(ctx: Context, r: Release) {
        ctx.getSharedPreferences("maskupdate", Context.MODE_PRIVATE).edit().putString("last", JSONObject()
            .put("code", r.code).put("name", r.name).put("notes", r.notes).put("fw", r.fwUrl).put("web", r.webUrl ?: "")
            .toString()).apply()
    }

    private fun remembered(ctx: Context): Release? = try {
        val o = JSONObject(ctx.getSharedPreferences("maskupdate", Context.MODE_PRIVATE).getString("last", null) ?: "")
        Release(o.getInt("code"), o.optString("name"), o.optString("notes"), o.getString("fw"), o.optString("web").ifEmpty { null })
    } catch (e: Exception) { null }

    /** Fetch firmware.bin and web.zip and keep them on the phone. */
    suspend fun download(ctx: Context, r: Release) {
        state.value = State.Downloading(r, 0f)
        state.value = withContext(Dispatchers.IO) {
            try {
                val net = Updater.internet(ctx) ?: return@withContext State.Failed("No internet any more", r)
                File(ctx.filesDir, "mask").listFiles()?.forEach { if (it.name != "${r.code}") it.deleteRecursively() }
                dir(ctx, r.code).mkdirs()
                val jobs = listOfNotNull(r.fwUrl to fw(ctx, r.code), r.webUrl?.let { it to web(ctx, r.code) })
                jobs.forEachIndexed { i, (url, f) ->
                    val part = File(f.path + ".part")
                    val c = Updater.open(net, url, 30_000)
                    try {
                        if (c.responseCode !in 200..299) throw Exception("GitHub said ${c.responseCode}")
                        val total = c.contentLengthLong.takeIf { it > 0 } ?: 1_600_000L
                        var got = 0L
                        var shown = 0f
                        c.inputStream.use { input -> part.outputStream().use { out ->
                            val buf = ByteArray(64 * 1024)
                            while (true) {
                                val n = input.read(buf); if (n < 0) break
                                out.write(buf, 0, n); got += n
                                val p = (i + (got.toFloat() / total).coerceAtMost(1f)) / jobs.size
                                if (p - shown > 0.01f) { shown = p; state.value = State.Downloading(r, p) }
                            }
                        } }
                    } finally { c.disconnect() }
                    part.renameTo(f)
                }
                if (fw(ctx, r.code).inputStream().use { it.read() } != 0xE9)
                    return@withContext State.Failed("The firmware download isn't an ESP32 image", r)
                State.Found(r, true)
            } catch (e: Exception) {
                State.Failed(e.message?.takeIf { it.length < 120 } ?: "Download failed", r)
            }
        }
    }

    /** Send it all to the mask. [info] is the mask's /otainfo, read just before. */
    suspend fun send(ctx: Context, r: Release, info: JSONObject) {
        fun step(t: String, p: Float) { state.value = State.Sending(r, t, p) }
        try {
            var maskFw = info.optInt("fw", 0)
            if (maskFw < r.code) {
                step("Sending the firmware", 0f)
                val bytes = withContext(Dispatchers.IO) { fw(ctx, r.code).readBytes() }
                val res = MaskLink.post("/ota", listOf(Triple("file", "firmware.bin", bytes)), timeout = 120_000) { p ->
                    step("Sending the firmware", p * 0.6f)
                }
                if (res == null || res.first !in 200..299) {
                    state.value = State.Failed(res?.second?.ifBlank { null }
                        ?: "The connection dropped before the firmware finished. Nothing was changed.", r)
                    return
                }
                // The mask restarts into the new firmware; it checks itself and goes back if it doesn't start
                step("The mask is restarting…", 0.65f)
                var back: JSONObject? = null
                for (i in 0 until 60) {
                    delay(2000)
                    MaskLink.kick()
                    back = MaskLink.json("/otainfo")
                    if (back != null) break
                }
                if (back == null) {
                    state.value = State.Failed("The mask hasn't come back after two minutes. Check it's on, rejoin its WiFi and try again.", r)
                    return
                }
                maskFw = back.optInt("fw", 0)
                if (maskFw < r.code) {
                    state.value = State.Failed("The new firmware didn't start, so the mask went back to the old one. Nothing else was changed.", r)
                    return
                }
            }
            val zip = web(ctx, r.code)
            var pages = 0
            var faces = 0
            if (zip.isFile) {
                val entries = withContext(Dispatchers.IO) {
                    val out = mutableListOf<Pair<String, ByteArray>>()
                    ZipInputStream(zip.inputStream()).use { z ->
                        while (true) { val e = z.nextEntry ?: break; if (!e.isDirectory) out += e.name to z.readBytes() }
                    }
                    out
                }
                val web = entries.filter { '/' !in it.first }
                web.forEachIndexed { i, (n, b) ->
                    step("Web pages · $n", 0.7f + 0.2f * i / web.size)
                    val res = MaskLink.post("/webfile", listOf(Triple("file", n, b)), timeout = 30_000)
                    if (res == null || res.first !in 200..299) {
                        state.value = State.Failed("Stopped at $n: ${res?.second?.ifBlank { null } ?: "the connection dropped"}. " +
                            "Pages already sent are fine; try again to finish.", r)
                        return
                    }
                    pages++
                }
                // Built-in faces the mask doesn't have yet. Ones it has may have been edited: leave them.
                step("Faces", 0.92f)
                Faces.refresh()
                val onMask = Faces.list.value.toSet()
                for ((n, b) in entries.filter { it.first.startsWith("anims/") }) {
                    val file = n.removePrefix("anims/")
                    if (file in onMask || file.removeSuffix(".json") in onMask) continue
                    if (Faces.upload(file.removeSuffix(".json"), b.decodeToString())) faces++
                }
                if (MaskLink.get("/webdone?ver=${r.code}") == null) {
                    state.value = State.Failed("The pages went over but the mask didn't note the version. Try again.", r)
                    return
                }
                Faces.refresh()
            }
            state.value = State.Done(r, "The mask is on v${r.name}. $pages web pages" +
                (if (faces > 0) ", $faces new face${if (faces == 1) "" else "s"}" else "") + ". Settings and your faces kept.")
        } catch (e: Exception) {
            state.value = State.Failed(e.message?.takeIf { it.length < 120 } ?: "Update failed", r)
        }
    }

    /** Where the mask is behind: firmware, web pages, both, or nothing. */
    fun behind(info: JSONObject?, r: Release): Boolean =
        info != null && (info.optInt("fw", 0) < r.code || (r.webUrl != null && info.optInt("web", 0) < r.code))
}

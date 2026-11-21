package com.bittenos.app

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.util.zip.ZipInputStream

/**
 * Updates the mask from the copy this app carries - a backup for when the PC isn't to hand. The app
 * build packs the mask's firmware, web pages and built-in faces into its assets (see bundleMask in
 * build.gradle.kts), so a new app always brings the matching mask with it and no internet is needed.
 * On the mask's WiFi it sends:
 *  1. the firmware to /ota, and waits for the mask to restart and report the new version
 *  2. each web page to /webfile, then /webdone to note the version
 *  3. any built-in face the mask doesn't have yet (faces it has are never overwritten)
 * Settings, the screen layout and the person's own faces are never sent or replaced. It only ever
 * offers to go forwards: a mask flashed from the PC with newer code is left alone.
 */
object MaskUpdate {
    data class Bundled(val code: Int, val name: String)

    sealed interface State {
        data object Idle : State
        data class Sending(val step: String, val progress: Float) : State
        data class Done(val note: String) : State
        data class Failed(val why: String) : State
    }

    val state = MutableStateFlow<State>(State.Idle)

    /** The mask version inside this app, or null if it was built without one. */
    fun bundled(ctx: Context): Bundled? = try {
        val l = ctx.assets.open("mask/version.txt").use { it.readBytes().decodeToString() }.lines()
        Bundled(l[0].trim().toInt(), l.getOrElse(1) { "" }.trim())
    } catch (e: Exception) { null }

    /** The firmware is behind, or the web pages are (a phone update that stopped partway). */
    fun behind(info: JSONObject?, b: Bundled?): Boolean = info != null && b != null &&
        (info.optInt("fw", 0) < b.code || (info.optInt("fw", 0) == b.code && info.optInt("web", b.code) < b.code))

    /** Same for the home screen, from /stats (fwVer is there from versioned firmware on). */
    fun behindStats(stats: JSONObject?, b: Bundled?): Boolean = stats != null && b != null && stats.optInt("fwVer", 0) < b.code

    /** Send it all to the mask. [info] is the mask's /otainfo, read just before. */
    suspend fun send(ctx: Context, info: JSONObject) {
        val b = bundled(ctx) ?: run { state.value = State.Failed("This app doesn't carry a mask update"); return }
        fun step(t: String, p: Float) { state.value = State.Sending(t, p) }
        try {
            if (info.optInt("fw", 0) < b.code) {
                step("Sending the firmware", 0f)
                val bytes = withContext(Dispatchers.IO) { ctx.assets.open("mask/firmware.bin").use { it.readBytes() } }
                val res = MaskLink.post("/ota", listOf(Triple("file", "firmware.bin", bytes)), timeout = 120_000) { p ->
                    step("Sending the firmware", p * 0.6f)
                }
                if (res == null || res.first !in 200..299) {
                    state.value = State.Failed(res?.second?.ifBlank { null }
                        ?: "The connection dropped before the firmware finished. Nothing was changed.")
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
                    state.value = State.Failed("The mask hasn't come back after two minutes. Check it's on, rejoin its WiFi and try again.")
                    return
                }
                if (back.optInt("fw", 0) < b.code) {
                    state.value = State.Failed("The new firmware didn't start, so the mask went back to the old one. Nothing else was changed.")
                    return
                }
            }
            val entries = withContext(Dispatchers.IO) {
                val out = mutableListOf<Pair<String, ByteArray>>()
                ZipInputStream(ctx.assets.open("mask/web.zip")).use { z ->
                    while (true) { val e = z.nextEntry ?: break; if (!e.isDirectory) out += e.name to z.readBytes() }
                }
                out
            }
            val web = entries.filter { '/' !in it.first }
            web.forEachIndexed { i, (n, bytes) ->
                step("Web pages · $n", 0.7f + 0.2f * i / web.size)
                val res = MaskLink.post("/webfile", listOf(Triple("file", n, bytes)), timeout = 30_000)
                if (res == null || res.first !in 200..299) {
                    state.value = State.Failed("Stopped at $n: ${res?.second?.ifBlank { null } ?: "the connection dropped"}. " +
                        "Pages already sent are fine; send again to finish.")
                    return
                }
            }
            // Built-in faces the mask doesn't have yet. Ones it has may have been edited: leave them.
            step("Faces", 0.92f)
            Faces.refresh()
            val onMask = Faces.list.value.toSet()
            var faces = 0
            for ((n, bytes) in entries.filter { it.first.startsWith("anims/") }) {
                val file = n.removePrefix("anims/")
                if (file in onMask || file.removeSuffix(".json") in onMask) continue
                if (Faces.upload(file.removeSuffix(".json"), bytes.decodeToString())) faces++
            }
            if (MaskLink.get("/webdone?ver=${b.code}") == null) {
                state.value = State.Failed("The pages went over but the mask didn't note the version. Send again.")
                return
            }
            Faces.refresh()
            state.value = State.Done("The mask is on v${b.name}. ${web.size} web pages" +
                (if (faces > 0) ", $faces new face${if (faces == 1) "" else "s"}" else "") + ". Settings and your faces kept.")
        } catch (e: Exception) {
            state.value = State.Failed(e.message?.takeIf { it.length < 120 } ?: "Update failed")
        }
    }
}

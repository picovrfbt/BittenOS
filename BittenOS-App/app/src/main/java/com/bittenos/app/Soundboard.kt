package com.bittenos.app

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.net.Uri
import android.os.Build
import android.provider.OpenableColumns
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import java.net.SocketTimeoutException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Sounds that play from this phone: imported from the phone's files, named, and played with a tap
 * or from a button on the remote.
 *
 * Each sound sits in a numbered slot that never changes while it exists, because the mask's
 * button and remote settings point at the number. Deleting a sound frees its slot; the next import
 * takes the lowest free one.
 *
 * The remote reaches the phone through the mask: a button set to "A sound" makes the mask send a
 * small UDP packet to every app that polled it lately, and count it in /stats too, so a dropped
 * packet is still caught on the next poll a second later.
 */
object Soundboard {
    const val SLOTS = 24
    const val PORT = 7374

    data class Sound(val slot: Int, val name: String, val file: String, val ms: Long, val volume: Float)

    private lateinit var app: Context
    private val dir by lazy { File(app.filesDir, "sounds").apply { mkdirs() } }
    private val prefs by lazy { app.getSharedPreferences("sounds", Context.MODE_PRIVATE) }
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val _sounds = MutableStateFlow<List<Sound>>(emptyList())
    val sounds: StateFlow<List<Sound>> = _sounds

    /** The slot playing now, or -1. */
    val playing = MutableStateFlow(-1)
    /** "Remote: Top press · airhorn", for a moment after the remote plays one. */
    val lastRemote = MutableStateFlow<String?>(null)
    /** Let a new sound cut off the one playing (on), or play over it (off). */
    val oneAtATime = MutableStateFlow(true)

    private var player: MediaPlayer? = null
    private val extra = mutableListOf<MediaPlayer>()
    private var lastSeq = -1L
    private var started = false

    fun init(ctx: Context) {
        if (started) return
        started = true
        app = ctx.applicationContext
        _sounds.value = load()
        oneAtATime.value = prefs.getBoolean("one", true)
        scope.launch { listen() }
        scope.launch { watchStats() }
        scope.launch { syncNames() }
    }

    private fun load(): List<Sound> = try {
        val a = JSONArray(prefs.getString("list", "[]"))
        (0 until a.length()).mapNotNull { i ->
            val o = a.getJSONObject(i)
            Sound(o.getInt("slot"), o.getString("name"), o.getString("file"), o.optLong("ms"),
                o.optDouble("vol", 1.0).toFloat()).takeIf { File(dir, it.file).exists() }
        }.sortedBy { it.slot }
    } catch (e: Exception) { emptyList() }

    private fun save(list: List<Sound>) {
        _sounds.value = list.sortedBy { it.slot }
        val a = JSONArray()
        _sounds.value.forEach { s ->
            a.put(JSONObject().put("slot", s.slot).put("name", s.name).put("file", s.file).put("ms", s.ms).put("vol", s.volume.toDouble()))
        }
        prefs.edit().putString("list", a.toString()).apply()
        namesDirty = true
    }

    fun setOneAtATime(v: Boolean) { oneAtATime.value = v; prefs.edit().putBoolean("one", v).apply() }

    // ---- importing -----------------------------------------------------------------------------

    /** Copy audio files the person picked into the app, each into the lowest free slot. */
    suspend fun import(uris: List<Uri>): Int = withContext(Dispatchers.IO) {
        var added = 0
        val list = _sounds.value.toMutableList()
        for (u in uris) {
            val slot = (0 until SLOTS).firstOrNull { s -> list.none { it.slot == s } } ?: break
            val display = displayName(u) ?: "sound"
            val ext = display.substringAfterLast('.', "").lowercase().takeIf { it.length in 2..4 } ?: "audio"
            val file = "s${slot}_${System.currentTimeMillis()}.$ext"
            try {
                app.contentResolver.openInputStream(u)?.use { i -> File(dir, file).outputStream().use { i.copyTo(it) } } ?: continue
            } catch (e: Exception) { continue }
            val ms = duration(File(dir, file))
            if (ms <= 0) { File(dir, file).delete(); continue }          // not something Android can play
            list += Sound(slot, cleanName(display.substringBeforeLast('.')), file, ms, 1f)
            added++
        }
        save(list)
        added
    }

    fun cleanName(n: String) = n.replace(';', ' ').replace('_', ' ').trim().take(24).ifEmpty { "Sound" }

    private fun displayName(u: Uri): String? = try {
        app.contentResolver.query(u, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    } catch (e: Exception) { null }

    private fun duration(f: File): Long = try {
        MediaMetadataRetriever().run {
            setDataSource(f.absolutePath)
            val d = extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
            release(); d
        }
    } catch (e: Exception) { 0L }

    fun rename(slot: Int, name: String) =
        save(_sounds.value.map { if (it.slot == slot) it.copy(name = cleanName(name)) else it })

    fun setVolume(slot: Int, v: Float) =
        save(_sounds.value.map { if (it.slot == slot) it.copy(volume = v.coerceIn(0f, 1f)) else it })

    fun delete(slot: Int) {
        val s = _sounds.value.firstOrNull { it.slot == slot } ?: return
        if (playing.value == slot) stop()
        File(dir, s.file).delete()
        save(_sounds.value.filter { it.slot != slot })
    }

    /** Move a sound to another slot, swapping with whatever is there. Settings follow the number. */
    fun moveTo(slot: Int, to: Int) {
        if (to !in 0 until SLOTS || to == slot) return
        save(_sounds.value.map { when (it.slot) { slot -> it.copy(slot = to); to -> it.copy(slot = slot); else -> it } })
    }

    fun name(slot: Int) = _sounds.value.firstOrNull { it.slot == slot }?.name

    // ---- playing --------------------------------------------------------------------------------

    fun play(slot: Int) {
        val s = _sounds.value.firstOrNull { it.slot == slot } ?: return
        scope.launch(Dispatchers.Main) {
            if (oneAtATime.value) stopAll() else player?.let { extra += it; player = null }
            val mp = MediaPlayer()
            try {
                mp.setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
                mp.setDataSource(File(dir, s.file).absolutePath)
                mp.setVolume(s.volume, s.volume)
                // The same output the voice uses: the cable or the Bluetooth speaker
                if (Build.VERSION.SDK_INT >= 28) {
                    val want = VoiceService.output(app)
                    val am = app.getSystemService(AudioManager::class.java)
                    am.getDevices(AudioManager.GET_DEVICES_OUTPUTS).firstOrNull { want.matches(it) }?.let { mp.setPreferredDevice(it) }
                }
                mp.setOnCompletionListener {
                    it.release()
                    if (player === it) { player = null; playing.value = -1 } else extra.remove(it)
                }
                mp.prepare()
                mp.start()
                player = mp
                playing.value = slot
            } catch (e: Exception) {
                mp.release()
                playing.value = -1
            }
        }
    }

    fun stop() { scope.launch(Dispatchers.Main) { stopAll() } }

    private fun stopAll() {
        player?.let { try { it.stop() } catch (e: Exception) {}; it.release() }
        player = null
        extra.forEach { try { it.stop() } catch (e: Exception) {}; it.release() }
        extra.clear()
        playing.value = -1
    }

    // ---- the remote, through the mask --------------------------------------------------------------

    private fun fromRemote(seq: Long, slot: Int) {
        if (seq <= lastSeq) return                // the second copy of the packet, or the poll catching up
        lastSeq = seq
        val st = (MaskLink.state.value as? MaskLink.State.Connected)?.stats
        val who = st?.let { listOf("Top", "Middle", "Bottom").getOrNull(it.optInt("rmtBtn", -1)) }
        lastRemote.value = (who?.let { "$it · " } ?: "") + (name(slot) ?: "sound ${slot + 1} (none in that slot)")
        play(slot)
    }

    private suspend fun listen() {
        while (true) {
            val sock = try {
                DatagramSocket(null).apply {
                    reuseAddress = true
                    bind(InetSocketAddress(PORT))
                    soTimeout = 2000
                    MaskLink.network?.let { n -> try { n.bindSocket(this) } catch (e: Exception) {} }
                }
            } catch (e: Exception) { delay(3000); continue }
            val boundTo = MaskLink.network
            val buf = ByteArray(64)
            try {
                while (MaskLink.network == boundTo) {
                    val p = DatagramPacket(buf, buf.size)
                    try { sock.receive(p) } catch (e: SocketTimeoutException) { continue }
                    if (p.length >= 9 && buf[0] == 'B'.code.toByte() && buf[1] == 'S'.code.toByte() &&
                        buf[2] == 'N'.code.toByte() && buf[3] == 'D'.code.toByte()) {
                        val seq = ByteBuffer.wrap(buf, 4, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xffffffffL
                        fromRemote(seq, buf[8].toInt() and 0xff)
                    }
                }
            } catch (e: Exception) {
            } finally { sock.close() }          // the WiFi changed: bind to the new one
        }
    }

    /** The fallback: /stats counts every sound asked for, so a lost packet still plays a second late. */
    private suspend fun watchStats() {
        var host: String? = null
        MaskLink.state.collect { st ->
            val c = st as? MaskLink.State.Connected ?: return@collect
            if (!c.stats.has("snd")) return@collect
            val seq = c.stats.optLong("snd")
            if (c.host != host || seq < lastSeq) {      // a new mask, or it restarted: start counting from here
                host = c.host
                lastSeq = seq
                return@collect
            }
            if (seq > lastSeq) fromRemote(seq, c.stats.optInt("sndSlot", 0))
        }
    }

    // ---- telling the mask the names ------------------------------------------------------------------

    @Volatile private var namesDirty = true

    /** The mask keeps the names so its own pickers (and the web page) can show them. */
    private suspend fun syncNames() {
        while (true) {
            delay(1500)
            if (MaskLink.host == null) continue
            val want = (0 until SLOTS).map { s -> name(s) ?: "" }.joinToString(";").trimEnd(';')
            val has = Cfg.data.value.optString("sndNames", "")
            if (!namesDirty && has == want) continue
            if (!Cfg.fresh.value) continue                  // wait until the real settings are in
            if (has != want) {
                val (ok, _) = MaskLink.saveConfig(listOf("sndNames" to want))
                if (ok) { Cfg.data.value.put("sndNames", want); namesDirty = false }
            } else namesDirty = false
        }
    }

    fun fmtLen(ms: Long) = if (ms < 10_000) "%.1f s".format(ms / 1000.0) else "${ms / 1000} s"
}

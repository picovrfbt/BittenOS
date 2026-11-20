package com.bittenos.app

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONArray
import org.json.JSONObject

/**
 * Changes made while the mask was out of reach, waiting to go across.
 *
 * Only settings the mask keeps are queued: brightness and voice gain. Showing a face, sleep,
 * panels and rave are things you want *now*, so they still need the mask in reach; replaying
 * them later would only surprise you.
 *
 * Each change remembers what the mask had when you made it (base). If the mask has something else
 * by the time it is back, somebody changed it there in the meantime, and the app asks rather than
 * overwriting it.
 */
object Pending {
    enum class Key(val label: String, val unit: String) {
        Brightness("Brightness", " of 15"),
        VoiceGain("Voice gain", "%"),
    }

    data class Change(val key: Key, val value: Int, val base: Int?, val at: Long)
    data class Conflict(val change: Change, val maskNow: Int)

    private val _list = MutableStateFlow<List<Change>>(emptyList())
    val list: StateFlow<List<Change>> = _list

    private val _conflicts = MutableStateFlow<List<Conflict>>(emptyList())
    val conflicts: StateFlow<List<Conflict>> = _conflicts

    /** "Sent 2 waiting changes", shown for a moment after they go across. */
    private val _note = MutableStateFlow<String?>(null)
    val note: StateFlow<String?> = _note

    private lateinit var app: Context
    private val prefs by lazy { app.getSharedPreferences("pending", Context.MODE_PRIVATE) }
    private val lock = Mutex()

    fun init(ctx: Context) {
        app = ctx.applicationContext
        _list.value = load()
    }

    /** Queue a change; a later change to the same setting replaces it but keeps the first base. */
    fun put(key: Key, value: Int, base: Int?) {
        val old = _list.value.firstOrNull { it.key == key }
        val c = Change(key, value, old?.base ?: base, System.currentTimeMillis())
        // Back to what the mask already has: nothing left to send.
        _list.value = if (c.base == value) _list.value.filter { it.key != key }
                      else _list.value.filter { it.key != key } + c
        save()
    }

    fun discard(key: Key) {
        _list.value = _list.value.filter { it.key != key }
        save()
    }

    fun clearNote() { _note.value = null }

    /** The waiting value for a setting, if there is one. */
    fun valueOf(key: Key): Int? = _list.value.firstOrNull { it.key == key }?.value

    /** Called when the mask comes back. Sends what it can, holds conflicts for the person to choose. */
    suspend fun flush() = lock.withLock {
        val todo = _list.value
        if (todo.isEmpty()) return@withLock
        var sent = 0
        val clashes = mutableListOf<Conflict>()
        for (c in todo) {
            val now = maskValue(c.key) ?: continue            // could not ask: try again next time
            when {
                now == c.value -> { drop(c.key) }              // already there
                c.base != null && now != c.base -> clashes += Conflict(c, now)
                send(c.key, c.value) -> { drop(c.key); sent++ }
            }
        }
        _conflicts.value = clashes
        if (sent > 0) _note.value = if (sent == 1) "Sent 1 waiting change" else "Sent $sent waiting changes"
    }

    /** The person's answer to a conflict. */
    suspend fun resolve(key: Key, keepMine: Boolean) {
        val cf = _conflicts.value.firstOrNull { it.change.key == key } ?: return
        if (keepMine) {
            if (send(key, cf.change.value)) drop(key) else return
        } else drop(key)
        _conflicts.value = _conflicts.value.filter { it.change.key != key }
    }

    private fun drop(key: Key) {
        _list.value = _list.value.filter { it.key != key }
        save()
    }

    private suspend fun maskValue(key: Key): Int? = when (key) {
        Key.Brightness -> MaskLink.get("/stats", 2500)?.let {
            try { JSONObject(it).takeIf { j -> j.has("bright") }?.optInt("bright") } catch (e: Exception) { null }
        }
        Key.VoiceGain -> MaskLink.micStream()?.takeIf { it.has("gain") }?.optInt("gain")
    }

    private suspend fun send(key: Key, v: Int): Boolean = when (key) {
        Key.Brightness -> MaskLink.brightness(v, true) != null
        Key.VoiceGain -> MaskLink.micStream(v) != null
    }

    private fun save() {
        val a = JSONArray()
        _list.value.forEach { c ->
            a.put(JSONObject().put("k", c.key.name).put("v", c.value).put("at", c.at).apply {
                c.base?.let { put("b", it) }
            })
        }
        prefs.edit().putString("list", a.toString()).apply()
    }

    private fun load(): List<Change> = try {
        val a = JSONArray(prefs.getString("list", "[]"))
        (0 until a.length()).mapNotNull { i ->
            val o = a.getJSONObject(i)
            val k = Key.entries.firstOrNull { it.name == o.optString("k") } ?: return@mapNotNull null
            Change(k, o.getInt("v"), if (o.has("b")) o.getInt("b") else null, o.optLong("at"))
        }
    } catch (e: Exception) { emptyList() }
}

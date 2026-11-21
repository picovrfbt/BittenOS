package com.bittenos.app

import android.content.Context
import com.bittenos.app.ui.AccentChoice
import com.bittenos.app.ui.Toast
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONArray
import org.json.JSONObject

/**
 * The mask's settings (config.json), the way the web page treats them: every control saves the
 * moment it changes, one setting at a time, so changing one can never overwrite another. If the
 * mask does not take it, the value goes back, so the screen never shows a state the mask is not in.
 */
object Cfg {
    private val _data = MutableStateFlow(JSONObject())
    val data: StateFlow<JSONObject> = _data

    /** True once this run has read the settings from the mask itself, not the copy kept from last time. */
    val fresh = MutableStateFlow(false)

    fun init() { if (_data.value.length() == 0) MaskLink.lastConfig?.let { _data.value = it } }

    suspend fun load(): Boolean {
        val c = MaskLink.config() ?: return false
        _data.value = c
        fresh.value = true
        return true
    }

    // Defaults match the web page's DEFAULTS, for settings an older config does not have yet.
    private val DEF = JSONObject(mapOf<String, Any>(
        "transEna" to true, "boopCountEna" to true, "boopLightEna" to true, "boopShade" to 30, "boopConfirm" to 5,
        "tiltTrig" to 0.6, "eggPresses" to 6, "eggAction" to "slots",
        "rmt0" to "nextFace", "rmt0H" to "displays", "rmt1" to "prevFace", "rmt1H" to "sleep", "rmt2" to "slots",
        "rmt2H" to "dino", "rmt0D" to "none", "rmt1D" to "none", "rmt2D" to "none",
        "transMs" to 80, "sleepTol" to 0.2, "sleepMins" to 5, "autoBriEna" to false, "autoBriMode" to "blank",
        "autoBriSecs" to 60, "autoBriMin" to 1, "autoBriMax" to 8, "bOled" to 2, "autoBriDark" to 5,
        "autoBriBright" to 400, "oledFlip" to false, "mouthMode" to "shapes", "boopMilestone" to 100,
        "aMilestone" to "heart.json", "sassBoops" to 5, "sassSecs" to 10, "aSass" to "upset.json",
        "blinkMode" to "face", "blinkDoublePct" to 25, "btnShort" to "nextFace", "btnDouble" to "none",
        "btnHold" to "displays", "btnHoldMs" to 1500, "sleepDeepEna" to false, "sleepDeepMins" to 20,
        "wakeFlickEna" to true, "wakeFlickBoot" to true, "wakeFlickStyle" to "crt", "wakeFlickMs" to 1800,
        "wakeFlickGlitch" to 1, "bootBarEna" to true, "bootBarMs" to 1500, "stripEna" to true, "stripRevA" to false,
        "stripRevB" to true, "stripBlushEna" to true, "stripIdleEna" to false, "stripRaveEna" to true,
        "stripNum" to 20, "stripSplit" to 10, "stripBlushMs" to 2500, "stripBlush" to "#ff2d96",
        "stripFlareEna" to false, "stripBlushHold" to true, "stripAutoEna" to false, "stripProxEna" to true,
        "stripAutoMin" to 15, "stripRaveGain" to 160, "aSleep" to "sleep.json", "micStreamGain" to 500, "deathHoldEna" to true,
        "boopThresh" to 15, "spMin" to 40, "spMax" to 500, "spTrig" to 10, "bVisor" to 5, "bStrip" to 77,
        "boopEna" to true, "speechEna" to false, "tiltEna" to false, "oledEna" to true, "sleepEna" to false,
        "bobEna" to true, "staEna" to false, "bleEna" to false, "mdnsName" to "protogen",
    ))

    private fun has(k: String) = _data.value.has(k) && !_data.value.isNull(k)
    fun bool(k: String) = if (has(k)) _data.value.optBoolean(k) else DEF.optBoolean(k)
    fun int(k: String) = if (has(k)) _data.value.optInt(k) else DEF.optInt(k)
    fun dbl(k: String) = if (has(k)) _data.value.optDouble(k) else DEF.optDouble(k, 0.0)
    fun str(k: String) = if (has(k)) _data.value.optString(k) else DEF.optString(k)
    fun arr(k: String): JSONArray? = _data.value.optJSONArray(k)

    /** Save one or more settings. Shown at once; put back if the mask did not take them. */
    suspend fun set(vararg kv: Pair<String, Any>, ok: String? = null): Boolean {
        val before = _data.value
        val next = JSONObject(before.toString())
        kv.forEach { (k, v) -> next.put(k, v) }
        _data.value = next
        if (MaskLink.host == null) {
            _data.value = before
            Toast.bad("No mask · not saved")
            return false
        }
        val (done, why) = MaskLink.saveConfig(kv.map { (k, v) -> k to fmt(v) })
        if (!done) {
            _data.value = before
            Toast.bad(why.ifBlank { "Not saved" })
        } else ok?.let { Toast.ok(it) }
        return done
    }

    /** The phrase arrays are saved one slot at a time, with an index in the key. */
    fun putPhraseLocal(i: Int, text: String, speed: Int, once: Boolean) {
        val next = JSONObject(_data.value.toString())
        fun put(k: String, v: Any) {
            val a = next.optJSONArray(k) ?: JSONArray().also { next.put(k, it) }
            while (a.length() <= i) a.put(if (v is String) "" else if (v is Boolean) false else 55)
            a.put(i, v)
        }
        put("phrase", text); put("phraseSpeed", speed); put("phraseOnce", once)
        _data.value = next
    }

    fun phrase(i: Int) = arr("phrase")?.optString(i, "") ?: ""
    fun phraseSpeed(i: Int) = arr("phraseSpeed")?.optInt(i, 55) ?: 55
    fun phraseOnce(i: Int) = arr("phraseOnce")?.optBoolean(i, false) ?: false

    private fun fmt(v: Any): String = when (v) {
        is Boolean -> if (v) "true" else "false"
        is Double -> if (v == Math.floor(v) && Math.abs(v) < 1e9) v.toLong().toString() else "%.3f".format(java.util.Locale.ROOT, v)
        is Float -> fmt(v.toDouble())
        else -> v.toString()
    }
}

/** Settings that belong to this phone, not the mask. */
object AppPrefs {
    private lateinit var app: Context
    private val prefs by lazy { app.getSharedPreferences("app", Context.MODE_PRIVATE) }
    val accent = MutableStateFlow(AccentChoice.Mint)
    val keepOn = MutableStateFlow(false)
    /** Keep working with the screen off (AwakeService). On by default. */
    val screenOff = MutableStateFlow(true)
    val raveMode = MutableStateFlow(1)
    val raveMic = MutableStateFlow(true)
    /** The face maker's last face, for "Continue". */
    val lastEdited = MutableStateFlow<String?>(null)

    private var started = false

    fun init(ctx: Context) {
        if (started) return
        started = true
        app = ctx.applicationContext
        screenOff.value = prefs.getBoolean("screenOff", true)
        accent.value = AccentChoice.entries.firstOrNull { it.name == prefs.getString("accent", null) } ?: AccentChoice.Mint
        keepOn.value = prefs.getBoolean("keepOn", false)
        raveMode.value = prefs.getInt("raveMode", 1)
        raveMic.value = prefs.getBoolean("raveMic", true)
        lastEdited.value = prefs.getString("lastEdited", null)
    }

    fun setAccent(a: AccentChoice) { accent.value = a; prefs.edit().putString("accent", a.name).apply() }
    fun setKeepOn(v: Boolean) { keepOn.value = v; prefs.edit().putBoolean("keepOn", v).apply() }
    fun setScreenOff(ctx: Context, v: Boolean) {
        screenOff.value = v
        prefs.edit().putBoolean("screenOff", v).apply()
        if (v) AwakeService.start(ctx) else AwakeService.stop(ctx)
    }
    fun setRaveMode(v: Int) { raveMode.value = v; prefs.edit().putInt("raveMode", v).apply() }
    fun setRaveMic(v: Boolean) { raveMic.value = v; prefs.edit().putBoolean("raveMic", v).apply() }
    fun setLastEdited(v: String) { lastEdited.value = v; prefs.edit().putString("lastEdited", v).apply() }
}

package com.bittenos.app

import android.content.Context
import android.graphics.Bitmap
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * The faces on the mask: the list, a copy of each face's file (kept on the phone, so the previews
 * still draw while the mask is away), the panel layout, and the picture of each face.
 */
object Faces {
    private lateinit var app: Context
    private val dir by lazy { File(app.filesDir, "faces").apply { mkdirs() } }
    private val prefs by lazy { app.getSharedPreferences("faces", Context.MODE_PRIVATE) }

    private val _list = MutableStateFlow<List<String>>(emptyList())
    /** File names ("happy.json"), in the mask's own order. */
    val list: StateFlow<List<String>> = _list
    /** Bumped whenever a preview may have changed, so tiles redraw. */
    val version = MutableStateFlow(0)

    private val anims = HashMap<String, JSONObject>()
    private val sizes = HashMap<String, Int>()
    private val pics = HashMap<String, ImageBitmap>()
    private var layout: List<List<String>> = emptyList()
    private val lock = Mutex()

    fun init(ctx: Context) {
        app = ctx.applicationContext
        _list.value = prefs.getString("list", null)?.split(';')?.filter { it.isNotEmpty() } ?: emptyList()
        MaskLink.lastLayout?.let { layout = parseLayout(it) }
    }

    fun label(f: String) = f.removeSuffix(".json")

    /** Ask the mask for its faces, then fetch any face file the phone does not have yet. */
    suspend fun refresh(): Boolean = lock.withLock {
        val txt = MaskLink.fetch("/getfiles") ?: return@withLock false
        val parts = txt.split(';').map { it.trim() }
        val names = parts.drop(1).filter { it.isNotEmpty() }
        _list.value = names
        prefs.edit().putString("list", names.joinToString(";")).apply()
        MaskLink.layout()?.let { layout = parseLayout(it) }
        for (n in names) if (!anims.containsKey(n)) load(n, fromMask = !File(dir, n).exists())
        version.value++
        true
    }

    /** Fetch one face again from the mask, after it was saved or renamed. */
    suspend fun reload(name: String) { load(name, true); version.value++ }

    private suspend fun load(name: String, fromMask: Boolean) {
        val f = File(dir, name)
        val text = if (fromMask) MaskLink.fetch("/anims/" + MaskLink.enc(name).replace("+", "%20"), 5000)
                       ?.also { withContext(Dispatchers.IO) { f.writeText(it) } }
                   else withContext(Dispatchers.IO) { try { f.readText() } catch (e: Exception) { null } }
        val j = text?.let { try { JSONObject(it) } catch (e: Exception) { null } } ?: return
        anims[name] = j
        sizes[name] = text.length
        pics.remove(name)
    }

    fun anim(name: String): JSONObject? = anims[name] ?: File(dir, name).takeIf { it.exists() }?.let {
        try { JSONObject(it.readText()).also { j -> anims[name] = j; sizes[name] = it.length().toInt() } } catch (e: Exception) { null }
    }

    fun bytes(name: String) = sizes[name] ?: 0
    fun frames(name: String) = anim(name)?.optJSONObject("visor")?.optJSONArray("frames")?.length() ?: 0

    /** The mask can only blink a single frame, or one long look plus a short blink. */
    fun canBlink(name: String): Boolean {
        val v = anim(name)?.optJSONObject("visor") ?: return false
        val fr = v.optJSONArray("frames") ?: return false
        if (fr.length() == 0) return false
        if (v.has("canBlink")) return v.optBoolean("canBlink")
        val spans = (0 until fr.length()).map { fr.optJSONObject(it)?.optInt("timespan", 0) ?: 0 }.sortedDescending()
        return spans.size == 1 || spans[0] >= spans[1] * 3
    }

    fun forget(name: String) {
        anims.remove(name); pics.remove(name); sizes.remove(name)
        File(dir, name).delete()
        version.value++
    }

    // ---- the picture of a face --------------------------------------------------------------

    private fun parseLayout(txt: String): List<List<String>> {
        val rows = mutableListOf(mutableListOf<String>())
        for (t in txt.trim().split(';')) if (t == "_") rows += mutableListOf<String>() else rows.last() += t
        return rows
    }

    private fun isPanel(c: String?) = c != null && c.length > 1 && c[0] == 't' && c.substring(1).all { it.isDigit() }

    /** Same encoding as the face maker: the last byte is the top row, the lowest bit the leftmost pixel. */
    fun panelBits(hexIn: String?): Array<IntArray> {
        val hex = ("0000000000000000" + (hexIn ?: "0")).takeLast(16)
        return Array(8) { i ->
            val b = hex.substring(16 - 2 * (i + 1), 16 - 2 * i).toInt(16)
            IntArray(8) { j -> (b shr j) and 1 }
        }
    }

    private fun leds(name: String): JSONArray? =
        anim(name)?.optJSONObject("visor")?.optJSONArray("frames")?.optJSONObject(0)?.optJSONArray("leds")

    /** Nearly every face is a mirror image, so the tile shows one half at twice the size. */
    private fun halfIfMirrored(leds: JSONArray, cols: List<Int>): List<Int> {
        if (cols.size < 2 || cols.size % 2 != 0) return cols
        val half = cols.size / 2
        var diff = 0
        for (row in layout) for (i in 0 until half) {
            val l = row.getOrNull(cols[i]); val r = row.getOrNull(cols[cols.size - 1 - i])
            if (isPanel(l) != isPanel(r)) return cols
            if (!isPanel(l)) continue
            val a = panelBits(leds.optString(l!!.substring(1).toInt(), "0"))
            val b = panelBits(leds.optString(r!!.substring(1).toInt(), "0"))
            for (y in 0 until 8) for (k in 0 until 8) if (a[y][k] != b[y][7 - k] && ++diff > 4) return cols
        }
        return cols.subList(0, half)
    }

    fun preview(name: String, lit: Int, off: Int): ImageBitmap? {
        pics[name]?.let { return it }
        val leds = leds(name) ?: return null
        if (layout.isEmpty()) return null
        val width = layout.maxOf { it.size }
        var cols = (0 until width).filter { c -> layout.any { isPanel(it.getOrNull(c)) } }
        if (cols.isEmpty()) return null
        cols = halfIfMirrored(leds, cols)
        val p = 3; val gap = 3; val panel = 8 * p
        val w = cols.size * (panel + gap) - gap
        val h = layout.size * (panel + gap) - gap
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        for ((ry, row) in layout.withIndex()) for ((cx, c) in cols.withIndex()) {
            val cell = row.getOrNull(c)
            if (!isPanel(cell)) continue
            val bits = panelBits(leds.optString(cell!!.substring(1).toInt(), "0"))
            val x0 = cx * (panel + gap); val y0 = ry * (panel + gap)
            for (r in 0 until 8) for (k in 0 until 8) {
                val col = if (bits[r][k] == 1) lit else off
                for (dy in 0 until p - 1) for (dx in 0 until p - 1) bmp.setPixel(x0 + k * p + dx, y0 + r * p + dy, col)
            }
        }
        return bmp.asImageBitmap().also { pics[name] = it }
    }

    /** The accent changed: draw the pictures again in the new colour. */
    fun clearPictures() { pics.clear(); version.value++ }

    // ---- scrolling text -------------------------------------------------------------------------

    // 5x7 font from the MD_MAX72XX library's system font (LGPL-2.1), ASCII 32-126, as the web page has it.
    // Each character is a list of columns; bit 0 is the top row.
    private val FONT: List<IntArray> = "0,0|95|7,0,7|20,127,20,127,20|68,74,255,74,50|99,19,8,100,99|54,73,73,54,72|7|62,65,65|65,65,62|8,42,28,42,8|8,8,62,8,8|96,224|8,8,8,8|96,96|96,16,8,4,3|62,81,73,69,62|4,2,127|113,73,73,73,70|65,73,73,73,54|15,8,8,8,127|79,73,73,73,49|62,73,73,73,48|3,1,1,1,127|54,73,73,73,54|6,73,73,73,62|108,108|108,236|8,20,34|20,20,20,20|34,20,8|1,89,9,9,6|62,65,93,89,78|126,9,9,9,126|127,73,73,73,54|62,65,65,65,65|127,65,65,65,62|127,73,73,73,65|127,9,9,9,1|62,65,65,73,121|127,8,8,8,127|65,127,65|48,65,65,65,63|127,8,20,34,65|127,64,64,64,64|127,2,12,2,127|127,4,8,16,127|62,65,65,65,62|127,9,9,9,6|62,65,65,97,126|127,9,25,41,70|70,73,73,73,49|1,1,127,1,1|63,64,64,64,63|31,32,64,32,31|63,64,56,64,63|99,20,8,20,99|3,4,120,4,3|97,81,73,69,67|127,65,65|3,4,8,16,96|65,65,127|4,2,1,2,4|128,128,128,128|1,2,4|56,68,68,124|127,68,68,56|56,68,68,68|56,68,68,127|56,84,84,88|4,126,5,1|24,164,164,124|127,4,4,120|125|132,133,124|127,16,40,68|127|124,4,120,4,120|124,4,4,120|56,68,68,56|252,36,36,24|24,36,36,252|124,4,4,8|88,84,84,52|4,127,4|60,64,64,124|28,32,64,124|60,64,48,64,60|108,16,16,108|28,160,160,124|100,84,84,76|8,54,65,65|127|65,65,54,8|2,1,2,1".split('|').map { c -> c.split(',').map { it.toInt() }.toIntArray() }

    private fun textColumns(text: String): List<Int> {
        val out = mutableListOf<Int>()
        for (ch in text) {
            val code = ch.code
            out += FONT[(if (code in 32..126) code else 63) - 32].toList()
            out += 0
        }
        return out
    }

    /** 8 columns (bit 0 = top) to the face maker's panel hex (last byte = top row, bit 0 = leftmost). */
    private fun panelHex(cols: IntArray): String {
        val sb = StringBuilder()
        for (r in 7 downTo 0) {
            var b = 0
            for (k in 0 until 8) if ((cols[k] shr r) and 1 == 1) b = b or (1 shl k)
            sb.append("%02x".format(b))
        }
        return sb.toString().trimStart('0').ifEmpty { "0" }
    }

    /**
     * The scrolling-text face, built here the way the web page builds it: the eyes and nose of the
     * face showing now, with the text running across the mouth row. Play once loads [then] after.
     */
    fun textAnim(text: String, speed: Int, then: String?, eyesFace: String?): JSONObject? {
        if (layout.isEmpty()) return null
        fun panelsIn(row: List<String>) = row.filter { isPanel(it) }.map { it.substring(1).toInt() }
        val count = (layout.flatMap { panelsIn(it) }.maxOrNull() ?: return null) + 1
        val strip = layout.map { panelsIn(it) }.maxByOrNull { it.size } ?: return null
        val width = strip.size * 8
        val cols = textColumns(text)
        val eyes = eyesFace?.let { leds(it) } ?: leds("default.json")
        val frames = JSONArray()
        for (shift in 0 until cols.size + width) {
            val l = JSONArray()
            for (i in 0 until count) l.put(eyes?.optString(i, "0") ?: "0")
            strip.forEachIndexed { p, panel ->
                val c8 = IntArray(8) { k -> val t = p * 8 + k + shift - width; if (t >= 0 && t < cols.size) cols[t] else 0 }
                l.put(panel, panelHex(c8))
            }
            frames.put(JSONObject().put("timespan", speed).put("leds", l))
        }
        val visor = JSONObject().put("type", "custom")
            .put("isMouth", JSONArray().apply { repeat(count) { put(false) } })
            .put("frames", frames)
        if (then != null) visor.put("then", then)
        return JSONObject().put("ears", JSONObject().put("type", "none")).put("visor", visor)
    }

    /** How long one pass of a text face takes, to follow the mask back to the face after Play once. */
    fun textMs(text: String, speed: Int): Long {
        val strip = layout.map { row -> row.count { isPanel(it) } }.maxOrNull() ?: 8
        return ((textColumns(text).size + strip * 8) * speed).toLong()
    }

    // ---- managing faces ------------------------------------------------------------------------

    /** The settings that name this face, so deleting or renaming it can say what it leaves behind. */
    fun usedAs(name: String): List<String> {
        val out = mutableListOf<String>()
        fun chk(k: String, what: String) { if (Cfg.str(k) == name) out += what }
        chk("aBoop", "the boop face"); chk("aSleep", "the sleeping face"); chk("aTilt", "the left tilt face")
        chk("aTilt2", "the right tilt face"); chk("aMilestone", "the milestone face"); chk("aSass", "the sass face")
        for (s in listOf("btnShort" to "button press", "btnDouble" to "button double press", "btnHold" to "button hold"))
            if (Cfg.str(s.first) == "face") chk(s.first + "Face", "the " + s.second + " face")
        val names = listOf("top", "middle", "bottom")
        for (b in 0..2) for ((suf, g) in listOf("" to "press", "H" to "hold", "D" to "double tap"))
            if (Cfg.str("rmt$b$suf") == "face") chk("rmt$b${suf}Face", "the remote's ${names[b]} $g face")
        return out
    }

    /** Save a face file on the mask under [label] (no .json). */
    suspend fun upload(label: String, content: String): Boolean {
        val r = MaskLink.post("/savefile", listOf(
            Triple("file", null, label.toByteArray()), Triple("content", null, content.toByteArray()))) ?: return false
        val ok = r.first in 200..399 && !r.second.contains("Error opening file") && !r.second.contains("No valid parameters")
        if (ok) { forget("$label.json"); }
        return ok
    }

    suspend fun delete(name: String): Pair<Boolean, String> {
        val r = MaskLink.okRedirect("/deletefile?file=" + MaskLink.enc(name))
        if (r.first) forget(name)
        return r
    }

    /** There is no rename on the mask: save under the new name, then delete the old one. */
    suspend fun rename(name: String, label: String, wasOn: Boolean): String? {
        val content = MaskLink.fetch("/anims/" + MaskLink.enc(name).replace("+", "%20"), 5000) ?: return "The mask is out of reach"
        if (!upload(label, content)) return "Rename failed · the old face is untouched"
        delete(name)
        if (wasOn) MaskLink.okRedirect("/change?anim=" + MaskLink.enc("$label.json"))
        return null
    }

    /** Show a face built on the phone (scrolling text, a face maker try-out). */
    suspend fun showBuilt(anim: JSONObject): String? {
        val r = MaskLink.post("/change", listOf(Triple("anim", null, anim.toString().toByteArray()))) ?: return "The mask is out of reach"
        return if (r.first in 300..399 || (r.first in 200..299 && r.second.isBlank())) null else r.second.ifBlank { "The mask said no" }
    }
}

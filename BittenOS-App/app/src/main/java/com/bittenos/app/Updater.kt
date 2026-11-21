package com.bittenos.app

import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * Updates this app from the GitHub repo. The repo holds the built APK and a version.json next to
 * it; the app reads the newest commit that touched the app, then that commit's version.json, and
 * offers the APK from that same commit if its number is higher than the one running.
 *
 * The mask's WiFi has no internet and the app binds itself to it, so every request here goes out
 * explicitly over a network that does (mobile data, or a house WiFi).
 */
object Updater {
    const val REPO = "picovrfbt/BittenOS"
    const val BRANCH = "furbitten-14panel"
    private const val DIR = "BittenOS-App"

    data class Change(val sha: String, val title: String, val date: String)
    data class Release(val code: Int, val name: String, val notes: String, val sha: String, val changes: List<Change>)

    sealed interface State {
        data object Idle : State
        data object Checking : State
        data class Latest(val r: Release) : State
        data class Available(val r: Release) : State
        data class Downloading(val r: Release, val progress: Float) : State
        data class Ready(val r: Release, val file: File) : State
        data class Failed(val why: String) : State
    }

    val state = MutableStateFlow<State>(State.Idle)
    /** When the last check finished, so the home screen doesn't ask GitHub on every launch. */
    private var checkedAt = 0L

    fun current(ctx: Context): Pair<Int, String> {
        val p = ctx.packageManager.getPackageInfo(ctx.packageName, 0)
        @Suppress("DEPRECATION")
        val code = if (Build.VERSION.SDK_INT >= 28) p.longVersionCode.toInt() else p.versionCode
        return code to (p.versionName ?: "?")
    }

    /** A network that reaches the internet: never the mask's own WiFi. */
    private fun internet(ctx: Context): Network? {
        val cm = ctx.getSystemService(ConnectivityManager::class.java)
        @Suppress("DEPRECATION")
        val all = cm.allNetworks.filter { n ->
            val c = cm.getNetworkCapabilities(n) ?: return@filter false
            c.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) && c.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
        }
        // A house WiFi first (no data used), then mobile data
        return all.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
            ?: all.firstOrNull()
    }

    private fun open(net: Network, url: String, timeout: Int): HttpURLConnection =
        (net.openConnection(URL(url)) as HttpURLConnection).apply {
            connectTimeout = timeout; readTimeout = timeout
            setRequestProperty("User-Agent", "BittenOS-app")
            setRequestProperty("Cache-Control", "no-cache")
        }

    private fun text(net: Network, url: String): String {
        val c = open(net, url, 10_000)
        try {
            if (c.responseCode !in 200..299) throw Exception("GitHub said ${c.responseCode}")
            return c.inputStream.use { it.readBytes().decodeToString() }
        } finally { c.disconnect() }
    }

    /** Look at the repo. [quiet] skips it if a check finished in the last hour. */
    suspend fun check(ctx: Context, quiet: Boolean = false) {
        if (quiet && System.currentTimeMillis() - checkedAt < 3_600_000) return
        val s = state.value
        if (s is State.Checking || s is State.Downloading || s is State.Ready) return
        state.value = State.Checking
        state.value = withContext(Dispatchers.IO) {
            try {
                val net = internet(ctx) ?: return@withContext State.Failed(
                    "No internet. Turn on mobile data, or join a WiFi that has internet.")
                val log = JSONArray(text(net, "https://api.github.com/repos/$REPO/commits?sha=$BRANCH&path=$DIR&per_page=12"))
                if (log.length() == 0) return@withContext State.Failed("Nothing found in the repo")
                val changes = (0 until log.length()).map { i ->
                    val o = log.getJSONObject(i)
                    val cm = o.getJSONObject("commit")
                    Change(o.getString("sha"), cm.getString("message").lineSequence().first(),
                        cm.getJSONObject("committer").optString("date").take(10))
                }
                val sha = changes.first().sha
                val v = JSONObject(text(net, "https://raw.githubusercontent.com/$REPO/$sha/$DIR/version.json"))
                val r = Release(v.getInt("code"), v.optString("name"), v.optString("notes"), sha, changes)
                checkedAt = System.currentTimeMillis()
                if (r.code > current(ctx).first) State.Available(r) else State.Latest(r)
            } catch (e: Exception) {
                State.Failed(e.message?.takeIf { it.length < 120 } ?: "Couldn't reach GitHub")
            }
        }
    }

    /** Fetch the APK from the same commit the version came from. */
    suspend fun download(ctx: Context, r: Release) {
        state.value = State.Downloading(r, 0f)
        state.value = withContext(Dispatchers.IO) {
            try {
                val net = internet(ctx) ?: return@withContext State.Failed("No internet any more")
                val dir = File(ctx.cacheDir, "update").apply { mkdirs(); listFiles()?.forEach { it.delete() } }
                val f = File(dir, "BittenOS-${r.code}.apk")
                val c = open(net, "https://raw.githubusercontent.com/$REPO/${r.sha}/$DIR/BittenOS.apk", 30_000)
                try {
                    if (c.responseCode !in 200..299) throw Exception("GitHub said ${c.responseCode}")
                    val total = c.contentLengthLong.takeIf { it > 0 } ?: 16_000_000L
                    var got = 0L
                    var shown = 0f
                    c.inputStream.use { input ->
                        f.outputStream().use { out ->
                            val buf = ByteArray(64 * 1024)
                            while (true) {
                                val n = input.read(buf)
                                if (n < 0) break
                                out.write(buf, 0, n)
                                got += n
                                val p = (got.toFloat() / total).coerceAtMost(1f)
                                if (p - shown > 0.01f) { shown = p; state.value = State.Downloading(r, p) }
                            }
                        }
                    }
                } finally { c.disconnect() }
                // Make sure it really is this app, and really newer, before handing it to Android
                @Suppress("DEPRECATION")
                val info = ctx.packageManager.getPackageArchiveInfo(f.path, 0)
                    ?: return@withContext State.Failed("The download isn't a working APK")
                @Suppress("DEPRECATION")
                val code = if (Build.VERSION.SDK_INT >= 28) info.longVersionCode.toInt() else info.versionCode
                when {
                    info.packageName != ctx.packageName -> State.Failed("The download is a different app")
                    code <= current(ctx).first -> State.Failed("The download isn't newer than this one")
                    else -> State.Ready(r, f)
                }
            } catch (e: Exception) {
                State.Failed(e.message?.takeIf { it.length < 120 } ?: "Download failed")
            }
        }
    }

    /** False until "Install unknown apps" is allowed for BittenOS. */
    fun canInstall(ctx: Context) = ctx.packageManager.canRequestPackageInstalls()

    fun allowInstalls(ctx: Context) {
        ctx.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + ctx.packageName))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    /** Hand the APK to Android's installer, which asks before it updates. */
    fun install(ctx: Context, f: File) {
        val uri = FileProvider.getUriForFile(ctx, ctx.packageName + ".files", f)
        ctx.startActivity(Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK))
    }
}

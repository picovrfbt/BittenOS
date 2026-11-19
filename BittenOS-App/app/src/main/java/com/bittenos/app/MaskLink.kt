package com.bittenos.app

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.URL
import java.net.URLEncoder
import java.util.concurrent.ConcurrentHashMap

/**
 * Knows whether the mask is reachable, and where.
 *
 * Two ways to be connected, and the app tells them apart:
 *  - on the mask's own WiFi: the mask is always 192.168.4.1 there.
 *  - on the same house network or hotspot as the mask: found by mDNS, or the last address that
 *    worked.
 *
 * Detection is by asking, not by network name: whatever answers /stats looking like BittenOS is
 * the mask. That needs no location permission (Android hides WiFi names without it) and keeps
 * working when the mask's network is renamed.
 */
object MaskLink {
    const val AP_HOST = "192.168.4.1"

    sealed interface State {
        data object Searching : State
        /** The phone is not on any WiFi network. */
        data object NoWifi : State
        /** On WiFi, but nothing there answers like a mask. */
        data object NotFound : State
        data class Connected(
            val host: String,
            /** true on the mask's own access point; false when both share some other network */
            val ownWifi: Boolean,
            val stats: JSONObject,
            val at: Long,
        ) : State
    }

    private val _state = MutableStateFlow<State>(State.Searching)
    val state: StateFlow<State> = _state

    val host: String? get() = (_state.value as? State.Connected)?.host

    /** The WiFi network the mask was found on, for sockets that must not wander off to mobile data. */
    @Volatile var network: Network? = null
        private set

    private lateinit var app: Context
    private lateinit var cm: ConnectivityManager
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val kicks = Channel<Unit>(Channel.CONFLATED)
    private val candidates = ConcurrentHashMap.newKeySet<String>()
    @Volatile private var wifi: Network? = null
    private var started = false

    private val prefs by lazy { app.getSharedPreferences("link", Context.MODE_PRIVATE) }

    var manualHost: String?
        get() = prefs.getString("manual", null)?.takeIf { it.isNotBlank() }
        set(v) {
            prefs.edit().putString("manual", v?.trim()).apply()
            v?.trim()?.takeIf { it.isNotEmpty() }?.let { candidates += it }
            kick()
        }

    fun start(ctx: Context) {
        if (started) return
        started = true
        app = ctx.applicationContext
        cm = app.getSystemService(ConnectivityManager::class.java)
        candidates += AP_HOST
        prefs.getString("last", null)?.let { candidates += it }
        manualHost?.let { candidates += it }

        // requestNetwork rather than only listening: it also asks Android to keep this WiFi up
        // when it has no internet, which is exactly what the mask's own network looks like.
        // Without it, Android's switch-to-mobile-data habit can drop the mask from under the app.
        val req = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()
        cm.requestNetwork(req, object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(n: Network) { wifi = n; kick() }
            override fun onLost(n: Network) {
                if (wifi == n) wifi = null
                if (network == n) { network = null; cm.bindProcessToNetwork(null) }
                kick()
            }
        })
        startNsd()
        scope.launch { run() }
    }

    /** Look again now rather than at the next tick: the app came forward, an address was typed. */
    fun kick() { kicks.trySend(Unit) }

    private suspend fun run() {
        var fails = 0
        while (true) {
            val cur = _state.value
            if (cur is State.Connected) {
                val s = fetchStats(cur.host)
                if (s != null) {
                    fails = 0
                    _state.value = cur.copy(stats = s, at = System.currentTimeMillis())
                } else if (++fails >= 3) {          // three missed seconds: it has gone
                    fails = 0
                    network?.let { network = null; cm.bindProcessToNetwork(null) }
                    _state.value = if (wifi == null) State.NoWifi else State.NotFound
                }
                withTimeoutOrNull(1000) { kicks.receive() }
            } else {
                val found = probeAll()
                if (found != null) {
                    val (h, s) = found
                    // Every socket the app opens from here on uses this WiFi - the voice stream
                    // included - even though it has no internet and Android would rather not.
                    wifi?.let { network = it; cm.bindProcessToNetwork(it) }
                    prefs.edit().putString("last", h).apply()
                    _state.value = State.Connected(h, h == AP_HOST, s, System.currentTimeMillis())
                } else {
                    _state.value = if (wifi == null) State.NoWifi else State.NotFound
                }
                withTimeoutOrNull(2000) { kicks.receive() }
            }
        }
    }

    private suspend fun probeAll(): Pair<String, JSONObject>? = coroutineScope {
        // Probed even with no WiFi: the mask can join the phone's own hotspot, and then the phone
        // is on no WiFi network at all.
        val results = candidates.toList()
            .map { h -> async { fetchStats(h, 1200)?.let { h to it } } }
            .awaitAll()
            .filterNotNull()
        // The mask's own network wins a tie: it is the one that cannot be taken away.
        results.firstOrNull { it.first == AP_HOST } ?: results.firstOrNull()
    }

    private fun isMask(j: JSONObject) =
        j.optString("fw") == "BittenOS" || (j.has("loopHz") && j.has("anim") && j.has("boops"))

    private suspend fun fetchStats(h: String, timeout: Int = 1500): JSONObject? =
        httpGet(h, "/stats", timeout)?.let {
            try { JSONObject(it).takeIf(::isMask) } catch (e: Exception) { null }
        }

    private suspend fun httpGet(h: String, path: String, timeout: Int): String? = withContext(Dispatchers.IO) {
        try {
            val url = URL("http://$h$path")
            val n = network ?: wifi
            val c = (n?.openConnection(url) ?: url.openConnection()) as HttpURLConnection
            c.connectTimeout = timeout
            c.readTimeout = timeout
            c.useCaches = false
            try {
                if (c.responseCode !in 200..399) null
                else c.inputStream.bufferedReader().use { it.readText() }
            } finally { c.disconnect() }
        } catch (e: Exception) { null }
    }

    // ---- talking to it ------------------------------------------------------------------

    /** GET a path on the mask. null when not connected, or when it did not answer. */
    suspend fun get(path: String, timeout: Int = 2500): String? {
        val h = host ?: return null
        val r = httpGet(h, path, timeout)
        kick()          // an action usually changes something /stats shows: refresh it straight away
        return r
    }

    private fun enc(s: String) = URLEncoder.encode(s, "UTF-8")

    /** The faces on the mask, as the firmware names the files ("happy.json"). */
    suspend fun faces(): List<String>? =
        get("/getfiles")?.split(';')?.map { it.trim() }?.drop(1)?.filter { it.isNotEmpty() }?.sorted()

    suspend fun showFace(file: String) = get("/change?anim=" + enc(file))
    suspend fun brightness(v: Int, save: Boolean) =
        get("/brightness?value=$v" + if (save) "&save=1" else "")
    suspend fun sleep(on: Boolean) = get("/sleep?state=" + if (on) "on" else "off")
    suspend fun panels(on: Boolean) = get("/display?state=" + if (on) "on" else "off")
    suspend fun rave(on: Boolean) = get(if (on) "/rave?go=1" else "/rave?stop=1")
    suspend fun micStream(gain: Int? = null): JSONObject? =
        get("/micstream" + (gain?.let { "?gain=$it" } ?: ""))?.let {
            try { JSONObject(it) } catch (e: Exception) { null }
        }

    // ---- mDNS: finding it on a house network ---------------------------------------------

    private val nsdQueue = ArrayDeque<NsdServiceInfo>()
    private var nsdResolving = false
    private var nsd: NsdManager? = null

    private fun startNsd() {
        val m = app.getSystemService(NsdManager::class.java) ?: return
        nsd = m
        try {
            m.discoverServices("_http._tcp", NsdManager.PROTOCOL_DNS_SD, object : NsdManager.DiscoveryListener {
                override fun onServiceFound(si: NsdServiceInfo) {
                    synchronized(nsdQueue) { nsdQueue.addLast(si) }
                    nsdNext()
                }
                override fun onServiceLost(si: NsdServiceInfo) {}
                override fun onDiscoveryStarted(t: String) {}
                override fun onDiscoveryStopped(t: String) {}
                override fun onStartDiscoveryFailed(t: String, code: Int) {}
                override fun onStopDiscoveryFailed(t: String, code: Int) {}
            })
        } catch (e: Exception) { /* no mDNS here: the other ways of finding it still work */ }
    }

    /** The old resolve API takes one request at a time, so found services wait their turn. */
    private fun nsdNext() {
        val m = nsd ?: return
        val info = synchronized(nsdQueue) {
            if (nsdResolving || nsdQueue.isEmpty()) return
            nsdResolving = true
            nsdQueue.removeFirst()
        }
        val done = {
            synchronized(nsdQueue) { nsdResolving = false }
            nsdNext()
        }
        try {
            @Suppress("DEPRECATION")
            m.resolveService(info, object : NsdManager.ResolveListener {
                override fun onResolveFailed(si: NsdServiceInfo, code: Int) = done()
                override fun onServiceResolved(si: NsdServiceInfo) {
                    @Suppress("DEPRECATION")
                    val a = si.host
                    if (a is Inet4Address) { a.hostAddress?.let { candidates += it }; kick() }
                    done()
                }
            })
        } catch (e: Exception) { done() }
    }
}

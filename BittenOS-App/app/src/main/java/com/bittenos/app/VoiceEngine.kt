package com.bittenos.app

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTimestamp
import android.media.AudioTrack
import android.net.Network
import android.os.Build
import android.os.Process
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import kotlin.math.abs
import kotlin.math.floor

/**
 * The mask's mic, played out of this phone as fast as Android allows.
 *
 * Three threads:
 *  - hello: knocks on the mask's UDP port every 300 ms. The mask sends to whoever knocked last and
 *    stops 1.5 s after the knocking stops, so nothing has to be switched off on the mask.
 *  - receive: puts each 10 ms packet into a ring at the place its sequence number says, filling
 *    a lost one with silence so the timing never slips.
 *  - audio: pulls from the ring at the phone's own output rate through AudioTrack's low-latency
 *    mode - the fast mixer path the browser does not get.
 *
 * The ring is held at a small target by playing a shade fast or slow (at most 1%, inaudible on
 * speech), because the mask's 16 kHz and the phone's 48 kHz are two different clocks.
 */
class VoiceEngine(
    private val ctx: Context,
    private val host: String,
    private val network: Network?,
    targetMs: Int,
    private val output: Output = Output.Aux,
) {
    /** Where the voice comes out. Aux is the quick one; Bluetooth adds the speaker's own radio delay. */
    enum class Output {
        Aux, Bluetooth;

        fun matches(d: AudioDeviceInfo): Boolean = d.type in types()
        private fun types(): Set<Int> = when (this) {
            Aux -> setOf(AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
                AudioDeviceInfo.TYPE_USB_HEADSET, AudioDeviceInfo.TYPE_USB_DEVICE,
                AudioDeviceInfo.TYPE_LINE_ANALOG, AudioDeviceInfo.TYPE_AUX_LINE)
            Bluetooth -> buildSet {
                add(AudioDeviceInfo.TYPE_BLUETOOTH_A2DP)
                if (Build.VERSION.SDK_INT >= 31) { add(AudioDeviceInfo.TYPE_BLE_HEADSET); add(AudioDeviceInfo.TYPE_BLE_SPEAKER) }
            }
        }
    }

    data class Stats(
        val running: Boolean = false,
        /** audio received from the mask in the last half second */
        val receiving: Boolean = false,
        /** the jitter buffer, ms: the part Smoothing sets */
        val bufferMs: Int = 0,
        /** the phone's own output stage, ms, from AudioTrack's timestamps */
        val outputMs: Int = -1,
        /** peak level 0..1 over the last burst */
        val level: Float = 0f,
        val lostPackets: Long = 0,
        val latePackets: Long = 0,
        /** times the buffer ran dry: each one is a crackle */
        val dryOuts: Long = 0,
        val lowLatencyPath: Boolean = false,
        val outputRate: Int = 0,
        /** what Android is actually playing through, e.g. "Bluetooth: JBL Clip 3" */
        val routedTo: String = "",
        /** false while the chosen output is not connected and something else is playing instead */
        val onChosen: Boolean = true,
        val output: Output = Output.Aux,
    ) {
        /** Everything this phone can see. WiFi itself (~2-5 ms) and the mask's 10 ms packet are extra. */
        val delayMs: Int get() = if (outputMs < 0) -1 else bufferMs + outputMs + 10
    }

    companion object {
        const val PORT = 7373
        const val IN_RATE = 16000
        const val CHUNK = 160
        private val _stats = MutableStateFlow(Stats())
        val stats: StateFlow<Stats> = _stats
    }

    private val target = targetMs * IN_RATE / 1000                      // in mask samples
    private val ringSize = 1 shl 14                                      // ~1 s of mask audio
    private val ring = FloatArray(ringSize)
    @Volatile private var writePos = -1L      // monotonic sample count; -1 until the first packet
    @Volatile private var running = false
    @Volatile private var lastRx = 0L
    @Volatile private var lost = 0L
    @Volatile private var late = 0L
    private var socket: DatagramSocket? = null
    private val threads = mutableListOf<Thread>()

    fun start() {
        if (running) return
        running = true
        val s = DatagramSocket()
        network?.bindSocket(s)                // the mask's WiFi, even with mobile data on
        s.soTimeout = 300
        s.receiveBufferSize = 64 * 1024
        socket = s
        threads += Thread(::helloLoop, "voice-hello")
        threads += Thread(::receiveLoop, "voice-rx")
        threads += Thread(::audioLoop, "voice-audio")
        threads.forEach { it.start() }
    }

    fun stop() {
        running = false
        socket?.close()
        threads.forEach { it.join(1000) }
        threads.clear()
        _stats.value = Stats()
    }

    private fun helloLoop() {
        val hello = "BVOXH".toByteArray()
        // host may carry a port for the web page ("10.0.2.2:8090" in the emulator); the voice port is fixed
        val addr = try { InetAddress.getByName(host.substringBefore(':')) } catch (e: Exception) { return }
        while (running) {
            try { socket?.send(DatagramPacket(hello, hello.size, addr, PORT)) } catch (e: Exception) {}
            try { Thread.sleep(300) } catch (e: InterruptedException) { return }
        }
    }

    private fun receiveLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        val buf = ByteArray(2048)
        val p = DatagramPacket(buf, buf.size)
        var expected = -1
        while (running) {
            try { socket?.receive(p) ?: return } catch (e: SocketTimeoutException) { continue } catch (e: Exception) { return }
            val n = p.length
            if (n < 6 || buf[0] != 'B'.code.toByte() || buf[1] != 'V'.code.toByte()) continue
            val seq = (buf[2].toInt() and 0xFF) or ((buf[3].toInt() and 0xFF) shl 8)
            val samples = (n - 4) / 2
            lastRx = System.nanoTime()
            var w = writePos
            if (w < 0 || expected < 0) { w = 0; expected = seq }
            val ahead = (seq - expected) and 0xFFFF
            when {
                ahead == 0 -> {}
                ahead < 50 -> {           // packets went missing: keep their place, in silence
                    for (k in 0 until ahead * CHUNK) ring[((w + k) and (ringSize - 1).toLong()).toInt()] = 0f
                    w += ahead * CHUNK
                    lost += ahead
                }
                ahead > 0xFFFF - 50 -> { late++; continue }   // arrived after its turn: too late to use
                else -> {}                // the mask restarted: just carry on from here
            }
            for (k in 0 until samples) {
                val lo = buf[4 + 2 * k].toInt() and 0xFF
                val hi = buf[5 + 2 * k].toInt()
                ring[((w + k) and (ringSize - 1).toLong()).toInt()] = ((hi shl 8) or lo).toShort() / 32768f
            }
            writePos = w + samples      // published last, so the audio thread never reads ahead of it
            expected = (seq + 1) and 0xFFFF
        }
    }

    private fun audioLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
        val am = ctx.getSystemService(AudioManager::class.java)
        // The fast path only takes the hardware's own rate and burst size, so ask for exactly those
        // and do the 16k -> 48k conversion here rather than let Android's resampler disqualify us.
        val rate = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
        val burst = am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)?.toIntOrNull()?.takeIf { it > 0 } ?: 192
        val minBytes = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_FLOAT)
        val track = try {
            AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)       // the volume buttons move this one
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                        .build())
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setSampleRate(rate)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build())
                .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setBufferSizeInBytes(maxOf(minBytes, burst * 4 * 2))
                .build()
        } catch (e: Exception) { running = false; return }
        // Shrink what Android holds to two bursts; grown again one burst at a time if it underruns.
        var held = burst * 2
        track.bufferSizeInFrames = held
        val fast = track.performanceMode == AudioTrack.PERFORMANCE_MODE_LOW_LATENCY
        // Ask for the chosen output. If it is not there yet (cable out, speaker off), Android plays
        // wherever it would anyway, and the loop below moves over as soon as it turns up.
        fun choose() {
            val dev = am.getDevices(AudioManager.GET_DEVICES_OUTPUTS).firstOrNull { output.matches(it) }
            if (dev != null && track.preferredDevice?.id != dev.id) track.setPreferredDevice(dev)
        }
        choose()
        track.play()
        var lastChoose = System.nanoTime()

        val out = FloatArray(burst)
        val step0 = IN_RATE.toDouble() / rate
        val mask = (ringSize - 1).toLong()
        var pos = -1.0            // where we are reading, in mask samples
        var primed = false
        var availAvg = target.toDouble()
        var dry = 0L
        var written = 0L
        var lastUnderruns = 0
        var lastPublish = 0L
        var outMs = -1
        var peak = 0f
        val ts = AudioTimestamp()

        try {
            while (running) {
                val w = writePos
                if (w >= 0 && pos < 0) pos = maxOf(0.0, (w - target).toDouble())
                val avail = if (pos < 0) 0.0 else w - pos
                if (!primed && avail >= target) primed = true
                if (primed && avail > target + IN_RATE / 10) {       // 100 ms over: jump, do not drift
                    pos = (w - target).toDouble()
                    availAvg = target.toDouble()
                }
                var i = 0
                if (primed) {
                    availAvg += ((w - pos) - availAvg) * 0.02
                    val err = (availAvg - target) / target
                    val trim = (err * 0.05).coerceIn(-0.01, 0.01)
                    val step = step0 * (1 + trim)
                    while (i < burst) {
                        val ip = floor(pos).toLong()
                        if (ip + 1 >= w) { primed = false; dry++; break }    // ran dry: refill to target
                        val f = (pos - ip).toFloat()
                        val a = ring[(ip and mask).toInt()]
                        val b = ring[((ip + 1) and mask).toInt()]
                        val v = a + (b - a) * f
                        out[i++] = v
                        if (abs(v) > peak) peak = abs(v)
                        pos += step
                    }
                }
                while (i < burst) out[i++] = 0f
                track.write(out, 0, burst, AudioTrack.WRITE_BLOCKING)
                written += burst

                val u = track.underrunCount
                if (u > lastUnderruns) {          // Android itself ran dry: give it one more burst
                    lastUnderruns = u
                    if (held < burst * 8) { held += burst; track.bufferSizeInFrames = held }
                }

                val now = System.nanoTime()
                if (now - lastPublish > 100_000_000L) {
                    if (track.getTimestamp(ts)) {
                        // Frames written but not yet out of the speaker, corrected for how long ago
                        // the timestamp was taken.
                        val played = ts.framePosition + (now - ts.nanoTime) * rate / 1_000_000_000L
                        outMs = (((written - played) * 1000L) / rate).toInt().coerceAtLeast(0)
                    }
                    if (now - lastChoose > 1_000_000_000L) { choose(); lastChoose = now }
                    val routed = track.routedDevice
                    val bufMs = if (pos < 0) 0 else ((writePos - pos) * 1000 / IN_RATE).toInt().coerceAtLeast(0)
                    _stats.value = Stats(
                        running = true,
                        receiving = now - lastRx < 500_000_000L,
                        bufferMs = bufMs,
                        outputMs = outMs,
                        level = peak,
                        lostPackets = lost,
                        latePackets = late,
                        dryOuts = dry,
                        lowLatencyPath = fast,
                        outputRate = rate,
                        routedTo = routed?.let { describe(it) } ?: "",
                        onChosen = routed == null || output.matches(routed),
                        output = output,
                    )
                    peak = 0f
                    lastPublish = now
                }
            }
        } finally {
            try { track.stop() } catch (e: Exception) {}
            track.release()
        }
    }

    private fun describe(d: AudioDeviceInfo): String {
        val kind = when {
            Output.Bluetooth.matches(d) -> "Bluetooth"
            Output.Aux.matches(d) -> "Aux"
            d.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "The phone's speaker"
            d.type == AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> "The phone's earpiece"
            else -> "Output"
        }
        val name = d.productName?.toString()?.trim().orEmpty()
        return if (kind.startsWith("The phone") || name.isEmpty() || name == Build.MODEL) kind else "$kind: $name"
    }
}

package com.bittenos.app.ui

import android.view.HapticFeedbackConstants
import android.view.View
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.PressInteraction
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalView
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * How the app feels under the finger: a small buzz on every tap, a slight press-in while held,
 * and the ripple. Everything tappable goes through here so it all answers the same way.
 */
object Buzz {
    const val Tap = HapticFeedbackConstants.VIRTUAL_KEY
    const val Long = HapticFeedbackConstants.LONG_PRESS
    const val Tick = HapticFeedbackConstants.CLOCK_TICK
    fun on(v: View, kind: Int = Tap) { v.performHapticFeedback(kind) }
}

/** A tap anywhere: ripple, a press-in to [pressed] scale, and a buzz. */
@OptIn(ExperimentalFoundationApi::class)
fun Modifier.hudClick(
    enabled: Boolean = true, pressed: Float = 0.96f, buzz: Boolean = true,
    onLongClick: (() -> Unit)? = null, onClick: () -> Unit,
): Modifier = composed {
    val view = LocalView.current
    val src = remember { MutableInteractionSource() }
    val down by src.collectIsPressedAsState()
    val scale by animateFloatAsState(if (down && enabled) pressed else 1f,
        if (down) tween(70) else spring(dampingRatio = 0.55f, stiffness = 900f), label = "press")
    this.graphicsLayer { scaleX = scale; scaleY = scale }
        .combinedClickable(src, ripple(), enabled = enabled,
            onLongClick = onLongClick?.let { l -> { Buzz.on(view, Buzz.Long); l() } },
            onClick = { if (buzz) Buzz.on(view); onClick() })
}

/**
 * Press and hold to keep stepping: one step at once, then repeats that speed up the longer it is
 * held. For the + and − of steppers.
 */
fun Modifier.repeatStep(enabled: Boolean, onStep: () -> Unit): Modifier = composed {
    val view = LocalView.current
    val step by rememberUpdatedState(onStep)
    val scope = rememberCoroutineScope()
    val src = remember { MutableInteractionSource() }
    val down by src.collectIsPressedAsState()
    val scale by animateFloatAsState(if (down && enabled) 0.88f else 1f, tween(80), label = "step")
    this.graphicsLayer { scaleX = scale; scaleY = scale }
        .pointerInput(enabled) {
            if (!enabled) return@pointerInput
            awaitEachGesture {
                val d = awaitFirstDown()
                val press = PressInteraction.Press(d.position)
                src.tryEmit(press)
                Buzz.on(view); step()
                var job: Job? = scope.launch {
                    delay(420)
                    var gap = 110L
                    while (true) { Buzz.on(view, Buzz.Tick); step(); delay(gap); gap = (gap * 0.88).toLong().coerceAtLeast(40) }
                }
                // wait for every finger to lift
                while (true) {
                    val ev = awaitPointerEvent()
                    if (ev.changes.none { it.pressed }) break
                }
                job?.cancel(); job = null
                src.tryEmit(PressInteraction.Release(press))
            }
        }
}

/** Buzz once when an integer value changes, for sliders with steps. */
@Composable
fun rememberTicker(): (Int) -> Unit {
    val view = LocalView.current
    val last = remember { intArrayOf(Int.MIN_VALUE) }
    return { v -> if (last[0] != Int.MIN_VALUE && v != last[0]) Buzz.on(view, Buzz.Tick); last[0] = v }
}

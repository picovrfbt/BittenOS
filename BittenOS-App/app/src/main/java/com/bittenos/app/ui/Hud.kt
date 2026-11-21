package com.bittenos.app.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow

// ---- the HUD's tokens, as in the design canvas ------------------------------------------------

object Hud {
    val Bg = Color(0xFF0B0F13)
    val PanelBg = Color(0xFF11171C)
    val FieldBg = Color(0xFF0E1418)
    val Line = Color(0xFF222B33)
    val Line2 = Color(0xFF2A343D)
    val Text = Color(0xFFE6EDF2)
    val Text2 = Color(0xFFB7C2CB)
    val Dim = Color(0xFF8B98A3)
    val Faint = Color(0xFF6B7781)
    val Ghost = Color(0xFF4A555E)
    val Track = Color(0xFF1E262D)
    val Cyan = Color(0xFF3DD9F5)       // voice
    val Pink = Color(0xFFFF3D8B)       // rave
    val Amber = Color(0xFFF5B53D)      // care
    val Red = Color(0xFFFF5A5A)

    // spacing 1: the room the user picked on 2026-11-20
    val Edge = 14.dp
    val PanelPad = 15.dp
    val PanelGap = 16.dp
    val HeadGap = 13.dp
    val TileGap = 6.dp
    val SegGap = 5.dp

    val Sans: FontFamily = FontFamily.SansSerif
    val Mono: FontFamily = FontFamily.Monospace
}

/** The accent the user picks in Network & app. Voice cyan, rave pink and care amber never change. */
enum class AccentChoice(val label: String, val color: Color) {
    Mint("MINT", Color(0xFF4EF5B8)), Cyan("CYAN", Color(0xFF3DD9F5)), Amber("AMBER", Color(0xFFF5B53D)),
    White("WHITE", Color(0xFFE6EDF2)), Red("RED", Color(0xFFFF5A5A));
}

val LocalAccent = staticCompositionLocalOf { AccentChoice.Mint.color }

/** The accent mixed into the background, for the lit state of buttons and tiles. */
@Composable fun accentDeep(a: Color = LocalAccent.current): Color = lerpColor(Hud.Bg, a, 0.13f)
@Composable fun accentSoft(a: Color = LocalAccent.current): Color = lerpColor(Hud.Bg, a, 0.2f)
@Composable fun onAccent(a: Color = LocalAccent.current): Color = lerpColor(Color.Black, a, 0.14f)

fun lerpColor(a: Color, b: Color, t: Float) = Color(
    a.red + (b.red - a.red) * t, a.green + (b.green - a.green) * t, a.blue + (b.blue - a.blue) * t, 1f)

// ---- text ------------------------------------------------------------------------------------

/** Spaced capitals, the HUD's label voice. */
@Composable
fun Caps(
    text: String, modifier: Modifier = Modifier, color: Color = Hud.Dim, size: TextUnit = 10.sp,
    weight: FontWeight = FontWeight.SemiBold, spacing: TextUnit = 0.16.em, maxLines: Int = 1,
    align: TextAlign? = null,
) = Text(text.uppercase(), modifier, color = color, fontSize = size, fontWeight = weight,
    letterSpacing = spacing, fontFamily = Hud.Sans, maxLines = maxLines, overflow = TextOverflow.Ellipsis,
    textAlign = align)

@Composable
fun Mono(text: String, modifier: Modifier = Modifier, color: Color = LocalAccent.current, size: TextUnit = 11.sp,
         weight: FontWeight = FontWeight.Medium, maxLines: Int = 1) =
    Text(text, modifier, color = color, fontSize = size, fontFamily = Hud.Mono, fontWeight = weight,
        maxLines = maxLines, overflow = TextOverflow.Ellipsis)

@Composable
fun Note(text: String, modifier: Modifier = Modifier, color: Color = Hud.Dim) =
    Text(text, modifier, color = color, fontSize = 12.sp, lineHeight = 17.sp, fontFamily = Hud.Sans)

@Composable
fun Hint(text: String, modifier: Modifier = Modifier, color: Color = Hud.Faint) =
    Text(text, modifier, color = color, fontSize = 11.sp, lineHeight = 15.sp, fontFamily = Hud.Sans)

// ---- the striped page --------------------------------------------------------------------------

fun Modifier.stripes(): Modifier = this.background(Hud.Bg).drawBehind {
    val step = 16.dp.toPx()
    val c = Color.White.copy(alpha = 0.022f)
    var x = -size.height
    while (x < size.width) {
        drawLine(c, Offset(x, size.height), Offset(x + size.height, 0f), strokeWidth = 2.dp.toPx())
        x += step
    }
}

/** A scrolling screen body with the HUD's edge spacing. */
@Composable
fun HudColumn(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState()).navigationBarsPadding()
        .padding(top = 2.dp, bottom = 24.dp)) { content() }
}

// ---- panels -----------------------------------------------------------------------------------

/** "01 // FACES" panel. code and title are spaced caps in the accent; trailing sits at the right. */
@Composable
fun Panel(
    code: String, title: String, modifier: Modifier = Modifier, titleColor: Color = LocalAccent.current,
    trailing: (@Composable RowScope.() -> Unit)? = null, content: @Composable ColumnScope.() -> Unit,
) {
    Column(modifier.padding(horizontal = Hud.Edge).padding(bottom = Hud.PanelGap).fillMaxWidth()
        .background(Hud.PanelBg, RoundedCornerShape(3.dp)).border(1.dp, Hud.Line, RoundedCornerShape(3.dp))
        .padding(Hud.PanelPad)) {
        Row(Modifier.fillMaxWidth().padding(bottom = Hud.HeadGap).heightIn(min = 18.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Caps(if (code.isEmpty()) title else "$code // $title", Modifier.weight(1f), color = titleColor,
                size = 11.sp, weight = FontWeight.Bold, spacing = 0.18.em, maxLines = 2)
            trailing?.invoke(this)
        }
        content()
    }
}

/** The screen's own title line under the header: "004 // SENSORS ... LINKED". */
@Composable
fun Crumb(code: String, title: String) {
    val st by com.bittenos.app.MaskLink.state.collectAsState()
    val linked = st is com.bittenos.app.MaskLink.State.Connected
    Row(Modifier.fillMaxWidth().padding(start = Hud.Edge, end = Hud.Edge, top = 6.dp, bottom = 14.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Mono("$code // ${title.uppercase()}", color = LocalAccent.current, size = 10.sp)
        Box(Modifier.weight(1f).height(1.dp).background(Hud.Line))
        Box(Modifier.size(6.dp).background(if (linked) LocalAccent.current else Hud.Amber, CircleShape))
        Mono(if (linked) "LINKED" else "AWAY", color = if (linked) LocalAccent.current else Hud.Amber, size = 10.sp)
    }
}

@Composable
fun Field(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier.fillMaxWidth().background(Hud.FieldBg, RoundedCornerShape(2.dp))
        .border(1.dp, Hud.Line, RoundedCornerShape(2.dp)).padding(horizontal = 12.dp, vertical = 11.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) { content() }
}

/** Label on the left, value on the right. */
@Composable
fun LabVal(lab: String, value: String, valueColor: Color = LocalAccent.current) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Caps(lab, Modifier.weight(1f))
        Mono(value, color = valueColor)
    }
}

/** A settings row: title and a small hint on the left, the control on the right. */
@Composable
fun SetRow(title: String, hint: String? = null, enabled: Boolean = true, top: Boolean = true,
           control: @Composable RowScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.45f)) {
        if (top) Box(Modifier.fillMaxWidth().height(1.dp).background(Hud.Line.copy(alpha = 0.6f)))
        Row(Modifier.fillMaxWidth().heightIn(min = 52.dp).padding(vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Column(Modifier.weight(1f)) {
                Caps(title, color = Hud.Text, size = 10.5.sp, maxLines = 2)
                if (!hint.isNullOrEmpty()) Hint(hint, Modifier.padding(top = 2.dp))
            }
            control()
        }
    }
}

// ---- buttons ----------------------------------------------------------------------------------

enum class BtnKind { Plain, On, Out, Fill, Danger, Pink, Cyan, Amber }

@Composable
fun HudButton(
    text: String, onClick: () -> Unit, modifier: Modifier = Modifier, kind: BtnKind = BtnKind.Plain,
    sub: String? = null, enabled: Boolean = true, height: Dp = 44.dp, size: TextUnit = 10.sp,
) {
    val a = LocalAccent.current
    val (border, bg, fg) = when (kind) {
        BtnKind.Plain -> Triple(Hud.Line2, Hud.FieldBg, Hud.Text2)
        BtnKind.On -> Triple(a, accentDeep(a), a)
        BtnKind.Out -> Triple(a, Color.Transparent, a)
        BtnKind.Fill -> Triple(a, a, onAccent(a))
        BtnKind.Danger -> Triple(Hud.Red, lerpColor(Hud.Bg, Hud.Red, 0.12f), Hud.Red)
        BtnKind.Pink -> Triple(Hud.Pink, lerpColor(Hud.Bg, Hud.Pink, 0.12f), Hud.Pink)
        BtnKind.Cyan -> Triple(Hud.Cyan, lerpColor(Hud.Bg, Hud.Cyan, 0.12f), Hud.Cyan)
        BtnKind.Amber -> Triple(Hud.Amber, Hud.Amber, Color(0xFF1C1405))
    }
    Column(modifier.heightIn(min = height).alpha(if (enabled) 1f else 0.4f)
        .background(bg, RoundedCornerShape(2.dp)).border(1.dp, border, RoundedCornerShape(2.dp))
        .clickable(enabled = enabled, onClick = onClick).padding(horizontal = 8.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        Caps(text, color = fg, size = size, weight = FontWeight.Bold, align = TextAlign.Center, maxLines = 2)
        if (sub != null) Mono(sub, color = fg.copy(alpha = 0.75f), size = 8.5.sp)
    }
}

/** A row of equal buttons, one of them on. */
@Composable
fun <T> Seg(options: List<Pair<T, String>>, selected: T?, onPick: (T) -> Unit, modifier: Modifier = Modifier,
            subs: List<String?>? = null, enabled: Boolean = true, height: Dp = 44.dp, cols: Int = options.size) {
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
        options.chunked(cols).forEachIndexed { r, row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
                row.forEachIndexed { i, (v, label) ->
                    HudButton(label, { onPick(v) }, Modifier.weight(1f), if (v == selected) BtnKind.On else BtnKind.Plain,
                        sub = subs?.getOrNull(r * cols + i), enabled = enabled, height = height)
                }
                repeat(cols - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}

/** The small text-link button: "EDIT ›". */
@Composable
fun LinkBtn(text: String, onClick: () -> Unit, color: Color = LocalAccent.current) {
    Box(Modifier.heightIn(min = 32.dp).clickable(onClick = onClick).padding(horizontal = 4.dp),
        contentAlignment = Alignment.Center) {
        Caps(text, color = color, size = 10.sp, weight = FontWeight.Bold)
    }
}

@Composable
fun Stepper(value: String, onMinus: () -> Unit, onPlus: () -> Unit, enabled: Boolean = true) {
    val a = LocalAccent.current
    Row(Modifier.height(34.dp).alpha(if (enabled) 1f else 0.4f).border(1.dp, Hud.Line2, RoundedCornerShape(2.dp)),
        verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(34.dp).clickable(enabled = enabled, onClick = onMinus), contentAlignment = Alignment.Center) {
            Text("−", color = a, fontSize = 17.sp)
        }
        Mono(value, Modifier.widthIn(min = 50.dp), color = Hud.Text, size = 11.sp)
        Box(Modifier.size(34.dp).clickable(enabled = enabled, onClick = onPlus), contentAlignment = Alignment.Center) {
            Text("+", color = a, fontSize = 17.sp)
        }
    }
}

/** Stepper bound to a number with limits, so screens only say min, max and step. */
@Composable
fun NumStepper(v: Double, min: Double, max: Double, step: Double, fmt: (Double) -> String,
               enabled: Boolean = true, onSet: (Double) -> Unit) {
    fun snap(x: Double) = (Math.round(x / step) * step).coerceIn(min, max)
    Stepper(fmt(v), { onSet(snap(v - step)) }, { onSet(snap(v + step)) }, enabled)
}

@Composable
fun HudToggle(checked: Boolean, onChange: (Boolean) -> Unit, enabled: Boolean = true, color: Color = LocalAccent.current) {
    val off = Hud.Faint
    Box(Modifier.size(42.dp, 24.dp).alpha(if (enabled) 1f else 0.4f)
        .background(if (checked) lerpColor(Hud.Bg, color, 0.2f) else Hud.Track, RoundedCornerShape(12.dp))
        .border(1.dp, if (checked) color else Hud.Line2, RoundedCornerShape(12.dp))
        .clickable(enabled = enabled) { onChange(!checked) }) {
        Box(Modifier.padding(4.dp).offset(x = if (checked) 18.dp else 0.dp).size(14.dp)
            .background(if (checked) color else off, CircleShape))
    }
}

/** The HUD slider: a thin track filled with the accent and a small square thumb. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HudSlider(
    value: Float, onChange: (Float) -> Unit, range: ClosedFloatingPointRange<Float>, modifier: Modifier = Modifier,
    steps: Int = 0, enabled: Boolean = true, onDone: () -> Unit = {}, color: Color = LocalAccent.current,
    mark: Float? = null,
) {
    Slider(value = value.coerceIn(range.start, range.endInclusive), onValueChange = onChange,
        modifier = modifier.fillMaxWidth().height(30.dp), enabled = enabled, valueRange = range, steps = steps,
        onValueChangeFinished = onDone,
        thumb = {
            Box(Modifier.size(16.dp).shadow(6.dp, RoundedCornerShape(2.dp), ambientColor = color, spotColor = color)
                .background(if (enabled) color else Hud.Faint, RoundedCornerShape(2.dp))
                .border(2.dp, Hud.Bg, RoundedCornerShape(2.dp)))
        },
        track = { st ->
            val f = ((st.value - range.start) / (range.endInclusive - range.start)).coerceIn(0f, 1f)
            Canvas(Modifier.fillMaxWidth().height(4.dp)) {
                drawRect(Hud.Track)
                drawRect(if (enabled) color else Hud.Faint, size = size.copy(width = size.width * f))
                if (mark != null) {
                    val mx = size.width * ((mark - range.start) / (range.endInclusive - range.start)).coerceIn(0f, 1f)
                    drawLine(Hud.Dim, Offset(mx, -6.dp.toPx()), Offset(mx, size.height + 6.dp.toPx()), 1.dp.toPx())
                }
            }
        })
}

/** A meter: filled to [f], with a line at [mark]. */
@Composable
fun Gauge(f: Float, mark: Float? = null, over: Boolean = false, idle: Boolean = false, color: Color = LocalAccent.current) {
    Canvas(Modifier.fillMaxWidth().height(8.dp)) {
        drawRect(Hud.Track)
        drawRect(if (idle) Hud.Faint else if (over) Hud.Amber else color, size = size.copy(width = size.width * f.coerceIn(0f, 1f)))
        if (mark != null) {
            val mx = size.width * mark.coerceIn(0f, 1f)
            drawLine(Hud.Amber, Offset(mx, -3.dp.toPx()), Offset(mx, size.height + 3.dp.toPx()), 2.dp.toPx())
        }
    }
}

/** A segmented level meter, like the voice LEVEL bar. */
@Composable
fun Meter(level: Float, color: Color = LocalAccent.current) {
    Row(Modifier.fillMaxWidth().height(14.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        val n = 24
        val lit = (level.coerceIn(0f, 1f) * n).toInt()
        repeat(n) { i ->
            Box(Modifier.weight(1f).fillMaxSize().background(
                when { i >= lit -> Hud.Track; i >= n - 4 -> Hud.Red; i >= n - 8 -> Hud.Amber; else -> color }))
        }
    }
}

// ---- text field -------------------------------------------------------------------------------

@Composable
fun HudInput(
    value: String, onChange: (String) -> Unit, placeholder: String, modifier: Modifier = Modifier,
    password: Boolean = false, number: Boolean = false, onDone: (() -> Unit)? = null, maxLen: Int = 64,
    singleLine: Boolean = true,
) {
    var show by remember { mutableStateOf(false) }
    Box(modifier.heightIn(min = 44.dp).background(Hud.FieldBg, RoundedCornerShape(2.dp))
        .border(1.dp, Hud.Line2, RoundedCornerShape(2.dp)).padding(horizontal = 10.dp, vertical = 12.dp),
        contentAlignment = Alignment.CenterStart) {
        if (value.isEmpty()) Mono(placeholder.uppercase(), color = Hud.Faint, size = 12.sp)
        Row(verticalAlignment = Alignment.CenterVertically) {
            BasicTextField(value, { onChange(it.take(maxLen)) }, Modifier.weight(1f), singleLine = singleLine,
                textStyle = TextStyle(color = Hud.Text, fontSize = 13.sp, fontFamily = Hud.Mono),
                cursorBrush = SolidColor(LocalAccent.current),
                visualTransformation = if (password && !show) PasswordVisualTransformation() else VisualTransformation.None,
                keyboardOptions = KeyboardOptions(
                    keyboardType = if (number) KeyboardType.Number else if (password) KeyboardType.Password else KeyboardType.Text,
                    imeAction = if (onDone != null) ImeAction.Send else ImeAction.Done),
                keyboardActions = KeyboardActions(onSend = { onDone?.invoke() }))
            if (password) Box(Modifier.clickable { show = !show }.padding(start = 8.dp)) {
                Caps(if (show) "HIDE" else "SHOW", color = Hud.Faint, size = 9.sp)
            }
        }
    }
}

// ---- pickers and sheets ----------------------------------------------------------------------

/** A value with a ▾ that opens a list to pick from. */
@Composable
fun <T> Picker(value: T?, options: List<Pair<T, String>>, onPick: (T) -> Unit, title: String,
               modifier: Modifier = Modifier, enabled: Boolean = true) {
    var open by remember { mutableStateOf(false) }
    val label = options.firstOrNull { it.first == value }?.second ?: (value?.toString() ?: "—")
    Row(modifier.heightIn(min = 36.dp).widthIn(max = 200.dp).alpha(if (enabled) 1f else 0.4f)
        .border(1.dp, Hud.Line2, RoundedCornerShape(2.dp)).clickable(enabled = enabled) { open = true }
        .padding(horizontal = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Mono(label, Modifier.weight(1f, fill = false), color = Hud.Text, size = 11.sp)
        Spacer(Modifier.width(6.dp))
        Text("▾", color = LocalAccent.current, fontSize = 12.sp)
    }
    if (open) Sheet(title, { open = false }) {
        options.forEach { (v, l) ->
            val on = v == value
            Row(Modifier.fillMaxWidth().heightIn(min = 48.dp).clickable { onPick(v); open = false }
                .padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(l, Modifier.weight(1f), color = if (on) LocalAccent.current else Hud.Text, fontSize = 14.sp,
                    fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal)
                if (on) Text("✓", color = LocalAccent.current)
            }
            Box(Modifier.fillMaxWidth().height(1.dp).background(Hud.Line.copy(alpha = 0.5f)))
        }
    }
}

/** A sheet that slides up from the bottom, in the HUD's colours. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Sheet(title: String, onClose: () -> Unit, titleColor: Color = LocalAccent.current,
          content: @Composable ColumnScope.() -> Unit) {
    val st = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(onDismissRequest = onClose, sheetState = st, containerColor = Hud.PanelBg,
        shape = RoundedCornerShape(topStart = 6.dp, topEnd = 6.dp), dragHandle = {
            Box(Modifier.padding(top = 8.dp).size(36.dp, 3.dp).background(Hud.Line2))
        }) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)
            .padding(bottom = 24.dp).navigationBarsPadding()) {
            Caps(title, Modifier.padding(vertical = 12.dp), color = titleColor, size = 11.sp, weight = FontWeight.Bold,
                spacing = 0.18.em, maxLines = 2)
            content()
        }
    }
}

/** Ask before something that cannot be undone. */
@Composable
fun Confirm(title: String, text: String, yes: String, onYes: () -> Unit, onClose: () -> Unit, danger: Boolean = true) {
    Sheet(title, onClose, titleColor = if (danger) Hud.Red else LocalAccent.current) {
        Note(text)
        Spacer(Modifier.height(14.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(Hud.SegGap)) {
            HudButton("Keep it", onClose, Modifier.weight(1f))
            HudButton(yes, { onYes(); onClose() }, Modifier.weight(1f), if (danger) BtnKind.Danger else BtnKind.Fill)
        }
    }
}

// ---- toasts ----------------------------------------------------------------------------------

object Toast {
    data class Msg(val text: String, val bad: Boolean, val at: Long = System.nanoTime())
    val msg = MutableStateFlow<Msg?>(null)
    fun ok(t: String) { msg.value = Msg(t, false) }
    fun bad(t: String) { msg.value = Msg(t, true) }
}

@Composable
fun ToastHost(modifier: Modifier = Modifier) {
    val m by Toast.msg.collectAsState()
    val cur = m ?: return
    LaunchedEffect(cur) { delay(2200); if (Toast.msg.value == cur) Toast.msg.value = null }
    val c = if (cur.bad) Hud.Red else LocalAccent.current
    Row(modifier.padding(horizontal = 16.dp, vertical = 12.dp).fillMaxWidth()
        .background(lerpColor(Hud.Bg, c, 0.14f), RoundedCornerShape(2.dp)).border(1.dp, c, RoundedCornerShape(2.dp))
        .padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
        Caps((if (cur.bad) "! " else "✓ ") + cur.text, color = c, size = 10.sp, weight = FontWeight.Bold, maxLines = 3)
    }
}

// ---- link nodes and the moving packet ------------------------------------------------------------

@Composable
fun LinkNode(text: String, color: Color, dotFirst: Boolean = false) {
    Row(Modifier.height(24.dp).border(1.dp, color, RoundedCornerShape(2.dp)).padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        if (dotFirst) Box(Modifier.size(6.dp).background(color, CircleShape))
        Caps(text, color = Hud.Text, size = 9.5.sp, weight = FontWeight.Bold, spacing = 0.14.em)
        if (!dotFirst) Box(Modifier.size(6.dp).background(color, CircleShape))
    }
}

@Composable
fun Wire(modifier: Modifier, color: Color, moving: Boolean = true, delayMs: Int = 0) {
    val t = rememberInfiniteTransition(label = "wire")
    val x by t.animateFloat(0f, 1f, infiniteRepeatable(tween(1600, delayMs, LinearEasing), RepeatMode.Restart), label = "pkt")
    Canvas(modifier.height(2.dp)) {
        drawLine(Color(0xFF24323A), Offset(0f, 1f), Offset(size.width, 1f), 2.dp.toPx(),
            pathEffect = PathEffect.dashPathEffect(floatArrayOf(3.dp.toPx(), 3.dp.toPx())))
        if (moving) {
            val w = 30.dp.toPx()
            val at = -w + (size.width + w) * x
            drawLine(color, Offset(at.coerceAtLeast(0f), 1f), Offset((at + w).coerceAtMost(size.width), 1f), 2.dp.toPx())
        }
    }
}

/** A thin rule between groups inside a panel. */
@Composable
fun Rule(modifier: Modifier = Modifier) = Box(modifier.fillMaxWidth().height(1.dp).background(Hud.Line.copy(alpha = 0.6f)))

/** The provider every screen sits in. */
@Composable
fun HudTheme(accent: Color, content: @Composable () -> Unit) {
    CompositionLocalProvider(LocalAccent provides accent) { BittenTheme(content) }
}


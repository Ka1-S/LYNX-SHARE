package io.github.danzfadlullah.lynx.ui

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import androidx.core.content.ContextCompat
import io.github.danzfadlullah.lynx.R

/**
 * DawView is the touch-first DAW canvas.
 *
 * Four regions:
 *   - Channel Rack (left): 8 step pads x 4 channels, tap to trigger note
 *   - Piano Roll (center-left): piano keys + note clips
 *   - Playlist (center-right): arrangement tracks
 *   - Mixer (right): 4 channel strips with vertical faders
 *
 * All input is touch: tap, drag, pan. No keyboard / mouse.
 */
class DawView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        textSize = 14f
        style = Paint.Style.FILL
    }

    // Touch state
    private var touchX = 0f
    private var touchY = 0f
    private var isDragging = false

    // Visual state
    private var isPlaying = false
    private var bpm = 120f
    private var mixerGains = floatArrayOf(0.8f, 0.8f, 0.8f, 0.8f)

    // Waveform for display (0=Sine,1=Square,2=Saw,3=Triangle)
    private var waveform = 0

    // Region layout
    private var channelRackWidth = 0f
    private var pianoRollWidth = 0f
    private var playlistWidth = 0f
    private var mixerWidth = 0f

    // Channel rack pad geometry
    private var rackLeft = 0f
    private var rackTop = 0f
    private var rackPadW = 0f
    private var rackPadH = 0f

    // Mixer fader geometry
    private var mixerLeft = 0f
    private var mixerStripW = 0f
    private var mixerFaderTop = 0f
    private var mixerFaderBottom = 0f

    // Drum trigger button geometry (below channel rack)
    private var drumBtnY = 0f
    private var drumBtnH = 0f
    private var drumBtnW = 0f
    private var drumBtnGap = 0f

    // Note clips for piano roll (x = time in beats, y = pitch)
    private val noteClips = mutableListOf<NoteClip>()

    /** Callback when a pad/key is tapped — wired to MainActivity's native noteOn */
    var onNoteTap: ((note: Int, velocity: Int) -> Unit)? = null

    /** Callback when user taps piano roll to add a note — wired to MainActivity's nativeAddNote */
    var onNoteAdded: ((note: Int, xBeat: Float, yBeat: Float) -> Unit)? = null

    /** Callback when drum pad is tapped — wired to MainActivity's drum trigger */
    var onDrumTap: ((type: Int) -> Unit)? = null // 0=Kick,1=Snare,2=Hihat

    /** Callback when mixer fader is dragged — wired to MainActivity's setChannelGain */
    var onMixerFaderChanged: ((channel: Int, gain: Float) -> Unit)? = null

    constructor(context: Context) : this(context, null)

    init {
        setFocusable(true)
        setFocusableInTouchMode(true)
        // Seed a simple demo pattern
        noteClips.clear()
        for (i in 0 until 8) {
            if (i % 2 == 0) {
                noteClips.add(NoteClip(60 + i % 4 * 3, i * 0.5f, 60f, 0.5f, 20f))
            }
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        channelRackWidth = w * 0.18f
        pianoRollWidth = w * 0.34f
        playlistWidth = w * 0.28f
        mixerWidth = w * 0.20f

        val topBarH = 56f
        rackLeft = 20f
        rackTop = topBarH + 60f
        rackPadW = (channelRackWidth - 40f) / 8f
        // Channel rack pads occupy upper portion; drum buttons below
        val rackAreaH = h - topBarH - 80f - 50f // reserve 50px for drum buttons
        rackPadH = rackAreaH / 4f

        drumBtnY = topBarH + 60f + rackAreaH + 10f
        drumBtnH = 36f
        drumBtnW = (channelRackWidth - 40f - 20f) / 3f
        drumBtnGap = 10f

        // Mixer geometry (right side)
        mixerLeft = channelRackWidth + pianoRollWidth + playlistWidth
        mixerStripW = (w - mixerLeft - 60f) / 4f
        mixerFaderTop = topBarH + 80f
        mixerFaderBottom = h - 60f
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()

        paint.color = ContextCompat.getColor(context, R.color.colorBackground)
        canvas.drawRect(0f, 0f, w, h, paint)

        val topBarH = 56f
        val rackRight = channelRackWidth
        val pianoRight = rackRight + pianoRollWidth
        val playlistRight = pianoRight + playlistWidth

        drawRegionDivider(canvas, rackRight, topBarH, h)
        drawRegionDivider(canvas, pianoRight, topBarH, h)
        drawRegionDivider(canvas, playlistRight, topBarH, h)

        drawChannelRack(canvas, 0f, topBarH, rackRight, h)
        drawPianoRoll(canvas, rackRight, topBarH, pianoRight, h)
        drawPlaylist(canvas, pianoRight, topBarH, playlistRight, h)
        drawMixer(canvas, playlistRight, topBarH, w, h)

        // Transport playhead
        if (isPlaying) {
            val playheadX = (System.currentTimeMillis() % 4000) / 4000f * playlistWidth + pianoRight
            paint.color = ContextCompat.getColor(context, R.color.colorAccent)
            paint.strokeWidth = 2f
            paint.style = Paint.Style.STROKE
            canvas.drawLine(playheadX, topBarH, playheadX, h, paint)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                touchX = event.x
                touchY = event.y
                isDragging = false
                handleTap(touchX, touchY)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = abs(event.x - touchX)
                val dy = abs(event.y - touchY)
                if (dx > 8f || dy > 8f) {
                    isDragging = true
                    handleDrag(event.x, event.y)
                    touchX = event.x
                    touchY = event.y
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (!isDragging) {
                    handleTap(touchX, touchY)
                }
                isDragging = false
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private fun handleTap(x: Float, y: Float) {
        val topBarH = 56f
        if (y < topBarH) return

        // Drum buttons area (below channel rack)
        if (y >= drumBtnY && y <= drumBtnY + drumBtnH && x < channelRackWidth) {
            val d = (((x - rackLeft - drumBtnGap) / (drumBtnW + drumBtnGap)) + 0.5f).toInt().coerceIn(0, 2)
            onDrumTap?.invoke(d)
            invalidate()
            return
        }

        when {
            x < channelRackWidth -> {
                val step = ((((x - rackLeft) / rackPadW) + 0.5f).toInt().coerceIn(0, 7))
                val ch = ((((y - rackTop) / rackPadH) + 0.5f).toInt().coerceIn(0, 3))
                val note = 60 + ch * 4 + step
                onNoteTap?.invoke(note, 100)
                invalidate()
            }
            x < channelRackWidth + pianoRollWidth -> {
                // Piano roll: add note clip at tap position
                val localX = x - channelRackWidth
                val localY = y - topBarH
                val note = 60 + ((localY - 50f).coerceIn(0, 120) / 10f).toInt().coerceIn(0, 11)
                val xBeat = localX / 100f  // approximate beats
                val yBeat = localY / 100f
                noteClips.add(NoteClip(note, xBeat, yBeat, 0.5f, 20f))
                onNoteAdded?.invoke(note, xBeat, yBeat)
                invalidate()
            }
            else -> {
                // Playlist / mixer: clip or fader
                invalidate()
            }
        }
    }

    private fun handleDrag(x: Float, y: Float) {
        // Check mixer fader drag
        if (x >= mixerLeft && x <= w && y >= mixerFaderTop && y <= mixerFaderBottom) {
            val c = (((x - mixerLeft - 20f) / mixerStripW) + 0.5f).toInt().coerceIn(0, 3)
            val level = 1.0f - ((y - mixerFaderTop) / (mixerFaderBottom - mixerFaderTop)).coerceIn(0f, 1f)
            onMixerFaderChanged?.invoke(c, level)
            return
        }
        // Pan / fader drag — to be wired to engine automation
        invalidate()
    }

    private fun drawRegionDivider(canvas: Canvas, x: Float, top: Float, bottom: Float) {
        paint.color = ContextCompat.getColor(context, R.color.colorAccentSurface)
        paint.strokeWidth = 1f
        canvas.drawLine(x, top, x, bottom, paint)
    }

    private fun drawChannelRack(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        paint.color = ContextCompat.getColor(context, R.color.colorSurface)
        canvas.drawRect(left, top, right, bottom, paint)

        paint.color = ContextCompat.getColor(context, R.color.colorChannelRack)
        paint.textSize = 16f
        paint.textAlign = Paint.Align.CENTER
        canvas.drawText("CHANNEL RACK", (left + right) / 2, top + 32f, paint)
        paint.textSize = 14f
        paint.textAlign = Paint.Align.CENTER

        // Channel labels
        for (ch in 0 until 4) {
            val py = rackTop + ch * rackPadH + rackPadH / 2
            paint.color = ContextCompat.getColor(context, R.color.colorTextSecondary)
            canvas.drawText("Ch ${ch + 1}", left + 8f, py + 5f, paint)
        }

        // 8 step pads x 4 channels
        val now = System.currentTimeMillis() % 4000
        val stepNow = (now / 500).toInt() % 8
        for (ch in 0 until 4) {
            for (step in 0 until 8) {
                val px = rackLeft + step * rackPadW
                val py = rackTop + ch * rackPadH
                val active = (step + ch) % 3 == 0
                val playingNow = isPlaying && step == stepNow
                paint.color = when {
                    playingNow -> ContextCompat.getColor(context, R.color.colorButtonActive)
                    active -> ContextCompat.getColor(context, R.color.colorAccentSurface)
                    else -> ContextCompat.getColor(context, R.color.colorButtonInactive)
                }
                val radius = min(rackPadW, rackPadH) * 0.35f
                canvas.drawRoundRect(px, py, px + rackPadW * 0.75f, py + rackPadH * 0.75f, radius, radius, paint)
            }
        }

        // Drum trigger buttons (below channel rack)
        val drumNames = arrayOf("KICK", "SNARE", "HIHAT")
        val drumColors = intArrayOf(
            ContextCompat.getColor(context, R.color.colorChannelRack),
            ContextCompat.getColor(context, R.color.colorPianoRoll),
            ContextCompat.getColor(context, R.color.colorPlaylist)
        )
        for (d in 0 until 3) {
            val dx = rackLeft + d * (drumBtnW + drumBtnGap)
            paint.color = drumColors[d]
            canvas.drawRoundRect(dx, drumBtnY, dx + drumBtnW, drumBtnY + drumBtnH, drumBtnH * 0.3f, drumBtnH * 0.3f, paint)
            paint.color = ContextCompat.getColor(context, R.color.colorText)
            paint.textSize = 12f
            canvas.drawText(drumNames[d], dx + drumBtnW / 2, drumBtnY + drumBtnH / 2 + 4f, paint)
        }
    }

    private fun drawPianoRoll(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        paint.color = ContextCompat.getColor(context, R.color.colorSurface)
        canvas.drawRect(left, top, right, bottom, paint)

        paint.color = ContextCompat.getColor(context, R.color.colorPianoRoll)
        paint.textSize = 16f
        paint.textAlign = Paint.Align.CENTER
        canvas.drawText("PIANO ROLL", (left + right) / 2, top + 32f, paint)
        paint.textSize = 14f
        paint.textAlign = Paint.Align.CENTER

        // Piano keys strip
        val keyW = (right - left - 20f) / 12f
        val keyH = 60f
        for (i in 0 until 12) {
            val px = left + 10f + i * keyW
            val isWhite = i % 12 in listOf(0, 2, 4, 5, 7, 9, 11)
            paint.color = if (isWhite) Color.WHITE else Color.BLACK
            canvas.drawRect(px, top + 50f, px + keyW * 0.9f, top + 50f + keyH, paint)
        }

        // Note clips
        val waveformNames = arrayOf("Sine", "Square", "Saw", "Triangle")
        paint.color = ContextCompat.getColor(context, R.color.colorAccent).also { it.alpha = 200 }
        for (clip in noteClips) {
            // Convert beat coordinates to screen pixels (1 beat ≈ 100px horizontal)
            val px = left + 10f + clip.x * 100f
            // Map yBeat to vertical position within piano roll area
            val py = top + 120f + clip.y * 20f
            canvas.drawRect(px, py, px + clip.w * 80f, py + clip.h, paint)
            paint.color = Color.WHITE
            paint.textSize = 10f
            canvas.drawText(clip.note.toString(), px + clip.w * 40f, py + clip.h / 2 + 3f, paint)
            paint.color = ContextCompat.getColor(context, R.color.colorAccent).also { it.alpha = 200 }
        }

        // Waveform indicator
        paint.color = ContextCompat.getColor(context, R.color.colorTextSecondary)
        paint.textSize = 12f
        canvas.drawText("Wave: ${waveformNames[waveform]}", left + 10f, bottom - 20f, paint)
    }

    private fun drawPlaylist(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        paint.color = ContextCompat.getColor(context, R.color.colorSurface)
        canvas.drawRect(left, top, right, bottom, paint)

        paint.color = ContextCompat.getColor(context, R.color.colorPlaylist)
        paint.textSize = 16f
        paint.textAlign = Paint.Align.CENTER
        canvas.drawText("PLAYLIST", (left + right) / 2, top + 32f, paint)
        paint.textSize = 14f
        paint.textAlign = Paint.Align.CENTER

        val trackH = (bottom - top - 80f) / 5f
        for (t in 0 until 5) {
            val py = top + 60f + t * trackH
            canvas.drawRect(left + 10f, py + 4f, right - 10f, py + trackH - 4f, paint)
            paint.color = ContextCompat.getColor(context, R.color.colorButtonInactive)
            canvas.drawRect(left + 10f + 40f, py + 8f, left + 10f + 40f + trackH * 0.6f, py + trackH - 8f, paint)
            paint.color = ContextCompat.getColor(context, R.color.colorTextSecondary)
            canvas.drawText("Track ${t + 1}", left + 30f, py + trackH / 2 + 6f, paint)
        }
    }

    private fun drawMixer(canvas: Canvas, left: Float, top: Float, right: Float, bottom: Float) {
        paint.color = ContextCompat.getColor(context, R.color.colorSurface)
        canvas.drawRect(left, top, right, bottom, paint)

        paint.color = ContextCompat.getColor(context, R.color.colorMixer)
        paint.textSize = 16f
        paint.textAlign = Paint.Align.CENTER
        canvas.drawText("MIXER", (left + right) / 2, top + 32f, paint)
        paint.textSize = 14f
        paint.textAlign = Paint.Align.CENTER

        val stripW = (right - left - 60f) / 4f
        val faderTop = top + 80f
        val faderBottom = bottom - 60f

        for (c in 0 until 4) {
            val sx = left + 20f + c * stripW
            val faderX = sx + stripW * 0.25f
            val faderW = stripW * 0.5f

            // Fader track
            paint.color = ContextCompat.getColor(context, R.color.colorButtonInactive)
            canvas.drawRect(faderX, faderTop, faderX + faderW, faderBottom, paint)

            // Fader thumb (level)
            val level = mixerGains[c]
            val thumbY = faderBottom - level * (faderBottom - faderTop)
            paint.color = ContextCompat.getColor(context, R.color.colorAccent)
            canvas.drawRect(faderX - 4f, thumbY - 10f, faderX + faderW + 4f, thumbY + 10f, paint)

            // Label + level text
            paint.color = ContextCompat.getColor(context, R.color.colorTextSecondary)
            canvas.drawText("Ch ${c + 1}", sx + stripW / 2, bottom - 35f, paint)
            canvas.drawText("${(level * 100).toInt}%", sx + stripW / 2, bottom - 20f, paint)
        }
    }

    fun setPlaying(playing: Boolean) {
        isPlaying = playing
        invalidate()
    }

    fun setBpm(newBpm: Float) {
        bpm = newBpm
        invalidate()
    }

    fun setWaveform(wf: Int) {
        waveform = wf
        invalidate()
    }

    private data class NoteClip(val note: Int, val x: Float, val y: Float, val w: Float, val h: Float)
}

package io.github.danzfadlullah.lynx

import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import io.github.danzfadlullah.lynx.ui.DawView
import java.io.File

class MainActivity : AppCompatActivity() {

    companion object {
        init {
            System.loadLibrary("lynx_jni")
        }
    }

    // Native methods declared in jni_bridge.cpp
    external fun nativeInit(assetsPath: String): String
    external fun nativeStartEngine()
    external fun nativeStopEngine()
    external fun nativeTogglePlay()
    external fun nativeIsPlaying(): Boolean
    external fun nativeGetTempo(): Float
    external fun nativeSetTempo(bpm: Float)
    external fun nativeNoteOn(note: Int, velocity: Int)
    external fun nativeNoteOff(note: Int)
    external fun nativeGetWaveform(): Int
    external fun nativeSetWaveform(waveform: Int)
    external fun nativeTriggerDrum(type: Int, velocity: Int)
    external fun nativeSaveProject(path: String): String
    external fun nativeLoadProject(path: String): Boolean
    external fun nativeAddNote(
        patternId: Int, note: Int, velocity: Int,
        startBeat: Float, durationBeat: Float, channel: Int
    )
    external fun nativeExportWav(path: String, numBars: Int): String
    external fun nativeImportFlm(fileBytes: ByteArray): String
    external fun nativeIsFlmFile(fileBytes: ByteArray): Boolean

    private lateinit var dawView: DawView
    private var isPlaying = false
    private var currentPatternId = 1
    private val projectFile: File

    init {
        projectFile = File(filesDir, "project.lynx")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        dawView = findViewById(R.id.daw_view)

        // Wire channel-rack pad taps to the synth
        dawView.onNoteTap = { note, velocity ->
            nativeNoteOn(note, velocity)
        }

        // Wire drum buttons to drum synth
        dawView.onDrumTap = { type ->
            nativeTriggerDrum(type, 100)
        }

        // Wire mixer fader drag
        dawView.onMixerFaderChanged = { channel, gain ->
            nativeSetChannelGain(channel, gain)
        }

        // Wire piano roll taps to add note to project
        dawView.onNoteAdded = { note, xBeat, yBeat ->
            nativeAddNote(currentPatternId, note, 90, xBeat, 1.0f, 0)
            dawView.invalidate()
        }

        // Initialize the native engine with the app's assets directory
        val result = nativeInit(applicationInfo.dataDir ?: filesDir.absolutePath)
        if (!result.startsWith("ok")) {
            Log.e("LYNX", "Engine init: $result")
        }

        // Load existing project if any
        if (projectFile.exists()) {
            val ok = nativeLoadProject(projectFile.absolutePath)
            if (!ok) {
                Log.e("LYNX", "Failed to load existing project")
            }
        }

        // Transport buttons
        findViewById<Button>(R.id.btn_play).setOnClickListener {
            togglePlayback()
        }
        findViewById<Button>(R.id.btn_stop).setOnClickListener {
            stopPlayback()
        }
        findViewById<Button>(R.id.btn_record).setOnClickListener {
            toggleRecord()
        }

        // Save project button
        findViewById<Button>(R.id.btn_save).setOnClickListener {
            val res = nativeSaveProject(projectFile.absolutePath)
            if (res == "ok") {
                Toast.makeText(this, "Project disimpan", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "Gagal menyimpan: $res", Toast.LENGTH_SHORT).show()
            }
        }

        // Load project button
        findViewById<Button>(R.id.btn_load).setOnClickListener {
            val ok = nativeLoadProject(projectFile.absolutePath)
            if (ok) {
                Toast.makeText(this, "Project dimuat", Toast.LENGTH_SHORT).show()
                dawView.invalidate()
            } else {
                Toast.makeText(this, "Gagal memuat project", Toast.LENGTH_SHORT).show()
            }
        }

        // Export WAV button
        findViewById<Button>(R.id.btn_export).setOnClickListener {
            val wavFile = File(filesDir, "export.wav")
            val result = nativeExportWav(wavFile.absolutePath, 4) // 4 bars
            if (result == "ok") {
                Toast.makeText(this, "WAV diekspor: ${wavFile.name}", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "Gagal ekspor: $result", Toast.LENGTH_SHORT).show()
            }
        }

        // Import FLM button
        findViewById<Button>(R.id.btn_import_flm).setOnClickListener {
            pickFlmFile()
        }

        // Tempo slider
        val tempoSlider = findViewById<SeekBar>(R.id.slider_tempo)
        val tempoValue = findViewById<TextView>(R.id.tv_tempo_value)
        tempoSlider.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    val bpm = 60f + progress.toFloat() // 60..180 BPM
                    tempoValue.text = bpm.toString()
                    onTempoChanged(bpm)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })

        // Waveform selector (cycle on tap of tempo value)
        tempoValue.setOnClickListener {
            val current = nativeGetWaveform()
            val next = (current + 1) % 4
            nativeSetWaveform(next)
            dawView.setWaveform(next)
            val names = arrayOf("Sine", "Square", "Saw", "Triangle")
            tempoValue.text = "${nativeGetTempo().toInt()} · ${names[next]}"
        }

        // Effect sliders
        val sliderDelayMix = findViewById<SeekBar>(R.id.slider_delay_mix)
        sliderDelayMix.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    nativeSetDelayMix(progress / 100f)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })

        val sliderDelayFb = findViewById<SeekBar>(R.id.slider_delay_fb)
        sliderDelayFb.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    nativeSetDelayFeedback(progress / 100f)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })

        val sliderVerbMix = findViewById<SeekBar>(R.id.slider_reverb_mix)
        sliderVerbMix.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    nativeSetReverbMix(progress / 100f)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })

        val sliderVerbRoom = findViewById<SeekBar>(R.id.slider_reverb_room)
        sliderVerbRoom.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    nativeSetReverbRoomSize(progress / 100f)
                }
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })

        // Initial tempo display
        tempoValue.text = "${nativeGetTempo().toInt()} · Sine"
        tempoSlider.progress = (nativeGetTempo() - 60).toInt()
    }

    override fun onResume() {
        super.onResume()
        nativeStartEngine()
    }

    override fun onPause() {
        super.onPause()
        // Auto-save before pause
        nativeSaveProject(projectFile.absolutePath)
        nativeStopEngine()
    }

    override fun onDestroy() {
        super.onDestroy()
        nativeSaveProject(projectFile.absolutePath)
        nativeStopEngine()
    }

    private fun togglePlayback() {
        nativeTogglePlay()
        isPlaying = nativeIsPlaying()
        dawView.setPlaying(isPlaying)
        updateTransportUI()
    }

    private fun stopPlayback() {
        if (isPlaying) {
            nativeTogglePlay()
            isPlaying = false
            dawView.setPlaying(isPlaying)
            updateTransportUI()
        }
    }

    private fun toggleRecord() {
        dawView.invalidate()
    }

    private fun updateTransportUI() {
        val btn = findViewById<Button>(R.id.btn_play)
        btn.text = if (isPlaying) getString(R.string.stop) else getString(R.string.play)
        btn.setBackgroundColor(
            if (isPlaying) android.graphics.Color.parseColor("#EF4444")
            else android.graphics.Color.parseColor("#3B82F6")
        )
        dawView.invalidate()
    }

    fun onTempoChanged(newBpm: Float) {
        nativeSetTempo(newBpm)
        dawView.invalidate()
    }

    // FLM Import
    private val flmPickerLauncher = registerForActivityResult(
        android.app.ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == RESULT_OK && result.data != null) {
            val uri = result.data?.data
            uri?.let { importFlmFile(it) }
        }
    }

    private fun pickFlmFile() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream"))
            addCategory(Intent.CATEGORY_OPENABLE)
        }
        flmPickerLauncher.launch(intent)
    }

    private fun importFlmFile(uri: android.net.Uri) {
        contentResolver.openInputStream(uri)?.use { inputStream ->
            val bytes = inputStream.readBytes()
            if (nativeIsFlmFile(bytes)) {
                val result = nativeImportFlm(bytes)
                if (result == "ok") {
                    Toast.makeText(this, "FLM project diimpor", Toast.LENGTH_SHORT).show()
                    dawView.invalidate()
                } else {
                    Toast.makeText(this, "Gagal impor FLM: $result", Toast.LENGTH_LONG).show()
                }
            } else {
                Toast.makeText(this, "File bukan format FLM yang valid", Toast.LENGTH_SHORT).show()
            }
        } ?: run {
            Toast.makeText(this, "Gagal membaca file", Toast.LENGTH_SHORT).show()
        }
    }
}

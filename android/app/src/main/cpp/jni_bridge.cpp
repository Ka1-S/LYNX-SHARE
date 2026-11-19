// JNI bridge between Kotlin UI and the LYNX C++ audio engine.
// JUCE-free: uses std::vector<float> buffers and AAudio output via NDK.

#include <jni.h>
#include <string>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>
#include <vector>

#include "audio_engine.h"
#include "aaudio_output.h"
#include "wav_export.h"
#include "simple_synth.h"
#include "flm_parser.h"
#include "flm_importer.h"

namespace {

std::mutex g_engine_mutex;
std::unique_ptr<AudioEngine> g_engine;
std::unique_ptr<AAudioOutput> g_output;
std::atomic<bool> g_initialized{false};

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeInit(
        JNIEnv* env, jobject /* thiz */, jstring assetsPath) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);

    if (g_initialized.load()) {
        return env->NewStringUTF("ok");
    }

    const char* path = env->GetStringUTFChars(assetsPath, nullptr);
    std::string assetPath(path ? path : "");
    env->ReleaseStringUTFChars(assetsPath, path);

    g_engine = std::make_unique<AudioEngine>();
    g_engine->setAssetPath(assetPath);

    if (!g_engine->initialize()) {
        g_engine.reset();
        return env->NewStringUTF("error: engine init failed");
    }

    g_initialized.store(true);
    return env->NewStringUTF("ok");
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeStartEngine(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine || !g_initialized.load()) {
        return;
    }

    double sr = g_engine->getSampleRate();
    int framesPerBurst = 512;

    g_output = std::make_unique<AAudioOutput>();
    auto renderCb = [](float* buffer, int numFrames) {
        std::lock_guard<std::mutex> lock(g_engine_mutex);
        if (g_engine) {
            g_engine->render(buffer, numFrames);
        } else {
            for (int i = 0; i < numFrames * 2; ++i) {
                buffer[i] = 0.0f;
            }
        }
    };

    if (!g_output->start(static_cast<int>(sr), framesPerBurst, renderCb)) {
        g_output.reset();
    }

    g_engine->start();
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeStopEngine(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        g_engine->stop();
    }
    if (g_output) {
        g_output->stop();
        g_output.reset();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeTogglePlay(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        g_engine->togglePlayback();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeIsPlaying(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        return g_engine->isPlaying() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeGetTempo(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        return static_cast<jfloat>(g_engine->getTempoBpm());
    }
    return 120.0f;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSetTempo(
        JNIEnv* /* env */, jobject /* thiz */, jfloat bpm) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        g_engine->setTempoBpm(static_cast<double>(bpm));
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeNoteOn(
        JNIEnv* /* env */, jobject /* thiz */, jint note, jint velocity) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        g_engine->noteOn(static_cast<int>(note), static_cast<int>(velocity));
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeNoteOff(
        JNIEnv* /* env */, jobject /* thiz */, jint note) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        g_engine->noteOff(static_cast<int>(note));
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeGetWaveform(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        return static_cast<jint>(g_engine->getWaveform());
    }
    return 0;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSetWaveform(
        JNIEnv* /* env */, jobject /* thiz */, jint waveform) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) {
        g_engine->setWaveform(static_cast<AudioEngine::Waveform>(waveform));
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSaveProject(
        JNIEnv* env, jobject /* thiz */, jstring path) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine) {
        return env->NewStringUTF("error: engine not ready");
    }
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    bool ok = g_engine->saveProject(cpath ? cpath : "");
    if (cpath) env->ReleaseStringUTFChars(path, cpath);
    return env->NewStringUTF(ok ? "ok" : "error: save failed");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeLoadProject(
        JNIEnv* env, jobject /* thiz */, jstring path) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine) {
        return JNI_FALSE;
    }
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    bool ok = g_engine->loadProject(cpath ? cpath : "");
    if (cpath) env->ReleaseStringUTFChars(path, cpath);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeAddNote(
        JNIEnv* env, jobject /* thiz */, jint patternId, jint note, jint velocity,
        jfloat startBeat, jfloat durationBeat, jint channel) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine) return;
    Note pn;
    pn.start = static_cast<std::int64_t>(startBeat * 960); // beats to ticks (PPQ 960)
    pn.length = static_cast<std::int64_t>(durationBeat * 960);
    pn.channel = static_cast<int>(channel);
    pn.key = static_cast<int>(note);
    pn.velocity = static_cast<float>(velocity) / 127.0f;
    auto song = g_engine->getSong();
    bool found = false;
    for (auto& pat : song.patterns) {
        if (pat.uid == static_cast<std::uint64_t>(patternId)) {
            pat.notes.push_back(pn);
            pat.sortNotes();
            found = true;
            break;
        }
    }
    if (!found) {
        Pattern pat;
        pat.uid = static_cast<std::uint64_t>(patternId);
        pat.name = "Pattern " + std::to_string(patternId);
        pat.notes.push_back(pn);
        song.patterns.push_back(pat);
    }
    g_engine->applySong(song);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeTriggerDrum(
        JNIEnv* /* env */, jobject /* thiz */, jint type, jint velocity) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine) return;
    g_engine->triggerDrum(
        static_cast<AudioEngine::DrumType>(type),
        static_cast<float>(velocity) / 127.0f);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSetDelayMix(
        JNIEnv* /* env */, jobject /* thiz */, jfloat mix) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) g_engine->setDelayMix(mix);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeGetDelayMix(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) return g_engine->getDelayMix();
    return 0.3f;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSetDelayFeedback(
        JNIEnv* /* env */, jobject /* thiz */, jfloat fb) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) g_engine->setDelayFeedback(fb);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeGetDelayFeedback(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) return g_engine->getDelayFeedback();
    return 0.3f;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSetReverbMix(
        JNIEnv* /* env */, jobject /* thiz */, jfloat mix) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) g_engine->setReverbMix(mix);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeGetReverbMix(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) return g_engine->getReverbMix();
    return 0.3f;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSetReverbRoomSize(
        JNIEnv* /* env */, jobject /* thiz */, jfloat room) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) g_engine->setReverbRoomSize(room);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeGetReverbRoomSize(
        JNIEnv* /* env */, jobject /* thiz */) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) return g_engine->getReverbRoomSize();
    return 0.5f;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeSetChannelGain(
        JNIEnv* /* env */, jobject /* thiz */, jint channel, jfloat gain) {
    // Stored in DawView for now; engine integration when per-channel separation added
    (void)channel; (void)gain;
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeExportWav(
        JNIEnv* env, jobject /* thiz */, jstring path, jint numBars) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine) {
        return env->NewStringUTF("error: engine not ready");
    }

    const char* cpath = env->GetStringUTFChars(path, nullptr);
    std::string outPath(cpath ? cpath : "");
    if (cpath) env->ReleaseStringUTFChars(path, cpath);

    // Render project to stereo buffer
    int sampleRate = 44100;
    double tempo = g_engine->getTempoBpm();
    double beatsPerSecond = tempo / 60.0;
    double secondsPerBeat = 1.0 / beatsPerSecond;
    int numBarsInt = static_cast<int>(numBars);
    if (numBarsInt < 1) numBarsInt = 1;
    if (numBarsInt > 32) numBarsInt = 32;

    int numBeats = numBarsInt * 4; // 4/4 time
    double durationSec = numBeats * secondsPerBeat;
    int numFrames = static_cast<int>(durationSec * sampleRate);

    std::vector<float> buffer(numFrames * 2, 0.0f);

    // Simple render: play all notes in all patterns
    auto song = g_engine->getSong();
    SimpleSynth synth;
    synth.setSampleRate(sampleRate);
    synth.setWaveform(g_engine->getWaveform());

    for (const auto& pat : song.patterns) {
        for (const auto& note : pat.notes) {
            double noteStartSec = note.start * secondsPerBeat / 960.0;
            int startFrame = static_cast<int>(noteStartSec * sampleRate);
            if (startFrame >= numFrames) continue;

            // Trigger note
            synth.noteOn(note.key, static_cast<int>(note.velocity * 127.0f));

            // Render note duration
            int noteDurationFrames = static_cast<int>(note.length * secondsPerBeat / 960.0 * sampleRate);
            if (noteDurationFrames < 1) noteDurationFrames = 1;

            std::vector<float> noteBuffer(noteDurationFrames * 2);
            synth.render(noteBuffer.data(), noteDurationFrames);

            // Mix into output
            for (int i = 0; i < noteDurationFrames * 2; ++i) {
                if (startFrame * 2 + i < static_cast<int>(buffer.size())) {
                    buffer[startFrame * 2 + i] += noteBuffer[i] * 0.5f;
                }
            }

            synth.noteOff(note.key);
        }
    }

    // Write WAV
    auto result = ::writeWav(outPath, sampleRate, buffer);
    if (result.ok) {
        return env->NewStringUTF("ok");
    } else {
        std::string err = "error: " + result.error;
        return env->NewStringUTF(err.c_str());
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeImportFlm(
        JNIEnv* env, jobject /* thiz */, jbyteArray fileBytes) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine) {
        return env->NewStringUTF("error: engine not ready");
    }

    jsize len = env->GetArrayLength(fileBytes);
    std::vector<std::uint8_t> bytes(len);
    env->GetByteArrayRegion(fileBytes, 0, len, reinterpret_cast<jbyte*>(bytes.data()));

    // Parse FLM
    auto parseResult = lynx::flm_android::parseFlm(bytes);
    if (!parseResult.ok) {
        return env->NewStringUTF(("error: " + parseResult.error).c_str());
    }

    // Convert to Song
    lynx::flm_android::FlmImportOptions opts;
    opts.projectFolder = ""; // Will be set by caller if needed
    auto songOpt = lynx::flm_android::importFlmProject(parseResult.project, opts);
    if (!songOpt) {
        return env->NewStringUTF("error: failed to convert FLM project");
    }

    // Apply to engine
    g_engine->applySong(*songOpt);

    return env->NewStringUTF("ok");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_danzfadlullah_lynx_MainActivity_nativeIsFlmFile(
        JNIEnv* env, jobject /* thiz */, jbyteArray fileBytes) {
    jsize len = env->GetArrayLength(fileBytes);
    if (len < 4) return JNI_FALSE;
    
    std::vector<std::uint8_t> bytes(4);
    env->GetByteArrayRegion(fileBytes, 0, 4, reinterpret_cast<jbyte*>(bytes.data()));
    
    return lynx::flm_android::isFlmData(bytes.data(), bytes.size()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

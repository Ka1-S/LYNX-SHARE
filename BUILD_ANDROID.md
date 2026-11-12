# Building LYNX SHARE for Android

## Prerequisites

- Android NDK r25 or later
- CMake 3.22+
- JUCE 8.0.4 (fetched automatically)
- Android SDK with API 24+ (Android 7.0 Nougat)

## Setup

### 1. Install Android NDK

```bash
# Using Android Studio SDK Manager or command line
sdkmanager "ndk;25.2.9519653"
```

Set the ANDROID_NDK environment variable:
```bash
export ANDROID_NDK=/path/to/android-ndk-r25c
```

### 2. Configure CMake for Android

Create a build directory and configure with the Android toolchain:

```bash
mkdir build-android
cd build-android

cmake .. \
    -DCMAKE_SYSTEM_NAME=Android \
    -DCMAKE_SYSTEM_VERSION=24 \
    -DCMAKE_ANDROID_NDK=$ANDROID_NDK \
    -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a \
    -DCMAKE_ANDROID_STL_TYPE=c++_shared \
    -DLYNX_BUILD_APP=ON \
    -DLYNX_BUILD_PLUGINS=OFF \
    -DLYNX_LTO=OFF \
    -GNinja
```

### 3. Build

```bash
ninja
```

Or use cmake --build:
```bash
cmake --build . --config Release
```

### 4. Create APK

After building the native library, you need to package it into an APK. This requires Gradle:

```bash
# Create a Gradle project structure
mkdir -p android/app/src/main/jniLibs/arm64-v8a
cp LYNX android/app/src/main/jniLibs/arm64-v8a/libLYNX.so

# Copy Java sources
cp -r ../src/platform/android/java android/app/src/main/java

# Copy Android resources
cp -r ../src/platform/android/res android/app/src/main/res
cp ../src/platform/android/AndroidManifest.xml android/app/src/main/AndroidManifest.xml
```

Create `android/app/build.gradle`:
```gradle
plugins {
    id 'com.android.application'
}

android {
    namespace 'com.ka1_s.lynx'
    compileSdk 34

    defaultConfig {
        applicationId "com.ka1_s.lynx"
        minSdk 24
        targetSdk 34
        versionCode 1
        versionName "0.1.26"
    }

    buildTypes {
        release {
            minifyEnabled false
        }
    }

    sourceSets {
        main {
            jniLibs.srcDirs = ['src/main/jniLibs']
        }
    }
}

dependencies {
    implementation 'androidx.appcompat:appcompat:1.6.1'
    implementation 'androidx.core:core-ktx:1.10.1'
}
```

Create `android/settings.gradle`:
```gradle
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "LYNX SHARE"
include ':app'
```

Create `android/build.gradle`:
```gradle
plugins {
    id 'com.android.application' version '8.2.0' apply false
    id 'org.jetbrains.kotlin.android' version '1.9.0' apply false
}
```

Build the APK:
```bash
cd android
./gradlew assembleRelease
```

The APK will be at `android/app/build/outputs/apk/release/app-release.apk`.

## Architecture Support

Currently only `arm64-v8a` is supported. To add 32-bit support:

```bash
cmake .. -DCMAKE_ANDROID_ARCH_ABI=armeabi-v7a
```

## Key Android-Specific Changes

### Audio
- Uses AAudio (API 26+) for low-latency audio with OpenSL ES fallback
- No VST/AU/LV2 plugin support (built-in plugins only)
- Sample rate: 44.1kHz or 48kHz (device dependent)

### File System
- Settings: `/data/data/com.ka1_s.lynx/files/LYNX`
- Cache: `/data/data/com.ka1_s.lynx/cache/LYNX`
- Documents: External storage (scoped storage on Android 10+)

### Permissions Required
- `RECORD_AUDIO` - for audio input
- `INTERNET` - for live collaboration
- `WAKE_LOCK` - keep CPU awake during playback
- `FOREGROUND_SERVICE` - background audio playback

### UI Adaptations
- Touch-friendly controls
- Virtual keyboard for text input
- Adaptive layout for different screen sizes
- No hover states (touch-only)

## Known Limitations

1. **No external plugin support** - Only built-in instruments and effects
2. **No ASIO/JACK/PipeWire** - Android uses AAudio/OpenSL ES only
3. **Limited file access** - Scoped storage on Android 10+
4. **No MIDI hardware support** - Virtual MIDI only
5. **Memory constraints** - Large projects may hit memory limits

## Testing

Run on a connected device:
```bash
adb install -r android/app/build/outputs/apk/release/app-release.apk
adb logcat -s LYNX_Audio LYNX_Lifecycle LYNX_JNI
```

## Debugging

View native logs:
```bash
adb logcat -s LYNX_Audio LYNX_Lifecycle LYNX_JNI DEBUG
```

Use Android Studio's native debugger for C++ debugging.

## Performance Tips

1. Use smaller buffer sizes (256 samples) for lower latency
2. Disable GPU rendering if experiencing graphics issues
3. Limit polyphony in synths for better performance
4. Use the "Simple" workflow for better touch experience
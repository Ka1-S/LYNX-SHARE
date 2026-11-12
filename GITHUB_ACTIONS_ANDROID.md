# GitHub Actions Build untuk LYNX SHARE Android

## File Workflow yang Dibuat

| File | Deskripsi |
|------|-----------|
| `.github/workflows/android-build.yml` | Workflow lengkap: native build + APK packaging + Release |

## Cara Menggunakan

### 1. Push ke GitHub (otomatis build)
```bash
git add .github/workflows/android-build.yml
git commit -m "Add GitHub Actions Android build workflow"
git push origin main
```
Build akan jalan otomatis di:
- Push ke `main`/`master`
- Pull request ke `main`/`master`
- Tag `v*` (misal: `v0.1.26`)

### 2. Manual Trigger (via GitHub UI)
1. Buka repository di GitHub
2. Tab **Actions** → **Build Android APK**
3. Klik **Run workflow**
4. Pilih:
   - **Build type**: `Release` atau `Debug`
   - **Android ABI**: `arm64-v8a` (default) atau `armeabi-v7a`
5. Klik **Run workflow**

### 3. Tag Release (produksi APK)
```bash
git tag v0.1.26
git push origin v0.1.26
```
Ini akan:
- Build APK
- Upload ke **GitHub Releases** dengan catatan otomatis
- APK bisa di-download dari halaman Releases

## Output Artifacts

| Trigger | Artifact | Retensi |
|---------|----------|---------|
| Push/PR | `lynx-share-android-arm64-v8a-Release` | 30 hari |
| Tag `v*` | APK di GitHub Releases | Permanen |
| Manual | Pilihan ABI/build type | 30 hari |

## Konfigurasi Signing (Untuk Release Produksi)

Tambahkan secrets di **Settings → Secrets and variables → Actions**:

| Secret | Deskripsi |
|--------|-----------|
| `KEYSTORE_PATH` | Path keystore (base64 encoded file, atau gunakan action untuk decode) |
| `KEYSTORE_PASSWORD` | Password keystore |
| `KEY_ALIAS` | Alias key |
| `KEY_PASSWORD` | Password key |

**Catatan**: Workflow saat ini build **unsigned APK** jika secrets tidak diset. Untuk Play Store butuh signed APK.

## Memory & Performance

Workflow membatasi:
```yaml
# CMake parallel jobs (2 untuk hemat RAM)
cmake --build build-android --parallel 2

# Gradle JVM heap
-Dorg.gradle.jvmargs="-Xmx2g"
```
Ini aman untuk runner GitHub (7GB RAM).

## Customization

### Ganti versi NDK
Edit `env.ANDROID_NDK_VERSION` di workflow:
```yaml
env:
  ANDROID_NDK_VERSION: r27  # atau r26d, r25c, dll
```

### Tambah ABI x86_64 (untuk emulator)
Tambah di matrix `build-multi-abi`:
```yaml
matrix:
  abi: [arm64-v8a, armeabi-v7a, x86, x86_64]
```

### Build Debug + Release sekaligus
Workflow `build-multi-abi` sudah build keduanya saat manual trigger.

## Troubleshooting

| Error | Solusi |
|-------|--------|
| `Native library not found` | Cek nama output: `LYNX` atau `libLYNX.so` |
| `Gradle out of memory` | Tambah `-Dorg.gradle.jvmargs="-Xmx3g"` |
| `NDK not found` | Pastikan `android-actions/setup-ndk@v2` jalan sebelum CMake |
| `Keystore not found` | Tambah secrets atau hapus `signingConfig` di `build.gradle.kts` |

## Link Cepat
- **Actions tab**: `https://github.com/<user>/<repo>/actions`
- **Releases**: `https://github.com/<user>/<repo>/releases`
- **Artifacts**: Di tiap run workflow, tab **Artifacts**

---

**Workflow sudah siap pakai!** Cukup push file `.github/workflows/android-build.yml` ke repo Anda.
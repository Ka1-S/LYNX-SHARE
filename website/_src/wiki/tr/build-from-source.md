---
source: edc71061e6
---
LYNX SHARE, JUCE 8 ile C++20 olarak yazılmıştır ve CMake ile derlenir. Kaynak kodu GNU Affero General Public License v3.0 altında [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) adresindedir. Sürüm dosyaları etiketli kaynaktan GitHub Actions ile derlenir; bu sayfaya yalnızca kodu değiştirmek ya da kendiniz için derlemek istiyorsanız ihtiyacınız var.

## Linux'ta derlemek

1. Aşağıda listelenen bağımlılıkları kurun.
2. Kaynağı `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` ile alın ve `lynx` klasörüne girin.
3. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` ile yapılandırın.
4. `cmake --build build` ile derleyin.

Ubuntu 24.04 için bağımlılıklar:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Windows'ta Visual Studio ile derlemek

Visual Studio 2022 Build Tools ("Desktop development with C++" ile), CMake 3.22 veya daha yenisi ve Git gerekir.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Windows için WSL'den veya Linux'tan derlemek

Projenin kendisi böyle geliştirilir: Windows programı Visual Studio olmadan Zig ile çapraz derlenir.

```
./scripts/build-windows-zig.sh
```

Sonuç `build-win/LYNX.exe` olur.

| Gereksinim | Sürüm | Varsayılan yer |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE şu komutla alınabilir:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Yollarınız farklıysa `ZIG` ve `JUCE_DIR` ortam değişkenlerini ayarlayın.

## macOS'ta derlemek

Xcode komut satırı araçları (`xcode-select --install`), CMake ve Ninja gerekir; örneğin `brew install cmake ninja` ile.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` evrensel bir uygulama üretir. Yalnızca bu Mac için derlemek istiyorsanız çıkarın.

## Çekirdek testlerini çalıştırmak

Çekirdek (model, sequencer, enstrümanlar, efektler ve `.flp` içe aktarma) sade C++'tır ve JUCE olmadan, herhangi bir C++20 derleyicisiyle test edilir.

```
./scripts/build-local.sh ["folder with .flp files"]
```

Çalıştırma `N checks, 0 failures` ile bitmelidir. Bir FL Studio 2026 kurulumunun `Data` klasörü verilirse FL Studio ile gelen `.flp` projelerini de ayrıştırır. Betik iki küçük araç da derler: bir `.flp` dosyasının içini döken `build-local/flpdump` ve `build-local/renderwav`.

Derlemeden sonra program, ses aygıtı olmadan kendini denetleyebilir:

```
LYNX.exe --selftest report.txt
```

Rapor `result=OK` ile biter. Daha fazla seçenek [Komut satırı](/wiki/command-line/) sayfasındadır.

## Kaynak ağacı

| Klasör | İçeriği |
|---|---|
| `src/core/` | Model, sequencer, enstrümanlar, efektler ve `.flp` içe aktarma. Sade C++, JUCE olmadan test edilebilir |
| `src/flp/` | `.flp` formatı okuyucusu |
| `src/engine/` | Ses motoru: aygıt, graf, sample'lar, plugin'ler, proje dosyaları |
| `src/ui/` | Arayüz: Channel Rack, Piano Roll, Playlist, Mixer, pencereler |
| `third_party/` | CLAP ve WavPack çözücüsü |
| `tests/` | Çekirdek testleri |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Mimari, durum, yol haritası ve FL Studio'nun davranış analizi |

## Bilmekte fayda var

- JUCE 8, MinGW'yi resmen desteklemez. Zig derleme betiği farkları dolanır. Düzeltmeler `cmake/mingw/` ve `src/platform/MingwFixes.cpp` içindedir.
- Aynı anda yalnızca bir `build-windows-zig.sh` çalıştırın. Aynı anda iki Zig derlemesi kilitlenebilir. Ayrı worktree'lerde paralel derleme için her birine `ZIG_GLOBAL_CACHE_DIR` ve `ZIG_LOCAL_CACHE_DIR` ile kendi önbelleğini verin.
- Bazı motorlar yerleşik sesleri ve tabloları sizin kendi FL Studio kurulumunuzdan okur. Depoda FL Studio'dan hiçbir şey yoktur.
- `docs/` içindeki belgelerin bir kısmı hâlâ Portekizce. Çevrilmekte.
- Değiştirilmiş bir sürümü, bir ağ hizmeti olarak da olsa dağıtırsanız lisans, kaynağını aynı lisansla yayımlamanızı şart koşar.

## İlgili sayfalar

- [Tam geliştirme ortamı (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [Linux'ta LYNX SHARE (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [Depo ve README dosyası](https://github.com/Ka1-S/LYNX-SHARE)
- [Komut satırı](/wiki/command-line/)
- [Dosya formatları](/wiki/file-formats/)

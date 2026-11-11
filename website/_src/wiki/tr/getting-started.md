---
source: a6d9063ec3
---
LYNX SHARE; Windows, macOS ve Linux için ücretsiz, açık kaynaklı bir DAW'dır. Pattern'lerle çalışır: onları Channel Rack ve Piano Roll'da programlar, Playlist'te aranje eder ve Mixer'de mixlersiniz. FL Studio projelerini (`.flp`) ve Ableton Live set'lerini (`.als`) açar, kendi `.lynx` formatında kaydeder.

Alfa sürümdedir. Hatalar olabilir, sık sık kaydedin.

## Çalıştırma

1. Sisteminizin desteklenip desteklenmediğini aşağıdaki tablodan kontrol edin.
2. İndirin ve kurun. [Kurulum](/wiki/install/) sayfasında her sistem için adımlar var.
3. Başlatın. Kısa bir kurulum sihirbazı dili seçer, ses çıkışını dener ve arka planda taranacak plugin klasörlerini listeler. Sihirbazı atlayabilir, sonra `Seçenekler › Kurulum sihirbazı (dil, ses, plugin klasörleri)...` ile yeniden açabilirsiniz.
4. Hiçbir şey duymuyorsanız [Ses ayarı](/wiki/audio-setup/) sayfasına gidin.
5. [Pencerenin turu](/wiki/tour/) sayfasını okuyun, sonra [İlk şarkınızı](/wiki/first-song/) yapın.

## Neye ihtiyacınız var

| Sistem | Gereksinim |
|---|---|
| Windows | Windows 10 veya 11, 64 bit |
| macOS | macOS 11 Big Sur veya daha yenisi, Intel ya da Apple silicon |
| Linux | glibc 2.35 veya daha yenisiyle x86_64 (Ubuntu 22.04+, Debian 12+, Fedora 36+, güncel Arch veya Manjaro), X11 veya XWayland |

Bir ses arayüzüne ihtiyacınız yok. LYNX SHARE, sisteminizin zaten kullandığı ses aygıtı üzerinden çalar. FL Studio'ya da ihtiyacınız yok. Aynı bilgisayarda kuruluysa LYNX SHARE onun sample'larını bulur ve `.flp` projeleri için aynı plugin dosyalarını kullanır.

Üçüncü taraf plugin'ler isteğe bağlıdır. LYNX SHARE'in [katalogda](/instruments-and-effects/) listelenen kendi enstrümanları ve efektleri var.

## Çalışmanızı kaydedin

`Ctrl+S` kaydeder, `Ctrl+Shift+S` yeni adla kaydeder, `Ctrl+Alt+S` yeni bir sürüm kaydeder (Song_2, Song_3 ve böyle devam eder). Bir otomatik kayıt vardır; `Dosya › Otomatik kayıt klasörünü aç` ve `Dosya › Son yedeğe dön...` ile ulaşılır.

`Dosya › Şablondan yeni` dört başlangıç noktası sunar: Empty, Basic beat, Trap with 808 ve House.

## Demo projesi

Windows kurulum dosyası ve zip'i, macOS `.dmg` ve Linux `.tar.gz` bir demo projesi, `Demo.lynx`, içerir. `Dosya › Aç...` (`Ctrl+O`) ile açın. Windows'ta kurulum klasöründe, `LYNX.exe`'nin yanındadır.

Onu bir terminalden de üretebilirsiniz. Bkz. [Komut satırı](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Sonra nereye

| İstediğiniz | Okuyun |
|---|---|
| Her pencereyi öğrenmek | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Başka bir programdan proje açmak | [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/), [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/) |
| VST, CLAP, LV2 veya AU plugin'lerinizi kullanmak | [Plugin'ler](/wiki/plugins/) |
| Bir tuşa bakmak | [Kısayollar](/wiki/shortcuts/) |
| Bir sorunu çözmek | [Sorun giderme](/wiki/troubleshooting/) ve [SSS](/faq/) |

## Bilmekte fayda var

- Program dosyası Windows'ta `LYNX.exe`, macOS ve Linux'ta `LYNX` adını taşır. Ayarlar klasörünün adı da `LYNX`'tir.
- Windows ve macOS'ta ilk açılışta bir sistem uyarısı çıkabilir, çünkü Windows sürümü henüz kod imzalı değil ve macOS sürümü notarize edilmemiş. [Kurulum](/wiki/install/) sayfası nelere tıklanacağını açıklar.
- LYNX SHARE'in dilini bulundurmadığı bir sistemde arayüz varsayılan olarak İngilizcedir. Bunu `Seçenekler › Genel ayarlar...` içinde değiştirebilirsiniz. Yeni dil yeniden başlatmadan sonra uygulanır.
- FL Studio projelerindeki FLEX paketleri şifrelidir ve asla açılmaz. O kanallar, preset adına göre seçilen bir synth patch'i alır.

## İlgili sayfalar

- [Kurulum](/wiki/install/)
- [Ses ayarı](/wiki/audio-setup/)
- [Pencerenin turu](/wiki/tour/)
- [İlk şarkınız](/wiki/first-song/)
- [İndir](/download/)

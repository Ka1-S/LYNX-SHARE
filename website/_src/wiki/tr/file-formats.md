---
source: a79e43f031
---
LYNX SHARE projeleri `.lynx` dosyası olarak kaydeder. Ayrıca başka programların projelerini açar, yaygın ses dosyalarını yükler ve ses, MIDI ve diğer programlar için projeleri dışa aktarır. Tablo, 0.1.26 alpha sürümünün bütün formatlarını listeler.

Başka bir programın formatından açılan proje bir içe aktarmadır. `Dosya › Kaydet` o zaman bir `.lynx` dosyasını nereye yazacağını sorar ve özgün dosya asla değişmez.

## Başka bir programın projesini açmak

1. `Dosya › Aç...` (`Ctrl+O`) yolunu seçin.
2. Bir `.flp`, `.als`, `.flm`, `.mid` veya `.zip` dosyası seçin.
3. Bir `.flp` için nelerin yaklaşık karşılandığını veya dışarıda bırakıldığını okumak üzere `Yardım › Last FL Studio import report` yolunu seçin.
4. `Dosya › Kaydet` yolunu seçin ve `.lynx` dosyasının yazılacağı yeri seçin.

## Formatlar

| Format | Açma | Kaydetme / Dışa aktarma | Notlar |
|---|---|---|---|
| `.lynx` | `Dosya › Aç...` | `Dosya › Kaydet`, otomatik kayıtla birlikte | LYNX SHARE'in kendi proje formatı (JSON) |
| `.zip` | `Dosya › Aç...` onu bir klasöre çıkarır ve içindeki projeyi açar | `Dosya › Dışa aktar › Sample'larla proje (.zip)...` | FL Studio'nun sıkıştırılmış loop paketini de açar |
| `.lynxpack` | Bir özetten sonra açılır. Bir parti, host'un projesine birleştirilir | `Dosya › Çevrimiçi oturum › Projeyi gönder...` | Bir [çevrimiçi oturumda](/wiki/online-sessions/) gönderilen proje veya parti. Üçüncü taraf plugin partileri, ses render'ları ve plugin ayarlarıyla birlikte taşınır |
| `.flp` | `Dosya › Aç...` | `Dosya › Dışa aktar › FL Studio projesi...`, bir FL Studio 2026 projesi veya sıkıştırılmış loop paketi | Yerel plugin'ler, değişen doğrulukta yeniden oluşturulmuş motorlarda çalar. FLEX paketleri asla açılmaz. Dışa aktarırken yalnızca LYNX'e özgü enstrümanlar ve efektler sese render edilir ya da en yakın FL plugin'iyle değiştirilir, modülatörler dışarıda bırakılır. Bkz. [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/) |
| `.flm` | `Dosya › Aç...` | Hayır | FL Studio Mobile projesi. Kısmi: MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, Slicer ve DirectWave dışındaki enstrümanlar dahili synth ile çalar |
| `.als` | `Dosya › Aç...`, Live 11 ve 12 setleri | `Dosya › Dışa aktar › Ableton Live seti (.als)...`, Live 12 veya Live 11 için | İçe aktarırken Live'ın LYNX SHARE karşılığı olmayan cihazları atılır. Dışa aktarırken LYNX SHARE'in kendi enstrümanları ve efektleri dönüştürülmez, efekt parametresi otomasyonu ve insert'ten insert'e yönlendirmeler atılır. Bkz. [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `Dosya › Aç...` yeni bir proje oluşturur. `Dosya › İçe aktar › MIDI dosyası...` ve Piano Roll mevcut projeye içe aktarır | `Dosya › Dışa aktar › MIDI dosyası...` (`Ctrl+Shift+M`) | |
| `.wav` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › Wave dosyası...`, 16 bit, 24 bit veya 32 bit float | İsteğe bağlı dither, tempo bilgisi, döngü işaretçileri ve Playlist işaretçileri |
| `.mp3` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › MP3 dosyası...` | Sabit bit hızı |
| `.ogg` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › OGG dosyası...` | |
| `.flac` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › FLAC dosyası...`, sıkıştırma 0 ile 8 arası | Her düzeyde kayıpsız |
| `.aif`, `.aiff` | Sample veya ses klibi olarak | Hayır | |
| `.wv` | Sample olarak | Hayır | FL Studio'nun paketlerinde gelen WavPack sample'ları |
| `.sf2`, `.sf3` | LYNX Soundfont ile çalınır | Hayır | SoundFont'lar |
| `.fst` | Browser'dan | Hayır | Bir FL Studio yerel plugin'inin preset'i. Yalnızca LYNX SHARE'in motoru olan plugin'ler için |
| `.fsc` | Piano Roll ve Olay editörü | Piano Roll ve Olay editörü | FL Studio skor ve otomasyon dosyası |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | Barındırılan bir plugin'in preset menüsündeki `Preset dosyası yükle...` | `Preset kaydet...` ve `Preset dosyası dışa aktar...` | Üçüncü taraf plugin preset'leri. `.fxp` ve `.fxb` VST2 plugin'leri, `.vstpreset` VST3 plugin'leri içindir ve yalnızca plugin LYNX SHARE içinde çalışırken geçerlidir |
| Video dosyası | `Dosya › İçe aktar › Video dosyası...` bir videoyu Playlist'e koyar | Dışa aktarma penceresindeki `Bu miksle bir video da yaz` | İçe aktarma Windows ve macOS'ta sistem çözücülerini kullanır, Linux'ta FFmpeg gerektirir. Dışa aktarma FFmpeg gerektirir |

## Diğer dışa aktarmalar

| Komut | Ne yapar |
|---|---|
| Dışa aktarma penceresindeki `Dosyalar` seçeneği | Stem'ler: her mixer insert'i, her kanal veya yalnızca enstrümanlı her kanal için bir dosya |
| `Dosya › Dışa aktar › Tüm Playlist izleri` | Her Playlist izi için bir dosya |
| `Dosya › Dışa aktar › Tüm Playlist iz grupları` | Her iz grubu için bir dosya |
| `Dosya › Dışa aktar › Masterla...` | Projeyi render eder ve bilgisayarınızda bir ses yüksekliği hedefine masterlar |
| `Dosya › Dışa aktar › Proje kemikleri...` | Kanal ve efekt preset'lerini ve skorları bir klasöre yazar |

Dışa aktarma penceresi şarkıyı, geçerli pattern'i ya da Playlist seçimini render eder.

## Komut satırından

Windows'ta program `LYNX.exe` adındadır. macOS ve Linux'ta `LYNX` adındadır.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` ve `--export-flp` girdi olarak bir `.als` de kabul eder.

## Bilmekte fayda var

- 30 saniyeden uzun ses dosyaları bir disk önbelleğinden akıtılır.
- Kaydettiğinizde bir `.flp`, `.als` veya `.flm` asla yazılmaz. Dışa aktarma komutları yeni dosyalar yazar.
- `.wv` dosyaları yalnızca okunur.
- Bir `.zip` önce bir klasöre çıkarılır, sonra içindeki proje açılır.

## İlgili sayfalar

- [Proje içe aktarma](/wiki/importing-projects/)
- [Dışa aktarma](/wiki/exporting/)
- [Komut satırı](/wiki/command-line/)
- [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/)
- [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/)

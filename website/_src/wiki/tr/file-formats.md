---
source: e1ff018baf
---
LYNX SHARE projeleri `.lynx` dosyası olarak kaydeder. Ayrıca başka programların projelerini açar, yaygın ses dosyalarını yükler ve ses, MIDI ve diğer programlar için projeleri dışa aktarır. Tablo, 0.1.31 alpha sürümünün bütün formatlarını listeler.

Başka bir programın formatından açılan proje bir içe aktarmadır. `Dosya › Kaydet` o zaman bir `.lynx` dosyasını nereye yazacağını sorar ve özgün dosya asla değişmez.

## Başka bir programın projesini açmak

1. `Dosya › Aç...` (`Ctrl+O`) yolunu seçin.
2. Bir `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid` veya `.zip` dosyası seçin.
3. Nelerin yaklaşık karşılandığını veya dışarıda bırakıldığını okumak üzere `Yardım › Son proje içe aktarma raporu` yolunu seçin.
4. `Dosya › Kaydet` yolunu seçin ve `.lynx` dosyasının yazılacağı yeri seçin.

## Formatlar

| Format | Açma | Kaydetme / Dışa aktarma | Notlar |
|---|---|---|---|
| `.lynx` | `Dosya › Aç...` | `Dosya › Kaydet`, otomatik kayıtla birlikte | LYNX SHARE'in kendi proje formatı (JSON). `Dosya › Eski bir LYNX için kopya kaydet...`, 0.1.27 veya 0.1.28'in açabildiği bir kopya yazar ve o sürümün tutamadıklarını listeler |
| `.zip` | `Dosya › Aç...` onu bir klasöre çıkarır ve içindeki projeyi açar | `Dosya › Dışa aktar › Sıkıştırılmış loop paketi...` | FL Studio'nun sıkıştırılmış loop paketini de açar |
| `.lynxpack` | Bir özetten sonra açılır. Bir parti, host'un projesine birleştirilir | `Dosya › Çevrimiçi oturum › Projeyi gönder...` | Bir [çevrimiçi oturumda](/wiki/online-sessions/) gönderilen proje veya parti. Üçüncü taraf plugin partileri, ses render'ları ve plugin ayarlarıyla birlikte taşınır |
| `.flp` | `Dosya › Aç...` | `Dosya › Dışa aktar › FL Studio projesi...`, bir FL Studio 2026 projesi veya sıkıştırılmış loop paketi | Yerel plugin'ler, değişen doğrulukta yeniden oluşturulmuş motorlarda çalar. FLEX paketleri asla açılmaz. Dışa aktarırken yalnızca LYNX'e özgü enstrümanlar ve efektler sese render edilir ya da en yakın FL plugin'iyle değiştirilir, modülatörler dışarıda bırakılır. Bkz. [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/) |
| `.flm` | `Dosya › Aç...` | `Dosya › FL Studio Mobile projesi (.flm) olarak kopya kaydet...` | FL Studio Mobile projesi. Kısmi: MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, Slicer ve DirectWave dışındaki enstrümanlar dahili synth ile çalar. Dışa aktarma bir kopyadır: notaları, tempoyu, mixer'ı, FL Studio Mobile'ın kendi enstrümanlarını, DW Sampler'ı, 3x Osc'yi, davul setlerini, efektleri ve mixer seviyesi, pan, efektler, DW Sampler ile GMSynth otomasyonunu korur, sample'lar yanında durur. Kopyada diğer enstrümanlar notalarını MiniSynth üzerinde tutar, FLM modülü olmayan efektler ve diğer hedeflerin (synth'lerin kendi düğmeleri, üçüncü taraf plugin'ler) otomasyonu dışarıda kalır; kaydetmeden önce bir pencere hepsini listeler. |
| `.als` | `Dosya › Aç...`, Live 11 ve 12 setleri | `Dosya › Dışa aktar › Ableton Live seti (.als)...`, Live 12 veya Live 11 için | İçe aktarırken Live'ın LYNX SHARE karşılığı olmayan cihazları atılır. Dışa aktarırken LYNX SHARE'in kendi enstrümanları ve efektleri dönüştürülmez (Helix, Wavetable olur), efekt parametresi otomasyonu ve insert'ten insert'e yönlendirmeler atılır. Bkz. [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/) |
| `.dawproject` | `Dosya › Aç...` | `Dosya › Dışa aktar › DAWproject (.dawproject)...` | Bitwig, Studio One, Cubase ve diğer uygulamaların açık alışveriş formatı: track'ler, klasörler, klipler, notalar, warp işaretleriyle ses, otomasyon, tempo, işaretler, send'li mixer ve durumlarıyla VST3, VST2 ve CLAP plugin'leri. İçe aktarırken nota efektleri ve MIDI ifadeleri dışarıda kalır; dışa aktarırken klip ızgarası, diğer aranjmanlar ve nota başına bazı ayrıntılar. Her iki yönde de nelerin dışarıda kaldığı listelenir |
| `.band` | `Dosya › Aç...` (paket, içindeki `projectData` dosyası veya onun zip'i) | Hayır | GarageBand 10 veya iOS için GarageBand projesi, Basit iş akışı track'leri olarak. Kısmi: enstrüman track'leri, iOS için GarageBand'de yapılan Sampler enstrümanları dışında dahili synth'i ve davulları çalar. GarageBand 11 ve öncesinin projeleri okunamaz |
| `.blx` | `Dosya › Aç...` veya klasörü, Basit iş akışının `Klasör...` içe aktarmasıyla | Hayır | Cakewalk veya Sonar'ın indirdiği BandLab projesi, region'lar, tempo, tonalite, mix, ses seviyesi ve pan otomasyonu ile yaygın efektlerle Basit iş akışı track'leri olarak. Kısmi: BandLab'in enstrüman sesleri ve diğer pedalları içe aktarma raporunda listelenir |
| `.mid`, `.midi` | `Dosya › Aç...` yeni bir proje oluşturur. `Dosya › İçe aktar › MIDI dosyası...` ve Piano Roll mevcut projeye içe aktarır | `Dosya › Dışa aktar › MIDI dosyası...` (`Ctrl+Shift+M`) | |
| `.mod`, `.xm`, `.s3m`, `.it` | `Dosya › Aç...` | Hayır | Tracker modülleri (ProTracker, FastTracker 2, Scream Tracker 3, Impulse Tracker). Enstrümanlar sample'larıyla birlikte Sampler kanallarına, şarkı tempo değişiklikleriyle birlikte pattern kliplerine dönüşür ve `Görünüm › Tracker` notaları düzenler. Kısmen: slide, vibrato ve arpej gibi efektler, zarflar ve çalan bir notadaki ses değişiklikleri içe aktarma raporunda listelenir |
| `.wav` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › Wave dosyası...`, 16 bit, 24 bit veya 32 bit float | İsteğe bağlı dither, tempo bilgisi, döngü işaretçileri ve Playlist işaretçileri |
| `.mp3` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › MP3 dosyası...` | Sabit bit hızı |
| `.ogg` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › OGG dosyası...` | |
| `.flac` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › FLAC dosyası...`, sıkıştırma 0 ile 8 arası | Her düzeyde kayıpsız |
| `.aif`, `.aiff` | Sample veya ses klibi olarak | `Dosya › Dışa aktar › AIFF dosyası...`, 16 bit veya 24 bit | |
| `.m4a` | – | `Dosya › Dışa aktar › M4A dosyası...` | Yalnızca dışa aktarma. Windows ve macOS'ta sistemin kodlayıcısıyla AAC; Linux'ta sunulmaz |
| `.wv` | Sample olarak | Hayır | FL Studio'nun paketlerinde gelen WavPack sample'ları |
| `.sf2`, `.sf3` | LYNX Soundfont ile çalınır | Hayır | SoundFont'lar |
| `.dwp`, `.dwb` | Browser'da çift tıklama, Channel Rack'e bırakma veya DirectWave'in `Program / bank aç...` komutu | DirectWave'in program ve banka kaydı | DirectWave programları ve bankaları. Sample'lar DirectWave'in aradığı yerlerde aranır |
| `.gig` | Bir DirectWave kanalı olarak | Hayır | GigaStudio enstrümanları: velocity katmanları, round robin, loop'lar, akort ve amp zarfı. Release-trigger sample'ları, keyswitch'ler ve sıkıştırılmış sample'lar henüz okunmaz |
| `.ncw` | Sample veya ses klibi olarak | Hayır | Kontakt'ın sıkıştırılmış sample'ları. Kontakt `.nki` enstrümanları için Kontakt'ın kendisi plugin olarak gerekir; korumalı kütüphanelerin şifresi asla çözülmez |
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
| `Dosya › Dışa aktar › Diğer uygulamalar için...` | Stem'ler, MIDI dosyaları, bir Ableton Live seti, bir FL Studio projesi, mixdown ve bir README içeren tek bir klasör, isterseniz zip'li |
| `Dosya › Dışa aktar › Proje kemikleri...` | Kanal ve efekt preset'lerini ve skorları bir klasöre yazar |

Dışa aktarma penceresi şarkıyı, geçerli pattern'i ya da Playlist seçimini render eder.

## Komut satırından

Windows'ta program `LYNX.exe` adındadır. macOS ve Linux'ta `LYNX` adındadır.

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` ve `--export-flp` girdi olarak bir `.als` de kabul eder.

## Bilmekte fayda var

- 30 saniyeden uzun ses dosyaları bir disk önbelleğinden akıtılır.
- Kaydettiğinizde bir `.flp`, `.als`, `.flm`, `.dawproject`, `.band` veya `.blx` asla yazılmaz. Dışa aktarma komutları yeni dosyalar yazar.
- `.wv` dosyaları yalnızca okunur.
- Bir `.zip` önce bir klasöre çıkarılır, sonra içindeki proje açılır.

## İlgili sayfalar

- [Proje içe aktarma](/wiki/importing-projects/)
- [Dışa aktarma](/wiki/exporting/)
- [Komut satırı](/wiki/command-line/)
- [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/)
- [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/)

---
source: b05790879f
---
![WAV seçili dışa aktarma penceresi: solda aralık, kuyruk ve dosyalar, sağda format, altta miks seçenekleri](/assets/img/wiki/export.webp)

LYNX SHARE'den çıkan her şey `Dosya › Dışa aktar` üzerinden geçer. Ses formatları (WAV, MP3, OGG, FLAC, AIFF ve M4A) tek bir dışa aktarma penceresini paylaşır. MIDI, FL Studio projeleri, Ableton Live setleri, DAWproject dosyaları ve sıkıştırılmış projelerin aynı menüde kendi girişleri vardır.

## Şarkıyı WAV olarak dışa aktarmak

1. `Dosya › Dışa aktar › Wave dosyası...` yolunu seçin.
2. `Aralık` ayarını `Şarkı (Playlist)`, `Dosyalar` ayarını `Tek dosya (mix)` üzerinde bırakın.
3. Bir `Bit derinliği` seçin: 16 bit, 24 bit veya 32 bit (float).
4. Reverb'ler ve release'ler son ölçüden sonra da sönsün diye `Kuyruk` ayarını `Kalanı bırak` yapın.
5. `Dışa aktar...` düğmesine tıklayın, bir ad ve klasör seçin, render'ın bitmesini bekleyin.
6. İş bitince LYNX SHARE dosyanın nereye gittiğini ve tepe seviyesini bildirir.

`Ctrl+R`, pencereyi en son dışa aktardığınız formatla açar. `Ctrl+Shift+R` MP3 üzerinde açar.

## Neyin render edileceği

| Ayar | Ne yapar |
|---|---|
| `Aralık` | `Şarkı (Playlist)`, `Geçerli pattern` ya da `Playlist seçimi` |
| `Çal` | Aralığın dosyada kaç kez çalacağı, `Bir kez` ile 16 kez arası |
| `Kuyruk` | `Kalanı kes` tam sonda durur. `Kalanı bırak` sesin sönmesine izin verir. `Kalanı başa sar` kuyruğu başa karıştırır, loop'lar için |
| `En uzun kuyruk` | 3, 6, 12 veya 30 saniye. Ses daha önce sönerse kuyruk da daha önce biter |
| `Dosyalar` | Tek dosya veya stem'ler (aşağıya bakın) |

## Çıkış formatı

Düğmeler formatı seçer. M4A yalnızca sistemde bir AAC kodlayıcı olduğunda sunulur: Windows ve macOS'ta var, Linux'ta yok. `Ayrıca kaydet`, aynı render'dan aynı adla başka formatlar da yazar.

| Format | Seçenekler |
|---|---|
| WAV | `Bit derinliği`: 16 bit, 24 bit veya 32 bit (float). `Dither`: `Kapalı`, `TPDF` ya da `TPDF + noise shaping` |
| MP3 | `Bit hızı`, sabit |
| OGG | Bit hızı |
| FLAC | `Bit derinliği`, `0 (en hızlı)` ile `8 (en küçük)` arasında `Sıkıştırma`, `Dither`. Her düzeyde kayıpsız |
| AIFF | `Bit derinliği`: 16 bit veya 24 bit. `Dither` |
| M4A | `Bit hızı`: 96, 128, 160 veya 192 kbps. AAC, sistemin kodlayıcısıyla yazılır |

`Örnekleme hızı` ve `Kanallar` her format için geçerlidir. `Kanallar`; `Stereo`, `Mono (iki taraf karıştırılmış)`, `Mono (yalnızca sol)` veya `Mono (yalnızca sağ)` olabilir.

## Miks seçenekleri

| Ayar | Ne yapar |
|---|---|
| `Normalize et (tepe 0 dB'ye)` | En yüksek tepesi tam ölçeğe ulaşsın diye tüm dosyayı yükseltir veya alçaltır |
| `Master efektleri` | Kapalı: Master insert'inin efektleri bypass edilir |
| `Insert efektleri` | Kapalı: diğer insert'lerin efektleri bypass edilir |
| `Tüm plugin'ler için HQ` | Plugin'ler render kalitelerini kullanır. Kapalı: dışa aktarma çalma kalitesini kullanır |
| `Plugin ısınması` | Üçüncü taraf plugin'ler, dışa aktarma başlamadan önce kısa bir sessizliği işler |
| `Maks. polifoniyi kapat` | Kanalların ses sayısı sınırını kaldırır, böylece hiçbir nota kesilmez |
| `Gecikme telafisinin sessizliğini kırp` | Plugin gecikme telafisinin başa eklediği sessizliği kaldırır |
| `Tempo bilgisi`, `Döngü işaretleri`, `Çalma listesi işaretleri` | Yalnızca WAV: tempoyu, döngüyü ve Playlist işaretçilerini dosyada saklar |
| `Bitince dosya(lar)ı göster` | Sonuçta dosya yöneticisini açar |

## Stem'ler ve izler

![FLAC için ayarlanmış, her mixer insert'i başına bir dosya olan, toplam dokuz dosyalık dışa aktarma penceresi](/assets/img/wiki/export-flac-stems.webp)

`Dosyalar` ayarının üç stem modu vardır:

- `Mixer insert'i başına bir (stem'ler)`: ses alan her insert için bir dosya.
- `Kanal başına bir (stem'ler)`: çalan her kanal için bir dosya.
- `Kanal başına bir, yalnız enstrümanlar (send ve bus olmadan)`.

Stem'lerin yanında miksi de almak için `Tam mix de` kutusunu işaretleyin. Alttaki satır kaç dosya yazılacağını gösterir.

Şarkının Sections şeridinde bölümleri varsa `Bölüm başına bir dosya` onu bölümlerden keser ve her dosyayı kendi bölümünün adıyla adlandırır. Stem'lerde her bölüm kendi setini alır.

`Dosya › Dışa aktar › Tüm Playlist izleri` ve `Tüm Playlist iz grupları`, her Playlist izi ya da her grup için bir dosya yazar. Mixer'ın menüsündeki `Diske kayıt › Kayda hazır kanalları WAV dosya(lar)ına render al...` (`Alt+R`), dışa aktarma penceresini yalnızca kayda hazır insert'ler için açar.

## Export menüsünün diğer girişleri

| Komut | Tuş | Ne yapar |
|---|---|---|
| `MIDI dosyası...` | `Ctrl+Shift+M` | Notaları standart bir MIDI dosyası (`.mid`) olarak yazar |
| `Masterla...` | `Ctrl+Alt+M` | Projeyi render eder ve bilgisayarınızda LUFS cinsinden bir ses yüksekliği hedefine masterlar |
| `Sıkıştırılmış loop paketi...` | yok | Proje ve kullandığı her sample tek bir `.zip` içinde, FL Studio'nun sıkıştırılmış loop paketi olarak |
| `Proje veri dosyaları...` | yok | Projenin kullandığı her dosyayı seçtiğiniz bir klasöre kopyalar; proje kendi dosyalarını kullanmaya devam eder |
| `FL Studio projesi...` | yok | Bir `.flp` ya da her sample'ı içeren sıkıştırılmış loop paketi |
| `Ableton Live seti (.als)...` | yok | Live 12 veya Live 11 için bir set |
| `DAWproject (.dawproject)...` | yok | Bitwig Studio, Studio One, Cubase ve DAWproject okuyan diğer uygulamalar için bir proje |
| `Diğer uygulamalar için...` | yok | Stem'ler, MIDI dosyaları, bir Live seti, bir FL Studio projesi, mixdown ve bir README içeren tek bir klasör, isterseniz zip'li |
| `Proje kemikleri...` | yok | Kanal ve efekt preset'leri ile skorlar, bir klasörde |

## FL Studio'ya, Ableton Live'a ve DAWproject'e dışa aktarmak

`.flp` bir FL Studio 2026 projesidir. Sampler'lar, ses klipleri, FL Studio'nun kendi jeneratörleri ve efektleri, üçüncü taraf plugin'ler, pattern'ler, Playlist, Mixer ve otomasyon ayarlarıyla birlikte geçer. FL Studio'da karşılığı olmayan şeyler için `Sese render et (aynı ses)` ile `En yakın FL eklentisini kullan (düzenlenebilir kalır, farklı duyulabilir)` arasında seçim yaparsınız. İkinci seçenekte yalnızca LYNX'e özgü enstrümanlar dışarıda kalır. Şarkı konumunu izleyen modülatörler otomasyon klipleri olur: senkronlu bir LFO FL Studio'nun klip LFO'su olur; adımlar, rastgele kaynaklar ve makrolar noktalara dönüşür. Takipçiler, zarflar, keytrack ve velocity dışarıda bırakılır. Bir rapor, nelerin render edildiğini, değiştirildiğini ya da dışarıda bırakıldığını listeler. Ayrıntılar [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/) sayfasında.

`.als`, her kanal için bir MIDI izi, ses izleri, gruplar, return'ler, send'ler, rack'ler ve Drum Rack'ler, durumlarıyla VST2 ve VST3 plugin'leri, işaretçiler ile ses, pan ve tempo otomasyonunu içerir. LYNX SHARE'in kendi enstrümanları ve efektleri dönüştürülmez, ama izleri ve notaları korunur. İstisna Helix'tir: Live'ın Wavetable'ı olur. Efekt parametrelerinin otomasyonu ve insert'ten insert'e yönlendirmeler atılır. Ayrıntılar [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/) sayfasında.

`.dawproject`; track'leri, klasörleri, klipleri, notaları, warp işaretçileriyle sesi, otomasyonu, tempoyu, işaretçileri, send'li mixer'ı ve durumlarıyla VST3, VST2 ve CLAP plugin'lerini içerir. LYNX SHARE'in kendi enstrümanları ve efektleri, LYNX SHARE'in onları yeniden açabileceği şekilde yazılır. Klip ızgarası, diğer aranjmanlar ve nota başına bazı ayrıntılar dışarıda kalır ve bir rapor bunları listeler.

`Dosya › FL Studio Mobile projesi (.flm) olarak kopya kaydet...`, FL Studio Mobile için bir `.flm` yazar ve sample'ları yanındaki `<name> Samples` klasörüne kopyalar. Kaydetmeden önce bir pencere, `Dosya › Eski bir LYNX için kopya kaydet...` gibi, kopyanın neleri kaybettiğini listeler. `.flm` notaları ve pattern'leri (FL Studio Mobile'da track başına bir pattern vardır), tempoyu, ölçü başına vuruş sayısını, tonu ve akor setini, mixer'ın seviyelerini, panını, mute ve solo durumunu ve bus'lara gönderimleri, FL Studio Mobile'ın kendi enstrümanlarını (MiniSynth, SuperSaw, GMSynth ve Transistor Bass ile FL Studio'nun MiniSynth ve Transistor Bass'ı), davul sampler'larını bir davul seti olarak, Layer'ları birkaç enstrümanlı bir rack olarak, her Pocket (FLM) efektini mix'iyle ve ses kliplerini seviyesi, panı, perdesi ve ters çevirmesiyle korur. Ayrıca DW Sampler'ı (bölgeleriyle) ve 3x Osc'yi, mixer seviyesi ve panı, Pocket (FLM) efektleri ile DW Sampler ve GMSynth'in kendi değerleri için otomasyon kliplerini yazar. Diğer enstrümanlar (şimdilik FL Studio Mobile'ın Slicer'ı ve SoundFont Player'ı da) notalarını FL Studio Mobile'ın MiniSynth'i üzerinde tutar, FLM modülü olmayan efektler ve synth'lerin kendi düğmeleri ile üçüncü taraf plugin'ler gibi diğer hedeflerin otomasyonu dışarıda kalır; hepsi listelenir.

## Bilmekte fayda var

- MP3, OGG, M4A ve 32 bit WAV için `Dither` soluktur; WAV dışındaki her format için tempo ve işaretçi seçenekleri de soluktur.
- `Playlist seçimi` seçiliyken Playlist'te seçim yoksa şarkının tamamı render edilir. Pencere sizi uyarır.
- Master insert'i sessizse pencere, dosyanın sessiz olacağı konusunda uyarır.
- `Bu miksle bir video da yaz` yalnızca projede bir video izi varsa görünür ve FFmpeg gerektirir.
- `Donanım üzerinden gerçek zamanlı render al` yalnızca projede etkin bir donanım insert'i varsa görünür. Bu dışa aktarma şarkı kadar sürer.
- Bir insert'in ses klibine bounce edildiği Render to audio, bu sürümde yerine geçtiği insert'ten yaklaşık 5 dB daha yüksek çalar.

## İlgili sayfalar

- [Dosya formatları](/wiki/file-formats/)
- [Komut satırı](/wiki/command-line/)
- [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/)
- [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)

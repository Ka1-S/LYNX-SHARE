---
source: 37920663db
---
![Klip başlatma yerleşimi: Playlist'in yanında sahneli bir klip ızgarası](/assets/img/cliplaunch.webp)

LYNX SHARE Pattern iş akışıyla açılır: bir Channel Rack, pattern'ler ve bir Playlist. Klip başlatma iş akışı isteğe bağlıdır ve Playlist'in yanına sahneli bir klip ızgarası ekler. LYNX SHARE `.als` set'lerini okur ve yazar. Ableton ile bağlantısı yoktur.

Bu sayfa Bitwig Studio kullanıcıları için de geçerlidir: klip ızgarası aynı şekilde çalışır ve Bitwig'in DAWproject dosyaları (`.dawproject`) `Dosya › Aç...` ile açılır, `Dosya › Dışa aktar › DAWproject (.dawproject)...` ile yazılır.

## İlk adımlarınız

1. **Yerleşimi seçin.** LYNX SHARE ilk açıldığında bir karşılama kartı nereden geldiğinizi sorar. `Ableton Live veya Bitwig` düğmesine tıklayın: Klip başlatma yerleşimi açılır. Daha sonra `Görünüm › Yerleşim stili › Klip başlatma iş akışı` istediğiniz zaman ona geçirir.
2. **Bir yuvayı doldurun.** Browser'dan bir sample'ı veya pattern'i bir yuvaya sürükleyin ya da boş bir yuvaya sağ tıklayıp `Buraya bir pattern koy` seçeneğini seçin.
3. **Başlatın.** Bir klibin solundaki üçgene ya da tüm satırı başlatmak için `Sahneler` sütunundaki bir sahnenin üçgenine tıklayın. Ya da `.als` set'lerinizden birini açın (aşağıda).

Live'dan farklı olanlar:

- Playlist aranjmandır ve izleri ızgaranın sütunlarıdır. Bir klip başlatmış iz, `Playlist'e dön` düğmesine basana kadar ızgarada kalır.
- Izgara dışında tuşlar FL Studio'yu izler. `Ctrl+Z` bir adımı geri alır, tekrar basılırsa yineler.
- Live'ın kendi cihazlarından yalnızca bazıları dönüştürülür (bkz. Bilmekte fayda var). Üçüncü taraf plugin'ler durumlarıyla birlikte gelir.
- Pattern yerleşimi, yani FL Studio'nun Channel Rack ve pattern'lerle çalışma biçimi, aynı proje üzerinde çalışır.

## Bir Live set'i açma

1. `Dosya › Aç...` (`Ctrl+O`) komutunu seçin ve `.als` dosyasını seçin. Live 11 ve Live 12 ile kaydedilmiş set'ler okunur.
2. Set'in Session klipleri ve sahnelerinin gittiği klip ızgarasıyla çalışmak için `Görünüm › Yerleşim stili › Klip başlatma iş akışı` komutunu seçin.
3. Izgarayı, Playlist'i ya da ikisini birden göstermek için Playlist'in başlık çubuğundaki `Grid / Both / Song` anahtarını kullanın.
4. `Dosya › Kaydet` komutunu seçin. Bir `.lynx` dosyasının nereye yazılacağını sorar. `.als` değişmez.

`Görünüm › Yerleşim stili › Pattern iş akışı (varsayılan)` geri döner. Aynı seçenek `Seçenekler › Genel ayarlar...` içindedir. Pattern'ler ve Channel Rack her iki yerleşimde de kullanılabilir kalır.

## Neyin karşılığı ne

| Live'da | LYNX SHARE'de |
|---|---|
| Session görünümü | Klip başlatma iş akışının klip ızgarası |
| Aranjman görünümü | Playlist |
| İzler | Playlist izleri; klip ızgarasının sütunlarıdır |
| Sahneler | Sahneler, klip ızgarasının satırları. Bir sahne, başlatıldığında tempoyu ayarlayabilir |
| Başlatma kuantizasyonu | `Global`, `Yok`, 8 ölçüden 1 ölçüye kadar ya da 1/2 ile 1/32 arası bir nota değeri; isterseniz klip başına |
| Başlatma modları | `Tetik`, `Gate`, `Aç/kapa` ve `Tekrar` |
| Follow action'lar | Her biri için bir olasılıkla A eylemi ve B eylemi |
| Klip durdurma düğmeleri | Boş bir yuvada durdurma düğmesi olabilir |
| Aranjmana dön | `Playlist'e dön` |
| Warp modları | `Kanal ayarları › Zaman esnetme` içinde `Vuruş`, `Tonlar`, `Doku` ve `Karmaşık` |
| Simpler, Sampler ve drum rack'ler | LYNX SHARE sampler'ları |
| `.als` set'i | `.lynx` proje dosyası |

## Klip ızgarası

Bir yuva bir pattern klibi ya da bir ses klibi tutar.

- Bir iz aynı anda tek klip çalar. Başlatılan klip çalanın yerini alır.
- Başlatma, başlatma kuantizasyonunun sonraki step'ini bekler.
- `Legato`, yeni klibi, yerini aldığı klibin ulaştığı konumdan başlatır. `Döngü` klip başınadır.
- Sahneler eklenebilir, silinebilir ve yeniden adlandırılabilir.
- Klip başlatmış bir iz, `Playlist'e dön` düğmesine basana kadar Playlist'te sessiz kalır. Bir seçenek, transport durduğunda izleri geri verir.

Follow action'lar, klibin belirli sayıda ölçüsünden veya geçişinden sonra çalışır. Eylemler şunlardır: `Eylem yok`, `Durdur`, `Yeniden çal`, `Önceki`, `Sonraki`, `İlk`, `Son`, `Herhangi` ve `Diğer`. Bir anahtar, follow action'ları tüm ızgara için açar veya kapatır.

## Bir .als'den neler içe aktarılır

- MIDI, ses, grup ve return izleri, tempo, ölçü işareti, locator'lar, döngü, mixer ayarları (ses, pan, mute, solo, send'ler) ve bunların otomasyonu.
- Aynı plugin kuruluysa durumlarıyla birlikte VST2 ve VST3 plugin'leri. Bkz. [Üçüncü taraf plugin'ler](/wiki/plugins/).
- Session klipleri ve sahneler, başlatma modları, kuantizasyon, legato, loop, follow action'lar ve durdurma düğmeleriyle birlikte klip ızgarasına. Bunları sahne başına bir bölüm olacak şekilde Playlist'e yerleştirmek için `Seçenekler › Genel ayarlar...` içindeki `Live setlerinin oturum klipleri` ayarını değiştirin.
- Warp işaretçileri.

## Bir .als dışa aktarma

1. `Dosya › Dışa aktar › Ableton Live seti (.als)...` komutunu seçin.
2. `Şunun için kaydet` altında `Live 12` veya `Live 11 (Live 12'de de açılır)` seçeneğini seçin.
3. `Sample'lar` altında, onları set'in proje klasörüne kopyalayın ya da bulundukları yerden başvurun.
4. Bir klasör seçin. Set `<name> Project/<name>.als` olarak kaydedilir.

| Yazılanlar | Yazılmayanlar |
|---|---|
| Kanal başına bir MIDI izi, notaları MIDI klibi olarak | LYNX SHARE'in kendi enstrümanları ve efektleri; Helix ise Wavetable olur. İzleri ve notaları korunur |
| Stretch, transpoze, gain, fade ve warp moduyla ses izleri | Efekt parametrelerinin otomasyonu |
| Gruplar, return'ler ve send'ler | Insert'ten insert'e yönlendirmeler |
| Durumlarıyla VST2 ve VST3 plugin'leri | |
| İşaretçiler ile ses, pan ve tempo otomasyonu | |
| Klip ızgarası Session klipleri ve sahneler olarak, rack'ler ve Drum Rack'ler | |

Dışa aktarmadan sonra bir rapor, dışarıda bırakılanları listeler.

## Bilmekte fayda var

- Live'ın kendi cihazlarının çoğu LYNX SHARE'in kendi cihazları olarak ayarları ve otomasyonlarıyla gelir: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Hybrid Reverb, Delay, Echo, Grain Delay, Auto Filter, Saturator, Overdrive, Roar, Erosion, Vinyl Distortion, Corpus, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Spectral Time, Spectral Resonator, Looper, Simpler, Sampler, Drift, Operator, Wavetable ve Meld. Diğerleri atılır ve içe aktarma raporunda listelenir.
- Instrument ve Audio Effect Rack'leri zincirleri, bölgeleri ve makrolarıyla gelir ve bir Drum Rack, LYNX SHARE'in Drum Rack'i olur. Live'ın Arpeggiator'ı kanalın arpejiatörü olur. Pitch, Velocity, Chord, Note Length ve MIDI Effect Rack'ler notalara yazılır.
- İçe aktarma raporu dışarıda bırakılanları listeler.
- Device View'da bir rack, zincirlerinin cihazlarını satır içinde gösterir; Macro Map modu (rack'in `Map` düğmeleri) Live'daki gibi bir cihaz parametresini bir makroya verir.
- Session mikserinde Live'ın Solo / Cue anahtarı vardır; kulaklığınız için bir `Cue Out` ve bir `Cue ses düzeyi` içerir.
- Live, dışa aktarılmış bir set'te üst üste binen ses kliplerini kırpar.
- Bir Live set'indeki warp işaretçileri içe aktarmada korunur ve dışa aktarmada geri yazılır.

## İlgili sayfalar

- [Playlist](/wiki/playlist/)
- [Yerleşimler](/wiki/layouts/)
- [Proje içe aktarma](/wiki/importing-projects/)
- [Dosya formatları](/wiki/file-formats/)
- [Tek programda iki iş akışı](/why/#workflows)
- [Learn: eğitimler](/learn/)

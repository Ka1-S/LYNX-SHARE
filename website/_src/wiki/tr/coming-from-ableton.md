---
source: d5cc625221
---
![Klip başlatma yerleşimi: Playlist'in yanında sahneli bir klip ızgarası](/assets/img/cliplaunch.webp)

LYNX SHARE Pattern iş akışıyla açılır: bir Channel Rack, pattern'ler ve bir Playlist. Klip başlatma iş akışı isteğe bağlıdır ve Playlist'in yanına sahneli bir klip ızgarası ekler. LYNX SHARE `.als` set'lerini okur ve yazar. Ableton ile bağlantısı yoktur.

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
| Kanal başına bir MIDI izi, notaları MIDI klibi olarak | LYNX SHARE'in kendi enstrümanları ve efektleri. İzleri ve notaları korunur |
| Stretch, transpoze, gain, fade ve warp moduyla ses izleri | Efekt parametrelerinin otomasyonu |
| Gruplar, return'ler ve send'ler | Insert'ten insert'e yönlendirmeler |
| Durumlarıyla VST2 ve VST3 plugin'leri | |
| İşaretçiler ile ses, pan ve tempo otomasyonu | |
| Klip ızgarası Session klipleri ve sahneler olarak, rack'ler ve Drum Rack'ler | |

Dışa aktarmadan sonra bir rapor, dışarıda bırakılanları listeler.

## Bilmekte fayda var

- Live'ın kendi cihazlarının çoğu LYNX SHARE'in kendi cihazları olarak ayarları ve otomasyonlarıyla gelir: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift ve Operator (FM'siz). Diğerleri atılır ve içe aktarma raporunda listelenir.
- Instrument ve Audio Effect Rack'leri zincirleri, bölgeleri ve makrolarıyla gelir ve bir Drum Rack, LYNX SHARE'in Drum Rack'i olur. Live'ın MIDI Effect Rack'leri henüz dönüştürülmez ve içe aktarma raporunda listelenir.
- İçe aktarma raporu dışarıda bırakılanları listeler.
- Live, dışa aktarılmış bir set'te üst üste binen ses kliplerini kırpar.
- Bir Live set'indeki warp işaretçileri içe aktarmada korunur ve dışa aktarmada geri yazılır.

## İlgili sayfalar

- [Playlist](/wiki/playlist/)
- [Yerleşimler](/wiki/layouts/)
- [Proje içe aktarma](/wiki/importing-projects/)
- [Dosya formatları](/wiki/file-formats/)
- [Tek programda iki iş akışı](/why/#workflows)

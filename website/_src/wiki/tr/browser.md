---
source: 8df354b49e
---
![Solda üç sekmesi, arama kutusu, klasör ağacı ve önizleme şeridiyle Browser](/assets/img/wiki/browser-tree.webp)

Browser; sample klasörlerinizi, geçerli projenin içeriğini ve LYNX SHARE'in tanıdığı plugin'leri listeler. Göstermek veya gizlemek için `Alt+F8` tuşlarına basın. Tek başına `F8`, plugin seçiciyi açar: her enstrümanı ve efekti gösteren, ayrı bir tam ekran görünümdür.

## Projeye sample yükleme

1. Browser'ı göstermek için `Alt+F8` tuşlarına basın.
2. `TÜMÜ` sekmesinde bir klasörü açmak için üzerine tıklayın.
3. Dinlemek için bir ses dosyasına tıklayın. `Up` ve `Down` dosyalar arasında gezinir ve her birini önizler.
4. Sample'ı içeren yeni bir kanal oluşturmak için dosyaya çift tıklayın; kanal [Channel Rack](/wiki/channel-rack/)'e eklenir.
5. Ya da ses klibi oluşturmak için dosyayı [Playlist](/wiki/playlist/)'e sürükleyin.

## Sekmeler

| Sekme | Gösterdiği |
|---|---|
| TÜMÜ | Geçerli proje, Plugin veritabanı ve tüm sample klasörleri |
| PROJE | Yalnızca geçerli projenin kullandıkları: pattern'ler, efektler, generator'lar, otomasyon, ses dosyaları |
| YILDIZLI | Yıldızladığınız dosyalar ve klasörler (sağ tık, sonra `Yıldızlı`) |

## Gezinme ve önizleme

- Bir klasörü açmak veya kapatmak için üzerine tıklayın. Menü için sağ tıklayın (tümünü daralt, dosya yöneticisinde göster).
- Dinlemek için bir ses dosyasına tıklayın. Listenin altındaki önizleme şeridi dosyanın dalga formunu gösterir. Dosyayı yeniden dinlemek için şeride tıklayın ya da dosyaymış gibi şeritten sürükleyin.
- Önizleme şeridinin üstündeki panel, seçili dosyanın tarihini, boyutunu ve yolunu gösterir.

## Sample kullanma

| Eylem | Ne yapar |
|---|---|
| Çift tık | Sample'lı yeni kanal |
| Channel Rack'e sürükleme | Yeni kanal ya da bir satırın üstüne bırakırsanız sample o satıra yüklenir |
| Playlist'e sürükleme | Ses klibi |
| `Enter` | Dosyayı seçili kanala yükler |
| `Shift+Up` / `Shift+Down` | Önceki veya sonraki dosyaya geçer ve seçili kanala yükler |

Sonuncusu, pattern çalarken farklı sample'ları yerinde denemenizi sağlar.

## Diğer dosya türleri

- **Proje**: çift tık açar.
- **MIDI dosyası**: çift tık, onu proje olarak açar.
- **SoundFont veya SFZ**: tıklamak önizler, çift tık bir LYNX Soundfont kanalı oluşturur (ya da seçili kanala yükler).
- **Mixer preset'i**: çift tık, seçili Mixer insert'lerine yükler; ya da bir insert'in üstüne sürükleyin.
- **Efekt preset'i**: bir Mixer insert'inin üstüne sürükleyin.

## Plugin veritabanı

Plugin veritabanı girdisi, LYNX SHARE'in dahili enstrümanlarını ve efektlerini ve kurulu plugin'leri listeler. `Shift+F8` doğrudan oraya götürür.

- Bir enstrümana çift tıklayın, yeni kanal olarak eklenir; ya da Channel Rack'e sürükleyin.
- Bir efekte çift tıklayın, seçili [Mixer](/wiki/mixer/) insert'ine eklenir; ya da bir insert'in veya efekt yuvasının üstüne sürükleyin.
- `F2` seçili plugin'i yeniden adlandırır.
- Plugin'leri klasörlere ayırmak için bir plugin'e sağ tıklayıp `Klasöre taşı` seçeneğini seçin. Mixer'in yuva menüsü ve Channel Rack'in ekleme menüsü de her üst klasörü listeler.

## PROJE sekmesi

Burada listelenen pattern'lere tıklayarak onları geçerli pattern yapabilir, çift tıklayarak [Piano Roll](/wiki/piano-roll/)'da açabilir ya da Playlist'e sürükleyebilirsiniz. Geçerli proje bölümü ayrıca geri alma geçmişini de listeler: bir adıma geri dönmek için ona tıklayın.

## Arama

İmleci arama kutusuna getirmek için `Ctrl+F` tuşlarına basın. Seçili klasörde (ya da tüm klasörlerde) ve altında arar. PROJE sekmesinde projenin öğelerinde arar.

Tüm kelimeler eşleşmelidir. Kutu ayrıca `OR`, bir kelimeyi dışlayan eksi işareti ve `*` joker karakterlerini de kabul eder:

```
kick -loop
snare OR clap
hat*.wav
```

Klasörler arka planda dizinlenir. Browser menüsündeki `Arama dizinini yenile` dizini yeniden oluşturur.

## Kendi klasörleriniz

Sample, proje veya SoundFont klasörü eklemek için Browser'ın klasör listesindeki `Klasör ekle` seçeneğini kullanın. Arama klasörleri `Seçenekler › Dosya ayarları (arama klasörleri)` altındadır.

## Tuşlar

Ok tuşları, `Enter` ve `F2` için Browser'ın odakta olması gerekir.

| Komut | Tuş | Ne yapar |
|---|---|---|
| Browser'ı göster | `Alt+F8` | Browser'ı gösterir veya gizler |
| Plugin seçici | `F8` | Tam ekran plugin seçiciyi açar |
| Plugin veritabanı | `Shift+F8` | Plugin veritabanı girdisine atlar |
| Arama | `Ctrl+F` | İmleci arama kutusuna getirir |
| Gezin ve önizle | `Up` / `Down` | Önceki veya sonraki dosyayı seçer ve çalar |
| Klasörü kapat | `Left` | Klasörü kapatır ya da üst klasörüne geçer |
| Klasörü aç | `Right` | Klasörü açar |
| Yükle | `Enter` | Dosyayı seçili kanala yükler |
| Geç ve yükle | `Shift+Up` / `Shift+Down` | Önceki veya sonraki dosyaya geçer ve seçili kanala yükler |
| Plugin'i yeniden adlandır | `F2` | Plugin veritabanında seçili plugin'i yeniden adlandırır |

## Bilmekte fayda var

- Bir klasörü listeden kaldırmak onu diskten silmez.
- `F8` ile `Alt+F8` farklı şeylerdir. `F8` plugin seçici, `Alt+F8` Browser'dır.
- Arama sonuçları arka planda oluşturulan bir dizinden gelir. Browser menüsündeki `Arama dizinini yenile` onu yeniden oluşturur.
- `Enter` dosyayı seçili kanala yükler. Çift tık yeni kanal oluşturur.

## İlgili sayfalar

- [Channel Rack](/wiki/channel-rack/)
- [Üçüncü taraf plugin'ler](/wiki/plugins/)
- [Dosya formatları](/wiki/file-formats/)
- [Geri alma ve geçmiş](/wiki/undo-history/)
- [Enstrümanlar ve efektler](/instruments-and-effects/)

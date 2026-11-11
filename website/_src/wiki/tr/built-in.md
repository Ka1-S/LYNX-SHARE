---
source: 7ca5e689db
---
![Hızlı ekleme paleti: solda enstrümanlar, sağda efektler; her biri dahili veya dahili FL motoru olarak etiketli](/assets/img/wiki/quickadd.webp)

LYNX SHARE kendi enstrümanlarını ve efektlerini beraberinde getirir; böylece bir proje hiçbir üçüncü taraf plugin olmadan çalar. Üç tür vardır: LYNX SHARE'in orijinalleri (bir synth, bir sampler, bir SoundFont çalıcı, davullar, EQ'lar, kompresörler, limiter'lar, reverb'ler, delay'ler), FL Studio'nun yerel plugin'lerinin ayarlarını çalmak için yazılmış motorlar ve bilinen üçüncü taraf plugin'lerin yerini tutan efektler.

Bunların her birinin [katalogda](/instruments-and-effects/) kendi sayfası vardır. `Yardım › Dahili plugin'ler (çevrimiçi liste)...` her motorun durumuyla birlikte listeyi açar.

## Enstrüman ve efekt ekleme

1. Channel Rack'i göstermek için `F6` tuşuna basın.
2. Son satırın altındaki `+ kanal ekle` kutusuna tıklayın ve bir enstrüman seçin. Menü onları kategoriye göre gruplar.
3. Mixer'i göstermek için `F9` tuşuna basın ve kanalın üzerinden çaldığı insert'i seçin.
4. Efekt panelindeki boş bir yuvaya tıklayın ve bir efekt seçin. Penceresi açılır.
5. O pencereyi yeniden gizlemek veya göstermek için yuvanın adına tıklayın.

## Plugin seçici

Plugin seçici için `F8` tuşuna basın. Ekranı kaplar ve her generator'ı ve efekti kendi editörünün resmi olarak gösterir; kategoriler altta sekmeler halindedir.

| Komut | Tuş | Ne yapar |
|---|---|---|
| Arama | Yazın | Eşleşmeyen plugin'leri karartır |
| Plugin'ler arasında gezin | `Up` / `Down` | Vurguyu hareket ettirir |
| Ekle | `Enter`, çift tık veya sağ tık | Generator yeni bir kanal olur. Efekt seçili Mixer insert'ine girer |
| Resmi sürükleme | | Yeni kanal için Channel Rack'e, bir kanalın enstrümanını değiştirmek için o kanalın üstüne, ya da bir Mixer insert'ine veya yuvasına |
| Kapat | `Esc` veya `F8` | Hiçbir şey eklemeden seçiciyi kapatır |

LYNX SHARE'in kendi plugin'lerinin resimleri LYNX SHARE tarafından arka planda çizilir. Üçüncü taraf bir plugin, editörü bir kez açılana kadar yapımcısının adını taşıyan düz bir kutucuk olarak görünür.

`Düzen › Hızlı ekleme paleti...` bunun yerine düz bir liste açar: solda enstrümanlar, sağda efektler; yazdıkça süzülür. `Tab` listeler arasında geçer ve `Enter` vurgulanan öğeyi ekler. [Browser](/wiki/browser/)'ın Plugin veritabanı (`Shift+F8`) aynı plugin'leri ağaç olarak listeler.

![FL engines klasörü açık olan Browser'ın Plugin veritabanı](/assets/img/wiki/browser-plugins.webp)

## Preset'ler

Dahili bir efektin penceresinin başlığında bir preset çubuğu vardır.

| Kontrol | Ne yapar |
|---|---|
| Oklar | Önceki ve sonraki preset |
| Preset adı | Menüyü açar: `LYNX SHARE preset'leri`, `Preset'leriniz`, varsa `FL Studio preset'leri`, `Preset kaydet...` ve `Preset klasörünü göster` |
| `A` / `B` | Karşılaştırmak için iki ayar. Yanlarındaki düğme, duyduğunuzu diğerine kopyalar |

Preset adı ile A ve B ayarları projeyle birlikte kaydedilir.

## "FL'in ayarlarını açar" ne demek

FL Studio'nun yerel plugin'leri hiçbir zaman yüklenmez. LYNX SHARE'in her biri için kendi motoru vardır ve bu motor, FL plugin'inin kaydettiği ayarları okur. Bu iki yerde işler:

- Bir `.flp` projesi. Her FL generator'ı ve efekti, kaydedilmiş ayarlarla kendi LYNX SHARE motorunda açılır; kontrolleri düzenlenebilir ve otomatikleştirilebilir kalır.
- Bir `.fst` preset'i. Yeni bir kanal için Browser'da bir synth preset'ine çift tıklayın ya da bir efekt preset'ini bir Mixer insert'ine sürükleyin. Dolu bir efekt yuvasına sağ tıklayıp, FL Studio kurulumunuzda bulunan preset'leri listeleyen `FL preset'leri` seçeneğine bakın.

Her motorun orijinaline ne kadar yakın olduğu, Exact'ten Placeholder'a kadar motor başına belirtilir. Seviyeler [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/) sayfasında açıklanmıştır.

## FL Studio kurulumunuza ihtiyaç duyan motorlar

LYNX SHARE hiçbir Image-Line kodu, sesi veya wave verisi içermez. Bazı motorlar, orijinal plugin'e ait wave'lere, sample'lara, impulse'lara veya tablolara ihtiyaç duyar ve çalışırken bunları kendi FL Studio kurulumunuzdan okur.

| Tür | Motorlar |
|---|---|
| Enstrümanlar | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Efektler | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Dosyalar bulunamazsa, böyle bir motoru eklemek, bir FL Studio kurulumunun veri dosyalarının bulunamadığını veya okunamadığını belirten bir iletiyle başarısız olabilir.

## Üçüncü taraf plugin'lerin yerini tutanlar

Bir proje, kurulu olmayan bir üçüncü taraf efekti kullandığında, aynı işi gören bir LYNX SHARE efekti onun yuvasını alır: reverb yerine reverb, delay yerine delay.

- Yuva, LYNX SHARE rozetiyle birlikte orijinal plugin'in adını gösterir.
- Kaydedilmiş durum okunabiliyorsa ayarları çevrilir. Aksi halde yerini tutan efekt varsayılan bir preset'ten başlar.
- Orijinal plugin ve ayarları projede kalır. Plugin kurulduktan sonra proje onunla yeniden açılır.
- Projeyi bunlar olmadan dinlemek için yerini tutan bir yuvaya sağ tıklayın ve `Bu projedeki tüm LYNX SHARE yedeklerini bypass et` seçeneğini seçin.

Anahtar, `Seçenekler › Genel ayarlar...` altındaki `Eksik plugin'lerin yerine LYNX SHARE karşılıklarını kullan` seçeneğidir ve varsayılan olarak açıktır. Yeni bir projede aynı efektler, boş yuvanın menüsünde `LYNX SHARE karşılıkları` altında bulunur.

## Bilmekte fayda var

- Eksik enstrümanların yerine henüz bir şey konmaz. Eksik bir enstrüman plugin'i, kanal adına göre seçilen dahili bir synth olur.
- Orijinal plugin'in parametrelerinin otomasyonu korunur ama yerini tutan efekti yönetmez.
- FLEX paketleri şifrelidir ve asla açılmaz. Bir FLEX kanalı, preset adına göre seçilen bir synth patch'i alır.
- Bir insert'in 10 efekt yuvası vardır.

## İlgili sayfalar

- [Enstrüman ve efekt kataloğu](/instruments-and-effects/)
- [Üçüncü taraf plugin'ler](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/)

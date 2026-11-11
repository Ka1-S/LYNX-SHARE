---
source: 200df26182
---
![Step satırlarıyla Channel Rack](/assets/img/rack.webp)

Channel Rack, projedeki her enstrümanı (kanalı) satır satır listeler ve geçerli pattern'in step'lerini gösterir. Göstermek veya gizlemek için `F6` tuşuna basın. Transport'taki Pattern düğmesine sağ tıklamak da onu gösterir.

## Davul pattern'i yapma

1. Channel Rack'i göstermek için `F6` tuşuna basın.
2. Son satırın altındaki `+ kanal ekle` kutusuna tıklayın ve menüden bir enstrüman seçin. Yeni bir sampler kanalı için [Browser](/wiki/browser/)'da bir sample'a çift tıklamanız da yeter.
3. Step'leri açmak için kanalın satırında onlara tıklayın. Bir dizi step çizmek için sürükleyin.
4. Bir step'i silmek için ona sağ tıklayın.
5. Çalmak için `Space` tuşuna basın. Pattern modunda pattern döngüde çalar.
6. Seviyeyi ayarlamak için satırın ses knob'unu sürükleyin.

## Step programlama

- Bir step'i açmak için ona tıklayın. Sürüklemek bir dizi step çizer. Sürükleme yanık bir step'ten başlarsa bunun yerine siler.
- Sağ tık veya sağ tuşla sürükleme siler.
- Pattern'in sonundan sonra tıklamak bir step ekler ve pattern'i uzatır.
- Notaları düz step olmayan bir satır (başka tuşlar, daha uzun notalar, slide'lar) notaların minyatürünü gösterir. Ona tıklamak notaları [Piano Roll](/wiki/piano-roll/)'da açar.
- Kanal oluşturmak için ses dosyalarını rack'e bırakabilirsiniz.

Kanal menüsünde (kanalın adına sağ tıklayın) `Her 2 step'i doldur`, `Her 4 step'i doldur`, `Her 8 step'i doldur`, `Gelişmiş dolgu`, ayrıca `Sola döndür` ve `Sağa döndür` bulunur.

## Grafik şeridi

![Kick kanalının altında açık grafik şeridi; her step için bir velocity çubuğu](/assets/img/wiki/rack-graph.webp)

`Ctrl+K`, seçili kanalın altında grafik şeridini gösterir. Solundaki düğmeler neyi düzenleyeceğini seçer: Velocity, Pan, Release, İnce pitch, Mod X veya Mod Y. Sürüklemek her step için bir değer ayarlar, sağ tuşla sürüklemek bir rampa çizer, `Alt`+tık bir step'i sıfırlar.

## Bir satırın bölümleri

| Bölüm | Ne yapar |
|---|---|
| Sessiz ışığı | Tıklamak susturur. `Ctrl`+tık veya sağ tık solo yapar. |
| Pan ve ses knob'ları | Sürükleyin veya tekerleği kullanın. |
| Yönlendirme kutusu | Sürüklemek veya tekerlek Mixer insert'ini seçer. Çift tık o insert'i Mixer'de gösterir. Sağ tık sıradaki boş insert'i alır. |
| Ad düğmesi | Tıklamak kanalın editörünü açar veya kapatır. `Alt`+tık diğer editörleri açık tutar. `Ctrl`+tık dinletir. Yukarı veya aşağı sürüklemek sırayı değiştirir. Sağ tık kanal menüsünü açar. |
| Seçici | Tıklamak tek bir kanalı seçer. `Ctrl`+tık veya sağ tık ekler ya da çıkarır. `Shift`+tık bir aralık seçer. Çift tık hepsini seçer. |

## Pattern'ler

Rack her zaman geçerli pattern'i düzenler. Rack'in pattern seçicisi onun adını gösterir: liste için ada tıklayın ya da okları kullanın. Rack'in başlığı üzerinde `Shift`+tekerlek de pattern'i değiştirir.

Rack'in pattern uzunluğu kutusu uzunluğu ölçü olarak ayarlar (sürükleyin veya tekerleği kullanın). Notalardan alınan otomatik uzunluk için çift tıklayın.

Ana araç çubuğundaki swing knob'u tüm kanallara uygulanır. Kanal menüsündeki `Swing mix`, bir kanalın onu ne kadar izleyeceğini ayarlar.

| Komut | Tuş | Ne yapar |
|---|---|---|
| Sonraki pattern | `Numpad +` | Sonraki pattern'i geçerli yapar |
| Önceki pattern | `Numpad -` | Önceki pattern'i geçerli yapar |
| Sonraki boş pattern | `F4` | Sonraki boş pattern'e gider ve ad sorar |
| Yeniden adlandır ve renklendir | `F2` | Geçerli pattern'i yeniden adlandırır ve renklendirir |

## PC klavyesinden nota çalma

`Ctrl+T`, yazı klavyesini piyanoya çevirir: `Z` ile `M` ve `Q` ile `P` arası tuşlar seçili kanalları çalar. Açıkken editörlerin tek harfli kısayolları durdurulur.

## Tuşlar

Bunlar Channel Rack odaktayken çalışır.

| Komut | Tuş | Ne yapar |
|---|---|---|
| Kanal seç | `Up` / `Down` | Üstteki veya alttaki kanalı seçer |
| Seçimi genişlet | `Shift+Up` / `Shift+Down` | Üstteki veya alttaki kanalı seçime ekler |
| Kanalları taşı | `Alt+Up` / `Alt+Down` | Seçili kanalları yukarı veya aşağı taşır |
| Tümünü seç | `Ctrl+A` | Tüm kanalları seçer |
| Kes, kopyala, yapıştır | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Seçili kanalların step'lerini keser, kopyalar veya yapıştırır |
| Step'leri döndür | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Seçili kanalların step'lerini döndürür |
| Klonla | `Alt+C` | Seçili kanalları klonlar |
| Sil | `Alt+Del` | Seçili kanalları siler |
| Grupla | `Alt+G` | Seçili kanalları yeni bir filtre grubuna koyar |
| Mixer'e yönlendir | `Ctrl+L` | Seçili kanalları boş Mixer insert'lerine gönderir |
| Sessiz | `1` - `0` | 1 ile 10 arası kanalları susturur veya sesini açar |
| Solo | `Ctrl+1` - `Ctrl+0` | 1 ile 10 arası kanalları solo yapar |
| Grafik şeridi | `Ctrl+K` | Grafik şeridini gösterir veya gizler |

## Bir kanalı dondurma

Dondurma, bir kanalı sese render eder ve onun yerine o sesi çalar. Enstrüman, üçüncü taraf plugin de olsa, bellekten çıkarılır; böylece CPU ve bellek kullanmaz.

1. Kanala sağ tıklayıp `Dondur` seçin. Birden fazla kanal seçiliyken öğe `Seçili kanalları dondur` olarak görünür.
2. Satır bir kar tanesi etiketi alır. Kanalın sesi, pan'ı ve insert'inin efektleri eskisi gibi çalışır.
3. Notaları veya enstrümanı yeniden değiştirmek için kanala sağ tıklayıp `Çöz` seçin. Enstrüman sahip olduğu ayarlarla geri gelir.

Dondurulmuş bir kanal düzenlenemez: step'leri kilitlidir ve enstrüman penceresi açılmaz. Notalar, tempo veya aranjman dondurmadan sonra değişirse etiket renk değiştirir ve önceden yapılan ses, `Yeniden dondur` seçene kadar çalmaya devam eder. Render edilen ses dosyaları eksikse bunun yerine enstrüman çalar.

Dondurma yalnızca kanallarda çalışır. Bir Mixer insert'i dondurulamaz.

## Kanal menüsündeki diğer şeyler

`Piano Roll`, `Yeniden adlandır ve renklendir`, `Değiştir` (enstrümanı değiştir), `Klonla`, `Delete`, `Arpeggiator`, `Döngü noktası`, `MIDI'yi şuraya yaz`, `Kanalları sırala`, `Kullanılmayan kanalları seç` ve `Kanal ayarları`.

## Bilmekte fayda var

- Ekleme menüsünü yalnızca `+ kanal ekle` kutusu açar.
- Rack'in kendi pattern'i yoktur. Geçerli pattern'i herhangi bir yerde değiştirmek rack'in gösterdiğini değiştirir.
- Nota minyatürü gösteren bir satır step olarak düzenlenemez. Bunun için Piano Roll'u kullanın.
- Sayı tuşları yalnızca ilk on kanala ulaşır.
- Tablodaki tuşlar için rack'in odakta olması gerekir. Önce rack'e tıklayın.

## İlgili sayfalar

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Kısayollar](/wiki/shortcuts/)

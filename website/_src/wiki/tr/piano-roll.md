---
source: a73763934f
---
![Piano Roll](/assets/img/pianoroll.webp)

Piano Roll, geçerli pattern'deki tek bir kanalın notalarını gösterir; solda bir klavye, altta nota başına değerler için bir şerit bulunur. Göstermek veya gizlemek için `F7` tuşuna basın ya da [Channel Rack](/wiki/channel-rack/)'te bir kanalın menüsünden `Piano Roll` seçeneğini seçin.

Araç çubuğundaki kanal kutusu hangi kanalı düzenlediğinizi seçer. Bu pattern'de notası olan kanallar işaretlidir. Kutunun üzerinde tekerlek, notası olan kanallar arasında gezinir, `Ctrl`+tekerlek hepsi arasında.

## Birkaç nota yazmak

1. Channel Rack'te bir kanalın adına sağ tıklayın ve `Piano Roll` seçeneğini seçin.
2. Kalem için `P` tuşuna basın.
3. Nota eklemek için gridde tıklayın. Tuşu basılı tutup sürükleyerek yerleştirin.
4. Notayı uzatmak veya kısaltmak için ucunu sürükleyin.
5. Silmek için bir notaya sağ tıklayın.
6. Pattern'i duymak için `Space` tuşuna basın.

## Araçlar

![Piano Roll'un araç çubuğu: kanal kutusu, araçlar ve snap kutusu](/assets/img/wiki/pianoroll-tools.webp)

Her aracın bir harf tuşu vardır. Harfler, klavyeden piyano (`Ctrl+T`) kapalıyken çalışır.

| Araç | Tuş | Ne yapar |
|---|---|---|
| Kalem | `P` | Tıklama nota ekler, sürükleme yerleştirir |
| Fırça | `B` | Sürükleme bir nota dizisi boyar. `Shift` ile teker teker |
| Davul fırçası | `N` | Farenin altındaki tuşta grid hücresi başına bir vuruş |
| Silgi | `D` | Tıklama veya sürükleme siler |
| Sessiz | `T` | Tıklama veya sürükleme notaları susturur ya da sesini açar |
| Bıçak | `C` | Notaları boydan boya kesmek için bir çizgi sürükleyin, tıklama o noktadan keser |
| Kement | `E` | Sürükleme bir seçim kutusu çizer. `Shift` ekler |
| Zoom | `Z` | Tıklama yakınlaştırır, bir aralığı sürüklemek ona zoom yapar, sağ tık uzaklaştırır |
| Dinle | `Y` | Tıklama veya sürükleme imlecin altındaki notaları çalar |

## Notalarla çalışmak

- Bir notayı taşımak için sürükleyin. `Shift`+sürükleme bir kopya yapar.
- Boyutunu değiştirmek için notanın ucunu sürükleyin. Başlangıcı da sürüklenebilir; son yerinde kalır.
- Sağ tık, her araçta bir notayı siler.
- Özelliklerini açmak için bir notaya çift tıklayın.
- Bir notanın üzerinde `Alt`+tekerlek velocity değerini değiştirir.
- `Ctrl`+sürükleme seçer. `Ctrl`+tıklama bir notayı seçime ekler veya çıkarır.
- Snap'i yok saymak için sürüklerken `Alt` tuşunu basılı tutun. `Backspace` snap'i kapatır ve açar.
- Birden çok nota seçiliyken esnetme tutamacı onları zamanda esnetir veya sıkıştırır.

## Klavye ve cetvel

Bir sesi duymak için klavyedeki bir tuşa tıklayın. Tuşun daha sağına tıklamak daha yüksek çalar. Bir tuşa `Ctrl`+tıklama onun notalarını seçer. Klavyede orta tuşla sürüklemek tuşları daha uzun veya kısa yapar. Dikey zoom çarkı da aynısını yapar: zaman kaydırma çubuğunun ucundaki küçük kare. Yukarı veya aşağı sürükleyin ya da üzerinde fare tekerleğini kullanın.

Piano roll menüsündeki `Görünüm › Klavye stili`, `Düz tuşlar` ile geleneksel bir klavye çizen `Piyano tuşları (beyaz tuşlar arasında çizgiler)` arasında seçim yapar.

Konumu belirlemek için cetvele tıklayın. Cetvelde `Ctrl`+sürükleme, sağ tuşla sürükleme veya çift tıklayıp sürükleme bir zaman aralığındaki notaları seçer. `Ctrl`+tekerlek zoom yapar, `Shift`+tekerlek kaydırır.

## Araç çubuğu

| Ayar | Ne yapar |
|---|---|
| Snap | "Ana", ana araç çubuğundaki snap'i izler; ya da bu pencere için birini seçin. |
| Akor | Tıklama, tıklanan nota kök olacak şekilde seçilen akoru oluşturur. `Shift+N` tek notalara geri döner. Aynı liste, Piano roll menüsündeki `Akor damgası`'tir. |
| Gam | Bir gamın notalarını vurgular. Ayrı bir düğme, yeni ve taşınan notaları ve gelen MIDI'yi seçilen gama oturtur. Taşınan notalar o zaman gam adımlarıyla hareket eder, böylece bir akor gamda kalır; `Up` ve `Down` bir gam adımı hareket eder. |
| Slide (`S`) | Yeni notalar slide notası olur. Çalmakta olan nota onlara kayar (808 kayması). |
| Hayalet notalar (`Alt+V`) | Diğer kanalların notalarını sizinkilerin arkasında gösterir. |
| Nota rengi | Yeni notalar bu rengi alır. 16 tane vardır. |
| Takip | Pattern çalarken görünüm çalma imleciyle birlikte kayar. |
| Şerit kutusu | Alttaki şeridin neyi düzenleyeceğini seçer: Velocity, Pan, İnce pitch, Release, Mod X, Mod Y, Chance veya Velocity range. |

## Chance ve velocity range

Her notanın bir Chance'ı ve bir Velocity range'i vardır. Chance, notanın her turda çalma olasılığıdır. Velocity range, velocity'sinin notanın velocity'si ile o velocity artı range arasında değişmesini sağlar. Bunları düzenlemek için şerit kutusunda `Olasılık` veya `Velocity aralığı (LYNX)` seçin. Piano roll menüsündeki `Araçlar › Olasılık (LYNX)`, seçili notaları hepsi ya da yalnızca biri çalacak şekilde gruplar ve `Olasılığı ve velocity aralığını sıfırla` içerir.

## Nota araçları

Piano Roll'un `Araçlar` menüsü FL Studio'nun nota araçlarını diyalog olarak açar. Seçili notalar üzerinde, hiçbiri seçili değilse tüm notalar üzerinde çalışırlar. Bazıları:

| Araç | Tuş | Ne yapar |
|---|---|---|
| Limit | `Alt+K` | Notaları oktav oktav bir tuş aralığına taşır, sonra kırpar. `Ton / gam kısıtlaması` altında notaları bir ana tuşta ve FL Studio'nun akorlarından veya gamlarından birinde tutar; `Snap` `Üstte`, `Altta` veya `Dönüşümlü` olarak ayarlanır. `Üste / alta sar` ve `Transpoze`, aralığın dışındaki notaların nereye gideceğini belirler |
| Flip | `Alt+Y` | `Yatay çevir` notaları zamanda aynalar. İlk başta açık olan `Başlangıç zamanlarını koru` ile başlangıç zamanları aynalanır, notalar uzunluklarını korur. `Dikey çevir` tuşları aralıklarının ortası etrafında aynalar |
| Randomise | `Alt+R` | Velocity, pan, ince pitch, zamanlama ve uzunlukta, ayrıca Mod X, Mod Y ve release'de rastgele değişiklikler |
| Scale levels | `Alt+X` | Bir nota seviyesini seçimin başından sonuna ölçekler; `Ofset`, `Orta` (etrafında ölçeklediği değer) ve `Gerilim` ile |
| LFO | `Alt+O` | Bir nota seviyesine LFO şekli yazar. Bir `Periyot` kaydırıcısına sağ tıklayınca 1 adımdan 512 adıma kadar bir liste çıkar |

## MIDI araçları ve script'ler

Piano roll menüsündeki `Araçlar › MIDI Araçları...`, Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp ve Velocity Shaper içeren bir panel açar.

`Araçlar` menüsünün `Betikler` sütunu piano roll script'lerini listeler: LYNX SHARE'in kendileri ve FL Studio kuruluysa FL Studio'nun script klasörlerindekiler dahil `.pyscript` dosyaları. `Betik klasörünü aç...` kendi script'leriniz için klasörü açar, `Son betiği yeniden çalıştır` sonuncuyu tekrarlar.

## Tuşlar

| Komut | Tuş | Ne yapar |
|---|---|---|
| Tümünü seç | `Ctrl+A` | Her notayı seçer |
| Seçimi kaldır | `Ctrl+D` veya `Esc` | Seçimi temizler |
| Kopyala, kes, yapıştır | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Notaları kopyalar, keser veya yapıştırır |
| Çoğalt | `Ctrl+B` | Seçimi çoğaltır |
| Sil | `Del` | Seçimi siler |
| Transpoze | `Up` / `Down` | Seçimi bir yarım ton taşır |
| Oktav transpoze | `Ctrl+Up` / `Ctrl+Down` | Seçimi bir oktav taşır |
| Kaydır | `Left` / `Right` | Seçimi bir grid adımı taşır |
| Hızlı kuantize | `Ctrl+Q` | Başlangıçları ve uzunlukları kuantize eder |
| Başlangıçları kuantize et | `Shift+Q` | Yalnızca başlangıçları kuantize eder |
| Doğra | `Ctrl+U` | Notaları grid'e göre doğrar |
| Yapıştır | `Ctrl+G` | Birbirine değen notaları birleştirir |
| Legato | `Ctrl+L` | Her notayı bir sonrakine kadar sürdürür |
| Sustur, sesi aç | `Alt+M` / `Shift+Alt+M` | Seçimi susturur veya sesini açar |
| MIDI içe aktar | `Ctrl+M` | Bir MIDI dosyasını bu kanala içe aktarır |
| MIDI dışa aktar | `Ctrl+Shift+M` | Pattern'i MIDI dosyası olarak dışa aktarır |
| Zoom | `PageUp` / `PageDown` | Yakınlaştırır veya uzaklaştırır |

Nota araçları (Arpej yap, Strum, Çevir, Rastgele, Kuantize, Doğra ve diğerleri) iletişim kutuları açar. Tuşları [Kısayollar](/wiki/shortcuts/) sayfasındadır.

## Bilmekte fayda var

- Piano Roll tek pattern'deki tek kanalı gösterir. Geçerli pattern'i değiştirirseniz o pattern'in notalarını gösterir.
- Klavyeden piyano (`Ctrl+T`) açıkken araç harfleri hiçbir şey yapmaz.
- Sağ tık her araçla siler; tek bir nota için Silgi'ye geçmeye gerek yoktur.
- Burada `S` Slide anahtarıdır. Playlist'te aynı harf Kaydır aracıdır.
- Düz step olmayan notalar (başka tuşlar, daha uzun notalar, slide'lar), Channel Rack'te kanalın satırında step düğmeleri yerine bir minyatür göstermesine yol açar.

## İlgili sayfalar

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Kısayollar](/wiki/shortcuts/)
- [Dosya formatları](/wiki/file-formats/)

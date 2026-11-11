---
source: 5291e1e4ad
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

Bir sesi duymak için klavyedeki bir tuşa tıklayın. Tuşun daha sağına tıklamak daha yüksek çalar. Bir tuşa `Ctrl`+tıklama onun notalarını seçer. Klavyede orta tuşla sürüklemek tuşları daha uzun veya kısa yapar.

Konumu belirlemek için cetvele tıklayın. Cetvelde `Ctrl`+sürükleme, sağ tuşla sürükleme veya çift tıklayıp sürükleme bir zaman aralığındaki notaları seçer. `Ctrl`+tekerlek zoom yapar, `Shift`+tekerlek kaydırır.

## Araç çubuğu

| Ayar | Ne yapar |
|---|---|
| Snap | "Ana", ana araç çubuğundaki snap'i izler; ya da bu pencere için birini seçin. |
| Akor | Tıklama, tıklanan nota kök olacak şekilde seçilen akoru oluşturur. `Shift+N` tek notalara geri döner. |
| Gam | Bir gamın notalarını vurgular. Ayrı bir düğme, yeni ve taşınan notaları ve gelen MIDI'yi seçilen gama oturtur. |
| Slide (`S`) | Yeni notalar slide notası olur. Çalmakta olan nota onlara kayar (808 kayması). |
| Hayalet notalar (`Alt+V`) | Diğer kanalların notalarını sizinkilerin arkasında gösterir. |
| Nota rengi | Yeni notalar bu rengi alır. 16 tane vardır. |
| Takip | Pattern çalarken görünüm çalma imleciyle birlikte kayar. |
| Şerit kutusu | Alttaki şeridin neyi düzenleyeceğini seçer: Velocity, Pan veya İnce pitch. |

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

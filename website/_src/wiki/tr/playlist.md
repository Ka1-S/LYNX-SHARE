---
source: 1b54c6493f
---
![İzlerde kliplerle Playlist](/assets/img/playlist.webp)

Playlist, pattern'lerin şarkıya dönüştüğü yerdir. İzlerinde üç tür klip tutar: pattern klipleri, ses klipleri ve otomasyon klipleri. Göstermek veya gizlemek için `F5`, büyütmek veya eski haline getirmek için `Enter` tuşuna basın.

Transport'un iki modu vardır. Pattern modu geçerli pattern'i döngüde çalar. Song modu Playlist'i çalar. Geçmek için `L` tuşuna basın.

## Şarkıya pattern yerleştirme

1. Playlist'i göstermek için `F5` tuşuna basın.
2. Seçici panelinde bir pattern'e tıklayın. Seçici projenin pattern'lerini, seslerini ve otomasyonlarını listeler; tıkladığınız klip kaynağı olur.
3. Kalem için `P` tuşuna basın.
4. Klibi yerleştirmek için bir iz üzerine tıklayın. Tuşu basılı tutup sürükleyerek konumlandırın.
5. Fırça için `B` tuşuna basın ve klipleri uç uca boyamak için bir iz boyunca sürükleyin.
6. Song modu için `L`, sonra çalmak için `Space` tuşuna basın.

Ses dosyaları [Browser](/wiki/browser/)'dan Playlist'in üzerine sürüklenebilir.

## Araçlar

![Playlist'in araç çubuğu: aranjman kutusu, araçlar ve snap kutusu](/assets/img/wiki/playlist-tools.webp)

Harfler, yazı klavyesi piyanosu (`Ctrl+T`) kapalıyken çalışır.

| Araç | Tuş | Ne yapar |
|---|---|---|
| Kalem | `P` | Tıklamak Seçici'nin klibini yerleştirir |
| Fırça | `B` | Sürüklemek klipleri uç uca boyar |
| Silgi | `D` | Tıklamak veya sürüklemek siler |
| Sessiz | `T` | Tıklamak veya sürüklemek klipleri susturur ya da seslerini açar |
| Bıçak | `C` | Kliplerin üzerinden kesmek için bir çizgi sürükleyin, tıklamak o noktadan keser |
| Kement | `E` | Sürüklemek bir seçim kutusu çizer. `Shift` ekler |
| Zoom | `Z` | Tıklamak yakınlaştırır, bir aralığı sürüklemek ona yakınlaştırır, sağ tık uzaklaştırır |
| Dinle | `Y` | Tıklamak veya sürüklemek o noktadan çalar |
| Kaydır | `S` | Sürüklemek klibin içeriğini klibin içinde kaydırır |

## Kliplerle çalışma

- Taşımak için bir klibi sürükleyin. `Shift`+sürükleme kopya yapar.
- Boyutlandırmak için klibin ucunu sürükleyin. Başlangıcı sürüklemek, içerik yerinde kalarak kırpar.
- Sağ tık bir klibi siler. `Ctrl`+sürükleme seçer.
- Açmak için bir klibe çift tıklayın.
- Klibin sol üstündeki düğme klip menüsünü açar (benzersiz yap, yeniden adlandır ve renklendir, doğra; ses için ayrıca: pitch, tempoya uydur, ters).
- Bir ses klibinin menüsü FL Studio'nun düzenini iki sütunda izler. Sol sütunda `Ses klibi` bölümü (`Önizleme`, `Sessiz`, `Yeniden adlandır ve renklendir...`, `Rengi değiştir...`, `Rastgele renk`, `Simgeyi değiştir...`, `Kaynak kanalı seç`, `Kanal ayarları`, `Benzersiz yap`, `Tüm benzer klipleri seç`, `Delete`, `Özellikler...`) ve `Bölge` bölümü (`Bölge seç`, `Doğra`) vardır. Sağ sütunda `Sample` (`Örnek olarak benzersiz yap`, `Örneği düzenle`, `Sample'dan stem'leri çıkar`, `Tempoyu algıla`, `Tempoya uydur`), `Otomasyon` (`Otomatikleştir › Pan` veya `Ses`, `Şununla çapraz geçiş`) ve `Geçişler` (`Geçişleri sıfırla`) bulunur. `Burada böl`, `Ters`, `Normalize et` ve `MIDI'ye dönüştür` gibi LYNX SHARE'in kendi öğeleri en sonda, bir çizginin altında yer alır.
- `Otomatikleştir › Pan` veya `Ses`, kanalın Levels ayarındaki pan veya ses seviyesi için, kendi izinde, ses klibi kadar uzun bir otomasyon klibi oluşturur.
- Snap'i yok saymak için sürüklerken `Alt` tuşunu basılı tutun.
- Ses kliplerinin fade tutamaçları ve yukarı veya aşağı sürüklediğiniz bir klip gain'i vardır. Bir fade tutamacına sağ tıklarsanız `Çapraz geçiş oluştur` içeren menüsü açılır. Gain tutamacına sağ tıklarsanız `Gain'i sıfırla` ve `Normalize et` çıkar.
- `Shift+M` Stretch'i açıp kapatır: açıkken bir ses klibini boyutlandırmak sesi kesmek yerine uzatıp sıkıştırır.
- `Shift+Left` ve `Shift+Right` seçili klipleri bir snap adımı, `Shift+Up` ve `Shift+Down` bir iz kaydırır. `Alt+Left` ve `Alt+Right` onları snap olmadan, geçerli zoom'da bir ekran pikseli kaydırır. Hiç klip seçili değilse tüm klipler kayar.
- Bir ses klibinin menüsündeki `Örnek olarak benzersiz yap`, kliple çaldığı şeyi tutan yeni bir dosya verir; klibin pitch'i, stretch'i ve reverse'ü artık sesin içindedir.
- Warp işaretçileri olan bir ses klibi bunları üst kenarında küçük bayraklar olarak gösterir. İşaretçiyi taşımak için bayrağı yana sürükleyin.

## Edit ve Select menüleri

Playlist'in menüsünde bir `Düzen` ve bir `Seç` alt menüsü vardır. `Düzen` klip komutlarını taşır: kes, kopyala ve yapıştır, `Sola kaydır`, `Sağa kaydır`, `Yukarı kaydır`, `Aşağı kaydır`, `Benzersiz yap`, `Örnek olarak benzersiz yap`, `Boşluk ekle`, `Boşluğu sil`, birleştirme komutları ve fade seçenekleri. `Seç` içinde `Seçili kaynağa göre seç` (`Shift+C`), `Seçimi tersine çevir` (`Shift+I`), `Seçimi kaldır` (`Ctrl+D`), sessize alınmış ve üst üste binen klipler ile `Sihirli kement` vardır. `Sihirli kement` açıkken seçmek için fareyi hiçbir düğmeye basmadan klip çevresinde bir döngüyle gezdirin.

## İzler

Bir izin başlığında tıklamak seçer, `Ctrl`+tık ekler veya çıkarır, `Shift`+tık bir aralık seçer ve sağ tık iz menüsünü açar. `Shift`+tekerlek izi yukarı veya aşağı taşır. İzin ışığı onu susturur (tık) ya da solo yapar (`Ctrl`+tık veya sağ tık). Birini yeniden boyutlandırmak için iki iz arasındaki ayırıcıyı sürükleyin, hepsini yeniden boyutlandırmak için `Shift`+sürükleyin.

Playlist menüsündeki `Görünüm`, iz başlıklarının ne göstereceğini seçer: `Ses parçalarında denetimleri göster`, `Ses parçalarında seviyeleri göster` ve `Enstrüman parçalarında seviyeleri göster`.

İz menüsündeki `İz modu`, FL Studio'daki gibi bir izi `Atanmamış`, `Ses izi` veya `Enstrüman izi` yapar. `Ses izi` önce `Yeni mikser kanal(lar)ı` öğesini, sonra her mixer insert'ini numarası ve adıyla listeler. Seçim, seçili tüm izlere uygulanır. `Enstrüman izi` içinde `Mevcut kanalı kullan`, `Daha fazla eklenti...` ve Channel Rack'in enstrüman listesi vardır.

İz başlıklarının sol üstündeki `+` yeni bir iz için aynı seçenekleri açar: `Ses parçası ekle`, `Daha fazla eklenti...` ve enstrümanlar. `+` üzerine sağ tık hemen bir ses izi ekler.

## Pattern blokları

Playlist menüsünde `Pattern blokları (geçerli proje)` açıkken izlerin altındaki bir bölümde her pattern için bir satır bulunur. Orada Kalem bir blok yerleştirir ve satırı boyunca sürükler, Fırça blokları boyar, Silgi onları siler. Sağ tık, hangi araç olursa olsun siler.

## Cetvel, döngü ve işaretçiler

- Şarkı konumunu ayarlamak için cetvele tıklayın.
- Cetvelde sağ tuşla sürükleme, `Ctrl`+sürükleme veya çift tıklayıp sürükleme bir zaman seçimi yapar; bu aynı zamanda döngüdür. `Shift`+sürükleme onu taşır.
- `Ctrl+Ins` zaman seçiminin uzunluğunu tüm izlere ekler, `Ctrl+Del` kaldırır.
- `Alt+T` bir işaretçi ekler. Ona atlamak için işaretçiye tıklayın.
- `Ctrl`+tekerlek zoom yapar, `Alt`+tekerlek iz yüksekliğini değiştirir.

## Otomasyon

Bir knob'a veya fader'a sağ tıklayın ve `Otomasyon klibi oluştur` seçeneğini seçin. Klip Playlist'te görünür. Kalemle noktalarını sürükleyin, bir tane eklemek için eğri üzerine tıklayın. İzlerin altındaki otomasyon şeritleri isteğe bağlıdır ve varsayılan olarak kapalıdır. Bkz. [Otomasyon](/wiki/automation/).

Menüsü için bir noktaya sağ tıklayın. `Mod` listesi, o noktada biten bölüm için FL Studio'nun 13 eğri modunu içerir: `Yumuşak`, `Hold`, üç tek eğri, üç çift eğri, `Yarım sinüs`, `Merdiven`, `Yumuşak merdiven`, `Pulse` ve `Dalga`. Yeni bir nokta en son kullanılan modu alır. Modlar .flp dosyalarından okunur ve onlara yazılır.

## Aranjmanlar

Araç çubuğundaki aranjman kutusu aranjmanlar arasında geçiş yapar. Bir projede birkaç tane olabilir.

## Tuşlar

| Komut | Tuş | Ne yapar |
|---|---|---|
| Tümünü seç | `Ctrl+A` | Her klibi seçer |
| Kopyala, kes, yapıştır | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Klipleri kopyalar, keser veya yapıştırır |
| Çoğalt | `Ctrl+B` | Seçimi çoğaltır |
| Sil | `Del` | Seçili klipleri siler |
| Ripple silme | `Shift+Del` | Seçili klipleri siler ve boşlukları kapatır |
| Ripple düzenleme | `Shift+S` | Ripple düzenlemeyi açar veya kapatır |
| Sessiz, sesli | `Alt+M` / `Shift+Alt+M` | Seçili klipleri susturur veya seslerini açar |
| Klipleri kaydır | `Shift+Left` / `Shift+Right` | Seçili klipleri bir snap adımı kaydırır |
| Başka bir iz'e kaydır | `Shift+Up` / `Shift+Down` | Seçili klipleri bir iz kaydırır |
| Klipleri it | `Alt+Left` / `Alt+Right` | Seçili klipleri snap olmadan bir ekran pikseli kaydırır |
| Benzersiz yap | `Ctrl+Shift+V` | Seçili klipleri benzersiz yapar |
| Birleştir | `Ctrl+G` | Pattern kliplerini birleştirir |
| Stretch | `Shift+M` | Ses kliplerini boyutlandırırken Stretch'i açıp kapatır |
| Süre ekle | `Ctrl+Ins` | Zaman seçiminin uzunluğunu tüm izlere ekler |
| Süre sil | `Ctrl+Del` | Zaman seçimini tüm izlerden kaldırır |
| İşaretçi ekle | `Alt+T` | Bir işaretçi ekler |
| Zoom | `PageUp` / `PageDown` | Yakınlaştırır veya uzaklaştırır |
| Sığacak şekilde zoom | `Shift+4` | Her şeyi gösterecek şekilde zoom yapar |
| Büyüt | `Enter` | Playlist'i büyütür veya eski haline getirir |

## Bilmekte fayda var

- Playlist yalnızca Song modunda duyulur. Pattern modunda `Space`, izlerde ne olursa olsun geçerli pattern'i çalar.
- `Görünüm › Yerleşim stili › Klip başlatma iş akışı`, Playlist'in yanına bir klip ve sahne ızgarası ekler. İsteğe bağlıdır. Bkz. [Yerleşimler](/wiki/layouts/).
- Tek bir kontrolde üst üste binen birkaç otomasyon klibi varsa hangisinin çalacağı seçimi FL Studio'dan farklı olabilir (0.1.25 sürüm notları).
- FL Studio'daki gibi bir pattern aynı anda tek bir klipten çalar. Aynı pattern'in sonraki bir klibi, herhangi bir izde başladığında, önceki klip oradan sonra yeni nota başlatmaz. Farklı pattern'lerin klipleri birlikte çalar. Bu, yeni projelerde ve FL Studio projelerinde geçerlidir. 0.1.27'den önce kaydedilmiş bir .lynx projesi her klibi tam olarak çalmaya devam eder.
- Bağlı bir MIDI kontrolcüsüyle taşınan tempo ses kliplerini yeniden boyutlandırmaz.
- Linux'ta video için FFmpeg gerekir.

## İlgili sayfalar

- [Channel Rack](/wiki/channel-rack/)
- [Otomasyon](/wiki/automation/)
- [Kayıt](/wiki/recording/)
- [Dışa aktarma](/wiki/exporting/)
- [Yerleşimler](/wiki/layouts/)

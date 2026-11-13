---
source: b6e9ef0dd0
---
![Mixer](/assets/img/mixer.webp)

Her kanal bir Mixer insert'i üzerinden çalar. Her insert'in bir fader'ı, pan'ı, mute ve solo düğmeleri, bir tepe ölçeri, her biri kendi dry/wet miktarına sahip on efekt yuvası ve dahili bir EQ'su vardır. Başka yere yönlendirmedikçe bütün insert'ler Master'da buluşur. Mixer'ı göstermek veya gizlemek için `F9` tuşuna basın.

Bir projede en fazla 127 insert olabilir. Mixer'da yeni bir tane için ekleme şeridi vardır. Mixer'ın başlık çubuğundaki kutu, `Kompakt` ile `Çok büyük` arasında sekiz kanal şeridi düzeninden birini seçer.

## Bir kanala efekt koymak

1. [Channel Rack](/wiki/channel-rack/)'te kanalı seçin ve `Ctrl+L` tuşuna basın. Kanal, adını ve rengini alacağı boş bir insert'e gider.
2. Mixer'ı göstermek için `F9` tuşuna basın ve o insert'in kanal şeridine tıklayın.
3. Efekt panelinde boş bir yuvaya tıklayın ve bir efekt seçin.
4. Efektin penceresini göstermek veya gizlemek için yuvanın adına tıklayın.
5. Karşılaştırmak için efekti bypass etmek üzere yuvanın ışığına tıklayın.

## Bir kanalı bir insert'e göndermek

Channel Rack'te insert numarasını seçmek için kanalın yönlendirme kutusunu sürükleyin (veya üzerinde tekerleği kullanın). Kutuya çift tıklamak o insert'i Mixer'da gösterir.

Channel Rack'teki `Ctrl+L`, seçili birçok kanal üzerinde aynı anda çalışır. Her biri kendi boş insert'ine gider.

Mixer odaktayken `Ctrl+L`, seçili kanalları seçili insert'e bağlar; `Ctrl+Shift+L` ise onları, ondan başlayarak ardışık insert'lere bağlar.

## Insert'leri seçmek ve taşımak

- Seçmek için bir kanal şeridine tıklayın.
- `Ctrl`+tıklama ya da şeritler üzerinde sürükleme bir aralık seçer. `Ctrl+Shift`+tıklama birini ekler veya çıkarır.
- Seçili insert'leri taşımak için bir şeridi yana sürükleyin ya da `Alt+Left` ve `Alt+Right` tuşlarına basın.
- Insert menüsü için bir şeride sağ tıklayın. `F2` seçili insert'i yeniden adlandırır ve renklendirir.
- `Del` veya insert menüsündeki `Delete`, seçili insert'leri siler. LYNX SHARE önce sorar. Soruyu bir dahaki sefere atlamak için `Seçimimi hatırla`'ı işaretleyin.

Insert'leri taşımak, araya eklemek veya silmek, send'leri, sidechain'leri ve otomasyonu kendi insert'inde tutar.

Insert menüsünün `Mixer` bölümünde ve `Seçenekler › Mixer` içinde iki komut daha vardır. `Insert'leri Playlist gibi sırala`, Playlist iz'lerine bağlı insert'leri Playlist'in sırasıyla öne alır. `Silinen insert'lere giden bağlantıları gri olarak koru (LYNX)` açıkken silinen bir insert'e giden gönderimler gri olarak kalır, böylece onları yeniden bağlayabilirsiniz.

## Sessiz ve solo

Bir insert'i susturmak için mute ışığına tıklayın. Solo için sağ tıklayın veya `Ctrl`+tıklayın. `S` seçili insert'leri solo yapar, `Alt+S` onları, kendilerini besleyen insert'lerle birlikte solo yapar.

Sessize alınmış bir insert'in metresi hareket etmeye devam eder. Yeni projelerde ve içe aktarılan FL Studio projelerinde efektlerini de çalıştırmayı sürdürür; yalnızca Master'a çıkışı ve gönderimleri kesilir. Sessize alınmış bir insert'in işlem yapmasını durdurmak için insert menüsünün `Mixer` bölümünde veya `Seçenekler › Mixer` içinde `Sessize alınan insert'ler işlenmez (LYNX, CPU tasarrufu; bu proje)` seçeneğini açın. Metresi o zaman durur ve sessizden çıkarınca efektleri sıfırdan başlar.

## Efektler

Efekt paneli, seçili insert'in on yuvasını gösterir.

- Bir yuvayı yukarı veya aşağı taşımak için sürükleyin ya da üzerinde tekerleği kullanın.
- Seçenekleri (değiştir, taşı, sil) için bir yuvaya sağ tıklayın.
- Bir yuvanın menüsü açıkken `Del`, o yuvadaki plugin'i siler.
- Yuva menüsündeki `Ses efekti rafında grupla` efekti yeni bir rack'e koyar, `Tümünü ses efekti rafında grupla` bunu tüm zincir için yapar. `Grubu çöz`, tek zincirli bir rack'in efektlerini yuvalara geri koyar.
- Panelin başlığındaki anahtar, insert'in tüm efektlerini açar veya kapatır.
- Bir efekti [Browser](/wiki/browser/)'ın Plugin veritabanından bir insert'in veya efekt yuvasının üzerine sürükleyebilirsiniz.
- Insert menüsündeki `Temiz ses`, insert'e hazır bir vokal temizleme zinciri koyar.

## Bir yuvada Edison

Ses editörü ve kaydedici olan Edison, her efekt yuvasında bulunabilir; bir insert'te birkaç tane olabilir. Penceresi, bir sıra kayıt kontrolü olan ses editörüdür.

- Insert menüsündeki `Ses editörünü aç` (`Ctrl+E`), Edison'ı insert'in ilk boş yuvasına yükler ve açar. `Ses düzenleyiciyi aç ve kaydet` (`Shift+E`), şarkı çalınca kayda hazır halde yükler.
- Kayıt modları `Şimdi`, `Girişte` (varsayılan: girişin bir eşiği geçmesini bekler), `Giriş` ve `Çalarken`'dir. `Sona ekle` yeni bir kaydı oradaki sesin ardına koyar. En uzun kayıt süresi `Sınırsız` veya 1, 3, 5, 10, 30 ya da 60 dakikadır: yeni bir yuva 30 ile başlar, `Ses düzenleyiciyi aç ve kaydet` 5 ayarlar.
- `Sync`, şarkı çalarken Edison'ın sesini şarkının zamanında çalar.
- Edison, penceresini kapattığınızda da kayda devam eder.
- `Araçlar › Makrolar › Tüm Edison örneklerini kaldır`, bir soru sorduktan sonra projedeki her Edison yuvasını boşaltır.
- FL Studio projelerindeki (.flp) Edison yuvaları sesleriyle açılır.

## Insert EQ

Her insert'in kendi EQ'su vardır. Frekansını ve seviyesini ayarlamak için bir bandı sürükleyin, genişlik için tekerleği kullanın, 0 dB'ye geri koymak için bir banda çift tıklayın. Bypass etmek için EQ'nun başlığına sağ tıklayın. Dahili EQ'suz bir Mixer isterseniz yeni projeler temiz bir mixer ile başlayacak şekilde ayarlanabilir.

## Yönlendirme, send'ler ve sidechain

![Kick insert'i seçili Mixer: send'leri şeritlerin altında kablo olarak çizilmiş, sağda efekt paneli](/assets/img/wiki/mixer-sends.webp)

Yönlendirmek istediğiniz insert'i seçin. Bütün diğer şeritlerde bir yönlendirme anahtarı belirir:

- Seçili insert'i oraya göndermek için başka bir şeritteki anahtara tıklayın. Etkinleştirdikten sonra sürüklemek send seviyesini ayarlar.
- `Ctrl`+tıklama yalnızca o insert'e yönlendirir.
- `Shift`+tıklama sidechain send yapar. `Ctrl+Shift`+tıklama yalnızca sidechain yapar.
- Sağ tıklama aynı seçenekleri bir menü olarak açar.
- Pre-fader gönderim için rota anahtarına veya gönderimin seviye düğmesine sağ tıklayıp `Fader öncesi send (bu kanalın fader'ı onu değiştirmez)`'ı işaretleyin. Gönderim o zaman `PRE` ile işaretlenir.

Kompresör gibi yan girişi olan efektlerin, dinlenecek insert'leri listeleyen bir `Sidechain kaynağı` seçicisi vardır. Birini seçmek yönlendirmeyi sizin için kurar.

## Gruplar ve VCA fader'ları

Mixer grupları, ok işaretleriyle katlanır (tümü için `Alt`+tıklama). Bir VCA fader'ı, üye insert'lerin kendi fader'ları kalırken seviyelerini birlikte oynatır. Bir tanesini insert menüsünden oluşturun: Group altında `VCA › Seçilenlerden yeni VCA` seçeneğini seçin.

## Kayıt

Her insert'in bir kayda hazırlama düğmesi vardır. Bir insert kayda hazırken Rec ve Play, ses girişini bir Playlist klibine kaydeder. `Shift+R` seçili insert'i kayda hazırlar ve kaydı tek adımda başlatır. Bkz. [Kayıt](/wiki/recording/).

Başka sesleri de kaydeden bir insert için harici bir giriş seçtiğinizde LYNX SHARE onu `Yalnızca harici giriş` olarak değiştirmek isteyip istemediğinizi sorar; böylece kayıtta yalnızca giriş bulunur. Insert menüsündeki `Mixer › Diske kayıt › Gecikme telafisi` varsayılan olarak açıktır. Kapatılırsa kaydedilen kayıtlar hiçbir gecikme telafisi uygulanmadan yerleştirilir.

## Tuşlar

`Ctrl+L` ve `Ctrl+Shift+L` bu anlamı yalnızca Mixer odaktayken taşır.

| Komut | Tuş | Ne yapar |
|---|---|---|
| Kanalları bağla | `Ctrl+L` | Seçili kanalları seçili insert'e bağlar |
| Ardışık insert'lere bağla | `Ctrl+Shift+L` | Seçili kanalları, seçili olandan başlayan insert'lere bağlar |
| Insert'leri taşı | `Alt+Left` / `Alt+Right` | Seçili insert'leri taşır |
| Yeniden adlandır ve renklendir | `F2` | Seçili insert'i yeniden adlandırır ve renklendirir |
| Solo | `S` | Seçili insert'leri solo yapar |
| Kaynaklarıyla solo | `Alt+S` | Seçili insert'leri ve onları besleyen insert'leri solo yapar |
| Insert'leri sil | `Del` | Seçili insert'leri, bir soru sorduktan sonra siler |
| Hazırla ve kaydet | `Shift+R` | Seçili insert'i kayda hazırlar ve kaydı başlatır |
| Open audio editor | `Ctrl+E` | Edison'ı insert'in ilk boş yuvasına yükler ve açar |
| Open audio editor and record | `Shift+E` | Aynısı, şarkı çalınca kayda hazır |

## Bilmekte fayda var

- `Seçenekler › Mixer` içindeki `Master'ın yanında ses yüksekliği ölçer (LYNX)`, Master çıkışının kısa vadeli ve entegre loudness'ını (LUFS) ve true peak'ini bir `Sıfırla` düğmesiyle gösterir.
- Birden fazla çıkışı olan bir enstrüman plugin'i her çıkışını kendi insert'inde çalabilir. Bkz. [Üçüncü taraf plugin'ler](/wiki/plugins/).
- `Ctrl+L` Channel Rack'te ve Mixer'da farklı işler yapar. Rack'te boş insert'leri seçer. Mixer'da seçili insert'i kullanır.
- Yönlendirme anahtarları seçili şeridde değil, diğer şeritlerde görünür.
- Sınır, proje başına 127 insert ve insert başına on efekt yuvasıdır.
- Edison'ın kaydı, proje dosyasının içinde değil, projenin klasöründe bir WAV dosyası olarak saklanır.
- Klip gridinin altındaki Session mixer yalnızca Klip başlatma iş akışına aittir. Bu Mixer'daki insert'lerin aynısını gösterir. Bkz. [Düzenler](/wiki/layouts/).

## İlgili sayfalar

- [Channel Rack](/wiki/channel-rack/)
- [Kayıt](/wiki/recording/)
- [Otomasyon](/wiki/automation/)
- [Üçüncü taraf plugin'ler](/wiki/plugins/)
- [Enstrümanlar ve efektler](/instruments-and-effects/)

---
source: b2de12e24d
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

Insert'leri taşımak, araya eklemek veya silmek, send'leri, sidechain'leri ve otomasyonu kendi insert'inde tutar.

## Sessiz ve solo

Bir insert'i susturmak için mute ışığına tıklayın. Solo için sağ tıklayın veya `Ctrl`+tıklayın. `S` seçili insert'leri solo yapar, `Alt+S` onları, kendilerini besleyen insert'lerle birlikte solo yapar.

## Efektler

Efekt paneli, seçili insert'in on yuvasını gösterir.

- Bir yuvayı yukarı veya aşağı taşımak için sürükleyin ya da üzerinde tekerleği kullanın.
- Seçenekleri (değiştir, taşı, sil) için bir yuvaya sağ tıklayın.
- `Del` seçili yuvadaki plugin'i siler.
- Panelin başlığındaki anahtar, insert'in tüm efektlerini açar veya kapatır.
- Bir efekti [Browser](/wiki/browser/)'ın Plugin veritabanından bir insert'in veya efekt yuvasının üzerine sürükleyebilirsiniz.

## Insert EQ

Her insert'in kendi EQ'su vardır. Frekansını ve seviyesini ayarlamak için bir bandı sürükleyin, genişlik için tekerleği kullanın, 0 dB'ye geri koymak için bir banda çift tıklayın. Bypass etmek için EQ'nun başlığına sağ tıklayın. Dahili EQ'suz bir Mixer isterseniz yeni projeler temiz bir mixer ile başlayacak şekilde ayarlanabilir.

## Yönlendirme, send'ler ve sidechain

![Kick insert'i seçili Mixer: send'leri şeritlerin altında kablo olarak çizilmiş, sağda efekt paneli](/assets/img/wiki/mixer-sends.webp)

Yönlendirmek istediğiniz insert'i seçin. Bütün diğer şeritlerde bir yönlendirme anahtarı belirir:

- Seçili insert'i oraya göndermek için başka bir şeritteki anahtara tıklayın. Etkinleştirdikten sonra sürüklemek send seviyesini ayarlar.
- `Ctrl`+tıklama yalnızca o insert'e yönlendirir.
- `Shift`+tıklama sidechain send yapar. `Ctrl+Shift`+tıklama yalnızca sidechain yapar.
- Sağ tıklama aynı seçenekleri bir menü olarak açar.

Kompresör gibi yan girişi olan efektlerin, dinlenecek insert'leri listeleyen bir `Sidechain kaynağı` seçicisi vardır. Birini seçmek yönlendirmeyi sizin için kurar.

## Gruplar ve VCA fader'ları

Mixer grupları, ok işaretleriyle katlanır (tümü için `Alt`+tıklama). Bir VCA fader'ı, üye insert'lerin kendi fader'ları kalırken seviyelerini birlikte oynatır. Bir tanesini insert menüsünden oluşturun: Group altında `VCA › Seçilenlerden yeni VCA` seçeneğini seçin.

## Kayıt

Her insert'in bir kayda hazırlama düğmesi vardır. Bir insert kayda hazırken Rec ve Play, ses girişini bir Playlist klibine kaydeder. `Shift+R` seçili insert'i kayda hazırlar ve kaydı tek adımda başlatır. Bkz. [Kayıt](/wiki/recording/).

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
| Efekti sil | `Del` | Seçili yuvadaki plugin'i siler |
| Hazırla ve kaydet | `Shift+R` | Seçili insert'i kayda hazırlar ve kaydı başlatır |

## Bilmekte fayda var

- `Sese render al` (bir insert'in ses klibine bounce edilmesi), yerine geçtiği insert'ten yaklaşık 5 dB daha yüksek çalar. Bu, 0.1.25 sürüm notlarındaki bilinen bir sorundur.
- `Ctrl+L` Channel Rack'te ve Mixer'da farklı işler yapar. Rack'te boş insert'leri seçer. Mixer'da seçili insert'i kullanır.
- Yönlendirme anahtarları seçili şeridde değil, diğer şeritlerde görünür.
- Sınır, proje başına 127 insert ve insert başına on efekt yuvasıdır.

## İlgili sayfalar

- [Channel Rack](/wiki/channel-rack/)
- [Kayıt](/wiki/recording/)
- [Otomasyon](/wiki/automation/)
- [Üçüncü taraf plugin'ler](/wiki/plugins/)
- [Enstrümanlar ve efektler](/instruments-and-effects/)

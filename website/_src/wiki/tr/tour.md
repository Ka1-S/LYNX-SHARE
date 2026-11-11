---
source: edf15b371e
---
![Demo projesiyle ana pencere: üstte araç çubuğu, solda Browser, arkada Playlist ve üzerinde yüzen Channel Rack](/assets/img/wiki/desktop.webp)

LYNX SHARE'in tek bir ana penceresi vardır. Üst kenar boyunca bir araç çubuğu uzanır. Altında beş pencere tutan bir masaüstü bulunur: Browser, Playlist, Channel Rack, Piano Roll ve Mixer. Yeni bir başlangıçta Browser solda durur, Playlist kalan alanı doldurur ve Channel Rack onun üzerinde yüzer. Piano Roll ve Mixer, çağırana kadar gizlidir.

## Etrafı tanıma

1. Fareyi herhangi bir kontrolün üzerine getirin. Menülerin altındaki ipucu çubuğu onun ne yaptığını ve hangi fare düğmelerini ve tuşları aldığını söyler.
2. Channel Rack'i gizlemek için `F6`, göstermek için yeniden `F6` tuşuna basın.
3. Mixer'i göstermek için `F9`, sonra Piano Roll için `F7` tuşuna basın.
4. Taşımak için bir pencereyi başlık şeridinden sürükleyin. Büyütmek için başlığa çift tıklayın, eski haline getirmek için yine çift tıklayın.
5. Her şeyi yerine koymak için `Görünüm › Düzeni sıfırla` komutunu seçin.

## Araç çubuğu

![Araç çubuğu: menüler ve ipucu çubuğu, transport, master knob'ları, kayıt seçenekleri, snap ve pattern, pencere düğmeleri, dosya düğmeleri, akor göstergesi, ölçerler ve çıkış monitörü](/assets/img/wiki/toolbar-1920.webp)

Soldan sağa, varsayılan düzende:

| Grup | İçerdikleri |
|---|---|
| Menü ve ipucu çubuğu | `Dosya`, `Düzen`, `Ekle`, `Pattern'ler`, `Görünüm`, `Seçenekler`, `Araçlar` ve `Yardım` menüleri ve altlarındaki ipucu çubuğu. Her zaman ilk |
| Transport | `Pattern` ve `Song` modu düğmeleri, Çal, Durdur ve Kayıt, BPM cinsinden tempo ve ölçü, vuruş ve step olarak konum |
| Master ses ve pitch | `MASTER` ses knob'u ve sent cinsinden `PITCH` knob'u |
| Kayıt seçenekleri | Metronom, geri sayım, döngülü kayıt, adım adım giriş ve diğer kayıt anahtarları |
| Snap ve pattern | Ana snap (varsayılan olarak `Çizgi (zoom'a göre)`), geçerli pattern, yeni pattern için bir düğme ve pattern menüsü |
| Pencere kısayolları | Her pencere için bir düğme ve plugin seçici |
| Dosya ve geri alma | Yeni, Aç, Kaydet, Dışa aktar, Geri al ve Yinele |
| Akor algılama | Bir MIDI aygıtında veya yazı klavyesinde çalınan akorun adını verir |
| CPU, bellek ve polifoni | Ses CPU yükü, kullanılan bellek ve ses sayısı |
| Çıkış monitörü | Çıkışın bir osiloskobu. Tıklamak diğer modlarında ilerletir |

`Denetleyicilere çoklu bağlantı` adlı bir grup da vardır ve varsayılan olarak gizlidir.

`Seçenekler › Araç çubuğu` araç çubuğunu değiştirir: `Araç çubuğunu düzenle (taşımak, eklemek veya kaldırmak için panelleri sürükleyin)`, `Ayırıcılar`, `Araç çubuğu panellerini gizle`, `Preset'ler`, her grup için bir anahtar ve `Simge boyutu` (küçük, orta veya büyük).

## Transport

| Kontrol | Tuş | Ne yapar |
|---|---|---|
| Pattern modu | `L` | Geçerli pattern'i döngüde çalar. Channel Rack'i göstermek için düğmeye sağ tıklayın |
| Song modu | `L` | Playlist aranjmanını çalar. Playlist'i göstermek için düğmeye sağ tıklayın |
| Çal | `Space` | Çalar. Yeniden basılırsa durur ve başladığı yere döner. `Ctrl+Space` duraklatır |
| Durdur | | Durdurur ve başa döner. Çift tık ayrıca tüm notaları kapatır (all notes off) |
| Kayıt | `R` | Kaydı açar veya kapatır. Bkz. [Kayıt](/wiki/recording/) |

## İpucu çubuğu

Boştayken ipucu çubuğu `Point at anything for help` yazar; yanında kısayol listesi için `F1`, pencereler için `F5` ile `F9` ve çal/durdur için `Space` görünür. Bir kontrolün üzerindeyken kontrolün adını verir ve orada sürüklemenin, `Ctrl`, `Alt` ve sağ tıkın ne yaptığını listeler. Arka planda plugin taraması gibi uzun işler ilerlemesini onda bildirir.

## Beş pencere

| Pencere | Tuş | Ne işe yarar |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Pattern, ses ve otomasyon kliplerini bir şarkıya aranje etmek |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Enstrüman başına bir satır; geçerli pattern'in step'leriyle |
| [Piano Roll](/wiki/piano-roll/) | `F7` | Geçerli pattern'de bir kanalın notaları |
| [Mixer](/wiki/mixer/) | `F9` | Fader'lı insert'ler, efektler ve yönlendirme |
| [Browser](/wiki/browser/) | `Alt+F8` | Sample'lar, preset'ler, projenin içeriği ve plugin veritabanı |

Her tuş pencereyi gösterir ve odağı ona verir, ya da zaten odaktaysa gizler. Tek başına `F8`, beş pencereden biri olmayan plugin seçiciyi açar. Araç çubuğunun pencere düğmeleri tuşlarla aynı işi yapar. Birine sağ tıklamak o pencereyi büyütülmüş gösterir. `Enter` Playlist'i büyütür veya eski haline getirir.

## Pencereler nasıl hareket eder ve yerleşir

Her pencerenin bir başlık şeridi vardır. Solunda pencerenin kendi seçeneklerini taşıyan bir menü düğmesi, sonra `Ayrılmış (kendi penceresi)`, `Büyütülmüş` ve `Kapat` yer alır. Sağında büyütme ve kapatma düğmeleri bulunur.

- Bir pencereyi taşımak için başlık şeridini sürükleyin. Diğer pencerelere ve kenarlara yapışır. Yapışmadan taşımak için `Alt` tuşunu basılı tutun.
- Yeniden boyutlandırmak için bir kenarı sürükleyin.
- `Ayrılmış (kendi penceresi)`, pencereyi ikinci bir monitör için ayrı bir sistem penceresine çevirir. Ayrılmış pencerede onu geri koyan bir `Yerleştir` düğmesi vardır. `Görünüm › Ayrılmış paneller (kendi penceresi)` beşini de listeler.
- `Görünüm › Ekran dışındaki panelleri geri getir`, masaüstünün dışında kalmış bir pencereyi geri getirir.
- `Görünüm › Düzeni farklı kaydet...` düzeni bir adla saklar, `Görünüm › Düzen yükle` birini geri getirir.

Browser, masaüstünün arka planıdır: yan tarafta yerleşik kalır ve diğer pencereler her zaman onun üzerinde durur.

## Bilmekte fayda var

- Bir şeyi taşıyana, yeniden boyutlandırana veya ayırana kadar, ana pencere boyut değiştirdikçe pencereler varsayılan yerlerine döner. Ondan sonra düzeniniz hatırlanır ve pencereyle birlikte ölçeklenir.
- `F12` plugin, kanal ayarları ve araç pencerelerini kapatır. Beş ana pencereyi kapatmaz.
- Yazı klavyesi piyanosu açıkken (`Ctrl+T`) harf tuşları nota çalar ve tek harfli kısayolların çoğu durdurulur.
- `Görünüm › Yerleşim stili`, Pattern iş akışı (varsayılan) ile Playlist'in yanına bir klip ızgarası ekleyen Klip başlatma iş akışı arasında geçiş yapar.

## İlgili sayfalar

- [İlk şarkınız](/wiki/first-song/)
- [Yerleşimler](/wiki/layouts/)
- [Kısayollar](/wiki/shortcuts/)
- [Ayarlar](/wiki/settings/)

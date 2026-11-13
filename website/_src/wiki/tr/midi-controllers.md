---
source: d2bebc77b1
---
![Ayarların MIDI sayfası: giriş aygıtları, master sync'li çıkış aygıtları ve harici sync](/assets/img/wiki/settings-midi.webp)

LYNX SHARE, notaları ve kontrolcü hareketlerini herhangi bir MIDI giriş aygıtından alır. Aygıtlar ayarların MIDI sayfasında kurulur: `Seçenekler › MIDI ayarları...` ya da `F10`. Donanımınız yoksa klavye ve dokunmatik klavye de nota çalar.

## Bir klavye bağlamak

1. Klavyeyi takın ve `Seçenekler › MIDI ayarları...` (`F10`) yolunu açın.
2. `Giriş aygıtları` altında bulun ve işaretleyin. Listede yoksa `Aygıt listesini yenile` düğmesine tıklayın.
3. İki kutuyu `Her MIDI kanalı` ve `Seçili kanalları çalar` üzerinde bırakın.
4. Ayarları kapatın ve [Channel Rack](/wiki/channel-rack/)'te bir kanal seçin.
5. Çalın. Seçili kanal ses verir.

## Giriş aygıtları

| Ayar | Ne yapar |
|---|---|
| Onay kutusu | Aygıtı açar veya kapatır |
| `Her MIDI kanalı` | Her MIDI kanalını ya da yalnızca tek bir kanalı dinler. `MPE denetleyici` bu listenin sonundadır |
| `Seçili kanalları çalar` | Notalar Channel Rack'te seçili kanallara ya da her zaman sabit bir kanala gider |
| `Velocity` | `Çalındığı gibi`, `Yumuşak (hafif çalış daha yüksek duyulur)`, `Sert (daha güçlü çalış gerektirir)` veya `Sabit (her nota tam velocity'de)` |
| `Performans modu kanalı` | Performans modu açıkken bu MIDI kanalındaki notalar Playlist kliplerini başlatır |
| `Song işaretçisine atlama` | Bu MIDI kanalındaki notalar çalmak yerine şarkıyı bir işaretçiye götürür |
| `Kontrolcü türü` | `Otomatik algıla`, `Genel denetleyici` veya bir kontrolcü profili |
| `Ayak pedalı nota bırakmayı yönetir` | Varsayılan olarak açıktır. Bir sustain pedalı (CC 64) veya sostenuto pedalı (CC 66) notaları tutar. Kapalıyken pedal plugin'lere bir kontrolcü olarak gider |

## Bir knob'u bir kontrole bağlamak

1. LYNX SHARE'de knob'a veya fader'a sağ tıklayın.
2. `MIDI kontrolcüye bağla (kontrolcüde bir knob oynatın)` seçeneğini seçin.
3. Donanımınızdaki knob'u veya fader'ı oynatın. Bağlantı kurulur.

Bağlantı projeyle birlikte kaydedilir. Kaldırmak için kontrole yeniden sağ tıklayın ve `Kontrolcü bağlantısını kaldır` seçin. Menü öğesi kullanılan CC numarasını gösterir.

| Komut | Tuş | Ne yapar |
|---|---|---|
| `Kontrolcüye bağla...` | yok | Kontrol için `Uzaktan kontrol ayarları` penceresini açar; bağlantısını orada elle görür ve ayarlarsınız (aşağıda) |
| `Genel bağlantıyı geçersiz kıl...` | yok | Her projede çalışan bir bağlantı kurar. Projede değil, ayarlarda saklanır |
| `Genel bağlantıyı geçersiz kıl... (tüm projeler için: kontrolcüde bir knob oynatın)` | yok | Tek adımda genel bir bağlantı yapar: ardından donanım kontrolünü oynatın. Parantezsiz öğe bunun yerine `Genel bağlantı ayarları` penceresini açar |
| `Genel bağlantıyı kaldır` | yok | Onu yeniden kaldırır |
| `Denetleyicilere çoklu bağlantı` | `Ctrl+J` | Açıkken, oynattığınız kontroller sırayla, sonra oynatacağınız donanım kontrolleriyle bağlanır. `Seçenekler` menüsündedir |
| `Pickup (devralma modu)` | yok | MIDI sayfasında. Kontrolcü, bir parametreyi ancak şu anki değerine ulaştıktan sonra oynatır; böylece hiçbir şey sıçramaz |

MIDI sayfası bağlantıları `Bağlantılar (proje > genel)` altında listeler. `Kontrolcü türü` değeri `Genel denetleyici` olan bir cihaz yalnızca elle bağladığınızı yapar.

Üçüncü taraf bir plugin'in parametrelerine, kanalın menüsündeki ya da efekt yuvasının menüsündeki `Plugin parametresini otomatikleştir` ile ulaşılır. Oradaki her parametrenin aynı bağlantı öğeleri vardır. Bkz. [Otomasyon](/wiki/automation/).

## Uzaktan kontrol ayarları

`Kontrolcüye bağla...` FL Studio'daki gibi bu pencereyi açar. `Genel bağlantı ayarları` (`Genel bağlantıyı geçersiz kıl...` ile açılır) bağlantı listesi, `Omni`, dahili kontrolcü ve `Çakışmaları kaldır` dışında aynı alanlara sahiptir. MIDI sayfasındaki listede bir bağlantıya çift tıklamak da onu açar.

| Ayar | Ne yapar |
|---|---|
| `Port`, `Kanal`, `Ctrl` | MIDI giriş portu numarası, MIDI kanalı ve kontrolcü numarası. Standart kontrolcüleri adlarıyla görmek için `Ctrl` üzerine sağ tıklayın. O listedeki `Nota C0` bir nota bağlantısı yapar: başka bir tuş için numarayı oradan yukarı çıkarın |
| `Otomatik algıla` | Varsayılan olarak açık. Cihazınızda bir kontrolü oynatın, numaralar kendiliğinden dolar |
| `Omni` | Bağlantı seçili kanalı izler; bir mixer yuvasındaki efekt için seçili mixer izini izler |
| `Mod` | Bir nota veya düğme bağlantısının nasıl davrandığı. `Varsayılan` ve `Hold` tuşu izler: basılıyken velocity'sini, bırakılınca sıfırı verir. `Kilitle` her basışta iki uç arasında geçiş yapar. `Inc` her basışta değeri bir adım yukarı çıkarır. `Kal` velocity'yi ayarlar ve bırakıldıktan sonra korur |
| Dahili kontrolcü | Kontrolü bunun yerine projenin dahili kontrolcülerinden birine bağlar. Yalnızca projede biri varsa gösterilir. `Otomasyon kaydet`, o kontrolcünün hareketlerinin otomasyon olarak kaydedilmesini sağlar |
| `Eşleme formülü` | Girişin kontrole nasıl eşlendiği; `Giriş` değişkeni kullanılır. `Preset'ler` FL'nin formül listesini içerir. Yazdığınız formülü uygulamak için `Enter`'a basın. Yanındaki eğri sonucu gösterir |
| `Yumuşatma` | Kontrolün hareketlerini yumuşatır. `Zaman` süreyi belirler: negatif değerler için 1 kutup, pozitif değerler için 3 kutup |
| `Çakışmaları kaldır` | Varsayılan olarak açık. `Kabul et`'e tıkladığınızda aynı kontrolcüyü kullanan diğer bağlantıları kaldırır. Böyle bir bağlantı varken kırmızıya döner |
| `Sıfırla`, `Atla`, `Kabul et` | `Kabul et` bağlantıyı kaydeder (bir geri alma adımı). Multilink ile sıraya alınmış birden çok kontrol varsa `Atla` bir sonrakine geçer |

## Kontrolcü profilleri

Bir kontrolcü profili, bağlanacak hiçbir şey olmadan cihazın transport düğmelerini, fader'larını, düğmelerini, pad'lerini, ışıklarını ve ekranını ayarlar.

1. `Seçenekler › MIDI ayarları...` (`F10`) açın ve `Giriş aygıtları` altında cihazı işaretleyin.
2. Cihazın `Kontrolcü türü` değerini `Otomatik algıla` üzerinde bırakın. Parantez içindeki ad, cihazın port adından bulunan profildir.
3. `Genel denetleyici` görünüyorsa profili listeden kendiniz seçin.
4. Kutunun yanındaki satırı okuyun. `Feedback to`, ışıkları, motorlu fader'ları ve ekran metnini alan çıkış portunu belirtir.

| Profil | Neyi eşler |
|---|---|
| `Mackie Control Universal` | Fader'lar insert'lerin sesine ve Master'a. V-Pot'lar pan'a, seçili kanalın plugin'inin parametrelerine veya Channel rack'in seslerine. Her strip için arm, solo, mute ve select, fader bank'ları, play, stop, record, rewind, forward, Pattern veya Song modu, metronom, undo ve jog tekerleği. Ekran adları ve değerleri gösterir |
| `Mackie Control Extender (XT)` | 8 strip daha için aynısı. Cihazlar tek bir bank'ı paylaşır, sıra `Giriş aygıtları` listesindeki gibidir |
| `Novation Launchkey MK3 (DAW mode)` | Pot'lar pan, ses veya plugin parametrelerine, fader'lar sese, fader düğmeleri bir insert seçer. Bank, play, stop, record, loop, metronom ve undo düğmeleri. Session pad'leri seçili kanalın 16 step'idir. Klavyenin ikinci portunda kullanın |
| `Akai APC Key 25 mk2` | Düğmeler ses, pan veya plugin parametrelerine. Üstteki iki pad sırası seçili kanalın 16 step'idir, alttaki üçü nota çalar. İz düğmeleri bir insert seçer. Stop, play ve record. İkinci portta kullanın |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | Klavyenin MCU/HUI portunda Mackie Control eşlemesi |
| `Korg nanoKONTROL2 (Mackie Control mode)` | Mackie Control eşlemesi. Otomatik algılanmaz: cihazı bir Mackie Control modunda başlatın ve bu türü elle seçin |

Konum gönderen düğmeler ve fader'lar soft takeover kullanır: bir kontrolü yalnızca değerine ulaştıktan sonra hareket ettirirler. Elle bağladığınız kontroller bağlantılarını korur. `Denetleyici profilleri klasörü` kendi profilleriniz (`*.lynxctl`) için klasörü açar, `Aygıt listesini yenile` onları yeniden okur.

Dahili profiller her üreticinin belgelerinden yazıldı. Henüz donanım üzerinde denenmedi.

## MPE kontrolcüleri

MPE ilk aşamadadır. Bir MPE kontrolcüsü her notayı kendi pitch bend'i, timbre'ı (CC 74) ve basıncıyla çalar.

1. MIDI sayfasında cihazın `Her MIDI kanalı` yazan kutusunu açın ve `MPE denetleyici` seçin.
2. Yanında beliren kutuda üye kanalları seçin: `Kanal 2`'den `Channels 2-16`'ya kadar.
3. Çalın. Kayıt için bkz. [Kayıt](/wiki/recording/).

- Kaydedilen bir nota bunların her birinden tek değer tutar: bend'i ince pitch olarak, timbre'ı Mod X olarak ve en güçlü basıncı Mod Y olarak. Bir eğri değildir.
- Dahili enstrümanlardan Sampler ve dahili synth bu değerleri izler.
- Üçüncü taraf bir plugin, plugin penceresinin `Wrapper ayarları` bölümünde `MIDI` altındaki `MPE` açıkken MPE alır. Her nota o zaman kendi MIDI kanalından, 2 ile 16 arasında çıkar.
- Henüz gerçek bir MPE kontrolcüsüyle denenmedi.

## MIDI Map Mode ve Key Map Mode

[Klip başlatma iş akışında](/wiki/layouts/) klipler, sahneler ve düğmeler Ableton Live'daki gibi eşlenebilir. `İkisi de`, `Grid`, `Song` anahtarının yanındaki `MIDI` ve `Ton` düğmeleri, `Ctrl+M` ve `Ctrl+K` gibi, modları açar.

1. MIDI Map Mode için `Ctrl+M`'ye, Key Map Mode için `Ctrl+K`'ye basın. Eşlenebilen her öğe renklenir: MIDI için mavi, tuşlar için turuncu.
2. Bir öğeye tıklayın: bir klip slotu, bir sahne, bir izin durdurma düğmesi, `Tüm klipleri durdur`, play, stop, record, `Oturum Kaydı`, metronom veya Session mixer ya da Device View'un bir düğmesi.
3. MIDI cihazınızda bir kontrolü oynatın veya bir tuşa basın; Key Map Mode'da ise bilgisayar tuşuna basın. Öğe atamasını gösterir, örneğin `1/19` (kanal ve kontrolcü).
4. Eşlemeler Browser'ın yerine listelenir: Channel, Note/Control veya Key, Path, Name, Min ve Max. Min veya Max'ı sürükleyin ya da bir değer yazmak için çift tıklayın. Bir satıra sağ tıklayınca `Aralığı ters çevir` ve `Delete` çıkar.
5. Moddan çıkmak için aynı tuşa tekrar basın.

## Bilgisayar klavyesi

`Ctrl+T` ya da `Seçenekler › Klavyeden piyano`, bilgisayar klavyesini bir piyanoya çevirir. `Z` ile `M` arasındaki ve `Q` ile `P` arasındaki sıralar seçili kanalları çalar.

- Açıkken harf ve rakam tuşları nota olur. Piano Roll'un ve Playlist'in tek harfli araç tuşları yalnızca kapalıyken çalışır.
- Her zaman Piano Roll'un velocity değeriyle çalar. MIDI sayfasındaki `Velocity` ayarı MIDI aygıtları içindir.
- Aynı anahtar `Seçenekler › Genel ayarlar...` içinde de vardır.

## Dokunmatik klavye

![Dokunmatik klavye: oktav düğmeleri, tuş boyutu, velocity modu ve C4'ten başlayan bir piyano tuşu sırası](/assets/img/wiki/touch-keyboard.webp)

`Görünüm › Dokunmatik klavye` (`Alt+F7`), fareyle veya dokunmatik ekranla çaldığınız bir piyano açar. Channel Rack'te seçili kanalı çalar ve adını sağda gösterir.

| Ayar | Ne yapar |
|---|---|
| `-` ve `+` | Bir oktav aşağı veya yukarı |
| Tuş boyutu | `Küçük tuşlar`, `Orta tuşlar` ya da `Büyük tuşlar` |
| `Velocity: dokunduğunuz yer` | Tuşun üst yanına yakın dokunmak yumuşak, alt yanına yakın dokunmak sert çalar |
| `Velocity: sabit` | Her nota, kaydırıcıyla ayarlanan velocity değerinde çalar |
| `Nota adları` | Tuşların üzerinde adları gösterir |

## MIDI sync

MIDI sayfasındaki `Çıkış aygıtları`, aktarımı donanıma ve diğer yazılımlara gönderir. Bir aygıt için `Master senkron gönder` kutusunu işaretleyin ve `MIDI clock` (Start, Continue, Stop ve Song Position Pointer ile) ya da MIDI Time Code seçin. `Ofset (ms)` pozitif değerle sync'i daha geç, negatif değerle daha erken gönderir. `All notes off gönder` bu mesajı başlatmada ve dışa aktarmadan sonra aygıta gönderir.

`Harici clock senkronu` ters yönde çalışır: LYNX SHARE seçtiğiniz girişin MIDI clock'unu, tempo, Start, Continue, Stop ve Song Position Pointer ile birlikte izler. `Harici senkron ofseti (milisaniye)` pozitif değerle LYNX SHARE'i daha geç, negatif değerle daha erken çaldırır. `Durum` bir clock'un çalışıp çalışmadığını gösterir.

## Bilmekte fayda var

- `Harici clock senkronu` için bir giriş seçmek, o aygıtı `Giriş aygıtları` altında açar.
- Bağlı bir MIDI kontrolcüyle oynatılan tempo, ses kliplerini yeniden boyutlandırmaz. Bunun yerine son tempoyu yazın veya ayarlayın.
- Çaldıklarınızı kaydetmek için bkz. [Kayıt](/wiki/recording/). Gelen notalar [Piano Roll](/wiki/piano-roll/)'da bir gama oturtulabilir.
- Bir plugin'e gönderilen MIDI CC, kanal aftertouch ve pitch bend, `MIDI kontrolcüyü otomatikleştir (CC, aftertouch, pitch bend)` ile otomatikleştirilebilir.
- Pedalla tutulan bir nota, pedal kalkana kadar kaydedilir.
- Bir otomasyon klibi, bir kontrolü kontrolcüden oynatırken onu geri almaz. Kontrol, çalma bir sonraki başladığında otomasyonunu yeniden izler.
- `Uzaktan kontrol ayarları` içinde yapılan nota bağlantıları bir .flp dışa aktarımına yazılmaz.
- `Ctrl+M` ve `Ctrl+K` eşleme modlarını yalnızca Klip başlatma iş akışında başlatır. Map Mode'da henüz makro eşleme veya nota aralığı yoktur.

## İlgili sayfalar

- [Kayıt](/wiki/recording/)
- [Otomasyon](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Ayarlar](/wiki/settings/)
- [Kısayollar](/wiki/shortcuts/)

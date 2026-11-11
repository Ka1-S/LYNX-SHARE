---
source: 3b54a39c97
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
| `Her MIDI kanalı` | Her MIDI kanalını ya da yalnızca tek bir kanalı dinler |
| `Seçili kanalları çalar` | Notalar Channel Rack'te seçili kanallara ya da her zaman sabit bir kanala gider |
| `Velocity` | `Çalındığı gibi`, `Yumuşak (hafif çalış daha yüksek duyulur)`, `Sert (daha güçlü çalış gerektirir)` veya `Sabit (her nota tam velocity'de)` |
| `Performans modu kanalı` | Performans modu açıkken bu MIDI kanalındaki notalar Playlist kliplerini başlatır |
| `Song işaretçisine atlama` | Bu MIDI kanalındaki notalar çalmak yerine şarkıyı bir işaretçiye götürür |

## Bir knob'u bir kontrole bağlamak

1. LYNX SHARE'de knob'a veya fader'a sağ tıklayın.
2. `MIDI kontrolcüye bağla (kontrolcüde bir knob oynatın)` seçeneğini seçin.
3. Donanımınızdaki knob'u veya fader'ı oynatın. Bağlantı kurulur.

Bağlantı projeyle birlikte kaydedilir. Kaldırmak için kontrole yeniden sağ tıklayın ve `Kontrolcü bağlantısını kaldır` seçin. Menü öğesi kullanılan CC numarasını gösterir.

| Komut | Tuş | Ne yapar |
|---|---|---|
| `Override global link...` | yok | Her projede çalışan bir bağlantı kurar. Projede değil, ayarlarda saklanır |
| `Genel bağlantıyı kaldır` | yok | Onu yeniden kaldırır |
| `Denetleyicilere çoklu bağlantı` | `Ctrl+J` | Açıkken, oynattığınız kontroller sırayla, sonra oynatacağınız donanım kontrolleriyle bağlanır. `Seçenekler` menüsündedir |
| `Pickup (devralma modu)` | yok | MIDI sayfasında. Kontrolcü, bir parametreyi ancak şu anki değerine ulaştıktan sonra oynatır; böylece hiçbir şey sıçramaz |

MIDI sayfası bağlantıları `Bağlantılar (proje > genel)` altında listeler. Aynı sayfadaki `Kontrolcü türü`, `Genel kontrolcü` der.

Üçüncü taraf bir plugin'in parametrelerine, kanalın menüsündeki ya da efekt yuvasının menüsündeki `Plugin parametresini otomatikleştir` ile ulaşılır. Oradaki her parametrenin aynı bağlantı öğeleri vardır. Bkz. [Otomasyon](/wiki/automation/).

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

## İlgili sayfalar

- [Kayıt](/wiki/recording/)
- [Otomasyon](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Ayarlar](/wiki/settings/)
- [Kısayollar](/wiki/shortcuts/)

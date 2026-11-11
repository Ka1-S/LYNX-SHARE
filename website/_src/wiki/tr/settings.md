---
source: 841fdd7d67
---
![Ayarlar penceresinin Genel sayfası, üstte yedi sayfa sekmesi](/assets/img/wiki/settings-general.webp)

Ayarlar penceresinin yedi sayfası vardır: Ses, MIDI, Genel, Tema, Dosyalar, Proje ve Bilgi. İlk beşi programa aittir. Proje ve Bilgi açık projeye aittir ve onunla birlikte kaydedilir. Her sayfanın `Seçenekler` menüsünün en üstünde kendi girişi vardır.

## Bir ayarı değiştirmek

1. `Seçenekler` menüsünü açın ve bir sayfa seçin, örneğin `Genel ayarlar...`.
2. Başka bir sayfaya geçmek için üstteki başka bir sekmeye tıklayın.
3. Ayarı değiştirin. Dil ve arayüz yazı tipi dışında hemen uygulanır; bu ikisi LYNX SHARE yeniden başlatıldığında uygulanır.

`F10` MIDI sayfasını, `F11` Bilgi sayfasını açar.

## Ses

`Seçenekler › Ses ayarları...`. Sürücü, aygıtlar, örnekleme hızı, arabellek boyutu ve kayıt. Bu sayfayı [Ses ayarı](/wiki/audio-setup/) anlatır.

## MIDI

![MIDI sayfası: giriş aygıtları, master sync'li çıkış aygıtları ve harici sync](/assets/img/wiki/settings-midi.webp)

`Seçenekler › MIDI ayarları...`

| Ayar | Ne yapar |
|---|---|
| Giriş aygıtları | Her aygıt kapatılabilir, yalnızca tek bir MIDI kanalını dinleyebilir ve seçili kanallar yerine Channel Rack'in sabit bir kanalını çalabilir |
| `Aygıt listesini yenile` | Aygıtları yeniden arar |
| `Master senkron gönder` | Bir çıkış aygıtına Song Position Pointer'lı MIDI clock ya da MIDI Time Code gönderir. `Ofset (ms)` onu daha geç veya daha erken gönderir |
| `All notes off gönder` | Başlatmada ve dışa aktarmadan sonra aygıta all notes off gönderir |
| `Harici clock senkronu` | LYNX SHARE seçilen girişin MIDI clock'unu izler: tempo, start, continue, stop ve konum |
| `Velocity` | MIDI aygıtlarından gelen notalar için: çalındığı gibi, yumuşak, sert veya sabit |
| `Performans modu kanalı` | Bu kanaldaki notalar Performans modunda Playlist kliplerini başlatır |
| `Song işaretçisine atlama` | Bu kanaldaki notalar çalmak yerine şarkıyı bir işaretçiye götürür |
| `Pickup (devralma modu)` | Kontrolcü, bir parametreyi ancak şu anki değerine ulaştıktan sonra oynatır |
| Bağlantılar | Bu projenin kontrolcü bağlantıları, sonra genel olanlar; `Bağlantıyı kaldır` ve `Tümünü kaldır` ile |

## Genel

`Seçenekler › Genel ayarlar...`

| Ayar | Ne yapar |
|---|---|
| `Yerleşim stili` | Pattern iş akışı (varsayılan) ya da Klip başlatma iş akışı |
| `Live setlerinin oturum klipleri` | İçe aktarılan bir Live setinin session kliplerinin gideceği yer: klip gridi veya aranjmandan sonra Playlist |
| `Dil`, `Arayüz yazı tipi` | Bir sonraki açılışta uygulanır |
| `Arayüz boyutu`, `Menü yoğunluğu` | Tüm arayüzün ölçeği ve kompakt ya da rahat menüler |
| `Dalga formları` | Temiz (klibin rengi) ya da spektral renkler; isteğe bağlı olarak stereo kanallar ayrı |
| `Nota adları`, `Siyah tuşlar` | İngilizce, Germen veya solfej adları; diyez veya bemolle |
| `Plugin stili` | Dahili plugin pencerelerinin görünümü: otomatik ya da her plugin için tek bir stil |
| `Animasyonlar`, `Animasyon hızı` | Arayüz animasyonları ve çalma imlecinin ile ölçerlerin ne sıklıkla hareket ettiği |
| `GPU ile çiz (Direct2D)` | Grafik sürücüsü çöküyorsa ya da ekran titriyorsa kapatın |
| Açılış | Açılış ekranı, açılış sesi ve her açılışta arka planda yeni plugin aramak |
| Düzenleme | Klavyeden piyano, kanal editörlerini açık tutmak, arama süresini hatırlamak, esnetilmiş pattern kliplerini döngüye almak |
| `Maksimum geri alma seviyesi` | 25 ile 1000 arası |
| Yedek | Yedek kopyanın ne sıklıkla kaydedildiği, proje başına kaç tanesinin tutulduğu ve `Yedek klasörünü aç` |
| `Plugin'leri ayrı süreçte çalıştır` | Çöken bir plugin LYNX SHARE'i de çökertmez. Varsayılan olarak kapalı |
| `Eksik plugin'lerin yerine LYNX SHARE karşılıklarını kullan` | Varsayılan olarak açık. Proje özgün plugin'i korur |
| `Çökme raporları` | Bir çökme veya donmadan sonra sor (varsayılan), her zaman gönder ya da hiç gönderme |
| `Güncellemeleri otomatik denetle` | Açılışta güncelleme denetimi, `Şimdi denetle` ile birlikte |

## Tema

`Seçenekler › Tema (renkler, arka plan)...`

| Ayar | Ne yapar |
|---|---|
| Tema kutusu | Bir preset seçer. `İçe aktar...` ve `Dışa aktar...` `.lynxtheme` dosyalarını okur ve yazar, `Preset kaydet...` geçerli temayı saklar, `Varsayılan` sıfırlar |
| `Vurgu rengi`, `Arka plan`, `Çalışma alanı` | Bir temanın üç rengi. Çalışma alanı rengi arka planı kendiliğinden izleyebilir |
| Çalışma alanı görüntüsü | Pencerelerin arkasında bir PNG, JPEG veya GIF; `Görüntü sığdırma`, `Görüntü opaklığı` ve `Karart` ile |
| Açılış sesi | 30 saniye veya daha kısa kendi sesiniz ya da LYNX SHARE'in kendi sesi |

## Dosyalar

`Seçenekler › Dosya ayarları (arama klasörleri)...`

| Ayar | Ne yapar |
|---|---|
| Eksik sample'lar için arama klasörleri | Eksik dosyalar penceresi bu klasörlerde ve alt klasörlerinde kendiliğinden arar |
| Browser klasörleri | Browser'ın klasör listesinde gösterilir ve eksik sample'lar için de aranır |
| Plugin klasörleri | `Seçenekler › Plugin tara` ile plugin için taranır |

## Proje

![Proje sayfası: zaman ayarları, groove ve akort, timebase ve Playlist seçenekleri](/assets/img/wiki/settings-project.webp)

`Seçenekler › Proje ayarları (tempo, ölçü işareti)...`

| Ayar | Ne yapar |
|---|---|
| `Zaman ayarları` | Zaman bölümü (ölçü başına vuruş, vuruş başına step) ya da geleneksel bir ölçü işareti (pay ve payda) |
| `Tempo`, `Ölçü başına vuruş` | Projenin tempo değeri ve bir ölçüdeki vuruş sayısı |
| `Swing`, `Master pitch` | Genel swing miktarı ve sent cinsinden master pitch |
| `Otomatik çapraz geçişler` | Bir izde üst üste binen ses klipleri, çakışma boyunca çapraz geçiş yapar |
| `Comp geçişi` | Kayıtlardan comp oluştururken kullanılan çapraz geçiş uzunluğu, milisaniye cinsinden |
| `Kliplerde kesilmiş notaları çal` | Bir pattern klibinden önce başlayan nota, klibin başından itibaren çalar |
| `Kesme grupları için hızlı tık giderme` | Cut grupları notaları keserken daha hızlı rampa |
| `Pan yasası` | Circular (merkez 0 dB, pan yapılan yanda +3 dB) ya da Triangular (yüksek yan seviyesini korur). Mixer insert'leri ve Sampler kanalları için |

## Bilgi

`Seçenekler › Proje bilgisi...`. Projenin başlığı, yazarı, türü, web bağlantısı ve yorumları ile `Proje açıldığında bu sayfayı göster`. Alt kısımda projenin ne zaman oluşturulduğu ve çalışma süresi görünür.

## Bilmekte fayda var

- Timebase, çeyrek nota başına 960 tick olarak sabittir. FL Studio projeleri buna dönüştürülür.
- Şarkı boyunca ölçü işareti değişiklikleri Proje sayfasında değildir. Playlist cetveline bir ölçü işareti işaretçisi ekleyin.
- `Plugin'leri ayrı süreçte çalıştır`, o andan sonra açılan plugin'lere uygulanır. Diğerlerini taşımak için projeyi yeniden açın.
- Güncelleme ayarları Microsoft Store sürümünde yoktur, çünkü Store onu günceller.
- Kısayollar bir Ayarlar sayfasında değil, `Seçenekler › Kısayolları yönet...` içindedir.

## İlgili sayfalar

- [Ses ayarı](/wiki/audio-setup/)
- [MIDI kontrolcüleri](/wiki/midi-controllers/)
- [Kısayollar](/wiki/shortcuts/)
- [Plugin'ler](/wiki/plugins/)
- [Yerleşimler](/wiki/layouts/)

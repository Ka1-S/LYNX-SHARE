---
source: ab2c2f14eb
---
![Settings penceresinin General sayfası, üstte sayfa sekmeleri](/assets/img/wiki/settings-general.webp)

Ayarlar penceresinin sekiz sayfası vardır: Audio, MIDI, General, Theme, Files, LYNX extras, Project ve Info. İlk altısı programa aittir. Project ve Info açık projeye aittir ve onunla birlikte kaydedilir. Her sayfanın `Seçenekler` menüsünün en üstünde kendi girişi vardır.

## Bir ayarı değiştirmek

1. `Seçenekler` menüsünü açın ve bir sayfa seçin, örneğin `Genel ayarlar...`.
2. Başka bir sayfaya geçmek için üstteki başka bir sekmeye tıklayın.
3. Ayarı değiştirin. Dil ve arayüz yazı tipi dışında hemen uygulanır; bu ikisi LYNX SHARE yeniden başlatıldığında uygulanır.

`F10` MIDI sayfasını, `F11` Bilgi sayfasını açar.

## Ses

`Seçenekler › Ses ayarları...`. Sürücü, aygıtlar, örnekleme hızı, arabellek boyutu ve kayıt. Bu sayfayı [Ses ayarı](/wiki/audio-setup/) anlatır. `Kayıt` altında `Dosya formatı` artık `WAV 16 bit (cihazın örnekleme hızı)` de sunar.

## MIDI

![MIDI sayfası: giriş aygıtları, master sync'li çıkış aygıtları ve harici sync](/assets/img/wiki/settings-midi.webp)

`Seçenekler › MIDI ayarları...`

| Ayar | Ne yapar |
|---|---|
| Giriş aygıtları | Her aygıt kapatılabilir, yalnızca tek bir MIDI kanalını dinleyebilir ve seçili kanallar yerine Channel Rack'in sabit bir kanalını çalabilir |
| `Kontrolcü türü` | Her giriş aygıtı için: `Otomatik algıla`, `Genel denetleyici` veya Mackie Control gibi bir kontrolcü profili. Yanındaki satır ışıkları ve ekranı alan portu belirtir |
| `MPE denetleyici` | Bir aygıtın MIDI kanal listesinin sonunda. Her nota kendi bend'i, timbre'ı ve basıncıyla gelir. İkinci bir kutu üye kanalları ayarlar |
| `Aygıt listesini yenile` | Aygıtları yeniden arar ve kontrolcü profillerinizi yeniden okur |
| `Denetleyici profilleri klasörü` | Kendi kontrolcü profilleriniz (`*.lynxctl`) için klasörü açar |
| `Master senkron gönder` | Bir çıkış aygıtına Song Position Pointer'lı MIDI clock ya da MIDI Time Code gönderir. `Ofset (ms)` onu daha geç veya daha erken gönderir |
| `All notes off gönder` | Başlatmada ve dışa aktarmadan sonra aygıta all notes off gönderir |
| `Harici clock senkronu` | LYNX SHARE seçilen girişin MIDI clock'unu izler: tempo, start, continue, stop ve konum |
| `Velocity` | MIDI aygıtlarından gelen notalar için: çalındığı gibi, yumuşak, sert veya sabit |
| `Performans modu kanalı` | Bu kanaldaki notalar Performans modunda Playlist kliplerini başlatır |
| `Song işaretçisine atlama` | Bu kanaldaki notalar çalmak yerine şarkıyı bir işaretçiye götürür |
| `Pickup (devralma modu)` | Kontrolcü, bir parametreyi ancak şu anki değerine ulaştıktan sonra oynatır |
| `Ayak pedalı nota bırakmayı yönetir` | Varsayılan olarak açık: sustain ve sostenuto pedalları notaları tutar. Kapalıyken pedal plugin'lere gider |
| `Oyun kumandası kullan` | Bir oyun kumandası transport'u çalıştırır ve bir MIDI kontrolcüsü gibi bağlanabilir. Varsayılan olarak kapalı |
| Bağlantılar | Bu projenin kontrolcü bağlantıları, sonra genel olanlar; `Bağlantıyı kaldır` ve `Tümünü kaldır` ile |

## Genel

`Seçenekler › Genel ayarlar...`

| Ayar | Ne yapar |
|---|---|
| `Yerleşim stili` | Pattern iş akışı (varsayılan), Klip başlatma iş akışı veya Basit iş akışı |
| `Varsayılan şablon`, `Başlangıç projesi` | `Dosya › Yeni` ile açılan şablon ve başlangıçta açılan şey: `Boş proje`, `Varsayılan şablon` veya `Son kullanılan proje`. `Dosya › Şablon olarak kaydet...` kendi şablonlarınızı ekler |
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
| `Tüm yeni eklentileri ayır` | Açık: eklediğiniz bir plugin'in penceresi panellerin üstünde ayrı açılır. Kapalı (varsayılan): ana pencerede yerleşik açılır |
| `Piano roll'da monofonik boyama` | Açık: Paint aracı her yeni notanın altındaki notaların yerini alır, Shift ise üzerlerine boyar. Kapalı: tersi |
| `Bağlı modülleri otomatik seç` | Bir plugin penceresine odaklanmak onun mixer iz'ini seçer. Varsayılan olarak açık |
| `Yazma klavyesi önceliğini varsayılan olarak kapat` | Bundan sonra eklediğiniz plugin'ler için. Varsayılan olarak kapalı |
| `Maksimum geri alma seviyesi` | 25 ile 1000 arası |
| `Mesajları yönet` | `İpuçları ve uyarılar` altında. İşaretli iletiler gösterilir. Sorusunu atlamak için birinin işaretini kaldırın |
| Yedek | Yedek kopyanın ne sıklıkla kaydedildiği, proje başına kaç tanesinin tutulduğu ve `Yedek klasörünü aç` |
| `Plugin'leri ayrı süreçte çalıştır` | Çöken bir plugin LYNX SHARE'i de çökertmez. Varsayılan olarak kapalı |
| `Eksik plugin'lerin yerine LYNX SHARE karşılıklarını kullan` | Varsayılan olarak açık. Bir proje kurulu olmayan bir üçüncü taraf plugin'i kullandığında, LYNX SHARE'in karşılığı varsa eşleşen ayarlarla yüklenir. Proje özgün plugin'i korur ve kurulduğunda yeniden kullanır |
| `Açık eklenti düzenleyicisi sayısını sınırla` | En fazla 25 plugin editör penceresi açık kalır. Varsayılan olarak açık |
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

## LYNX extras

`Seçenekler › LYNX ekstraları...`. LYNX SHARE'in kendi eklemeleri tek yerde. Hepsi varsayılan olarak kapalıdır. Her anahtar, satırının adını verdiği menüdeki veya sayfadaki ayarın aynısıdır.

| Ayar | Ne yapar |
|---|---|
| `Master'ın yanında ses yüksekliği ölçer (LYNX)` | Master'ın kısa vadeli ve entegre loudness'ı (LUFS) ve true peak'i, bir Reset düğmesiyle |
| `Yalnızca kullanılan insert'leri göster` | Hiçbir şeyin kullanmadığı Mixer insert'lerini gizler |
| `Silinen insert'lere giden bağlantıları gri olarak koru (LYNX)` | Bir insert'i silmek ona giden bağlantıları korur, böylece yeniden bağlanabilirler |
| `Mikser grupları Playlist'i izler (LYNX)` | Mixer'ın grupları Playlist'in iz gruplarını yansıtır |
| `Otomasyon şeritleri` | Otomasyon bir Playlist izinin altında şeritlerde durabilir |
| `Cetvel: şarkı ölçüleri (LYNX)` | Piano roll'un cetveli, pattern'in şarkıda çaldığı yerin ölçü numaralarını gösterir |
| `Bir kontrolün üzerine gelince değerini farenin yanında göster (LYNX)` | Değer yalnızca ipucu çubuğunda değil, imlecin yanında da görünür |
| `Döngü kaydından sonra yalnızca son geçiş çalar` | Önceki geçişler altında, sessize alınmış olarak kalır |
| `W, seçili cihazın penceresini gösterir veya gizler` | Device view için bir tuş |
| `Bir cihazı seçmek, Mikser'de insertini seçer` | Mixer, Device view'da seçilen aygıtı izler |
| `Oyun kumandası kullan` | MIDI sayfasındakiyle aynı anahtar |

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
- Bazı anahtarlar bu pencerede değil, menülerdedir. `Kayıt sırasında düşük gecikme`, kayıt düğmesinin yanındaki kayıt seçenekleri menüsünde ve Mixer'ın `Diske kayıt` menüsündedir. `Klavye stili` Piano roll'un View menüsündedir.
- Plugin database bir klasördür: belgelerinizin `LYNX` klasöründeki `Plugin veritabanı`. Onu dosya yöneticisinde düzenleyebilirsiniz.

## İlgili sayfalar

- [Ses ayarı](/wiki/audio-setup/)
- [MIDI kontrolcüleri](/wiki/midi-controllers/)
- [Kısayollar](/wiki/shortcuts/)
- [Plugin'ler](/wiki/plugins/)
- [Yerleşimler](/wiki/layouts/)

---
source: 2e33c00ef4
---
![Kayıt penceresi ne kaydedileceğini soruyor: ses, notalar ve otomasyon, her şey ya da ses editörüne ses](/assets/img/wiki/record-dialog.webp)

Aktarımdaki kayıt düğmesi ya da `R` tuşu kaydı açar. İlk seferde bir pencere ne kaydetmek istediğinizi sorar. Ses, [Playlist](/wiki/playlist/)'e ses klipleri olarak gider. Notalar geçerli pattern'e girer. Oynattığınız kontroller otomasyon olur.

## Mikrofon veya enstrüman kaydetmek

1. `Seçenekler › Ses ayarları...` yolunu açın ve bir giriş aygıtı seçin.
2. [Mixer](/wiki/mixer/)'ı (`F9`) açın ve boş bir insert seçin.
3. İnsert'in efekt yuvalarının üstündeki giriş kutusuna tıklayın ve bir giriş seçin. Stereo çiftler önce gelir, tekli kanallar `(mono)` ile işaretlidir.
4. Giriş kutusunun yanındaki kayda hazırlama düğmesine tıklayın.
5. `R` tuşuna basın ve `Ses, Playlist'e ses klibi olarak` seçeneğini seçin.
6. Çalma kendiliğinden başlamadıysa çal'a basın ve performansınızı yapın.
7. Durdur'a basın. Kayıt, Playlist'te bir ses klibidir.

`Shift+R` 4'ten 6'ya kadar olan adımları tek tuşta yapar: seçili insert'i kayda hazırlar, yalnızca ses kaydeder ve çalmayı başlatır. Durdurmak için yeniden basın.

## Kayıt penceresinin sunduğu seçenekler

| Seçenek | Ne yapar |
|---|---|
| `Ses, Playlist'e ses klibi olarak` | Seçili mixer insert'ini kayda hazırlar ve girişi yoksa girişini sorar |
| `Notalar ve otomasyon` | Bir MIDI klavyeden veya bilgisayar klavyesinden notaları ve oynattığınız kontrolleri kaydeder |
| `Her şey` | Notalar, otomasyon ve kayda hazır insert'lerden ses |
| `Ses, ses editörüne` | Şarkı çalarken girişi yeni bir ses editörüne kaydeder |

Seçiminizi saklamak için `Bir daha sorma` kutusunu işaretleyin. Bundan sonra kayıt filtresi için kayıt düğmesine sağ tıklayın: `Otomasyon`, `Notalar`, `Ses`, `Klipler` ve pencereyi geri getirmek için `Kayıt başlarken ne kaydedileceğini sor`.

## Bir insert'in giriş kutusu

![3. insert'i kayda hazır, giriş kutusu 1 ve 2 girişlerine ayarlı Mixer](/assets/img/wiki/mixer-input.webp)

| Ayar | Ne yapar |
|---|---|
| `Ses giriş kaynağı` | Bu insert'in çaldığı ve kaydettiği aygıt girişi ya da `(yok)` |
| `Kayıt` | Kaydın nereden alındığı: `Yalnızca harici giriş`, `Harici ve mikser girişi`, `Efektlerden sonra`, `EQ'dan sonra`, `Seviye ve pan'dan sonra` veya `Kanaldan sonra` |
| `Kuru girişi de ikinci kayıt olarak al (LYNX)` | İşlenmemiş girişi, işlenmiş bir kaydın yanında saklar |
| `Harici girişi dinle` | `Kapalı`, `Kayda hazırken`, `Otomatik (kayda hazırken, çalma sırasında değil)` veya `Açık` |
| `Kayıt koruması (hazırlanamaz)` | Insert'i yanlışlıkla kayda hazırlanmaktan korur |

## Nota kaydetmek

Notalar bir MIDI klavyeden, bilgisayar klavyesinden (`Ctrl+T`) ya da dokunmatik klavyeden gelir ve geçerli pattern'de seçili kanallara gider. Bkz. [MIDI kontrolcüleri](/wiki/midi-controllers/).

- `Kaydedilen notaları harmanla` (`Ctrl+B`) overdub ile değiştirme arasında karar verir. Açıkken yeni notalar pattern'in notalarına katılır. Kapalıyken kaydedilen bir kanalın eski notaları temizlenir.
- `Kaydedilen notaları genel snap'e kuantize et` kayıt seçeneklerindedir.
- `Adım adım giriş` (`Ctrl+E`) durmuşken çalışır: çaldığınız her nota snap adımı kadar arayla, birbiri ardına pattern'e girer.
- `MIDI yakala` (`Ctrl+Shift+Alt+R`), kayıt kapalı olsa bile az önce çaldığınız notaları pattern'e koyar.

Kayıt sırasında oynattığınız kontroller Song modunda otomasyon klipleri, Pattern modunda pattern olayları olarak yazılır. Bkz. [Otomasyon](/wiki/automation/).

## Kayıt seçenekleri

Seçenekler `Seçenekler › Kayıt` içinde ve kayıt düğmesinin sağ tık menüsündedir.

| Komut | Tuş | Ne yapar |
|---|---|---|
| `Geri sayım` | `Ctrl+P` | Kayıt başlamadan önce Off, 1 ölçü, 2 ölçü veya 4 ölçü metronom |
| `Metronom` | `Ctrl+M` | Çalarken tık sesi |
| `Başlamak için ilk notayı bekle` | `Ctrl+I` | Kayıt siz çalana kadar bekler |
| `Döngülü kayıt` | yok | Kayıt devam etmek yerine döngüye girer |
| `Ses: döngü bölgesinde punch in/out` | yok | Döngü bölgesinden geçerek çalar ve yalnızca içindeki sesi tutar |
| `Ses: pre-roll` ve `post-roll` | yok | Punch-in'den önce ve punch-out'tan sonra çalınan Off, 1, 2 veya 4 ölçü |
| `Kayıt çalmayı başlatır` | yok | Kaydı açmak çal'a da basar |
| `Kayıt durunca insert'lerin kayıt hazırlığını kaldır` | yok | Her kayıttan sonra kayda hazır insert'ler serbest bırakılır |

## Döngülü kayıt ve kayıtlar

`Döngülü kayıt` açıkken kayıt, Song modunda döngü bölgesinde, Pattern modunda pattern üzerinde döner. Kapalıyken kayıt düz devam eder.

Bir ses döngüsü kaydının her geçişi sıradaki boş Playlist izine düşer ve izler bir grup oluşturur. Durdurduktan sonra neyin çalacağına `Kaydedilen notaları harmanla` karar verir: kapalıysa her geçiş susturulur, açıksa hepsi çalar. Geçişlerden tek bir kayıt oluşturmak için grubun bir izine sağ tıklayın ve `Bu kayıtlardan comp oluştur` seçin. Kayıt seçeneklerindeki `Döngü kaydından sonra yalnızca son geçiş çalar` tek bir ize geri döner.

## Kayıtlar nereye kaydedilir

- Kaydedilmiş bir proje, kayıtlarını proje dosyasının yanındaki `Audio/Recorded` içinde tutar.
- Kaydedilmemiş bir proje, Music klasörünüzdeki `LYNX/Recordings` içine kaydeder.
- Kayıtlar, ses aygıtının örnekleme hızında, 24 bit veya 32 bit float WAV dosyalarıdır. Format ve kayıt adları `Seçenekler › Ses ayarları...` içindeki `Kayıt` altında ayarlanır.
- `Kayıtlar klasörünü aç` kayıt düğmesinin sağ tık menüsündedir. `Dosya › Kullanılmayan kayıtları sil ve kaydet...` projenin artık kullanmadığı kayıtları siler.

## Bilmekte fayda var

- Girişi olmayan kayda hazır bir insert hiçbir şey kaydetmez. LYNX SHARE size bunu söyler ve giriş kutusunu işaret eder.
- Sürücüde 30 saniyeden az yer varsa kayıt başlamayı reddeder, 10 dakikanın altında uyarır.
- `Seçenekler › Giriş sesini kaydet`, hiçbir şey kayda hazır değilken ilk iki giriş kanalını yeni bir insert'e kaydeder.
- macOS'ta sistem, ilk kaydınızda mikrofon erişimi ister.
- LYNX SHARE kayıt sırasında kapanırsa bir sonraki açılışta kurtarılan kayıtları Playlist'e koymayı önerir.

## İlgili sayfalar

- [Ses ayarı](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Otomasyon](/wiki/automation/)
- [MIDI kontrolcüleri](/wiki/midi-controllers/)
- [Sorun giderme](/wiki/troubleshooting/)

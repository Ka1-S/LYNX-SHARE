---
source: b96330a448
---
![Altı ölçü boyunca yükselen bir eğriyle Otomasyon düzenleyici](/assets/img/wiki/automation.webp)

Otomasyon klibi, şarkı çalarken tek bir kontrolü hareket ettiren, [Playlist](/wiki/playlist/) üzerindeki bir eğridir: bir kanalın sesi, bir mixer fader'ı, bir plugin parametresi, tempo. Klibin kendi düzenleme penceresi vardır: Otomasyon düzenleyici.

Bir kontrolü oynatmanın üç yolu daha var. Olaylar bir pattern'in içinde saklanan değerlerdir. Bir bağlantı, MIDI knob'unun veya dahili bir kontrolcünün kontrolü sürmesini sağlar. Bir modülatör, bir knob'u kendi değerinin çevresinde salındırır.

## Bir knob'u otomatikleştirmek

1. Çalma imlecini otomasyonun başlayacağı yere getirin ya da Playlist'te bir zaman aralığı seçin.
2. Knob'a veya fader'a sağ tıklayın ve `Otomasyon klibi oluştur` seçeneğini seçin.
3. Klip, ilk boş Playlist izine düşer ve Otomasyon düzenleyici açılır. Eğri, kontrolün o anki değerinde düz bir çizgi olarak başlar.
4. Düzenleyicide bir nokta eklemek için tıklayın ve yerine sürükleyin.
5. `Alt` tuşunu basılı tutup bir parçayı yukarı veya aşağı sürükleyerek bükün.
6. Şarkıyı Song modunda çalın. Kontrol eğriyi izler.

Aynı klibi elde etmenin başka yolları:

- `Ekle › Son oynanan parametre için otomasyon`, en son oynattığınız kontrol için bir klip oluşturur.
- Son oynanan göstergesi ve `Araçlar › Son oynanan`, kontrole sağ tıklamayla açılan menüyle aynı menüyü açar.
- Üçüncü taraf bir plugin için Channel Rack'teki kanalın menüsünü ya da Mixer'daki efekt yuvasının menüsünü açıp `Plugin parametresini otomatikleştir` seçeneğini kullanın. `MIDI kontrolcüyü otomatikleştir (CC, aftertouch, pitch bend)` onun yanındadır.

Düzenleyiciyi yeniden açmak için klibe çift tıklayın ya da sağ tıklayıp `Noktaları düzenle...` seçin. Kontrolün kendi menüsündeki `Otomasyon klibini düzenle...` de aynı işi yapar.

## Otomasyon düzenleyici

| Komut | Tuş | Ne yapar |
|---|---|---|
| Yeni nokta | Tıklayın ya da sağ tıklayın, sonra sürükleyin | Bir nokta ekler ve tuşu basılı tutarken taşır |
| Noktayı taşımak | Sürükleyin | `Shift` zamanını, `Ctrl` değerini korur, `Alt` snap'i yok sayar |
| Parçayı bükmek | `Alt`+yukarı veya aşağı sürükleme | Fare altındaki parçanın gerilimini değiştirir |
| Gerilim tutamacı | Yukarı veya aşağı sürükleyin | Parçayı bükler. `Ctrl` daha ince ayar yapar, sağ tık düzleştirir |
| Eğri üzerinde nokta | `Shift`+sağ tık | Çizgi üzerine bir nokta ekler |
| Nokta menüsü | Bir noktaya sağ tıklayın | `Değer yaz...`, `Değeri kopyala`, `Değeri yapıştır`, `Noktayı sil` ve her şeyi ölçeklemek, çevirmek ya da düzleştirmek için `Tüm eğri` |
| Zoom | `Ctrl`+tekerlek | Görünümü yakınlaştırır |
| Kaydırma | Orta tuşla sürükleme | Görünümü kaydırır |

## LFO modu

![LFO modundaki Otomasyon düzenleyici; üçgen dalga ve SPD, TNS, SKEW, PW ile AMNT knob'ları](/assets/img/wiki/automation-lfo.webp)

Düzenleyicinin üstündeki `LFO` kutusunu işaretleyin. Artık bir dalga biçimi klibin değerini şekillendirir ve çizdiğiniz noktalar değerin kendisini değil, LFO'nun genliğini belirler.

| Ayar | Ne yapar |
|---|---|
| `SPD` | Bir döngünün uzunluğu, vuruş cinsinden |
| `TNS` | Dalganın gerilimi |
| `SKEW` | Dalganın eğikliği |
| `PW` | Darbe genişliği |
| `AMNT` | LFO'nun değeri ne kadar oynattığı |
| `MUL` | Açıkken: LFO, envelope seviyesiyle çarpılır. Kapalıyken: eklenir |

## Pattern içindeki olaylar

![Çubuklar olarak çizilmiş ses olaylarıyla Olay editörü](/assets/img/wiki/eventeditor.webp)

Olaylar bir pattern'e aittir ve o pattern nerede çalarsa orada çalar. Bir kontrole sağ tıklayıp `Pattern'deki olayları düzenle` seçeneğini seçin; kontrol için Olay editörü açılır.

- `Çiz` sürüklerken değer koyar, sağ tuşla sürüklemek siler. `Çizgi` düz bir rampa çizer. `Seç`, alttaki araçlar için bir zaman aralığı işaretler.
- `Snap` 1/2 step'ten 1 ölçüye kadar gider.
- `Gam`, `Ofset` ve `Rastgele`, seçimdeki ya da tüm pattern'deki olayları değiştirir.
- `LFO`, 1 vuruştan 4 ölçüye kadar bir sinüs, üçgen veya kare dalga yazar.
- `Temizle` olayları kaldırır.
- `Otomasyona...` olayları bir otomasyon klibine çevirir ve pattern'den kaldırır.
- `Dosya` olayları bir `.fsc` dosyası olarak açar ve kaydeder, bir MIDI dosyasını da içe aktarır.

Aynı sağ tık menüsündeki `Şarkıyı bu konumla başlat`, şarkı her başladığında kontrolün sıfırlanacağı değeri saklar.

## Hareketlerinizi kaydetmek

Kayıt filtresinde `Otomasyon` işaretliyken kaydı açın, çalmaya basın ve kontrolü oynatın. Song modunda hareketler bir otomasyon klibi olur. Pattern modunda geçerli pattern'de olaylar olur. Bkz. [Kayıt](/wiki/recording/).

## Bağlantılar ve modülatörler

- `MIDI kontrolcüye bağla (kontrolcüde bir knob oynatın)` [MIDI kontrolcüleri](/wiki/midi-controllers/) sayfasında anlatılır.
- `Dahili kontrolcüye bağla`, projedeki bir Envelope, Formula ya da Peak Controller'ın kontrolü sürmesini sağlar. Önce bunlardan birini ekleyin.
- `Modüle et`, bir LYNX SHARE knob'unu kendi değerinin çevresinde hareket ettiren bir modülatör, yani bir LFO veya rastgele bir kaynak bağlar. `Araçlar › Modülatörler...` bunları listeler.

## Otomasyon şeritleri

Şeritler varsayılan olarak kapalıdır. Playlist'in menüsünden `Otomasyon şeritleri` seçeneğini açın. Bundan sonra her iz başlığında izin altına bir şerit ekleyen bir düğme olur ve kontrol menüsüne `Şeritte otomatikleştir` eklenir. Bir şeritte tıklayarak nokta ekleyin ya da sürükleyin, sağ tıklayarak silin, `Alt`+sürükleyerek çizgiyi bükün. Şeridin menüsünde `Otomasyon klibine` vardır.

## Bilmekte fayda var

- Yeni klip, varsa zaman seçiminde, yoksa Song modunda çalma imlecinde başlar. Şarkının başında başlamasını istiyorsanız `Seçenekler › Genel ayarlar...` içindeki `Otomasyon kliplerini çalma konumunda oluştur` seçeneğini kapatın.
- Bir kontrol üzerindeki birden çok klip, her biri kendi yerinde çalar. İlk klibinden önce kontrol kendi değerini korur.
- Klibin başında kontrol yeni değere yaklaşık 8 ms'de ulaşır. FL Studio oraya kayarak gider, bu yüzden içe aktarılmış bir projede o noktalarda küçük farklar olabilir.
- Aynı kontrol üzerindeki kliplerin çakıştığı yerde LYNX SHARE'in seçtiği klip, FL Studio'nun seçtiğinden farklı olabilir.
- Modülatörler yalnızca LYNX SHARE'in kendi knob'larında çalışır ve `.flp` olarak dışa aktarırken dışarıda bırakılır.

## İlgili sayfalar

- [Playlist](/wiki/playlist/)
- [Kayıt](/wiki/recording/)
- [MIDI kontrolcüleri](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Kısayollar](/wiki/shortcuts/)

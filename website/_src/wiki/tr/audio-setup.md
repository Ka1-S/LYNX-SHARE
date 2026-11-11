---
source: 3801d5daff
---
![Ayarlar penceresinin Ses sayfası, henüz bir çıkış aygıtı seçilmemiş](/assets/img/wiki/settings-audio.webp)

Ayarlar penceresinin Ses sayfası, sesin nereye gideceğini ve kayıtların nereden geleceğini seçtiğiniz yerdir. `Seçenekler › Ses ayarları...` ile açın. Kurulum sihirbazı da (`Seçenekler › Kurulum sihirbazı (dil, ses, plugin klasörleri)...`) ses çıkışını sınar.

## Ses almak

1. `Seçenekler › Ses ayarları...` yolunu açın.
2. Bir `Sürücü` seçin. Kutunun altındaki not, o sürücünün ne işe yaradığını söyler.
3. Bir `Çıkış aygıtı` seçin.
4. `Test sesi çal` düğmesine tıklayın.
5. `Durum` bölümüne bakın. Çalışan sürücüyü, örnekleme hızını ve çıkış gecikmesini gösterir; aygıt yoksa `Açık ses aygıtı yok: bir çıkış aygıtı seçin.` yazar.

## Sürücüler

`Sürücü` kutusu sisteminizdeki sürücü türlerini listeler.

| Sistem | Sürücü | Ne yapar |
|---|---|---|
| Windows | Windows Audio (shared) | Diğer programların sesi kesilmez. Windows tüm programları sabit adımlarla mikslediği için arabellek bu adımın altına inemez |
| Windows | Windows Audio (shared, low-latency period) | Yine paylaşımlıdır, ama Windows'tan daha kısa bir miks periyodu ister. Yalnızca bazı sürücüler destekler |
| Windows | Windows Audio (exclusive, lowest latency) | LYNX SHARE aygıtı tek başına kullanır. O çalarken diğer programlar sessiz kalır |
| Windows | DirectSound (legacy) | Gecikmesi yüksek, eski Windows ses yolu. Diğerleri çalışmadığında içindir |
| Windows | ASIO | Düşük gecikmeli sürücüler. Üreticinin ASIO sürücüsü kendi aygıtıyla çalışır |
| Windows | LYNX ASIO (built in) | LYNX SHARE'in kendi ASIO sürücüsü; her aygıtta, çalma ve kayıt için çalışır. `Sürücü ayarları...` paylaşımlı veya özel modu ve aygıtları seçer |
| macOS | Core Audio | Sistemin kendi sesi, diğer programlarla paylaşılır |
| Linux | PipeWire (native) | LYNX SHARE doğrudan PipeWire ile konuşur. `System default`, sistemin ses ayarlarında seçili çıkışı izler |
| Linux | JACK / PipeWire | Çalışan bir JACK sunucusu için ya da `pipewire-jack` ile PipeWire için |
| Linux | ALSA | Ses kartıyla konuşur. `default` veya `pipewire` aygıtı paylaşır, bir `hw:` aygıtı kartı yalnızca LYNX SHARE için alır |

Linux'ta ilk açılışta PipeWire, JACK ve ALSA arasından aygıtı olan ilki açılır. Kaydedilmiş seçim korunur.

## Sayfadaki ayarlar

| Ayar | Ne yapar |
|---|---|
| `Sürücü` | Sürücü türü, yukarıdaki tablodan |
| `Çıkış aygıtı` | Master'ın çaldığı yer |
| `Giriş aygıtı` | Ses kaydı ve Mixer insert girişleri için kaynak. Varsayılan olarak `(yok)` |
| `Örnekleme hızı` | Aygıtın sunduğu hızlar |
| `Arabellek boyutu` | Aygıtın sunduğu boyutlar, her biri milisaniye cinsinden gecikmesiyle. Küçük arabellekler çalarken daha hızlı yanıt verir. Büyükleri CPU'ya daha fazla pay bırakır ve tıklama azalır |
| `Sürücü ayarları...` | Sürücünün kendi penceresini açar (ASIO sürücüleri) |
| `Test sesi çal` | Çıkış aygıtından bir test sesi çalar |
| `Giriş sesini kaydet` | Hiçbir insert kayda hazır değilken ilk iki giriş kanalını yeni bir insert'e kaydeder |
| `Dosya formatı` | Kayıtlar, aygıtın örnekleme hızında WAV 24 bit veya WAV 32 bit float olur |
| `Gidiş-dönüşü ölç` | `Ping çıkışı` çıkışından `Ping girişi` girişine bir kablo veya loopback üzerinden 8 tık çalar ve gecikmeyi ölçer; böylece kayıtlar çaldığınız yere oturur |
| `İnce ayar (sample)` | Her yeni kaydı kaydırır. Pozitif değer Playlist'te daha erken demektir |
| `Metronom kanalı` | Metronomun çaldığı Mixer kanalı. `---` Master'dır. Tık sesi dışa aktarmaya asla girmez |
| `Aktarımda kesilmiş notaları çal` | Başlangıç konumunu aşan notaları çalar |
| `Aktarımda eklentileri sıfırla` | Çalma başlarken veya atlarken efektlerin kuyruklarını temizler |

## Ses gelmiyorsa

- Önce `Durum` bölümüne bakın. Açık aygıt yoksa bir çıkış aygıtı seçin.
- Başka bir sürücü deneyin. Windows'ta `Windows Audio (paylaşımlı)` diğer programların sesini korur, `DirectSound (eski)` ise diğerleri çalışmadığında vardır.
- [Mixer](/wiki/mixer/)'daki Master fader'ını ve araç çubuğundaki `MASTER` knob'unu kontrol edin.
- Pattern modunda yalnızca geçerli pattern çalar. Boşsa hiçbir şey duymazsınız. Song modu için `L` tuşuna basın.
- Linux'ta ses bozuluyorsa `Durum`, ses iş parçacığının gerçek zamanlı öncelik alıp almadığını gösterir. Çözüm [Sorun giderme](/wiki/troubleshooting/) sayfasında.

## Bilmekte fayda var

- ASIO yalnızca Windows'ta vardır.
- `Windows Audio (paylaşımlı)` ile en küçük arabellek, sistemin miks adımıdır. `Sürücü` kutusunun altındaki not, aygıtınız için değeri verir. Daha düşük gecikme için özel modu veya bir ASIO sürücüsü kullanın.
- PipeWire'da arabellek 32 ile 2048 sample, örnekleme hızı 44,1 ile 192 kHz arasındadır. PipeWire kendi hızını korursa LYNX SHARE'in akışını yeniden örnekler.
- PipeWire'da adı verilmiş bir aygıtın fişi çekilirse LYNX SHARE `System default` seçeneğine döner ve arabellek boyutunu korur.
- `Durum` ayrıca ses CPU yükünü gösterir ve kopmaları (arabellek boşalmalarını) sayar.
- Sürücüleri ve aygıtları pencereyi açmadan listeleyebilirsiniz. [Komut satırı](/wiki/command-line/) sayfasındaki `--devices` bölümüne bakın.

## İlgili sayfalar

- [Ayarlar](/wiki/settings/)
- [Kayıt](/wiki/recording/)
- [MIDI kontrolcüleri](/wiki/midi-controllers/)
- [Sorun giderme](/wiki/troubleshooting/)

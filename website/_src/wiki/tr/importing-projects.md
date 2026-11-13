---
source: 358890815a
---
`Dosya › Aç...` (`Ctrl+O`), LYNX SHARE'in kendi `.lynx` projelerini ve ayrıca `.flp`, `.als`, `.flm` ve `.mid` dosyalarını açar. Başka bir programdan gelen proje bir içe aktarmadır: yeni, kaydedilmemiş bir LYNX SHARE projesi olur ve özgün dosyaya asla yazılmaz.

## Başka bir programdan proje açma

1. `Dosya › Aç...` komutunu seçin ve `.flp`, `.als`, `.flm` veya `.mid` dosyasını seçin.
2. LYNX SHARE projenin plugin'lerini ve sample'larını ararken bekleyin.
3. Bir şey bulunamadıysa `Eksik dosyalar` penceresi (Eksik dosyalar) açılır. Onu sample'larınıza yönlendirin ya da kapatın.
4. Başlık çubuğu artık projenin nereden geldiğini gösterir, örneğin "(imported from FL Studio)".
5. `Dosya › Kaydet` komutunu seçin. LYNX SHARE bir `.lynx` dosyasının nereye yazılacağını sorar.

## Neler geçer

| Format | Dönüştürülenler | Dönüştürülmeyenler |
|---|---|---|
| `.flp` (FL Studio) | Kanallar, sample'lar, pattern'ler, slide ve porta notaları, tüm aranjmanlar, işaretçiler, ölçü işareti değişiklikleri, otomasyon, kontrolcü bağlantıları, yönlendirmeli Mixer, sidechain'ler, kanal EQ'su ve yerel efektler | FLEX paketleri şifrelidir ve asla açılmaz. Bir FLEX kanalı, preset adına göre seçilen bir synth patch'i alır |
| `.als` (Live 11 ve 12) | MIDI, ses, grup ve return track'leri, tempo, ölçü işareti, locator'lar, döngü, ses, pan, mute, solo ve otomasyonlarıyla send'ler, Session klipleri ve sahneler, warp işaretçileri, rack'ler ve Drum Rack'ler ve Live'ın kendi cihazlarının çoğu (EQ Eight, Compressor, Glue Compressor, Reverb, Delay, Auto Filter, Saturator, Utility, Simpler, Drift ve daha fazlası) LYNX SHARE'in kendi cihazları olarak | Live'ın LYNX SHARE karşılığı olmayan cihazları (Hybrid Reverb, Grain Delay, Amp, Roar, FM Operator patch'leri ve diğerleri) atılır ve listelenir. MIDI Effect Rack'ler dönüştürülmez |
| `.flm` (FL Studio Mobile) | Tempo, ölçü işareti, kanal başına bir mixer insert'i, şerit başına bir Playlist izi, pattern'ler, ofset ve pitch'li ses klipleri, davul kit'leri, DW Sampler ve `.dwp` programları, Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth ve SuperSaw, efektler, otomasyon ve akor setleri | Diğer FLM generator'ları dahili synth ile çalar |
| `.mid` | Notalar, ölçü işaretleri ve her tempo değişikliği | Bir MIDI dosyası ses taşımaz: kanallar LYNX SHARE synth'ini ya da bir MIDI Out kanalı alır |

Bir `.flp` içindeki üçüncü taraf VST2, VST3 ve CLAP plugin'leri ile bir `.als` içindeki VST2 ve VST3 plugin'leri, aynı plugin bilgisayarınızda kuruluysa kaydedilmiş durumlarıyla yüklenir.

## FL Studio'nun kendi plugin'leri

FL Studio'nun generator'ları ve efektleri yüklenmez. LYNX SHARE onların kaydedilmiş ayarlarını okur ve yeniden oluşturduğu motorlarda çalar. Her motorun bir kalite seviyesi vardır: `Exact` (denenen her durumda orijinalle birebir aynı) seviyesinden `Placeholder` (ayarlar korunur ama plugin sessizdir ya da sesi olduğu gibi geçirir) seviyesine kadar. Seviyeler [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/) sayfasında açıklanmıştır.

Bir `.flp` açmak FL Studio kurulu olmadan da çalışır. Bazı motorlar, çalışma anında kendi FL Studio kurulumunuzdan okunan wave'lere, sample'lara veya impulse'lara ihtiyaç duyar.

## Eksik sample'lar

![Eksik dosyalar penceresi Sample'lar sekmesinde: bulunan dosyalar yeşil, eksik olanlar kehribar rengi; Klasörde ara, Bul ve Atla ile](/assets/img/wiki/missing-files.webp)

`Sample'lar` sekmesi bulunamayan her sample'ı, kaydedilmiş yolu ve onu neyin kullandığıyla birlikte listeler.

| Komut | Ne yapar |
|---|---|
| `Klasörde ara...` | Eksik tüm sample'lar için bir klasörü ve alt klasörlerini arar |
| `Bul...` | Bir sample'ın nerede olduğunu sorar. Aynı klasörde bulunan diğerleri de alınır |
| `Atla` | Sample'ı dışarıda bırakır. Kanalı sessiz kalır |

`Uygula`, bulunan dosyaları kullanır.

`Otomatik bulundu` sekmesi, LYNX SHARE'in kendiliğinden başka bir klasörde bulduğu dosyaları listeler. Yeni yerleri korumak için projeyi kaydedin.

## Eksik plugin'ler

LYNX SHARE her üçüncü taraf plugin'i önce tanımlayıcısıyla, sonra dosyasıyla, sonra adıyla arar. VST2 ile kaydedilmiş bir proje aynı ürünün VST3'üyle açılır, tersi de geçerlidir; ancak varsayılan ayarlarla.

Bir efekt kurulu değilse, aynı işi gören bir LYNX SHARE efekti yuvayı alır: reverb yerine reverb, kompresör yerine kompresör.

- Yuva, LYNX SHARE rozetiyle birlikte orijinal plugin'in adını gösterir.
- Kaydedilmiş durum okunabiliyorsa ayarlar çevrilir. Aksi halde yerini tutan efekt varsayılan bir preset'ten başlar. Ürünlerin, yerlerini tutanların ve her biri için nelerin çevrildiğinin tablosu [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md) içindedir.
- Orijinalin parametrelerinin otomasyonu korunur ama yerini tutanı yönetmez.
- Orijinal plugin'in referansı ve durumu projede kalır. Plugin kurulduktan sonra proje onunla yeniden açılır.
- Yerini tutan bir yuvanın sağ tık menüsündeki `Bu projedeki tüm LYNX SHARE yedeklerini bypass et`, hepsini tek seferde kapatır.

Anahtar, `Seçenekler › Genel ayarlar...` altındaki `Eksik plugin'lerin yerine LYNX SHARE karşılıklarını kullan` seçeneğidir ve varsayılan olarak açıktır. Kapalıyken eksik efektin yuvası mixer'den çıkarılır. Eksik enstrümanın yerine bir şey konmaz. Kanal adına göre seçilen bir preset'le dahili synth olur.

Gerçek plugin'i geri almak için onu kurun, `Seçenekler › Plugin tara` seçeneğini seçin, sonra projeyi yeniden açın.

## MIDI dosyaları

![MIDI verisi içe aktar penceresi: izler, kanallar, kanal türü ve seçenekler](/assets/img/wiki/midi-import-dialog.webp)

`Dosya › Aç...` bir MIDI dosyasından yeni bir proje oluşturur. `Dosya › İçe aktar › MIDI dosyası...` önce neyin alınacağını seçtiğiniz `MIDI verisi içe aktar` penceresini açar.

| Ayar | Ne yapar |
|---|---|
| `Hangi izler içe aktarılsın?` | Tüm izler ya da biri |
| `Hangi kanallar içe aktarılsın?` | Bir MIDI kanalını dışarıda bırakmak için ona tıklayın. Sağ tık yalnızca onu tutar |
| `Kanal türü` | `LYNX SHARE synth` veya `MIDI Out` |
| `Yeni proje başlat` | Boş bir proje başlatır. Kapalıyken: açık olana ekler |
| `İz başına bir kanal oluştur` | Kapalıyken: MIDI kanalı başına bir kanal |
| `Yeni kanallar için mixer kanalları ata` | Yeni kanalları ardışık mixer insert'lerine yönlendirir |
| `Ölçü işaretlerini içe aktar` | Dosyanın ölçü işareti projenin ölçü işareti olur |

[Piano Roll](/wiki/piano-roll/)'daki `Ctrl+M`, bir MIDI dosyasını geçerli kanala içe aktarır.

## Bilmekte fayda var

- `Yardım › Eksik dosyalar...` pencereyi sonradan yeniden açar. `Yardım › Last FL Studio import report` neyin yaklaşık karşılandığını, dışarıda bırakıldığını veya kurulu olmadığını listeler.
- Bir Live set'inin Session klipleri, Klip başlatma yerleşiminin klip ızgarasına gider. `Seçenekler › Genel ayarlar...` içindeki `Live setlerinin oturum klipleri` onları bunun yerine Playlist'e yerleştirir.

## İlgili sayfalar

- [Dosya formatları](/wiki/file-formats/)
- [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/)
- [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/)
- [Plugin'ler](/wiki/plugins/)
- [Dışa aktarma](/wiki/exporting/)

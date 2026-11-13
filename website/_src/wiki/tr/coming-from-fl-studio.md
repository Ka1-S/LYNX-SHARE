---
source: 3bf7e0c0ab
---
LYNX SHARE bildiğiniz iş akışını korur: Channel Rack, pattern'ler, Playlist ve Mixer, aynı tuşlarla. `.flp` projelerini doğrudan açar ve geri yazabilir.

LYNX SHARE bağımsız bir programdır. Image-Line ile bağlantısı yoktur, Image-Line tarafından onaylanmamış veya desteklenmemiştir ve içinde Image-Line kodu bulunmaz. FL Studio, Image-Line Software'in tescilli ticari markasıdır.

## Bir .flp projesini açmak

1. `Dosya › Aç...` (`Ctrl+O`) yolunu seçin ve `.flp` dosyasını seçin.
2. LYNX SHARE projenin plugin'lerini ve sample'larını çözümler, sonra projeyi açar. Başlıkta "(imported from FL Studio)" yazar.
3. Nelerin yaklaşık karşılandığını, dışarıda bırakıldığını veya bulunamadığını okumak için `Yardım › Last FL Studio import report` yolunu seçin.
4. Eksik sample'ları ve plugin'leri görmek için `Yardım › Eksik dosyalar...` yolunu seçin.
5. `Dosya › Kaydet` yolunu seçin. Bir `.lynx` dosyasını nereye yazacağını sorar. `.flp` dosyasının kendisi asla yazılmaz.

## Neyin karşılığı ne

| FL Studio'da | LYNX SHARE'de |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Aynı adlar, `F5`, `F6`, `F7` ve `F9` tuşlarında |
| Plugin seçici | `F8` |
| Browser | `Alt+F8` |
| Pattern / Song modu | `L` |
| Geri al | `Ctrl+Z` geri alır, geri aldıktan sonra basılırsa yineler. `Ctrl+Alt+Z` geriye doğru geri alır |
| Yeni sürüm kaydet | `Ctrl+Alt+S` |
| Dışa aktar | `Ctrl+R`, MP3 için `Ctrl+Shift+R`, MIDI için `Ctrl+Shift+M` |
| Mixer insert'leri, send'ler ve sidechain | Aynıdır; insert başına 10 efekt yuvası vardır |
| Fruity Wrapper | Üçüncü taraf plugin'ler doğrudan barındırılır, bkz. [Üçüncü taraf plugin'ler](/wiki/plugins/) |
| Yerel jeneratörler ve efektler | LYNX SHARE'in yeniden oluşturduğu motorlarda çalar, aşağıya bakın |
| `.flp` proje dosyası | `.lynx` proje dosyası |

`Yardım › Kısayollar` bütün tuşları listeler.

## Neler içe aktarılır

- Kanallar, sample'lar, pattern'ler, slide ve porta notaları, tüm aranjmanlar, işaretçiler, ölçü işareti değişiklikleri, otomasyon ve kontrolcü bağlantıları.
- Mixer: yönlendirme, seviyeler, sidechain'ler, iz EQ'su ve kayıtlı ayarlarıyla yerel efektler.
- Fruity Wrapper içindeki üçüncü taraf VST3, VST2 ve CLAP plugin'leri: bilgisayarınızda kurulu aynı plugin ile, kayıtlı durumuyla değiştirilir.

## FL Studio'nun yerel plugin'leri

FL Studio'nun kendi jeneratörleri ve efektleri yüklenmez. LYNX SHARE onları yeniden oluşturduğu motorlarda çalar ve her birinin bir kalite seviyesi vardır:

| Seviye | Anlamı |
|---|---|
| Exact | Denenen her durumda aslıyla bit düzeyinde aynı |
| Near-exact | Çoğu durumda bit düzeyinde aynı, kalanlarda fark yaklaşık -100 dB'nin altında |
| Close | Aynı algoritma; denenen durumların hemen hepsinde aslının -60 dB'si içinde |
| Approximate | Kayıtlı ayarlar, LYNX SHARE'in dahili bir synth'ine veya efektine eşlenir |
| Placeholder | Durum korunur, plugin sessizdir ya da sesi olduğu gibi geçirir |

Her motorun seviyesi ve her birinde hâlâ eksik olanlar [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md) dosyasındadır. Ayrıca bkz. [Dahili enstrümanlar ve efektler](/wiki/built-in/).

## Kendi FL Studio kurulumunuz neye gerekir

Bir `.flp` açmak için FL Studio'nun kurulu olması gerekmez. Aynı bilgisayardaki bir kurulum iki şey ekler:

- FL Studio'nun fabrika sample'ları ve plugin veritabanı; böylece sample'lar ve plugin'ler tarama yapmadan bulunur.
- Bazı motorların ihtiyaç duyduğu yerleşik dalgalar, sample'lar, impulse'lar ve tablolar (FL Keys, Soundgoodizer, Maximus ve PLUGINS.md içinde hançer işaretiyle belirtilen diğerleri). Çalışma sırasında kurulumunuzdan okunur ve LYNX SHARE ile birlikte dağıtılmaz.

Linux'ta LYNX SHARE, Wine altında bir Windows FL Studio arar. macOS'ta uygulama paketi aranır, ama bu denenmemiştir.

## Geri .flp olarak dışa aktarmak

1. `Dosya › Dışa aktar › FL Studio projesi...` yolunu seçin.
2. `FL Studio projesi (.flp)` veya `Sıkıştırılmış loop paketi (.zip, tüm sample'larla)` seçeneğini seçin.
3. FL Studio'da karşılığı olmayan şeylere ne olacağını seçin: sese render edilirler (aynı ses), ya da en yakın FL plugin'i kullanılır (düzenlenebilir kalır, farklı duyulabilir ve yalnızca LYNX'e özgü enstrümanlar dışarıda bırakılır).

Dosya bir FL Studio 2026 projesidir. Sampler'lar, ses klipleri, FL'nin kendi jeneratörleri ve efektleri, üçüncü taraf plugin'ler (Fruity Wrapper içinde), pattern'ler, Playlist, Mixer ve otomasyon ayarlarıyla birlikte geçer. Bir rapor, nelerin birebir geçtiğini ve nelerin render edildiğini, değiştirildiğini ya da dışarıda bırakıldığını listeler.

## FL Studio Mobile (.flm)

`Dosya › Aç...` ayrıca `.flm` projelerini de alır. İçe aktarma kısmidir.

- İçe aktarılanlar: tempo, ölçü işareti, her FLM kanalı için bir mixer insert'i, her şerit için bir Playlist izi, pattern'ler (her döngü turu bir klip olarak), ofset ve pitch'iyle ses klipleri, davul kit'leri, DW Sampler ve `.dwp` programları ile Slicer (FLM'nin kendi motorlarında), MiniSynth, Transistor Bass, 3x Osc, GMSynth ve SuperSaw enstrümanları, FLM'nin efektleri (Pocket efektleri olarak), seviye, pan, mute, solo, pitch bend, send'ler ve enstrüman parametrelerinin otomasyonu, ayrıca akor setleri ve ton kilidi.
- İçe aktarılmayanlar: FLM'nin dahili synth ile çalan diğer enstrümanları.
- Sample'lar `.flm` dosyasının yanında ve FL Studio'daki FLM kaynakları kopyasında aranır. Bulunamayan bir `.dwp` yerine geçici bir ses çalar.

## Bilmekte fayda var

- FLEX paketleri şifrelidir ve asla açılmaz. Bir FLEX kanalına, preset adına göre seçilen bir synth patch'i verilir.
- Modülatörler `.flp` dışa aktarmasında dışarıda bırakılır ve dışa aktarma raporu bunu belirtir.
- İçe aktarma raporu, dahili synth ile nelerin yaklaştırıldığını, nelerin karşılığı olmadığını, hangi plugin'lerin bulunduğunu ya da kurulu olmadığını, hangi efektlerin dışarıda bırakıldığını veya kısmen yeniden üretildiğini ve kaç sample'ın eksik olduğunu listeler.
- Kurulu olmayan bir üçüncü taraf efekt, aynı role sahip bir LYNX SHARE efektiyle değiştirilir. Asıl efekt projede kalır.

## İlgili sayfalar

- [Dahili enstrümanlar ve efektler](/wiki/built-in/)
- [Üçüncü taraf plugin'ler](/wiki/plugins/)
- [Dosya formatları](/wiki/file-formats/)
- [Kısayollar](/wiki/shortcuts/)
- [Yalnızca LYNX SHARE'in FL ayarlarıyla yaptıkları](/why/#fl-settings)

---
source: be0fc22373
---
![Plugin yöneticisinin alt kısmı; her plugin dosyasının ayrı bir süreçte incelendiğini belirten notla](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE, başka yapımcıların enstrüman ve efekt plugin'lerini barındırır. Onları plugin klasörlerini tarayarak bulur ve `Seçenekler › Plugin yöneticisi...` bulduklarını gösterir. LYNX SHARE'in kendi enstrümanları ve efektleri [Dahili enstrümanlar ve efektler](/wiki/built-in/) sayfasında anlatılır.

## Plugin tarama ve ekleme

1. `Seçenekler › Plugin tara` seçeneğini seçin. İlerleme ipucu çubuğunda görünür ve çalışmaya devam edebilirsiniz.
2. Plugin kendi klasöründeyse `Seçenekler › Plugin yöneticisi...` seçeneğini açın, `Plugin klasörleri...` düğmesine tıklayın, klasörü ekleyin, sonra `Yeni plugin bul` düğmesine tıklayın.
3. Plugin seçici için `F8` tuşuna basın ve plugin adının bir kısmını yazın.
4. Ona çift tıklayın veya `Enter` tuşuna basın. Enstrüman yeni bir kanal olur. Efekt seçili Mixer insert'ine girer.

İlk açılışta kurulum sihirbazı plugin klasörlerini listeler ve arka planda tarar. `Seçenekler › Kurulum sihirbazı (dil, ses, plugin klasörleri)...` onu yeniden açar. LYNX SHARE ayrıca her açılıştan birkaç saniye sonra yeni plugin'leri arar.

## Sisteme göre formatlar

| Sistem | Barındırılan formatlar |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

VST2, macOS'ta barındırılmaz. Linux'ta Windows plugin'leri, kendinizin kurduğu [yabridge](https://github.com/robbert-vdh/yabridge) ve Wine üzerinden çalışır: yabridge'in senkronladığı klasörü plugin klasörü olarak ekleyin.

## Plugin yöneticisi

| Kontrol | Ne yapar |
|---|---|
| Arama kutusu | Ada, yapımcıya, formata veya dosyaya göre süzer |
| `Tümü`, `Enstrümanlar`, `Efektler`, `Favoriler`, `Başarısız` | Hangi plugin'lerin listeleneceğini seçer |
| Yıldız sütunu | Favori işaretler |
| `Bit` sütunu | 32, 64 veya 32+64 (Windows) |
| `Durum` sütunu | `Tamam`, `Dosya yok` veya `Yüklenemedi` |
| `Yeni plugin bul` | Listede henüz olmayan dosyaları tarar |
| `Yeniden tara...` | Başarısız dosyaları unutur ve her dosyayı yeniden inceler |
| `Plugin klasörleri...` | Taranan klasörleri düzenler |
| `Plugin dosyası ekle...` | Tek bir plugin dosyası ekler |

Her plugin dosyası ayrı bir süreçte incelenir. Çöken veya 30 saniye takılan bir dosya başarısız olarak işaretlenir ve sonra atlanır. Windows'ta başarısız dosyalar nedenleriyle birlikte `%APPDATA%\LYNX\scan-failures.log` içinde listelenir.

Standart klasörler ve sizin eklediğiniz klasörler taranır:

- Windows: Program Files altında `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` ve `Steinberg\VSTPlugins`; ayrıca kuruluysa FL Studio'nun kullandığı klasörler.
- macOS: `~/Library/Audio/Plug-Ins` ve `/Library/Audio/Plug-Ins` içindeki `VST3`, `Components` ve `CLAP`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` ve `/usr/lib` ile `/usr/local/lib` altındaki aynı klasörler.

## Yerleşik ve ayrı plugin pencereleri

Bir plugin'in editörü, diğer paneller gibi ana pencerenin içinde açılır. Plugin penceresinin menüsündeki `Ayrılmış` o plugin'e kendi penceresini verir, aynı menüdeki `Üste sabitle` ise ayrılmış bir pencereyi LYNX SHARE'in diğer pencerelerinin üstünde tutar.

`Seçenekler › Genel ayarlar...` içindeki `Tüm yeni eklentileri ayır`, eklediğiniz her plugin'i ayrı pencerede açar. `Araçlar › Tüm plugin düzenleyicilerini şuna geçir` projedeki tüm plugin'ler için `Yerleşik` veya `Ayrılmış` ayarlar.

## Plugin'leri ayrı süreçte çalıştırma

Varsayılan olarak bir plugin LYNX SHARE'in içinde çalışır; bu yüzden çöken bir plugin programı da düşürebilir. İsteğe bağlı olarak her plugin kendi sürecinde çalışır.

1. `Seçenekler › Genel ayarlar...` seçeneğini açın.
2. `Plugin'leri ayrı süreçte çalıştır (LYNX SHARE'i çökmelere karşı korur)` seçeneğini açın.
3. Zaten açık olan plugin'lere uygulamak için projeyi yeniden açın.

Böyle bir plugin çökerse veya yanıt vermeyi bırakırsa susturulur, bir ileti görünür ve LYNX SHARE çalışmaya devam eder. O iletideki `Yeniden yükle`, plugin'i projeyi yeniden açmadan, son kayıttaki veya yedekteki ayarlarla yeniden başlatır. Plugin'in editörü kendi penceresinde açılır; yerleştirilemez ve üste sabitlenemez.

Bunu yalnızca tek bir plugin için yapmak üzere, plugin penceresinin wrapper menüsünde `Ayrı süreçte çalıştır` veya `Always run <plugin> in a separate process` seçeneğini kullanın. Bir plugin'in editörü çöktüğünde görünen ileti ikincisini de sunar.

Bu Windows ve Linux'ta çalışır, macOS'ta ise 0.1.25'ten beri.

LYNX SHARE içinde çöken bir plugin kapatılır ve iletisi de `Yeniden yükle` sunar. Sonradan kanalın menüsündeki veya efekt yuvasının menüsündeki `Çöken plugin'i yeniden yükle` kullanılabilir.

## 32 bit plugin'ler (yalnızca Windows)

Windows'ta 32 bit VST2 ve VST3 plugin'leri bir köprü (`LYNX-bridge32.exe`) üzerinden çalışır. Köprü gecikme eklemez. İçinde çöken plugin kendi kendine durur, LYNX SHARE çalmaya devam eder. Köprülenmiş bir plugin'in editörü kendi penceresinde açılır; yerleştirilemez ve üste sabitlenemez. Bir plugin'in hem 32 bit hem 64 bit derlemesi kuruluysa plugin penceresinin `Wrapper ayarları` bölümündeki `Bit boyutu` hangisinin yükleneceğini seçer.

## Plugin performans monitörü

![Plugin performans monitörü: her enstrüman ve efekt için işlem süresindeki payıyla bir satır](/assets/img/wiki/plugin-monitor.webp)

`Araçlar › Plugin performans monitörü...` her enstrümanı ve efekti aldığı işlem süresiyle listeler: anlık, ortalama ve tepe değerde. %100, sesin sürdüğü tüm süre boyunca bir işlemci çekirdeğinin meşgul olması demektir.

| Ayar | Ne yapar |
|---|---|
| `Bul` | Plugin'e, kanala veya insert'e göre süzer |
| `Yalnızca plugin'ler` | LYNX SHARE'in kendi enstrümanlarını ve efektlerini gizler |
| `Dondur` | Sayıların güncellenmesini durdurur |
| `Çalınca sıfırla` | Her çalma başladığında ortalamaları ve tepeleri yeniden başlatır |
| `Sıfırla` | Ortalamaları ve tepeleri şimdi temizler |

## Bir enstrümanın çıkışlarını ayrı insert'lere gönderme

Birden fazla çıkışı olan bir enstrüman plugin'i, her çıkışı kendi Mixer insert'inde çalabilir. Plugin penceresinde `Wrapper ayarları`'e, sonra `İşleme` sekmesine tıklayın. `Bağlantılar` altında her çıkışın onu açıp kapatan bir ışığı ve bir sayısı vardır: kanalın kendi insert'inden kaç insert sonrasında çaldığı. `Çıkışları otomatik eşle` her çıkışa bir sonraki insert'i verir, `Sıfırla` varsayılanlara döner.

## Plugin database klasörü

[Browser](/wiki/browser/)'daki Plugin database'in klasörleri, plugin seçici ve plugin menüleri diskte bir klasördür: Belgeler klasörünüzdeki `LYNX/Plugin database`. Açmak için Browser'da `Plugin veritabanı`'e sağ tıklayıp `Aç`'ı seçin.

- `Efektler` ve `Üreteçler`, plugin başına bir `.lynxplugin` dosyası tutar. Klasörler ve alt klasörler (4 seviyeye kadar) oluşturup dosyaları içine taşıyın.
- Menülerdeki plugin'i yeniden adlandırmak için dosyayı yeniden adlandırın. Plugin'i menülerden çıkarmak için dosyayı silin. Plugin kaldırılmış olmaz.
- `Kurulu`'da her plugin için bir kayıt vardır. Bir plugin'i geri getirmek için birini `Efektler` veya `Üreteçler` içine kopyalayın.

## Plugin'leri olmadan proje açma

`Dosya › Üçüncü taraf plugin'ler olmadan aç...`, açılmayan veya yüklenirken çöken bir proje için, bir LYNX SHARE projesini tüm üçüncü taraf plugin'ler kapalı olarak açar. Plugin'lerin kayıtlı ayarları korunur. `Dosya › Plugin'leri yeniden aç...` onları yeniden açar.

## Bir plugin kurulu olmadığında

LYNX SHARE aynı plugin'i önce tanımlayıcısıyla, sonra dosyasıyla, sonra adıyla arar. VST2 ile kaydedilmiş bir proje aynı ürünün VST3'üyle açılır, tersi de geçerlidir; ancak o zaman plugin varsayılan ayarlarıyla çalar.

Bir efekt hiç kurulu değilse aynı işi gören bir LYNX SHARE efekti yuvasını alır ve orijinal projede kalır. Plugin'in kayıtlı ayarları okunabiliyorsa yedek efekt eşleşen ayarlarla başlar. Anahtar, `Seçenekler › Genel ayarlar...` içindeki `Eksik plugin'lerin yerine LYNX SHARE karşılıklarını kullan`'tir. Bkz. [Dahili enstrümanlar ve efektler](/wiki/built-in/). `Yardım › Eksik dosyalar...` yerine konan plugin'leri listeler.

## Bilmekte fayda var

- Barındırılan bir plugin'in penceresinde bir preset çubuğu vardır: plugin'in kendi programları, preset'leriniz, `Preset kaydet...`, `Preset dosyası yükle...` ve `Preset dosyası dışa aktar...`. `.fxp`, `.fxb` ve `.vstpreset` dosyaları yalnızca LYNX SHARE'in içinde çalışan bir plugin'e yüklenir, ayrı bir süreçte çalışana yüklenmez.
- Monitör yalnızca penceresi açıkken ölçer.
- Bir plugin penceresi odaktayken, plugin'in kullanmadığı bir tuş yazı klavyesi piyanosunu çalar. Windows'ta, pencere başlığındaki klavye düğmesi kapalıyken LYNX SHARE tuşları, kısayollar dahil, plugin'den önce işler.
- Bir VST3 plugin'in kendi parametre menüsü, `Otomasyon klibi oluştur` gibi LYNX SHARE öğeleriyle başlar. Bkz. [Otomasyon](/wiki/automation/).
- Bir plugin'in `Wrapper ayarları` bölümünde `MIDI` altındaki `MPE`, her notayı kendi MIDI kanalından gönderir. Bkz. [MIDI kontrolcüleri](/wiki/midi-controllers/).
- Eksik enstrümanların yerine henüz bir şey konmaz. Eksik bir enstrüman plugin'i, kanal adına göre seçilen dahili bir synth olur.

## İlgili sayfalar

- [Dahili enstrümanlar ve efektler](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Sorun giderme](/wiki/troubleshooting/)

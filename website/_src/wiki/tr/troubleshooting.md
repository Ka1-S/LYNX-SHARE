---
source: a3748d4a2a
---
Aşağıdaki her başlık bir sorundur. Sizinki burada yoksa sondaki anlatıma göre bildirin.

## Programı başlatma

### Windows: SmartScreen ilk açılışta uyarıyor

Çalıştırılabilir dosya henüz kod imzalı değil; bu yüzden SmartScreen ilk çalıştırışınızda uyarabilir.

1. `More info` düğmesine tıklayın.
2. `Run anyway` düğmesine tıklayın.

Kurulum programı yönetici hakkı gerektirmez.

### macOS: "açılamıyor"

Uygulama Apple tarafından notarize edilmemiştir ve Developer ID imzası yoktur; bu yüzden ilk açılış için bir ek adım gerekir.

1. LYNX SHARE'i `Applications` klasörüne sürükleyin.
2. Ona sağ tıklayın (Control+tık) ve `Aç` seçin.
3. Yeniden `Aç` düğmesine tıklayın.

macOS 15 Sequoia veya daha yenisinde, bir kez açmayı deneyin, sonra `System Settings › Privacy & Security` bölümüne gidip `Open Anyway` düğmesine tıklayın. Ya da Terminal'de:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS, ilk kayıt yapışınızda mikrofon erişimi ister.

### Linux: AppImage başlamıyor

LYNX SHARE, glibc 2.35 veya daha yenisi olan bir x86_64 sistem (Ubuntu 22.04, Debian 12, Fedora 36, güncel Arch veya Manjaro) ve X11 veya XWayland ister.

1. Dosyayı çalıştırılabilir yapın ve aşağıdaki iki komutla çalıştırın.
2. Hâlâ başlamıyorsa libfuse2'yi kurun. Paket Ubuntu 22.04'te `libfuse2`, Ubuntu 24.04'te `libfuse2t64`, Arch'ta `fuse2` ve Fedora'da `fuse-libs` adını taşır.
3. Kurmak istemezseniz bunun yerine `.tar.gz` dosyasını indirin, çıkarın ve `./install.sh` komutunu çalıştırın.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh`, LYNX SHARE'i kullanıcınız için `~/.local` altına bir menü girdisiyle kurar. `./install.sh --uninstall` onu kaldırır.

## Ses

### Ses yok ya da yanlış ses aygıtı

1. `Seçenekler › Ses ayarları...` seçeneğini açın.
2. Bir sürücü seçin.
3. Bir çıkış aygıtı seçin.

| Sistem | Sürücüler |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO ve dahili LYNX ASIO sürücüsü |
| macOS | Core Audio |
| Linux | PipeWire (yerel), JACK ve ALSA. İlk açılışta LYNX SHARE, bunlardan aygıtı olan ilkini açar |

ASIO bir Windows sürücü modelidir ve yalnızca Windows'ta bulunur. `Seçenekler › Kurulum sihirbazı (dil, ses, plugin klasörleri)...` da ses aygıtını sınar. Bkz. [Ses ayarı](/wiki/audio-setup/).

### Linux: ses bozuluyor

Ses thread'inin gerçek zamanlı öncelikle çalışması gerekir.

1. `Seçenekler › Ses ayarları...` seçeneğini açın ve `Durum` değerini okuyun. Thread'in gerçek zamanlı öncelik alıp almadığını gösterir.
2. Almadıysa rtkit'i kurun (`sudo apt install rtkit`, `sudo dnf install rtkit` veya `sudo pacman -S rtkit`) ya da JACK kurulumlarında olduğu gibi kendinizi gerçek zamanlı sınırı olan `ses` grubuna ekleyin.
3. Oturumu kapatıp yeniden açın.

Ayrıntılar [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md) içindedir.

## Plugin'ler

### Bir plugin çöküyor

Yalnızca plugin durması için onu kendi sürecinde çalıştırın.

1. `Seçenekler › Genel ayarlar...` seçeneğini açın.
2. `Plugin'leri ayrı süreçte çalıştır (LYNX SHARE'i çökmelere karşı korur)` seçeneğini açın.
3. Projeyi yeniden açın.

Kendi sürecinde çöken veya yanıt vermeyi bırakan bir plugin susturulur. Plugin'i yeniden yüklemek için projeyi kaydedip yeniden açın. Bkz. [Üçüncü taraf plugin'ler](/wiki/plugins/).

### Bir plugin listede yok

1. `Seçenekler › Plugin tara` seçeneğini seçin.
2. Hâlâ yoksa `Seçenekler › Plugin yöneticisi...` seçeneğini açın, `Plugin klasörleri...` düğmesine tıklayın ve plugin'in klasörünü ekleyin.
3. `Başarısız` sekmesine bakın. Taramada başarısız olan bir plugin dosyası atlanır. Windows'ta nedeni `%APPDATA%\LYNX\scan-failures.log` içindedir.
4. Başarısız dosyaları yeniden denemek için `Yeniden tara...` düğmesine tıklayın.

### Bir proje kurulu olmayan bir plugin kullanıyor

Bir efektin yerini aynı işi gören bir LYNX SHARE efekti alır ve ayarları projede korunur. `Yardım › Eksik dosyalar...` eksik olanları listeler. Bkz. [Dahili enstrümanlar ve efektler](/wiki/built-in/).

### Linux'ta video çalışmıyor

Video izleri ve `Export with video` için FFmpeg gerekir. LYNX SHARE onu nasıl kuracağınızı söyler.

## Çökme raporları

![Bir sonraki açılışta gösterilen çökme raporu sorusu; Gönder, Gönderilecekleri göster ve Gönderme ile](/assets/img/wiki/crash-report.webp)

Çökme raporları isteğe bağlıdır. Bir çökme veya donmadan sonra, sonraki açılışta rapor gönderilip gönderilmeyeceği sorulur.

| Komut | Ne yapar |
|---|---|
| `Gönderilecekleri göster` | Tam metni gösterir |
| `Bunun yerine GitHub'da bildir` | GitHub'ın önceden doldurulmuş sorun formunu açar. Dosyaları kendiniz eklersiniz |
| `Raporun bulunduğu klasörü aç` | Rapor dosyalarını bilgisayarınızda gösterir |
| `Include file names (projects, samples)` | Kapalıyken her dosyanın yalnızca uzantısı gönderilir |
| `Sormadan her zaman gönder` | Sonraki raporları soru sormadan gönderir |

`Seçenekler › Genel ayarlar...` içindeki `Çökme raporları`, `Ask` (varsayılan), `Her zaman gönder` veya `Asla gönderme` ayarını yapar.

Bir rapor şunları içerir: LYNX SHARE sürümü, işletim sistemi, CPU, ekran kartları ve sürücüleri, ekran boyutu, ses sürücüsü ve aygıtı, çökme veya donma dosyası, oturumun son 60 adımı ve yüklenmiş üçüncü taraf plugin'lerin adları ve sürümleri.

Ses, proje, sample, bilgisayarınızın adı veya kullanıcı adınızı içermez. Sunucu hiçbir IP adresi saklamaz. Gönderilen kimlikler ayarlar klasöründeki `crash-reports\sent.log` içinde listelenir. Bir raporun silinmesini istemek için kimliğiyle bir sorun açın. Tam metin [gizlilik politikasındadır](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Sorun bildirme

![Sorun bildir penceresi: bir açıklama kutusu, isteğe bağlı bir e-posta adresi ve Gönder düğmesi](/assets/img/wiki/report-problem.webp)

1. `Yardım › Sorun bildir...` komutunu seçin.
2. Ne olduğunu anlatın. E-posta adresi isteğe bağlıdır; cevap istiyorsanız yazın.
3. Sorun bir çökmeyse `En yeni çökme / donma dosyalarını ve bu oturumun adımlarını ekle` kutusunu işaretli bırakın.
4. Metni denetlemek için `Gönderilecekleri göster` düğmesine tıklayın.
5. `Gönder` düğmesine tıklayın. Ondan önce hiçbir şey gönderilmez.

[github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues) adresinde de bir sorun açabilirsiniz.

## Bilmekte fayda var

- Bir çökmeden sonra projenizin yedek kopyaları LYNX SHARE'in otomatik kayıt klasöründedir.
- Çökmede bir plugin rol oynadıysa soru, o plugin'i bundan sonra ayrı bir süreçte çalıştırmayı önerir.
- Ayarlar klasörü Windows'ta `%APPDATA%\LYNX`, macOS'ta `~/Library/Application Support/LYNX` ve Linux'ta `~/.config/LYNX` yoludur.

## İlgili sayfalar

- [Ses ayarı](/wiki/audio-setup/)
- [Üçüncü taraf plugin'ler](/wiki/plugins/)
- [Kurulum](/wiki/install/)
- [SSS](/faq/)
- [Çökme koruması](/why/#crash)

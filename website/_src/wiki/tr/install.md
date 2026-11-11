---
source: 8a120ab7de
---
Her sürümün Windows, macOS ve Linux için derlemeleri [sürümler sayfasında](https://github.com/Ka1-S/LYNX-SHARE/releases) bulunur. Dosyalar, etiketlenmiş kaynaktan GitHub Actions ile derlenir ve SHA-256 değerleriyle `SHA256SUMS.txt` içinde listelenir. [İndir](/download/) sayfası güncel olanlara bağlanır.

## Windows'a kurma

Windows 10 veya 11, 64 bit.

1. `LYNX-<version>-setup-x64.exe` dosyasını indirin.
2. Çalıştırın. Yalnızca kullanıcınız için kurulur ve yönetici hakkı gerektirmez.
3. SmartScreen uyarabilir, çünkü çalıştırılabilir dosya henüz kod imzalı değil. `More info`, ardından `Run anyway` seçin.
4. LYNX SHARE'i Başlat menüsünden ya da masaüstü kısayolundan başlatın.

Kurulum programı, LYNX SHARE'i `.lynx` projelerini açan program yapar. FL Studio'nun varsayılanını değiştirmeden, `.flp` dosyaları için *Birlikte aç* altında LYNX SHARE'i sunar.

İki yol daha var:

| Yol | Dosya veya yer | Ne yapar |
|---|---|---|
| Taşınabilir zip | `LYNX-<version>-windows-x64.zip` | İstediğiniz yere çıkarın ve `LYNX.exe` dosyasını çalıştırın. Hiçbir şey kurulmaz. |
| Microsoft Store | [Store'da LYNX SHARE](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Store onu kurar ve güncel tutar. |

## macOS'a kurma

macOS 11 Big Sur veya daha yenisi. Tek bir evrensel `LYNX-<version>-macos-universal.dmg` hem Intel'i hem Apple silicon'u kapsar.

1. `.dmg` dosyasını açın ve LYNX SHARE'i *Uygulamalar*'a sürükleyin.
2. Uygulama Apple tarafından notarize edilmemiştir ve Developer ID imzası yoktur; bu yüzden ilk açılış için ek bir adım gerekir. *Uygulamalar*'da LYNX SHARE'e sağ tıklayın (veya Control+tıklayın), *Aç*'ı seçin, sonra yeniden *Aç*'a tıklayın.
3. macOS 15 Sequoia veya daha yenisinde, bir kez açmayı deneyin, sonra *Sistem Ayarları › Gizlilik ve Güvenlik* bölümüne gidip *Yine de Aç*'a tıklayın.

Terminal alternatifi:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS, ilk kayıt yapışınızda mikrofon erişimi ister.

## Linux'a kurma

glibc 2.35 veya daha yenisiyle x86_64 (Ubuntu 22.04+, Debian 12+, Fedora 36+, güncel Arch veya Manjaro). LYNX SHARE'in X11 veya XWayland'e ihtiyacı vardır. Ses PipeWire, JACK veya ALSA üzerinden gider ve bunun için ayrıca bir şey kurmak gerekmez.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Bazı dağıtımlar AppImage çalıştırmak için libfuse2'ye ihtiyaç duyar (Ubuntu 22.04'te `libfuse2`, 24.04'te `libfuse2t64`, Arch'ta `fuse2`, Fedora'da `fuse-libs`). İlk açılışta kurulum sihirbazı, LYNX SHARE'i uygulamalar menüsüne eklemeyi ve `.lynx` dosyalarını onunla açmayı önerir. Bunu sonra `Seçenekler › Genel ayarlar...` içinde ya da bir terminalden `./LYNX-*.AppImage --menu-entry on` (veya `kapalı`) ile değiştirebilirsiniz.

libfuse2 kurmak istemezseniz `LYNX-<version>-linux-x86_64.tar.gz` dosyasını kullanın: çıkarın ve `./install.sh` komutunu çalıştırın. Bu, LYNX SHARE'i kullanıcınız için `~/.local` altına bir menü girdisi, bir simge ve `.lynx` dosya türüyle kurar.

## Ayarların saklandığı yer

| Sistem | Ayarlar klasörü |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (ya da `$XDG_CONFIG_HOME` altında) |

Klasör, seçeneklerinizi, seçtiğiniz dili (`language.txt`), kısayollarınızı (`keys.json`), plugin klasörlerinizi (`plugin-folders.txt`) ve taranmış plugin'lerin listesini (`plugins.xml`) tutar.

## Güncelleme

Açılışta LYNX SHARE, GitHub'dan en yeni sürüm numarasını sorar. Sizinle ilgili hiçbir şey gönderilmez. Yeni bir sürüm çıktığında bir çubuk, `Yenilikler` düğmesi ve bir güncelleme düğmesiyle `LYNX SHARE <version> is available` yazısını gösterir. `Yardım › Güncellemeleri denetle...` istediğiniz zaman sorar.

| Derleme | Nasıl güncellenir |
|---|---|
| Windows kurulum programı | `Güncelle` yeni kurulum programını indirir, kaydedilmemiş değişiklikleri sorar, kurar ve yeniden başlatır |
| Windows taşınabilir zip, macOS | `Güncelle` yeni dosyayı indirir ve klasöründe gösterir |
| Linux AppImage | `Güncelle` AppImage dosyasını yerinde günceller. Gear Lever ve AppImageUpdate aynısını dışarıdan yapabilir |
| Linux `.tar.gz` | `İndir...` indirme sayfasını açar |
| Microsoft Store | Store günceller. Bu derlemenin kendi güncelleme denetimi yoktur |

Denetimi kapatmak için `Seçenekler › Genel ayarlar...` seçeneğini açın ve `Güncellemeleri otomatik denetle` işaretini kaldırın.

## Kaldırma

- Windows kurulum programı: LYNX SHARE'i Windows'un *Uygulamalar ve özellikler* listesinden kaldırın.
- Taşınabilir zip: çıkardığınız klasörü silin.
- Linux `.tar.gz`: `./install.sh --uninstall` komutunu çalıştırın.

## Bilmekte fayda var

- Windows derlemeleri, projenin kod imzalama başvurusu kabul edilene kadar imzasızdır. macOS uygulamasında yalnızca ad-hoc imza var. Linux paketleri imzalı değildir.
- Bir indirmeyi doğrulamak için SHA-256 değerini sürüm sayfasındaki `SHA256SUMS.txt` satırıyla karşılaştırın.
- Kurulum programı, zip, `.dmg` ve `.tar.gz` demo projesi `Demo.lynx` dosyasını içerir.
- Güncelleme çubuğunu kapatma düğmesiyle kapatmak o sürümü atlar.

## İlgili sayfalar

- [Buradan başlayın](/wiki/getting-started/)
- [Ses ayarı](/wiki/audio-setup/)
- [Sorun giderme](/wiki/troubleshooting/)
- [İndir](/download/)
- [Güncellemeler](/updates/)

---
source: 2298a70fec
---
![Geri alma geçmişi penceresi: Start'tan Add notes'a altı adım, geçerli adım olarak Mute: Kick](/assets/img/wiki/history.webp)

Projede yaptığınız her düzenleme geri alma geçmişinde bir adımdır: eklediğiniz bir nota, taşıdığınız bir klip, bir fader, bir yönlendirme değişikliği, tempo. `Düzen › Geri alma geçmişi...` (`Ctrl+Alt+H`) adımları bir liste olarak gösterir. Bundan ayrı olarak LYNX SHARE siz çalışırken projenin yedek kopyalarını kaydeder.

## Daha önceki bir adıma dönmek

1. `Düzen › Geri alma geçmişi...` yolunu seçin ya da `Ctrl+Alt+H` tuşlarına basın.
2. Liste adımları, en yenisi altta olacak şekilde gösterir. İlk satır `Başlangıç`, projenin açıldığı andaki hâlidir.
3. Bir adıma tıklayın. Proje, o adımdan hemen sonraki duruma döner ve sonraki adımlar grileşir.
4. Yeniden ileri gitmek için gri bir adıma tıklayın.
5. Seçtiğiniz adımdan devam etmek için yeni bir düzenleme yapın. Gri adımlar o zaman kaybolur.

## Geri alma tuşları

`Ctrl+Z`, FL Studio 2026 ve çoğu programda olduğu gibi her basışta geçmişte bir adım geri gider. `Ctrl+Y` veya `Ctrl+Shift+Z` yeniden ileri gider.

| Komut | Tuş | Ne yapar |
|---|---|---|
| `Geri al` | `Ctrl+Z` | Her basışta bir adım geri gider |
| `Yinele` | `Ctrl+Y` veya `Ctrl+Shift+Z` | İleri gider |
| `Geri alma geçmişi...` | `Ctrl+Alt+H` | Listeyi açar |

`Düzen` menüsü her öğenin etkileyeceği adımın adını gösterir, örneğin `Undo Add notes: Pattern 1`. Her geri alma veya yinelemeden sonra ipucu çubuğu adımın adını ve seviyeyi gösterir, örneğin `Level 2/3`.

FL Studio'nun diğer geri alma modu da var: `Seçenekler › Genel ayarlar...` içinde `Alternatif geri alma modu` seçeneğini kapatın. O zaman `Ctrl+Z` son adımı geri alır ve yeniden basılırsa yineler; yani son iki durum arasında geçiş yapar. `Ctrl+Alt+Z` (`Geriye doğru geri al`) daha geriye gider, `Ctrl+Y` veya `Ctrl+Shift+Z` ise yinelemeye devam eder.

## Neler adımdır, neler değildir

- Projedeki düzenlemeler adımdır: notalar, pattern'ler, klipler, kanallar, mixer seviyeleri ve yönlendirme, efektler, otomasyon, tempo, susturmalar.
- Bir kayıt bir adımdır. Bir [çevrimiçi oturumda](/wiki/online-sessions/) bir partiyi birleştirmek ve `Bu kayıtlardan comp oluştur` de öyle.
- Projeyi değiştirmeyen şeyler adım değildir: zoom, kaydırma, pencere taşıma ve programın kendi ayarları.
- Kaydetme ve dışa aktarma geri alınmaz. Yazdıkları dosyalar kalır.
- LYNX SHARE'in kendi plugin pencerelerinin, A ve B ayarlarının yanında, o plugin'in değişiklikleri için ayrı bir geri al ve yinele düğmesi vardır.

## Ayarlar

İkisi de `Seçenekler › Genel ayarlar...` içindedir.

| Ayar | Ne yapar |
|---|---|
| `Maksimum geri alma seviyesi` | Kaç adımın tutulacağı: 25, 50, 100, 200, 500 veya 1000. Varsayılan 100'dür |
| `Alternatif geri alma modu` | Varsayılan olarak açık: `Ctrl+Z` geçmişte geri gider. Kapalı: `Ctrl+Z` son adımı geri alma ile yineleme arasında geçiş yapar |

## Yedekler

LYNX SHARE varsayılan olarak her 2 dakikada bir açık projenin yedek kopyasını kaydeder ve proje başına en yeni 10 tanesini tutar. Yedek, ayarlar klasörünün içindeki `autosave` klasöründe duran, adı proje adı ile `(autosaved on ...)` ve tarih ile saatten oluşan sıradan bir `.lynx` dosyasıdır.

Var olan bir proje dosyasının üzerine her kaydedişinizde, yerine geçilen sürüm adında `(overwritten on ...)` olacak şekilde aynı klasöre kopyalanır.

| Komut | Ne yapar |
|---|---|
| `Dosya › Son yedeğe dön...` | Sorduktan sonra açık projeyi en yeni yedeğiyle değiştirir. Proje dosyasının kendisi yalnızca kaydettiğinizde değişir |
| `Dosya › Otomatik kayıt klasörünü aç` | Yedekleri dosya yöneticisinde gösterir |
| `Dosya › Yeni sürüm kaydet` (`Ctrl+Alt+S`) | Geçerli dosyanın yanına `Song_2`, `Song_3` gibi kaydeder |

`Seçenekler › Genel ayarlar...` içindeki `Yedek` bölümünde şu ayarlar bulunur:

| Ayar | Ne yapar |
|---|---|
| `Yedek kopya kaydet` | `Asla` ya da dakika cinsinden bir aralık |
| `Proje başına saklanan yedek` | 1, 3, 5, 10, 20 veya 50 |
| `Şarkı çalarken veya MIDI klavye çalınırken yedeklemeyi beklet` | Sizi bölmemesi için yedeği bekletir |
| `Yedekler arası en uzun bekleme` | 5 ile 60 dakika arası. Siz çalarken bile bu süreden sonra bir yedek alınır |

Son oturum beklenmedik şekilde sona erdiyse bir sonraki açılış, o oturumun kaydettiği en yeni yedeği açmayı önerir.

## Bilmekte fayda var

- Geçmiş projeyle birlikte kaydedilmez. Bir proje açmak ya da yenisini başlatmak geçmişi boşaltır.
- Geçmiş dolunca en eski adımlar atılır.
- Canlı bir çevrimiçi oturumda geri al ve yinele yalnızca sizin düzenlemelerinizi geri getirir, başkalarınınkini asla.
- `Yedek kopya kaydet` `Asla` iken bile bir dosyanın üzerine kaydederken alınan kopyalar yine tutulur.
- Ayarlar klasörü Windows'ta `%APPDATA%\LYNX`, macOS'ta `~/Library/Application Support/LYNX` ve Linux'ta `~/.config/LYNX` yoludur.

## İlgili sayfalar

- [Kısayollar](/wiki/shortcuts/)
- [Ayarlar](/wiki/settings/)
- [Dosya formatları](/wiki/file-formats/)
- [Sorun giderme](/wiki/troubleshooting/)
- [Yalnızca LYNX SHARE'in yaptıkları: çökmeye karşı güvenlik](/why/#crash)

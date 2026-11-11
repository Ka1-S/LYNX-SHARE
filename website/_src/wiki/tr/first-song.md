---
source: de4ead4a7a
---
Bu sayfa tüm yolu bir kez baştan sona gösterir: bir davul pattern'i, bir melodi, bir aranjman, bir mix ve dışa aktarılmış bir dosya. Her bölüm, penceresini ayrıntısıyla anlatan sayfaya bağlanır. Herhangi bir noktada hiçbir şey duymuyorsanız [Ses ayarı](/wiki/audio-setup/) sayfasına bakın.

## Davul pattern'i yapma

1. `Dosya › Şablondan yeni › Boş` komutunu seçin. Proje beş kanalla başlar: Kick, Clap, Hat, Snare ve Synth; hiç step yoktur.
2. [Channel Rack](/wiki/channel-rack/) görünmüyorsa `F6` tuşuna basın.
3. Kick satırında birkaç step'e tıklayın. Sürüklemek birkaçını birden çizer, sağ tık siler.
4. `Space` tuşuna basın. Transport Pattern modundadır, bu yüzden pattern döngüde çalar. Durdurmak için `Space` tuşuna yeniden basın.
5. Clap, Hat ve Snare satırlarına kendi step'lerini verin.
6. Pattern'i adlandırıp renklendirmek için `F2` tuşuna basın.

Olmayan bir ses eklemek için son satırın altındaki `+ kanal ekle` kutusuna tıklayın ve menüden bir enstrüman seçin. Kendi sample'ınızı kullanmak için [Browser](/wiki/browser/)'ı `Alt+F8` ile açın, dinlemek için bir ses dosyasına tıklayın ve yeni kanal olarak eklemek için ona çift tıklayın.

## Melodi yazma

1. `F4` tuşuna basın. LYNX SHARE sıradaki boş pattern'e gider ve adını sorar.
2. Synth kanalının adına sağ tıklayın ve `Piano Roll` seçeneğini seçin.
3. Kalem (`P`) ile nota eklemek için grid'e tıklayın.
4. Taşımak için notayı sürükleyin, uzunluğunu değiştirmek için ucunu sürükleyin. Sağ tık notayı siler.
5. Düzenlerken pattern'i duymak için `Space` tuşuna basın.

`Ctrl+T` yazı klavyesini piyanoya çevirir; böylece notaları önce `Z` ile `M` ve `Q` ile `P` arası tuşlarda deneyebilirsiniz. `P` gibi harf kısayollarını kullanmadan önce onu yeniden kapatın.

## Pattern'leri aranje etme

1. [Playlist](/wiki/playlist/)'i göstermek için `F5` tuşuna basın.
2. Playlist'in solundaki Seçici panelinde davul pattern'ine tıklayın. Artık klip kaynağı odur.
3. Kalem (`P`) ile pattern'i yerleştirmek için bir izin üzerine tıklayın. Yeniden yerleştirmek için biraz daha sağa tekrar tıklayın ya da Fırça (`B`) ile sürükleyip klipleri uç uca boyayın.
4. Seçici'de melodi pattern'ine tıklayın ve onu başka bir ize yerleştirin.
5. Song moduna geçmek için `L`, sonra `Space` tuşuna basın. Playlist şarkı konumundan çalar. Ayarlamak için cetvele tıklayın.

## Mix yapma ve efekt ekleme

1. [Mixer](/wiki/mixer/)'i göstermek için `F9` tuşuna basın. Başlangıçtaki beş kanal zaten 1'den 5'e kadar insert'ler üzerinden çalar; `+ kanal ekle` menüsünden eklenen kanal boş bir insert alır. Channel Rack'te kanalın yönlendirme kutusundaki sayı hangisi olduğunu söyler.
2. Kanalları kendilerine ait boş insert'lere taşımak için Channel Rack'te seçin ve `Ctrl+L` tuşuna basın. Her insert kanalın adını ve rengini alır.
3. Seviyesini ayarlamak için bir insert'in fader'ını sürükleyin.
4. Seçmek için bir insert'e tıklayın. Sağdaki efekt paneli on yuvasını gösterir.
5. Boş bir yuvaya tıklayın ve bir efekt seçin. Efektin penceresini göstermek veya gizlemek için yuvanın adına tıklayın. Yuvanın ışığı efekti bypass eder.

## Şarkıyı dışa aktarma

![Dışa aktarma penceresi: solda aralık ve kuyruk, sağda format, bit derinliği ve örnekleme hızı, altta mix seçenekleri](/assets/img/wiki/export.webp)

1. `Ctrl+S` tuşlarına basın ve önce projeyi kaydedin.
2. `Dosya › Dışa aktar › Wave dosyası...` komutunu seçin. Diğer formatlar için aynı menüde `MP3 dosyası...`, `OGG dosyası...` ve `FLAC dosyası...` bulunur.
3. `Aralık` ayarını `Şarkı (Playlist)` yapın. Diğer seçenekler `Geçerli pattern` ve `Playlist seçimi` seçenekleridir.
4. Diğer seçenekleri olduğu gibi bırakın.
5. `Dışa aktar...` düğmesine tıklayın, dosya adını seçin ve render'ın bitmesini bekleyin.

## Hangi tuş ne yaptı

| Komut | Tuş | Ne yapar |
|---|---|---|
| Channel Rack | `F6` | Channel Rack'i gösterir veya gizler |
| Sonraki boş pattern | `F4` | Sonraki boş pattern'e gider ve adlandırır |
| Piano Roll | `F7` | Piano Roll'u gösterir veya gizler |
| Playlist | `F5` | Playlist'i gösterir veya gizler |
| Pattern veya Song modu | `L` | `Space` tuşunun neyi çaldığını değiştirir |
| Mixer | `F9` | Mixer'i gösterir veya gizler |
| Boş insert'lere gönder | `Ctrl+L` | Seçili kanallara kendi Mixer insert'lerini verir |
| Kaydet | `Ctrl+S` | Projeyi `.lynx` olarak kaydeder |
| Dışa aktar | `Ctrl+R` | Son kullanılan formatla dışa aktarma penceresini açar |

## Bilmekte fayda var

- Pattern modunda yalnızca geçerli pattern'i duyarsınız. Aranjmandan sonra şarkı sessizse transport'un Song modunda olup olmadığına bakın.
- Transport Song modundaysa ve Playlist'te klipler varsa dışa aktarma penceresi `Şarkı (Playlist)` ile açılır. Aksi halde `Geçerli pattern` ile açılır.
- Metronom tıkı hiçbir zaman dışa aktarmanın parçası olmaz.
- Dışa aktarma penceresinin alt kısmı şarkının uzunluğunu ölçü olarak, toplam süresini ve dosyanın ihtiyaç duyduğu disk alanını gösterir.
- Varsayılan olarak yeni bir projede Master insert'inde bir Limiter vardır. Duyduğunuzun ve dışa aktardığınızın parçasıdır.
- Geri alma `Ctrl+Z` tuşudur. Yeniden basılırsa o adımı yineler. `Ctrl+Alt+Z` daha geriye gider.

## İlgili sayfalar

- [Pencerenin turu](/wiki/tour/)
- [Dışa aktarma](/wiki/exporting/)
- [Otomasyon](/wiki/automation/)
- [Kayıt](/wiki/recording/)
- [Enstrümanlar ve efektler](/instruments-and-effects/)

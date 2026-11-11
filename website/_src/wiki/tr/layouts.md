---
source: 1c353ba51c
---
![Klip başlatma iş akışı: solda klip ve sahne gridi, sağda Picker ve Playlist](/assets/img/cliplaunch.webp)

0.1.25'te `Görünüm › Yerleşim stili` menüsünde iki giriş vardır: `Pattern iş akışı (varsayılan)` ve `Klip başlatma iş akışı`. Aynı seçim `Seçenekler › Genel ayarlar` içinde, Workflow başlığı altında da bulunur. Seçim programı kapattığınızda korunur.

Pattern iş akışı; Channel Rack, pattern'ler ve Playlist'ten oluşur. Klip başlatma iş akışı, Playlist'in yanına sahneli bir klip gridi ekler. Pattern'ler ve Channel Rack kullanılabilir kalır.

## Gridde klip başlatmak

1. `Görünüm › Yerleşim stili › Klip başlatma iş akışı` yolunu seçin. Klip gridi Playlist'in yanında belirir.
2. Boş bir slota sağ tıklayın, `Buraya bir pattern koy` seçeneğini seçin ve bir pattern seçin. Boş bir slota çift tıklamak geçerli pattern'i oraya koyar.
3. Klibi başlatmak için kliple solundaki üçgene tıklayın. Aktarım durmuşsa başlar.
4. Bir satırdaki bütün klipleri başlatmak için `Sahneler` sütunundaki sahne üçgenine tıklayın.
5. Hepsini durdurmak için `Tüm klipleri durdur` düğmesine tıklayın.
6. İzlerin Playlist kliplerini yeniden çalması için `Playlist'e dön` düğmesine tıklayın.

## Ekranda ne değişir

- Klip gridi Playlist'in yanında gösterilir. Playlist izleri sütunlar, sahneler satırlardır.
- Playlist'in başlık çubuğuna `İkisi de`, `Grid`, `Song` anahtarı eklenir. `Grid` yalnızca gridi, `Song` yalnızca Playlist'i gösterir.
- İlk seferde Channel Rack gizlidir. `F6` veya araç çubuğundaki düğmesi onu gösterir.
- Playlist'in otomasyon şeritleri açılır.
- Pencere düzeni stil başına saklanır; böylece her stil bıraktığınız gibi geri gelir.

## Grid

Başlatılan klip hemen başlamaz. Bir sonraki başlatma kuantizasyonu noktasını bekler ve o ana kadar üçgeni yanıp söner. Gridin altındaki kutu, `Global` ayarlı her klibin kullandığı genel başlatma kuantizasyonunu belirler. Durdurma da aynı şekilde bekler.

Ayarları için bir klibe sağ tıklayın.

| Ayar | Ne yapar |
|---|---|
| `Başlatma kuantizasyonu` | Bu klibin ne zaman başlayacağı: `Global`, `Yok`, `8 ölçü` ile `1/32` arası |
| `Başlatma modu` | `Tetik`, `Gate`, `Aç/kapa` veya `Tekrar` |
| `Legato (konumu koru)` | Klip, o izde önce çalan klibin çalma konumunu devralır |
| `Döngü` | Açık: klip tekrarlanır. Kapalı: bir kez çalar, sonra iz durur |
| `Takip eylemi` | Klipten sonra ne olacağı: her birinin bir olasılığı olan iki eylem ve eylemlerin devreye gireceği süre |
| `Delete` | Slotu boşaltır |

`Durdurma düğmesi (sahne bu parçayı burada durdurur)` için boş bir slota sağ tıklayın. Boş bir slotun solundaki kareye tıklamak o izi durdurur. Bir sahneye sağ tıklayınca `Sahne ekle`, `Altına sahne ekle`, `Sahneyi sil`, `Sahneyi yeniden adlandır...` ve sahnenin tempo ayarı gelir.

Slot menüsünde tüm grid için iki seçenek daha vardır: `Aktarım durunca parçaları Playlist'e geri ver` ve `Tüm ızgarada takip eylemleri`.

| Komut | Tuş | Ne yapar |
|---|---|---|
| Taşı | Ok tuşları | Başka bir slot seçer |
| Klibi başlat | `Enter` | Seçili klibi başlatır |
| Sahneyi başlat | `Ctrl+Enter` | Seçili sahneyi başlatır |
| Klibi sil | `Del` | Seçili slotu boşaltır |

## Ableton Live setleri

Bir Live seti açtığınızda Session klipleri ve sahneleri, başlatma ayarlarıyla birlikte klip gridine girer. `Dosya › Dışa aktar › Ableton Live seti (.als)` gridi Session klipleri olarak geri yazar. `Seçenekler › Genel ayarlar` içindeki `Live setlerinin oturum klipleri` ayarında `Çalma listesi, aranjmandan sonra` seçeneği, onları bunun yerine Playlist'e yerleştirir.

## Geri dönmek

`Görünüm › Yerleşim stili › Pattern iş akışı (varsayılan)` yolunu seçin. Pattern iş akışında sahip olduğunuz pencere düzeni geri gelir ve otomasyon şeritleri önceki ayarlarına döner. Gridin klipleri projede kalır.

## Bilmekte fayda var

- Bir klip başlatan iz, `Playlist'e dön` düğmesine basana kadar Playlist'te sessiz kalır. `Aktarım durunca parçaları Playlist'e geri ver` seçeneği bunu her durdurmada sizin yerinize yapar.
- 0.1.25'te bir slot menüsünden ya da çift tıklayarak doldurulur. Bir klibi veya dosyayı slota sürükleyemezsiniz.
- Takip eylemleri kliplerde vardır, sahnelerde yoktur.
- Klip başlatma iş akışındayken elle açılan otomasyon şeritleri geri döndüğünüzde korunmaz.
- Pattern iş akışı seçiliyken gridden hiçbir şey yüklenmez.

## İlgili sayfalar

- [Playlist](/wiki/playlist/)
- [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/)
- [Proje içe aktarma](/wiki/importing-projects/)
- [Yalnızca LYNX'in yaptıkları: iş akışları](/why/#workflows)

Yeni başlayanlar için sadeleştirilmiş bir yerleşim 0.1.25'te yok. Bkz. [Basit yerleşim](/wiki/simple-layout/).

---
source: e15b0e3ba5
---
![Klip başlatma iş akışı: solda klip ve sahne gridi, sağda Picker ve Playlist](/assets/img/cliplaunch.webp)

`Görünüm › Yerleşim stili` menüsünde üç giriş vardır: `Pattern iş akışı (varsayılan)`, `Klip başlatma iş akışı` ve `Basit iş akışı`. Aynı seçim `Seçenekler › Genel ayarlar` içinde, Workflow başlığı altında da bulunur. Seçim programı kapattığınızda korunur.

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
- Grid'in altındaki `Mixer` düğmesi, Ableton Live'daki gibi Session mixer'ı gösterir veya gizler: `Giriş/Çıkış` (`Ses kaynağı`, `İzleme`, `Ses hedefi`), `Send'ler`, `Parça gecikmesi` ve her izin etkinleştirme düğmesi, solo, arm düğmesi ve metre ile ses seviyesi, ayrıca `Ana` şeridi.
- Grid'in altındaki `Groove'lar`, Groove Pool'u gösterir. `Groove'u notalara uygula` bir groove'u bir MIDI klibinin notalarına yazar.
- `MIDI` ve `Ton` düğmeleri MIDI Map Mode ve Key Map Mode'u başlatır. Bkz. [MIDI kontrolcüler](/wiki/midi-controllers/).

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

Klipler menüsünden kesilebilir, kopyalanabilir, yapıştırılabilir ve çoğaltılabilir (`Ctrl+D`); slotlar arasında ve Playlist'e veya Playlist'ten sürüklenebilir. Browser'dan sürüklenen bir pattern veya sample, ya da dışarıdan bırakılan ses dosyaları, düştükleri slotu doldurur. Bir sahnenin menüsünde ayrıca `Çoğalt` ve o anda çalan klipleri yeni bir sahneye koyan `Sahneyi yakala ve ekle` vardır. `Görünüm › Sahne temposu ve ölçü birimi` her sahne için bir tempo ve ölçü gösterir.

Slot menüsünde tüm grid için iki seçenek daha vardır: `Aktarım durunca parçaları Playlist'e geri ver` ve `Tüm ızgarada takip eylemleri`.

| Komut | Tuş | Ne yapar |
|---|---|---|
| Taşı | Ok tuşları | Başka bir slot seçer |
| Klibi başlat | `Enter` | Seçili klibi başlatır |
| Sahneyi başlat | `Ctrl+Enter` | Seçili sahneyi başlatır |
| Klibi sil | `Del` | Seçili slotu boşaltır |
| Draw Mode | `B` | Bir otomasyon şeridinde sürüklemek, Ableton Live'daki gibi grid genişliğinde adımlar çizer; Snap kapalıyken veya `Alt` basılıyken serbest elle çizer. `Seçenekler` içinde de bulunur |

## Ableton Live setleri

Bir Live seti açtığınızda Session klipleri ve sahneleri, başlatma ayarlarıyla birlikte klip gridine girer. `Dosya › Dışa aktar › Ableton Live seti (.als)` gridi Session klipleri olarak geri yazar. `Seçenekler › Genel ayarlar` içindeki `Live setlerinin oturum klipleri` ayarında `Çalma listesi, aranjmandan sonra` seçeneği, onları bunun yerine Playlist'e yerleştirir.

## Basit iş akışı

`Görünüm › Yerleşim stili › Basit iş akışı`, ilk şarkı için izlerin ve zaman çizelgesinin olduğu tek bir penceredir. `Görünüm › Parça paneli`, zaman çizelgesinin altında seçili iz için `Efektler`, `Enstrüman`, `Klavye` ve `Editör` sekmeleri olan bir dock gösterir. Bu iş akışında `Yardım` menüsünde `İlk şarkı turu` ve yedi adımlı bir ilk şarkı, bir demo şarkı veya boş bir proje sunan `Karşılama ekranı...` bulunur. Bkz. [Basit düzen](/wiki/simple-layout/).

`Görünüm` menüsündeki `Söz ve notlar` ve `Referans eşleme...` ile bir Mixer insert'inin menüsündeki `Temiz ses` her düzende vardır. Basit iş akışında `Temiz ses` bir izin menüsünde de bulunur.

## Geri dönmek

`Görünüm › Yerleşim stili › Pattern iş akışı (varsayılan)` yolunu seçin. Pattern iş akışında sahip olduğunuz pencere düzeni geri gelir ve otomasyon şeritleri önceki ayarlarına döner. Gridin klipleri projede kalır.

## Bilmekte fayda var

- Bir klip başlatan iz, `Playlist'e dön` düğmesine basana kadar Playlist'te sessiz kalır. `Aktarım durunca parçaları Playlist'e geri ver` seçeneği bunu her durdurmada sizin yerinize yapar.
- Grid'in yeni tuşları (`B`, `Q`, `Ctrl+M`, `Ctrl+K` ve diğerleri) yalnızca Klip başlatma iş akışında böyle çalışır. Pattern iş akışında FL Studio'daki anlamlarını korurlar.
- 6/8'lik bir sahne üç vuruşluk bir ölçü çalar, çünkü metre dörtlük notaları sayar.
- Takip eylemleri kliplerde vardır, sahnelerde yoktur.
- Klip başlatma iş akışındayken elle açılan otomasyon şeritleri geri döndüğünüzde korunmaz.
- Pattern iş akışı seçiliyken gridden hiçbir şey yüklenmez.

## İlgili sayfalar

- [Playlist](/wiki/playlist/)
- [Ableton Live'dan geliyorsanız](/wiki/coming-from-ableton/)
- [Proje içe aktarma](/wiki/importing-projects/)
- [Yalnızca LYNX'in yaptıkları: iş akışları](/why/#workflows)

Üçüncü giriş, ilk şarkı için tek bir penceredir. Bkz. [Basit yerleşim](/wiki/simple-layout/).

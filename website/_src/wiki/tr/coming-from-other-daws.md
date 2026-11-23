---
source: c82c79e958
---
LYNX SHARE'in üç yerleşimi vardır. Logic, Cubase, Reaper, Pro Tools veya Studio One'dan geliyorsanız, **Mixer'ın elinizin altında olduğu Basit yerleşimle** başlayın: bildiğiniz gibi, zaman çizelgesinde her enstrüman veya ses için bir iz. Pattern yerleşimi, yani FL Studio'nun çalışma biçimi, pattern istediğinizde orada durur.

## İlk adımlarınız

1. **Yerleşimi seçin.** LYNX SHARE ilk açıldığında bir karşılama kartı nereden geldiğinizi sorar. `Logic, Cubase, Reaper, Pro Tools veya Studio One` düğmesine tıklayın: Basit yerleşim New track kartıyla açılır ve Mixer ilk izinizle birlikte gelir. Daha sonra `Görünüm › Yerleşim stili › Basit iş akışı` istediğiniz zaman ona geçirir ve `F9` Mixer'ı açar.
2. **Bir iz ekleyin.** `+ Parça ekle` şunları sunar: `Ses / audio`, `Enstrüman`, `Davullar`, `Sampler`, `Gitar / bas`, `İçe aktar` ve `Döngüler`. Bir enstrüman izini bilgisayar tuşlarıyla, ekran klavyesiyle veya bir MIDI klavyeyle çalın.
3. **Kaydedin.** `Seçenekler › Ses ayarları...` içinden ses aygıtınızı seçin, sonra bkz. [Sesinizi kaydedin](/learn/record-voice/).

## Nelerin aynı olduğu

- Zaman çizelgesinde ses, MIDI ve otomasyon klipleri olan izler.
- Loop take'leri ve comping ile ses ve MIDI kaydı.
- Insert'ler, send'ler, sidechain, grup'lar ve gecikme telafisi olan bir Mixer.
- Üçüncü taraf plugin'ler: her sistemde VST3 ve CLAP, macOS'ta Audio Units, Linux'ta LV2, Windows ve Linux'ta VST2.
- Şarkının, iz başına stem'lerin ve MIDI'nin dışa aktarılması.

## Nelerin farklı olduğu

- **İzler ve mixer insert'leri ayrıdır.** Basit yerleşimde her yeni iz kendi kanalını ve mixer izini alır. Pattern yerleşiminde kanalları insert'lere FL Studio'daki gibi kendiniz yönlendirirsiniz.
- **Pattern'ler.** Pattern yerleşiminde notalar, Playlist'e yerleştirdiğiniz pattern'lerin içinde durur ve bir pattern birkaç enstrüman barındırabilir.
- **Tuşlar FL Studio'yu izler.** `Ctrl+Z` bir adımı geri alır, tekrar basılırsa yineler. `Ctrl+Alt+Z` daha geriye gider.
- **Logic, Cubase, Reaper veya Pro Tools'tan proje içe aktarma yoktur.** Projeleri DAWproject dosyalarıyla, stem'lerle veya MIDI ile taşıyın (aşağıda).

## Tuşlar

| Yapmak için | Basın |
|---|---|
| Çal veya durdurup başa dön | `Space` |
| Kaydı aç veya kapat | `R` |
| Mixer | `F9` |
| Piano roll | `F7` |
| Playlist | `F5` |
| Browser | `Alt+F8` |
| Aç, kaydet | `Ctrl+O`, `Ctrl+S` |
| Geri al ve daha geriye git | `Ctrl+Z`, `Ctrl+Alt+Z` |
| Ses, MP3, MIDI dışa aktar | `Ctrl+R`, `Ctrl+Shift+R`, `Ctrl+Shift+M` |

`Yardım › Kısayollar` tüm tuşları listeler. Bkz. [Kısayollar](/wiki/shortcuts/).

## Projeleri taşıma

- **Cubase, Studio One ve Bitwig**: orada bir DAWproject dosyası (`.dawproject`) kaydedin ve `Dosya › Aç...` ile açın. İzler, klasörler, klipler, notalar, warp işaretleriyle ses, otomasyon, tempo, işaretler, send'leri olan mixer ve durumlarıyla VST3, VST2 ve CLAP plugin'ler gelir. `Dosya › Dışa aktar › DAWproject (.dawproject)...` ters yönde çalışır.
- **Logic, Reaper ve Pro Tools**: her izi şarkının başından itibaren ses veya MIDI dosyası olarak tek bir klasöre dışa aktarın. LYNX SHARE'de `+ Parça ekle` düğmesine, sonra `İçe aktar` yanındaki `Klasör...` düğmesine tıklayın: her dosya, başlangıçtan başlayan bir iz olur.
- **Geri çıkarmak**: dışa aktarma penceresi her mixer insert'i veya kanal için bir dosya yazar, `Dosya › Dışa aktar › MIDI dosyası...` ise notaları yazar.

Bkz. [Dosya biçimleri](/wiki/file-formats/) ve [Dışa aktarma](/wiki/exporting/).

## Bilmekte fayda var

- Logic, Apple Inc.'in; Cubase, Steinberg Media Technologies'in; Reaper, Cockos Incorporated'ın; Pro Tools, Avid Technology'nin; Studio One, PreSonus Audio Electronics'in; Bitwig, Bitwig GmbH'nin ticari markasıdır. LYNX SHARE bağımsız bir programdır; onlarla bağlantısı yoktur ve onlar tarafından onaylanmamıştır.
- Klip başlatma yerleşimi, zaman çizelgesinin yanına sahneli bir klip gridi ekler. Bkz. [Yerleşimler](/wiki/layouts/).

## İlgili sayfalar

- [Yerleşimler](/wiki/layouts/)
- [Mixer](/wiki/mixer/)
- [Kayıt](/wiki/recording/)
- [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/)
- [Learn: eğitimler](/learn/)

---
source: a5c1d056c8
---
LYNX SHARE penceresini açmadan da çalışır. Bir seçenek projeyi sese render eder, diğerleri onu FL Studio projesine veya Ableton Live set'ine dönüştürür, birkaçı da test içindir. Projenin kendi kontrolleri de aynı seçenekleri kullanır.

Program Windows'ta `LYNX.exe`, macOS ve Linux'ta `LYNX` adını taşır. macOS'ta tam yol `/Applications/LYNX.app/Contents/MacOS/LYNX` biçimindedir. Seçenekler her sistemde aynıdır.

## Projeyi dosyaya render etme

1. Programın bulunduğu klasörde bir terminal açın. Windows'ta bu, kurulum klasörü ya da zip'i çıkardığınız klasördür.
2. Kendi projenizle ve çıkış dosyanızla `LYNX.exe --export project.flp out.flac` komutunu çalıştırın.
3. Programın kapanmasını bekleyin. Hiçbir pencere açılmaz.
4. Çıkışın yanına yazılan `out.flac.txt` dosyasını okuyun. İçe aktarılmış bir proje için neyin dönüştürüldüğünü, yaklaşık karşılandığını ve dışarıda bırakıldığını, ayrıca hangi plugin'lerin bulunduğunu veya eksik olduğunu listeler.

## Proje açma

```
LYNX.exe project.lynx
```

LYNX SHARE'i pencereyle başlatır ve dosyayı açar. `.lynx`, `.flp`, `.als`, `.flm` ve `.mid` kabul eder. Göreli yol, geçerli klasörden alınır.

```
LYNX.exe --no-plugins project.lynx
```

Bir `.lynx` projesini, `Dosya › Üçüncü taraf plugin'ler olmadan aç...` gibi, üçüncü taraf plugin'lerini yüklemeden açar. Her plugin ayarlarını korur. Bir plugin'in açılmasını engellediği proje için kullanın.

## Günlük kullanım seçenekleri

| Seçenek | Ne yapar |
|---|---|
| `--export <project> <out>` | Tüm şarkıyı kuyruğuyla birlikte render eder; tıpkı Dışa aktarma penceresi gibi. `<out>` uzantısı formatı belirler: `.wav` (24 bit), `.flac`, `.ogg` veya `.mp3` (320 kbps) |
| `--export ... --pattern` | Yalnızca geçerli pattern'i render eder |
| `--export ... --insert N` | Yalnızca N numaralı Mixer insert'inin çıkışını render eder |
| `--export ... --rate N` | Başka bir örnekleme hızında render eder, Hz cinsinden |
| `--stems <project> <base.wav>` | Kanalı olan her Mixer insert'i için bir WAV yazar: `base - 1.wav`, `base - 2.wav` ve böyle devam eder |
| `--export-flp <project> <out.flp>` | Bir FL Studio projesi yazar. `out.zip` ile sıkıştırılmış bir loop paketi yazar |
| `--export-flp ... --substitute` | Yalnızca LYNX'te bulunan plugin'leri sese render etmek yerine en yakın FL plugin'iyle değiştirir |
| `--export-als <project> <folder>` | Live 12 için `<folder>/<name> Project/<name>.als` konumuna bir Ableton Live set'i yazar |
| `--export-als ... --live11` | Set'i Live 11 için yazar |
| `--export-bundle <project> <folder>` | `Dosya › Dışa aktar › Diğer uygulamalar için...` paketini klasöre yazar |
| `--export-bundle ... --zip` | Klasörü zipler. Yerine `<folder>.zip` gelir |
| `--export-bundle ... --no-mixdown` | Mixdown'ı paketin dışında bırakır |
| `--devices <report.txt>` | Bu bilgisayarın ses sürücüsü türlerini ve aygıtlarını listeler |
| `--showcase <out.lynx>` | LYNX SHARE'in kendi demo projesini yazar |
| `--lang <id>` | Bu çalıştırma için başka bir arayüz dili kullanır, örneğin `--lang pt-BR` |
| `--menu-entry on` veya `kapalı` | Yalnızca Linux AppImage: LYNX SHARE'i uygulamalar menüsüne ekler veya kaldırır |

`--export`, `--stems` ve `--export-flp` proje olarak bir `.lynx`, `.flp` veya `.als` alır. `--export-als` ayrıca `.flm` de alır. `--export-bundle` bir `.lynx`, `.flp` veya `.als` alır.

Örnekler:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --export-bundle song.lynx out-folder --zip
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Test amaçlı seçenekler

Bunlar LYNX SHARE'in kendisini denetlemek için vardır. Hata raporunda işe yarar.

| Seçenek | Ne yapar |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Kendi kendini testi bir ses aygıtı olmadan çalıştırır. Başarılı olursa rapor `result=OK` ile biter |
| `--snapshot <prefix> [project]` | Arayüzü ve pencerelerini, önek adını taşıyan PNG dosyalarına çizer. Bu wiki'deki resimler bu şekilde yapılır |
| `--render <project> <out.wav> <seconds>` | Geçerli pattern'in o kadar saniyesini 24 bit WAV'a render eder |
| `--plugintest <plugin> <report.txt>` | Bir VST3 veya CLAP plugin'ini yükler ve test eder |
| `--sandboxtest <plugin> <report.txt>` | Bir plugin'i, takılan bir plugin dahil, ayrı bir süreçte test eder |
| `--scantest <folder> <report.txt>` | Bir klasörü plugin'ler için ayrı süreçlerde tarar |
| `--fldb <report.txt> [project.flp]` | Burada bulunan FL Studio kurulumunu, plugin veritabanını ve bir projenin plugin eşleşmelerini raporlar |
| `--devicetest <type> <device> <report.txt>` | Bir ses sürücüsünü bir saniyelik sessizlik için açar ve raporlar |
| `--uiperf <project> <report.txt>` | Mixer, Playlist ve Channel Rack'in çizim süresini ölçer |

Örnekler:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Bilmekte fayda var

- Sonuçlar terminale değil dosyalara gider. `--export` `<out>.txt` yazar, `--export-flp` `<out.flp>.txt` yazar, `--export-als` klasöre `export-als.txt` yazar, `--export-bundle` klasöre `export-bundle.txt` yazar ve `--stems` `<base.wav>.txt` yazar.
- Program standart girdiyi okur. Bir kabuk döngüsünde her çağrıya `< /dev/null` ekleyin.
- `--snapshot`, `LYNX_SNAPSHOT_SIZE` ortam değişkeni aksini söylemedikçe (örneğin `1920x1080`) 1280x720 boyutunda çizer.
- `--lang`, `Seçenekler › Genel ayarlar...` içinde listelenen dillerin kimliklerini alır, örneğin `en`, `pt-BR`, `de`, `es`, `fr` veya `ja`. Bilinmeyen bir kimlik yok sayılır.
- `--export`, Dışa aktarma penceresiyle aynı render yolunu kullanır ama pencerenin diğer seçeneklerini kullanmaz. Dither, bit derinliği veya normalize için pencereyi kullanın.
- Kaynak kodunda çok daha fazla test seçeneği vardır. Bunlar dahilidir ve sürümler arasında değişebilir.

## İlgili sayfalar

- [Dışa aktarma](/wiki/exporting/)
- [Dosya formatları](/wiki/file-formats/)
- [Kaynaktan derleme](/wiki/build-from-source/)
- [Sorun giderme](/wiki/troubleshooting/)

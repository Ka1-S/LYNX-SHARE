---
source: 5efc5c8254
---
![Kısayolları yönet penceresi: her eylemin tuşlarıyla aranabilir bir liste](/assets/img/wiki/shortcuts.webp)

Bunlar 0.1.25 alfadaki varsayılan tuşlardır. `Yardım › Kısayollar` (`F1`) uygulamanın içinde kısayol listesini gösterir. `Seçenekler › Kısayolları yönet...` resimdeki, tuşları değiştirdiğiniz editörü açar.

> macOS'ta uygulama tuşları Mac adlarıyla gösterir: Ctrl, ⌘ olarak ve Alt, ⌥ olarak görünür.

## Kısayol değiştirme

1. `Seçenekler › Kısayolları yönet...` seçeneğini seçin.
2. Arama kutusuna bir eylem ya da `Ctrl+L` gibi bir tuş yazın. Yanındaki liste aramayı tek bir yerle sınırlar, `Yalnızca değişenler` ise değiştirdiklerinizi gösterir.
3. Eyleme tıklayın.
4. Tuşlarını değiştirmek için `Değiştir`, ikinci bir tuş vermek için `Kısayol ekle` düğmesine tıklayın, sonra yeni tuşlara basın. `Esc` iptal eder.
5. Tuşlar aynı yerde zaten kullanılıyorsa bir ileti diğer eylemin adını verir ve tuşları taşımanıza izin verir.

`Kaldır` eylemin tuşlarını temizler. `Varsayılana sıfırla` bir eylemi, `Tümünü sıfırla...` hepsini geri yükler. Değişiklikleriniz ayarlar klasöründeki `keys.json` dosyasına kaydedilir (Windows'ta `%APPDATA%\LYNX`, macOS'ta `~/Library/Application Support/LYNX`, Linux'ta `~/.config/LYNX`). `keys.json'u aç` o dosyayı açar.

## Transport ve kayıt

| Tuş | Ne yapar |
|---|---|
| `Space` | Çalar, ya da durdurup çalmanın başladığı yere döner |
| `Ctrl+Space` | Çalar veya duraklatır |
| `Shift+Space` | Çalar, ya da şarkının veya pattern'in başında durur |
| `L` | Pattern modu ile Song modu arasında geçer |
| `R` | Kaydı açar veya kapatır |
| `Shift+R` | Sesi şimdi kaydet: seçili insert'i kayda hazırlar ve kaydeder |
| `Ctrl+M` | Metronomu açar veya kapatır |
| `Ctrl+P` | Kayıttan önceki geri sayımı açar veya kapatır |
| `Ctrl+I` | Kaydın ilk notayı beklemesini açar veya kapatır |
| `Ctrl+B` | Kaydedilen notaları harmanlamayı açar veya kapatır |
| `Ctrl+E` | Adım adım girişi açar veya kapatır |
| `Ctrl+H` | Sesi durdurur (all notes off) |
| `Ctrl+T` | Klavyeden piyanoyu açar veya kapatır |
| `Alt+Numpad *` / `Alt+Numpad /` | Sonraki veya önceki song işaretçisine atlar |

Bu tuşların bazıları odaktaki bir editörde başka bir şey yapar. Piano Roll'da `Ctrl+M` bir MIDI dosyasını içe aktarır ve `Ctrl+B` çoğaltır. Playlist'te `Ctrl+P` Performans modudur.

## Dosyalar ve düzenleme

| Tuş | Ne yapar |
|---|---|
| `Ctrl+N` | Yeni proje |
| `Ctrl+O` | Proje aç |
| `Ctrl+S` | Kaydet |
| `Ctrl+Shift+S` | Farklı kaydet |
| `Ctrl+Alt+S` | Yeni sürüm kaydet |
| `Ctrl+R` | Ses dışa aktar |
| `Ctrl+Shift+R` | MP3 dosyası dışa aktar |
| `Ctrl+Shift+M` | MIDI dosyası dışa aktar |
| `Ctrl+Z` | Geri al. Yeniden basılırsa o adımı yineler |
| `Ctrl+Alt+Z` | Adım adım geriye doğru geri al |
| `Ctrl+Y` veya `Ctrl+Shift+Z` | Yinele |
| `Ctrl+Alt+H` | Geri alma geçmişi |
| `Ctrl+F` | Browser'da ara |

## Pencereler

| Tuş | Ne yapar |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Plugin seçici |
| `Shift+F8` | Plugin veritabanı (Browser'da) |
| `Enter` | Playlist'i büyütür veya eski haline getirir |
| `F10` | MIDI ayarları |
| `F11` | Proje bilgisi |
| `F12` | Tüm pencereleri kapat (plugin'ler, kanal ayarları, araçlar) |
| `Alt+F12` | Tüm plugin pencerelerini kapat |
| `Ctrl+F12` | Odaktaki dışındaki tüm pencereleri kapat |
| `Alt+F7` | Dokunmatik klavye |
| `Ctrl+F5` | Tempo tapper |
| `F1` | Kısayol listesi |

## Pattern'ler

| Tuş | Ne yapar |
|---|---|
| `Numpad +` / `Numpad -` | Sonraki, önceki pattern |
| `Numpad 1` - `Numpad 9` | 1'den 9'a pattern seç |
| `F4` | Sonraki boş pattern ve adlandır |
| `Ctrl+F4` | Sonraki boş pattern, adlandırmadan |
| `Shift+F4` | İlk boş pattern ve adlandır |
| `F2` | Pattern'i yeniden adlandır ve renklendir |
| `Ctrl+Shift+C` | Pattern'i klonla |
| `Ctrl+Shift+Ins` | Geçerli olanın önüne boş bir pattern ekle |
| `Ctrl+Shift+Del` | Pattern'i sil |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Pattern'i yukarı, aşağı taşı |

## Channel Rack

| Tuş | Ne yapar |
|---|---|
| `Up` / `Down` | Üstteki, alttaki kanalı seç |
| `Shift+Up` / `Shift+Down` | Seçimi genişlet |
| `Alt+Up` / `Alt+Down` | Seçili kanalları taşı |
| `Ctrl+A` | Tüm kanalları seç |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Seçili kanalların step'lerini kes, kopyala, yapıştır |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Step'leri sola, sağa döndür |
| `Alt+C` | Seçili kanalları klonla |
| `Alt+Del` | Seçili kanalları sil |
| `Alt+G` | Seçili kanalları grupla |
| `Ctrl+L` | Seçili kanalları boş Mixer insert'lerine gönder |
| `Ctrl+K` | Step grafik şeritlerini aç veya kapat |
| `1` - `0` | 1 ile 10 arası kanalları sustur veya seslerini aç |
| `Ctrl+1` - `Ctrl+0` | 1 ile 10 arası kanalları solo yap |

## Piano Roll

| Tuş | Ne yapar |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Kalem, Fırça, Davul fırçası, Silgi, Sessiz, Bıçak, Kement, Zoom, Dinle |
| `S` | Slide notalarını aç veya kapat (yeni notalar) |
| `G` / `K` | Önceki, sonraki kanal |
| `H` / `J` | Notası olan önceki, sonraki kanal |
| `Ctrl+A` | Tümünü seç |
| `Ctrl+D` veya `Esc` | Seçimi kaldır |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopyala, kes, yapıştır |
| `Ctrl+B` | Çoğalt |
| `Del` | Seçimi sil |
| `Up` / `Down` | Seçimi bir yarım ses taşı |
| `Ctrl+Up` / `Ctrl+Down` | Seçimi bir oktav taşı |
| `Left` / `Right` | Seçimi bir grid step'i taşı |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Notaları ölçü içinde döndür |
| `Backspace` | Snap'i kapat veya aç |
| `Ctrl+Q` | Hızlı kuantize (başlangıçlar ve uzunluklar) |
| `Shift+Q` | Başlangıçları kuantize et |
| `Ctrl+U` | Grid'e göre doğra |
| `Ctrl+G` | Bitişik notaları birleştir |
| `Ctrl+L` | Legato |
| `Shift+D` | Notaları grid'e kısalt |
| `Ctrl+P` | Seçim için porta'yı aç veya kapat |
| `Alt+M` / `Shift+Alt+M` | Seçimi sustur, sesini aç |
| `Alt+V` | Hayalet notaları aç veya kapat |
| `Shift+I` | Seçimi ters çevir |
| `Shift+G` / `Alt+G` | Seçili notaları grupla, grubu çöz |
| `Alt+C` | Seçili notaları nota rengiyle renklendir |
| `Shift+C` | Aynı renkteki notaları seç |
| `Shift+N` | Akor damgası: yok (tek notalar) |
| `Ctrl+Ins` / `Ctrl+Del` | Boşluk ekle, sil (zaman seçimi) |
| `Ctrl+M` | MIDI dosyası içe aktar |
| `Ctrl+Shift+M` | MIDI dosyası olarak dışa aktar |
| `PageUp` / `PageDown` | Yakınlaştır, uzaklaştır |
| `Shift+1` / `Shift+2` / `Shift+3` | 1, 4, 16 ölçüye zoom yap |
| `Shift+4` | Her şeyi gösterecek şekilde zoom yap |
| `Shift+5` | Seçime zoom yap |
| `Shift+0` | Görünümü çalma kafasına ortala |

### Nota araçları

Her tuş Piano Roll'da bir nota aracının iletişim kutusunu açar.

| Tuş | Ne yapar |
|---|---|
| `Alt+A` | Arpej |
| `Alt+S` | Strum |
| `Alt+Y` | Çevir |
| `Alt+R` | Rastgele |
| `Alt+X` | Seviyeleri ölçekle |
| `Alt+K` | Bir aralıkla sınırla |
| `Alt+Q` | Kuantize |
| `Alt+U` | Doğra |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Artikülasyon |
| `Alt+E` | Riff machine |
| `Alt+P` | Akor yürüyüşü üret |

## Playlist

| Tuş | Ne yapar |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Kalem, Fırça, Silgi, Sessiz, Bıçak, Kement, Zoom, Dinle, Kaydır |
| `Ctrl+A` | Tümünü seç |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Kopyala, kes, yapıştır |
| `Ctrl+B` | Çoğalt |
| `Del` | Seçili klipleri sil |
| `Shift+Del` | Seçili klipleri sil ve boşlukları kapat |
| `Shift+S` | Ripple düzenlemeyi aç veya kapat |
| `Backspace` | Snap'i kapat veya aç |
| `Alt+M` / `Shift+Alt+M` | Seçili klipleri sustur, sesini aç |
| `Shift+M` | Ses klipleri boyutlandırılırken sesi esnetmeyi aç veya kapat |
| `Ctrl+Shift+V` | Seçili klipleri benzersiz yap |
| `Ctrl+G` | Pattern kliplerini birleştir |
| `Ctrl+Shift+G` | Benzer pattern kliplerini birleştir |
| `Ctrl+Alt+G` | Otomasyon kliplerini birleştir |
| `Ctrl+Shift+B` | Seçili Playlist izlerini klonla |
| `Ctrl+Q` | Kliplerin başlangıçlarını kuantize et |
| `7` / `8` | Seçili ses kliplerinin pitch'ini bir yarım ses aşağı, yukarı al |
| `9` | Seçili ses kliplerini ters çevir |
| `Ctrl+Ins` / `Ctrl+Del` | Zaman seçiminin uzunluğunu tüm izlere ekle, tüm izlerden kaldır |
| `Ctrl+Left` / `Ctrl+Right` | Önceki, sonraki zamanı seç |
| `Ctrl+Enter` | Seçimin çevresindeki zamanı seç |
| `Alt+T` | Zaman işaretçisi ekle |
| `Ctrl+T` | Otomatik işaretçi ekle |
| `Shift+Alt+T` | Ölçü işareti değişikliği ekle |
| `Shift+T` | Döngü işaretçisi yerleştir |
| `Ctrl+Alt+C` | Playlist seçimini birleştir |
| `Ctrl+P` | Performans modunu aç veya kapat |
| `PageUp` / `PageDown` | Yakınlaştır, uzaklaştır |
| `Shift+1` / `Shift+2` / `Shift+3` | 1, 4, 16 ölçüye zoom yap |
| `Shift+4` | Her şeyi gösterecek şekilde zoom yap |
| `Shift+5` | Seçime zoom yap |
| `Shift+0` | Görünümü çalma kafasına ortala |

## Mixer

| Tuş | Ne yapar |
|---|---|
| `Alt+Left` / `Alt+Right` | Seçili insert'leri sola, sağa taşı |
| `F2` | Seçili insert'i yeniden adlandır ve renklendir |
| `S` | Seçili insert'leri solo yap |
| `Alt+S` | Seçili insert'leri, onları besleyen insert'lerle birlikte solo yap |
| `Ctrl+L` | Seçili kanalları seçili insert'e bağla |
| `Ctrl+Shift+L` | Seçili kanalları seçili olandan başlayarak insert'lere bağla |
| `Alt+L` | Seçili insert'e bağlı kanalları seç |
| `Del` | Seçili efekt yuvasındaki plugin'i sil |
| `Ctrl+E` | Ses editörünü aç |
| `Shift+E` | Ses editörünü aç ve kaydet |
| `Alt+R` | Kayda hazır insert'leri wave dosyalarına render et |
| `Shift+Alt+R` | Seçili insert'leri wave dosyalarına render et |

## Browser

| Tuş | Ne yapar |
|---|---|
| `Up` / `Down` | Önceki, sonraki dosya (önizlemeyle) |
| `Shift+Up` / `Shift+Down` | Önceki, sonraki dosyayı seçili kanala yükle |
| `Enter` | Dosyayı seçili kanala yükle |
| `Left` | Klasörü kapat ya da üst klasöre git |
| `Right` | Klasörü aç |
| `F2` | Seçili plugin'i yeniden adlandır (Plugin veritabanı) |

## Bilmekte fayda var

- Yazı klavyesi piyanosu açıkken tuşları nota çalar ve tek harfli kısayolların çoğu durdurulur (`L` yine çalışır). `Ctrl+T` ile kapatın.
- Bir kısayol ya her yerde ya da tek bir pencerede çalışır. Bir pencereye ait olan, o pencere odaktayken her yerde çalışan aynı tuşlara üstün gelir.
- Bazı eylemlerin varsayılan tuşu yoktur, örneğin Hızlı ekleme paleti. Editörde onlara bir tuş verin.

## İlgili sayfalar

- [Pencerenin turu](/wiki/tour/)
- [Ayarlar](/wiki/settings/)
- [MIDI kontrolcüleri](/wiki/midi-controllers/)
- [FL Studio'dan geliyorsanız](/wiki/coming-from-fl-studio/)

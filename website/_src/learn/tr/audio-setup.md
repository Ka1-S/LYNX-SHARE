---
source: 297095d385
---
## Sürücü ve çıkış

![Ayarlar'ın Ses sayfası](/assets/img/learn/audio-setup-1.webp)

1. `Seçenekler › Ses ayarları...` seçeneğini açın.
2. Sürücüyü seçin. Windows'ta `LYNX ASIO (dahili)` her aygıtla çalışır ve kendine ait bir sürücü gerektirmez; arayüzünüzün kendi ASIO sürücüsü, FL Studio ASIO ve `Windows Audio (paylaşımlı)` veya `Windows Audio (özel, en düşük gecikme)` da oradadır. Mac'te Core Audio, Linux'ta PipeWire veya ALSA.
3. `Çıkış aygıtı` LYNX'i duyduğunuz yerdir. `System default` bilgisayarınızın kullandığı çıkışı izler.
4. `Giriş aygıtı` kayıt yaptığınız kaynaktır (bir mikrofon, bir gitar girişi). `Örnekleme hızı` genellikle 44100 veya 48000 Hz'dir.

## Arabellek boyutu

![Arabellek boyutları ve gecikmeleri](/assets/img/learn/audio-setup-2.webp)

1. `Arabellek boyutu`: küçük bir arabellek klavye çalarken daha hızlı yanıt verir; büyük olan işlemciye daha çok pay bırakır.
2. Çıtırtı veya kopma mı var? Artırın. Çalarken notalar geç mi geliyor? Azaltın.

## Sesi test edin

![Yardım, Ses çıkışını test et](/assets/img/learn/audio-setup-3.webp)

1. `Test sesi çal` seçilen çıkıştan kısa bir ses çalar.
2. Her yerden `Yardım › Ses çıkışını test et...` aynısını yapar.

## Kaydedilen take'ler geç mi kalıyor?

1. Arayüzünüzün sesi içeri alıp yeniden dışarı vermesi biraz zaman alır, bu yüzden bir take biraz geç yerleşebilir.
2. Aynı Ses sayfasında, `Kayıt gecikme telafisi` altında bir çıkışı bir girişe kabloyla bağlayın (ya da arayüzünüzün loopback'ini kullanın) ve `Gidiş-dönüşü ölç` düğmesine tıklayın.
3. LYNX birkaç tık sesi çalar, ne kadar geç döndüklerini ölçer ve o andan itibaren kaydedilen take'leri zamanında yerleştirir.

Ayrıca bkz. [Ses kurulumu](/wiki/audio-setup/) ve [Sorun giderme](/wiki/troubleshooting/).

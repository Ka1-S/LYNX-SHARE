---
source: 297095d385
---
## Sterownik i wyjście

![Strona Audio w Ustawieniach](/assets/img/learn/audio-setup-1.webp)

1. Otwórz `Opcje › Ustawienia audio...`.
2. Wybierz sterownik. W Windows `LYNX ASIO (wbudowany)` działa z każdym urządzeniem i nie potrzebuje własnego sterownika; są tam też własny sterownik ASIO twojego interfejsu, FL Studio ASIO oraz `Windows Audio (współdzielony)` albo `Windows Audio (wyłączny, najniższa latencja)`. Core Audio na Macu, PipeWire lub ALSA w Linuksie.
3. `Urządzenie wyjściowe` to miejsce, gdzie słyszysz LYNX. `System default` podąża za wyjściem, którego używa komputer.
4. `Urządzenie wejściowe` to źródło nagrania (mikrofon, wejście gitarowe). `Próbkowanie` to zwykle 44100 lub 48000 Hz.

## Rozmiar bufora

![Rozmiary bufora wraz z opóźnieniem](/assets/img/learn/audio-setup-2.webp)

1. `Rozmiar bufora`: mniejszy bufor reaguje szybciej, gdy grasz na klawiaturze; większy daje procesorowi więcej zapasu.
2. Trzaski lub zrywanie dźwięku? Zwiększ go. Nuty spóźniają się, gdy grasz? Zmniejsz go.

## Przetestuj dźwięk

![Pomoc, Testuj wyjście audio](/assets/img/learn/audio-setup-3.webp)

1. `Odtwórz dźwięk testowy` odtwarza krótki dźwięk przez wybrane wyjście.
2. Z każdego miejsca `Pomoc › Testuj wyjście audio...` robi to samo.

## Nagrane podejścia się spóźniają?

1. Interfejs potrzebuje chwili, żeby przyjąć dźwięk i znów go wypuścić, więc podejście może wylądować trochę za późno.
2. Na tej samej stronie Audio, w sekcji `Kompensacja latencji nagrywania`, połącz kablem wyjście z wejściem (albo użyj loopbacku interfejsu) i kliknij `Zmierz opóźnienie w obie strony`.
3. LYNX odtwarza kilka kliknięć, mierzy, o ile później wracają, i od tej pory ustawia nagrane podejścia w czasie.

Zobacz też [Konfiguracja audio](/wiki/audio-setup/) i [Rozwiązywanie problemów](/wiki/troubleshooting/).

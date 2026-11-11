---
source: 3801d5daff
---
![Strona Audio w oknie Ustawienia, z jeszcze niewybranym urządzeniem wyjściowym](/assets/img/wiki/settings-audio.webp)

Strona Audio w oknie Ustawienia określa, dokąd idzie dźwięk i skąd pochodzą nagrania. Otworzysz ją przez `Opcje › Ustawienia audio...`. Kreator konfiguracji (`Opcje › Kreator konfiguracji (język, audio, foldery pluginów)...`) też testuje wyjście audio.

## Uruchom dźwięk

1. Otwórz `Opcje › Ustawienia audio...`.
2. Wybierz `Sterownik`. Notatka pod polem mówi, do czego ten sterownik się nadaje.
3. Wybierz `Urządzenie wyjściowe`.
4. Kliknij `Odtwórz dźwięk testowy`.
5. Zajrzyj do sekcji `Stan`. Pokazuje działający sterownik, częstotliwość próbkowania i latencję wyjścia albo komunikat `Żadne urządzenie audio nie jest otwarte: wybierz urządzenie wyjściowe.`

## Sterowniki

Pole `Sterownik` zawiera typy sterowników twojego systemu.

| System | Sterownik | Co robi |
|---|---|---|
| Windows | Windows Audio (współdzielony) | Inne programy zachowują dźwięk. Windows miksuje wszystkie programy w stałych odstępach, więc bufor nie może być mniejszy niż ten odstęp |
| Windows | Windows Audio (współdzielony, krótki okres) | Też współdzielony, ale prosi Windows o krótszy okres miksowania. Obsługują go tylko niektóre sterowniki |
| Windows | Windows Audio (wyłączny, najniższa latencja) | LYNX SHARE używa urządzenia sam. Inne programy milkną, gdy gra |
| Windows | DirectSound (przestarzały) | Stara ścieżka audio Windows z dużą latencją. Na wypadek, gdy inne zawodzą |
| Windows | ASIO | Sterowniki o niskiej latencji. Sterownik ASIO producenta działa z jego własnym urządzeniem |
| Windows | LYNX ASIO (wbudowany) | Własny sterownik ASIO LYNX SHARE, dla każdego urządzenia, do odtwarzania i nagrywania. `Ustawienia sterownika...` wybiera tryb współdzielony lub wyłączny oraz urządzenia |
| macOS | Core Audio | Systemowe audio, współdzielone z innymi programami |
| Linux | PipeWire (natywny) | LYNX SHARE rozmawia bezpośrednio z PipeWire. `System default` podąża za wyjściem wybranym w systemowych ustawieniach dźwięku |
| Linux | JACK / PipeWire | Dla działającego serwera JACK albo PipeWire z `pipewire-jack` |
| Linux | ALSA | Rozmawia z kartą dźwiękową. `default` lub `pipewire` współdzielą urządzenie, urządzenie `hw:` oddaje kartę wyłącznie LYNX SHARE |

W Linuksie przy pierwszym uruchomieniu otwiera się pierwszy z PipeWire, JACK i ALSA, który ma urządzenia. Zapisany wybór zostaje.

## Ustawienia na stronie

| Ustawienie | Co robi |
|---|---|
| `Sterownik` | Typ sterownika, z tabeli powyżej |
| `Urządzenie wyjściowe` | Gdzie gra Master |
| `Urządzenie wejściowe` | Źródło nagrywania audio i wejść insertów Mixera. Domyślnie `(brak)` |
| `Próbkowanie` | Częstotliwości, które oferuje urządzenie |
| `Rozmiar bufora` | Rozmiary, które oferuje urządzenie, każdy z latencją w milisekundach. Mniejsze bufory szybciej reagują, gdy grasz. Większe dają procesorowi więcej luzu i mniej trzasków |
| `Ustawienia sterownika...` | Otwiera własne okno sterownika (sterowniki ASIO) |
| `Odtwórz dźwięk testowy` | Odtwarza dźwięk testowy przez urządzenie wyjściowe |
| `Nagrywaj audio z wejścia` | Nagrywa pierwsze dwa kanały wejścia do nowego insertu, gdy żaden insert nie jest uzbrojony |
| `Format pliku` | Nagrania to WAV 24-bit lub WAV 32-bit float, z częstotliwością próbkowania urządzenia |
| `Zmierz opóźnienie w obie strony` | Odtwarza 8 kliknięć z `Wyjście ping` do `Wejście ping` przez kabel lub pętlę zwrotną i mierzy opóźnienie, żeby ujęcia lądowały tam, gdzie je zagrałeś |
| `Precyzyjna korekta (próbki)` | Przesuwa każde nowe ujęcie. Wartość dodatnia oznacza wcześniej na Playliście |
| `Ścieżka metronomu` | Ścieżka Mixera, przez którą gra metronom. `---` to Master. Klik nigdy nie trafia do eksportu |
| `Odtwarzaj obcięte nuty przy transporcie` | Odtwarza nuty, które przecinają pozycję startu |
| `Resetuj wtyczki przy transporcie` | Czyści ogony efektów, gdy odtwarzanie rusza albo skacze |

## Gdy nie ma dźwięku

- Najpierw spójrz na `Stan`. Jeśli żadne urządzenie nie jest otwarte, wybierz urządzenie wyjściowe.
- Spróbuj innego sterownika. W Windows `Windows Audio (współdzielony)` pozwala innym programom zachować dźwięk, a `DirectSound (przestarzały)` jest na wypadek, gdy inne zawodzą.
- Sprawdź suwak Master w [Mixerze](/wiki/mixer/) i pokrętło `MASTER` na pasku narzędzi.
- W trybie Pattern gra tylko bieżący pattern. Jeśli jest pusty, nic nie słychać. Naciśnij `L`, żeby przejść do trybu Song.
- W Linuksie, jeśli dźwięk się rwie, `Stan` pokazuje, czy wątek audio dostał priorytet czasu rzeczywistego. Rozwiązanie jest w [Rozwiązywaniu problemów](/wiki/troubleshooting/).

## Warto wiedzieć

- ASIO istnieje tylko w Windows.
- Przy `Windows Audio (współdzielony)` najmniejszy bufor to systemowy odstęp miksowania. Notatka pod polem `Sterownik` podaje wartość dla twojego urządzenia. Dla niższej latencji użyj trybu wyłącznego albo sterownika ASIO.
- Z PipeWire bufor sięga od 32 do 2048 próbek, a częstotliwość próbkowania od 44,1 do 192 kHz. Jeśli PipeWire zachowa własną częstotliwość, przesampluje strumień LYNX SHARE.
- Z PipeWire, gdy nazwane urządzenie zostanie odłączone, LYNX SHARE wraca do `System default` i zachowuje rozmiar bufora.
- `Stan` pokazuje też obciążenie procesora przez audio i liczy zerwania dźwięku (niedobory bufora).
- Sterowniki i urządzenia możesz wylistować bez otwierania okna. Zobacz `--devices` w [Wierszu poleceń](/wiki/command-line/).

## Powiązane

- [Ustawienia](/wiki/settings/)
- [Nagrywanie](/wiki/recording/)
- [Kontrolery MIDI](/wiki/midi-controllers/)
- [Rozwiązywanie problemów](/wiki/troubleshooting/)

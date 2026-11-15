---
source: e5206a071d
---
Każdy nagłówek poniżej to jeden problem. Jeśli Twojego tu nie ma, zgłoś go tak, jak opisano na końcu.

## Uruchamianie programu

### Windows: SmartScreen ostrzega przy pierwszym uruchomieniu

Plik wykonywalny nie jest jeszcze podpisany, więc SmartScreen może ostrzec przy pierwszym uruchomieniu.

1. Kliknij `More info`.
2. Kliknij `Run anyway`.

Instalator nie wymaga praw administratora.

### macOS: „nie można otworzyć"

Aplikacja nie jest notaryzowana przez Apple i nie ma podpisu Developer ID, więc pierwsze uruchomienie wymaga jednego dodatkowego kroku.

1. Przeciągnij LYNX SHARE do `Applications`.
2. Kliknij go prawym przyciskiem (Control-klik) i wybierz `Otwórz`.
3. Kliknij `Otwórz` jeszcze raz.

W macOS 15 Sequoia lub nowszym spróbuj raz ją otworzyć, potem przejdź do `System Settings › Privacy & Security` i kliknij `Open Anyway`. Albo w Terminalu:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS prosi o dostęp do mikrofonu przy pierwszym nagrywaniu.

### Linux: AppImage się nie uruchamia

LYNX SHARE wymaga systemu x86_64 z glibc 2.35 lub nowszym (Ubuntu 22.04, Debian 12, Fedora 36, aktualny Arch lub Manjaro) oraz X11 lub XWayland.

1. Nadaj plikowi prawo wykonywania i uruchom go dwoma poleceniami poniżej.
2. Jeśli nadal się nie uruchamia, zainstaluj libfuse2. Pakiet nazywa się `libfuse2` w Ubuntu 22.04, `libfuse2t64` w Ubuntu 24.04, `fuse2` w Archu i `fuse-libs` w Fedorze.
3. Jeśli wolisz go nie instalować, pobierz zamiast tego `.tar.gz`, rozpakuj go i uruchom `./install.sh`.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` instaluje LYNX SHARE dla Twojego użytkownika w `~/.local` z wpisem w menu. `./install.sh --uninstall` go usuwa.

## Dźwięk

### Brak dźwięku albo złe urządzenie audio

1. Otwórz `Opcje › Ustawienia audio...`.
2. Wybierz sterownik.
3. Wybierz urządzenie wyjściowe.

| System | Sterowniki |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO i wbudowany sterownik LYNX ASIO |
| macOS | Core Audio |
| Linux | PipeWire (natywny), JACK i ALSA. Przy pierwszym starcie LYNX SHARE otwiera pierwszy z nich, który ma urządzenia |

ASIO to model sterowników Windows i istnieje tylko w Windows. `Opcje › Kreator konfiguracji (język, audio, foldery pluginów)...` także testuje urządzenie audio. Zobacz [Ustawienia audio](/wiki/audio-setup/).

### Linux: dźwięk się rwie

Wątek audio powinien działać z priorytetem czasu rzeczywistego.

1. Otwórz `Opcje › Ustawienia audio...` i przeczytaj `Stan`. Pokazuje, czy wątek dostał priorytet czasu rzeczywistego.
2. Jeśli nie, zainstaluj rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` lub `sudo pacman -S rtkit`) albo dodaj siebie do grupy `audio` z limitem czasu rzeczywistego, jak w konfiguracjach JACK.
3. Wyloguj się i zaloguj ponownie.

Szczegóły są w [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

### Dźwięk trzeszczy lub się urywa

LYNX SHARE zlicza każdy zanik (buffer underrun) i mówi, dlaczego do niego doszło. Wskaż wskaźnik `CPU` na pasku narzędzi albo otwórz `Narzędzia › Monitor wydajności pluginów...`. Czerwona liczba obok procentu CPU to licznik.

Podpowiedź podaje przyczynę ostatniego zaniku:

| Przyczyna | Co to znaczy | Co spróbować |
|---|---|---|
| Przetwarzanie trwało dłużej niż bufor | Blok audio liczył się dłużej, niż trwa. Podpowiedź podaje najwolniejszy kanał lub efekt w tym bloku | Zwiększ rozmiar bufora w `Opcje › Ustawienia audio...`, zamroź ten kanał albo obniż jakość pluginu |
| Przetwarzanie zbliżyło się do granicy bufora | Obciążenie było bliskie 100 % w okolicy zaniku | Zwiększ rozmiar bufora albo zamroź najcięższe kanały |
| Opóźnienie sterownika lub systemu | Sterownik lub system się spóźnił, choć obciążenie było niskie. Żaden plugin tego nie spowodował | Zwiększ rozmiar bufora, spróbuj innego sterownika (w Windows sterownika ASIO interfejsu), a w Linuksie sprawdź priorytet czasu rzeczywistego (wyżej) |

Każdy zanik jest też zapisywany w `audio-glitches.log` w folderze ustawień, razem z urządzeniem, buforem i najcięższymi kanałami.

## Pluginy

### Plugin się wywala

Większość awarii pluginów wyłącza tylko ten plugin: efekt jest pomijany (bypass), instrument milknie, a LYNX SHARE i projekt działają dalej. Komunikat `Plugin uległ awarii` proponuje wtedy:

- `Odśwież`, co uruchamia plugin od nowa z ustawieniami z ostatniego zapisu lub kopii zapasowej.
- `Zawsze uruchamiaj <plugin> w osobnym procesie`, co uruchamia go od nowa w jego własnym procesie, teraz i w każdym projekcie. Awaria tam nie może zatrzymać LYNX SHARE. Plugin w osobnym procesie otwiera swoje okno samodzielnie (nie można go zadokować).

Okno pluginu pokazuje te same dwa przyciski zamiast swojego edytora. `Opcje › Menedżer pluginów...` wymienia pluginy, które zawsze działają w osobnym procesie, w kolumnie `Osobny proces`; kliknij pole, aby włączyć lub wyłączyć jeden z nich.

Aby uruchamiać każdy plugin w jego własnym procesie:

1. Otwórz `Opcje › Ustawienia ogólne...`.
2. Włącz `Uruchamiaj pluginy w osobnym procesie (chroni LYNX SHARE przed awariami)`.
3. Otwórz projekt ponownie.

Plugin, który się wywalił albo przestał odpowiadać we własnym procesie, zostaje wyciszony. Zapisz projekt i otwórz go ponownie, aby wczytać plugin jeszcze raz. Zobacz [Pluginy zewnętrzne](/wiki/plugins/).

### Pluginu nie ma na liście

1. Wybierz `Opcje › Skanuj pluginy`.
2. Jeśli nadal go brakuje, otwórz `Opcje › Menedżer pluginów...`, kliknij `Foldery pluginów...` i dodaj folder pluginu.
3. Spójrz na kartę `Nieudane`. Plik pluginu, który nie przeszedł skanowania, jest pomijany. W Windows przyczyna jest w `%APPDATA%\LYNX\scan-failures.log`.
4. Kliknij `Skanuj od nowa...`, aby spróbować ponownie z plikami, które zawiodły.

### Projekt używa pluginu, który nie jest zainstalowany

Efekt zostaje zastąpiony efektem LYNX SHARE o tej samej roli, a jego ustawienia pozostają w projekcie. `Pomoc › Brakujące pliki...` wymienia, czego brakuje. Zobacz [Wbudowane instrumenty i efekty](/wiki/built-in/).

### Wideo nie działa w Linuksie

Ścieżki wideo i `Export with video` wymagają FFmpeg. LYNX SHARE podpowiada, jak go zainstalować.

## Raporty o awariach

![Pytanie o raport awarii pokazywane przy następnym starcie, z opcjami Wyślij, Pokaż, co zostanie wysłane, i Nie wysyłaj](/assets/img/wiki/crash-report.webp)

Raporty o awariach są dobrowolne. Po awarii albo zawieszeniu program przy następnym starcie pyta, czy wysłać raport.

| Polecenie | Co robi |
|---|---|
| `Pokaż, co zostanie wysłane` | Pokazuje dokładny tekst |
| `Zgłoś zamiast tego na GitHubie` | Otwiera formularz zgłoszenia na GitHubie, wstępnie wypełniony. Pliki dołączasz sam |
| `Otwórz folder z raportem` | Pokazuje pliki raportu na Twoim komputerze |
| `Include file names (projects, samples)` | Wyłączone: wysyłane jest tylko rozszerzenie każdego pliku |
| `Zawsze wysyłaj bez pytania` | Wysyła przyszłe raporty bez pytania |

`Raporty o awariach` w `Opcje › Ustawienia ogólne...` ustawia `Ask` (domyślnie), `Zawsze wysyłaj` lub `Nigdy nie wysyłaj`.

Raport zawiera wersję LYNX SHARE, system operacyjny, procesor, karty graficzne i ich sterowniki, rozmiar ekranu, sterownik i urządzenie audio, plik awarii lub zawieszenia, ostatnie 60 kroków sesji oraz nazwy i wersje wczytanych pluginów innych firm.

Nie zawiera audio, projektów, sampli, nazwy Twojego komputera ani nazwy użytkownika. Serwer nie przechowuje adresu IP. Wysłane identyfikatory są wymienione w `crash-reports\sent.log` w folderze ustawień. Aby raport został usunięty, otwórz zgłoszenie z jego identyfikatorem. Pełny tekst jest w [polityce prywatności](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Zgłoś problem

![Okno Zgłoś problem: pole opisu, opcjonalny adres e-mail i przycisk Wyślij](/assets/img/wiki/report-problem.webp)

1. Wybierz `Pomoc › Zgłoś problem...`.
2. Opisz, co się stało. Adres e-mail jest opcjonalny, na wypadek gdybyś chciał dostać odpowiedź.
3. Zostaw zaznaczone `Dołącz najnowsze pliki awarii / zawieszeń i kroki tej sesji`, jeśli problemem była awaria.
4. Kliknij `Pokaż, co zostanie wysłane`, aby sprawdzić tekst.
5. Kliknij `Wyślij`. Wcześniej nic nie jest wysyłane.

Możesz też otworzyć zgłoszenie na [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## Warto wiedzieć

- Po awarii kopie zapasowe Twojego projektu leżą w folderze autozapisu LYNX SHARE.
- Gdy w awarii brał udział jakiś plugin, pytanie proponuje uruchamianie tego pluginu odtąd w osobnym procesie.
- Folder ustawień to `%APPDATA%\LYNX` w Windows, `~/Library/Application Support/LYNX` w macOS i `~/.config/LYNX` w Linuksie.

## Powiązane

- [Ustawienia audio](/wiki/audio-setup/)
- [Pluginy zewnętrzne](/wiki/plugins/)
- [Instalacja](/wiki/install/)
- [FAQ](/faq/)
- [Ochrona przed awariami](/why/#crash)

---
source: b05790879f
---
![A janela de exportação com WAV selecionado: intervalo, cauda e arquivos à esquerda, formato à direita, opções de mixagem embaixo](/assets/img/wiki/export.webp)

Tudo o que sai do LYNX SHARE passa por `Arquivo › Exportar`. Os formatos de áudio (WAV, MP3, OGG, FLAC, AIFF e M4A) compartilham uma só janela de exportação. MIDI, projetos do FL Studio, sets do Ableton Live, arquivos DAWproject e projetos zipados têm entradas próprias no mesmo menu.

## Exportar a música como WAV

1. Escolha `Arquivo › Exportar › Arquivo Wave...`.
2. Deixe `Extensão` em `Song (Playlist)` e `Arquivos` em `Um arquivo (a mixagem)`.
3. Escolha um `Resolução`: 16 bits, 24 bits ou 32 bits (float).
4. Defina `Cauda` como `Deixar o restante`, para que reverbs e releases soem até o fim depois do último compasso.
5. Clique em `Exportar...`, escolha um nome e uma pasta e espere o render.
6. Ao terminar, o LYNX SHARE informa onde o arquivo foi parar e o nível de pico dele.

`Ctrl+R` abre a janela com o último formato que você exportou. `Ctrl+Shift+R` a abre em MP3.

## O que renderizar

| Opção | O que faz |
|---|---|
| `Extensão` | `Song (Playlist)`, `Pattern atual` ou `Seleção da Playlist` |
| `Tocar` | Quantas vezes o intervalo toca no arquivo, de `Uma vez` a 16 vezes |
| `Cauda` | `Cortar o restante` para exatamente no fim. `Deixar o restante` deixa o som soar até acabar. `Enrolar o restante` mistura a cauda no início, para loops |
| `Cauda máxima` | 3, 6, 12 ou 30 segundos. A cauda termina antes quando o som já sumiu |
| `Arquivos` | Um arquivo, ou stems (veja abaixo) |

## Formato de saída

Os botões escolhem o formato. O M4A só aparece onde o sistema tem um codificador AAC: no Windows e no macOS, não no Linux. `Salvar também` grava outros formatos a partir do mesmo render, com o mesmo nome.

| Formato | Opções |
|---|---|
| WAV | `Resolução`: 16 bits, 24 bits ou 32 bits (float). `Dither`: `Desligado`, `TPDF` ou `TPDF + noise shaping` |
| MP3 | `Taxa de bits`, constante |
| OGG | Taxa de bits |
| FLAC | `Resolução`, `Compressão` de `0 (mais rápida)` a `8 (menor arquivo)`, `Dither`. Sem perdas em todos os níveis |
| AIFF | `Resolução`: 16 bits ou 24 bits. `Dither` |
| M4A | `Taxa de bits`: 96, 128, 160 ou 192 kbps. AAC, gravado pelo codificador do sistema |

`Taxa` e `Canais` valem para todos os formatos. `Canais` é `Estéreo`, `Mono (os dois lados misturados)`, `Mono (só o esquerdo)` ou `Mono (só o direito)`.

## Opções de mixagem

| Opção | O que faz |
|---|---|
| `Normalizar (pico em 0 dB)` | Sobe ou desce o arquivo todo para que o pico mais alto chegue ao fundo de escala |
| `Efeitos do Master` | Desligado: os efeitos do insert Master ficam em bypass |
| `Efeitos dos inserts` | Desligado: os efeitos dos outros inserts ficam em bypass |
| `HQ para todos os plugins` | Os plugins usam a qualidade de render. Desligado: a exportação usa a qualidade da reprodução |
| `Aquecimento dos plugins` | Os plugins de terceiros processam um instante de silêncio antes de a exportação começar |
| `Desativar a polifonia máxima` | Tira o limite de vozes dos canais, para que nenhuma nota seja cortada |
| `Cortar o silêncio da compensação de atraso` | Remove o silêncio que a compensação de latência dos plugins acrescenta no início |
| `Informação de tempo`, `Marcadores de loop`, `Marcadores da playlist` | Só WAV: guardam no arquivo o tempo, o loop e os marcadores da Playlist |
| `Mostrar o(s) arquivo(s) ao terminar` | Abre o gerenciador de arquivos no resultado |

## Stems e faixas

![A janela de exportação em FLAC com um arquivo por insert do mixer, nove arquivos no total](/assets/img/wiki/export-flac-stems.webp)

`Arquivos` tem três modos de stems:

- `Um por insert do Mixer (stems)`: um arquivo para cada insert que recebe som.
- `Um por canal (stems)`: um arquivo para cada canal que toca.
- `Um por canal, só instrumentos (sem envios nem buses)`.

Marque `Também a mixagem completa` para ter a mixagem ao lado dos stems. A linha de baixo mostra quantos arquivos serão gravados.

Quando a música tem seções na faixa Sections, `Um arquivo por seção` a corta nas seções e dá a cada arquivo o nome da sua seção. Com stems, cada seção recebe o seu próprio conjunto.

`Arquivo › Exportar › Todas as trilhas da Playlist` e `Todos os grupos de trilhas da Playlist` gravam um arquivo por faixa da Playlist ou por grupo. No menu do Mixer, `Gravação em disco › Renderizar as faixas armadas em arquivo(s) WAV...` (`Alt+R`) abre a janela de exportação só para os inserts armados.

## Outras entradas do menu Exportar

| Comando | Tecla | O que faz |
|---|---|---|
| `Arquivo MIDI...` | `Ctrl+Shift+M` | Grava as notas como um arquivo MIDI padrão (`.mid`) |
| `Masterizar...` | `Ctrl+Alt+M` | Renderiza o projeto e masteriza até um alvo de loudness em LUFS, no seu computador |
| `Pacote de loops zipado...` | nenhuma | O projeto e todos os samples que ele usa em um só `.zip`, como o pacote zipado de loop do FL Studio |
| `Arquivos de dados do projeto...` | nenhuma | Copia todos os arquivos que o projeto usa para uma pasta que você escolhe; o projeto continua usando os próprios arquivos |
| `Projeto do FL Studio...` | nenhuma | Um `.flp`, ou um pacote zipado de loop com todos os samples |
| `Set do Ableton Live (.als)...` | nenhuma | Um set para o Live 12 ou Live 11 |
| `DAWproject (.dawproject)...` | nenhuma | Um projeto para o Bitwig Studio, o Studio One, o Cubase e outros programas que leem DAWproject |
| `Para outros apps...` | nenhuma | Uma pasta com stems, arquivos MIDI, um set do Live, um projeto do FL Studio, a mixagem final e um README, zipada se você quiser |
| `Ossos do projeto...` | nenhuma | Presets e pontuações de canais e efeitos, em uma pasta |

## Exportar para o FL Studio, o Ableton Live e o DAWproject

O `.flp` é um projeto do FL Studio 2026. Samplers, clipes de áudio, geradores e efeitos próprios do FL Studio, plugins de terceiros, patterns, a Playlist, o Mixer e a automação passam com as suas configurações. Para o que o FL Studio não tem, você escolhe entre `Renderizar em áudio (soa igual)` e `Usar o plugin do FL mais próximo (continua editável, pode soar diferente)`. Com a segunda escolha, os instrumentos só do LYNX ficam de fora. Os moduladores que seguem a posição da música viram clipes de automação: um LFO sincronizado vira o LFO de clipe do FL Studio, e passos, fontes aleatórias e macros viram pontos. Seguidores, envelopes, keytrack e velocity ficam de fora. Um relatório lista o que foi renderizado, substituído ou deixado de fora. Os detalhes estão em [Vindo do FL Studio](/wiki/coming-from-fl-studio/).

O `.als` contém uma faixa MIDI por canal, faixas de áudio, grupos, retornos, sends, racks e Drum Racks, plugins VST2 e VST3 com o estado deles, marcadores e automação de volume, pan e tempo. Os instrumentos e efeitos próprios do LYNX SHARE não são convertidos, mas as faixas e notas deles são mantidas. O Helix é a exceção: ele vira o Wavetable do Live. A automação de parâmetros de efeito e as rotas de insert para insert são descartadas. Os detalhes estão em [Vindo do Ableton Live](/wiki/coming-from-ableton/).

O `.dawproject` contém faixas, pastas, clipes, notas, áudio com seus marcadores de warp, automação, andamento, marcadores, o mixer com sends e plugins VST3, VST2 e CLAP com o estado deles. Os instrumentos e efeitos próprios do LYNX SHARE são gravados de um jeito que o LYNX SHARE consiga abri-los de novo. A grade de clipes, outros arranjos e alguns detalhes por nota ficam de fora, e um relatório lista o que ficou de fora.

`Arquivo › Salvar uma cópia como projeto do FL Studio Mobile (.flm)...` grava um `.flm` para o FL Studio Mobile e copia os samples para uma pasta `<name> Samples` ao lado. Antes de salvar, uma janela lista o que a cópia perde, como em `Arquivo › Salvar uma cópia para um LYNX mais antigo...`. O `.flm` mantém as notas e os patterns (o FL Studio Mobile tem um pattern por faixa), o andamento, os tempos por compasso, a tonalidade e o conjunto de acordes, os níveis, o pan, o mute e o solo do mixer e os envios para barramentos, os instrumentos próprios do FL Studio Mobile (MiniSynth, SuperSaw, GMSynth e Transistor Bass, e o MiniSynth e o Transistor Bass do FL Studio), os samplers de bateria como um drum kit, as Layers como um rack com vários instrumentos, cada efeito Pocket (FLM) com seu mix e os clipes de áudio com nível, pan, afinação e reverso. Ele também grava o DW Sampler (com suas zonas) e o 3x Osc, e clipes de automação do nível e do pan do mixer, dos efeitos Pocket (FLM) e dos valores próprios do DW Sampler e do GMSynth. Os outros instrumentos (por enquanto também o Slicer e o SoundFont Player do FL Studio Mobile) mantêm suas notas no MiniSynth do FL Studio Mobile, e os efeitos sem módulo FLM e a automação de outros destinos, como os knobs dos próprios synths e plugins de terceiros, ficam de fora; tudo é listado.

## Bom saber

- `Dither` fica esmaecido no MP3, no OGG, no M4A e no WAV de 32 bits, e as opções de tempo e marcadores ficam esmaecidas em todos os formatos menos o WAV.
- Com `Seleção da Playlist` e nenhuma seleção na Playlist, a música inteira é renderizada. A janela avisa.
- Se o insert Master estiver mudo, a janela avisa que o arquivo ficará silencioso.
- `Gravar também um vídeo com esta mixagem` só aparece quando o projeto tem uma faixa de vídeo, e precisa do FFmpeg.
- `Renderizar pelo hardware em tempo real` só aparece quando o projeto tem um insert de hardware ativo. Essa exportação leva o tempo da música.
- A renderização em áudio, em que um insert é convertido em clipe de áudio, toca cerca de 5 dB mais alto que o insert que ele substitui nesta versão.

## Veja também

- [Formatos de arquivo](/wiki/file-formats/)
- [Linha de comando](/wiki/command-line/)
- [Vindo do FL Studio](/wiki/coming-from-fl-studio/)
- [Vindo do Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)

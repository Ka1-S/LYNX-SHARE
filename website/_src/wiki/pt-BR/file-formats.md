---
source: a79e43f031
---
O LYNX SHARE salva projetos como arquivos `.lynx`. Ele também abre projetos de outros programas, carrega os arquivos de áudio comuns e exporta áudio, MIDI e projetos para outros programas. A tabela lista todos os formatos da 0.1.26 alpha.

Um projeto aberto a partir do formato de outro programa é uma importação. `Arquivo › Salvar` então pergunta onde gravar um arquivo `.lynx`, e o arquivo original nunca é alterado.

## Abrir um projeto de outro programa

1. Escolha `Arquivo › Abrir...` (`Ctrl+O`).
2. Selecione um arquivo `.flp`, `.als`, `.flm`, `.mid` ou `.zip`.
3. Para um `.flp`, escolha `Ajuda › Last FL Studio import report` para ler o que foi aproximado ou deixado de fora.
4. Escolha `Arquivo › Salvar` e indique onde gravar o arquivo `.lynx`.

## Formatos

| Formato | Abrir | Salvar / Exportar | Notas |
|---|---|---|---|
| `.lynx` | `Arquivo › Abrir...` | `Arquivo › Salvar`, com salvamento automático | O formato de projeto próprio do LYNX SHARE (JSON) |
| `.zip` | `Arquivo › Abrir...` extrai para uma pasta e abre o projeto que está dentro | `Arquivo › Exportar › Projeto com samples (.zip)...` | Também abre o pacote zipado de loop do FL Studio |
| `.lynxpack` | Abre depois de um resumo. Uma parte é mesclada ao projeto de quem criou a sessão | `Arquivo › Sessão online › Enviar projeto...` | Um projeto ou uma parte enviada em uma [sessão online](/wiki/online-sessions/). As partes com plugins de terceiros viajam como renders de áudio mais as configurações do plugin |
| `.flp` | `Arquivo › Abrir...` | `Arquivo › Exportar › Projeto do FL Studio...`, um projeto do FL Studio 2026 ou um pacote zipado de loop | Os plugins nativos tocam em motores recriados, com exatidão variável. Os pacotes FLEX nunca são abertos. Na exportação, instrumentos e efeitos só do LYNX são renderizados em áudio ou trocados pelo plugin do FL mais próximo, e os moduladores ficam de fora. Veja [Vindo do FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Arquivo › Abrir...` | Não | Projeto do FL Studio Mobile. Parcial: instrumentos que não sejam MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, o Slicer e DirectWave tocam no synth embutido |
| `.als` | `Arquivo › Abrir...`, sets do Live 11 e 12 | `Arquivo › Exportar › Set do Ableton Live (.als)...`, para o Live 12 ou Live 11 | Na importação, os devices do Live sem equivalente no LYNX SHARE são descartados. Na exportação, os instrumentos e efeitos próprios do LYNX SHARE não são convertidos, e a automação de parâmetros de efeito e as rotas de insert para insert são descartadas. Veja [Vindo do Ableton Live](/wiki/coming-from-ableton/) |
| `.mid`, `.midi` | `Arquivo › Abrir...` cria um projeto novo. `Arquivo › Importar › Arquivo MIDI...` e o Piano Roll importam para o projeto atual | `Arquivo › Exportar › Arquivo MIDI...` (`Ctrl+Shift+M`) | |
| `.wav` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo Wave...`, 16 bits, 24 bits ou 32 bits float | Dither, informação de tempo, marcadores de loop e marcadores da Playlist opcionais |
| `.mp3` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo MP3...` | Taxa de bits constante |
| `.ogg` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo OGG...` | |
| `.flac` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo FLAC...`, compressão de 0 a 8 | Sem perdas em todos os níveis |
| `.aif`, `.aiff` | Como sample ou clipe de áudio | Não | |
| `.wv` | Como sample | Não | Samples WavPack, como vêm nos pacotes do FL Studio |
| `.sf2`, `.sf3` | Tocado pelo LYNX Soundfont | Não | SoundFonts |
| `.fst` | Pelo Browser | Não | Preset de um plugin nativo do FL Studio. Só para os plugins que o LYNX SHARE tem motor |
| `.fsc` | Piano Roll e Editor de eventos | Piano Roll e Editor de eventos | Arquivo de partitura e automação do FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Carregar arquivo de preset...` no menu de presets de um plugin hospedado | `Salvar preset...` e `Exportar arquivo de preset...` | Presets de plugins de terceiros. `.fxp` e `.fxb` são para plugins VST2 e `.vstpreset` para plugins VST3, e só enquanto o plugin roda dentro do LYNX SHARE |
| Arquivo de vídeo | `Arquivo › Importar › Arquivo de vídeo...` põe um vídeo na Playlist | `Gravar também um vídeo com esta mixagem` na janela de exportação | A importação usa os decodificadores do sistema no Windows e no macOS, e precisa do FFmpeg no Linux. A exportação precisa do FFmpeg |

## Outras exportações

| Comando | O que faz |
|---|---|
| Opção `Arquivos` da janela de exportação | Stems: um arquivo por insert do mixer, por canal, ou por canal só com instrumentos |
| `Arquivo › Exportar › Todas as trilhas da Playlist` | Um arquivo por faixa da Playlist |
| `Arquivo › Exportar › Todos os grupos de trilhas da Playlist` | Um arquivo por grupo de faixas |
| `Arquivo › Exportar › Masterizar...` | Renderiza o projeto e masteriza até um alvo de loudness, no seu computador |
| `Arquivo › Exportar › Ossos do projeto...` | Grava presets e pontuações de canais e efeitos em uma pasta |

A janela de exportação renderiza a música, o pattern atual ou a seleção da Playlist.

## Pela linha de comando

No Windows o programa é `LYNX.exe`. No macOS e no Linux é `LYNX`.

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` e `--export-flp` também aceitam um `.als` como entrada.

## Bom saber

- Arquivos de áudio com mais de 30 segundos são lidos em streaming de um cache em disco.
- Um `.flp`, `.als` ou `.flm` nunca é gravado quando você salva. Os comandos de exportação gravam arquivos novos.
- Os arquivos `.wv` são só de leitura.
- Um `.zip` é extraído primeiro para uma pasta, e depois o projeto que está dentro é aberto.

## Veja também

- [Importar projetos](/wiki/importing-projects/)
- [Exportação](/wiki/exporting/)
- [Linha de comando](/wiki/command-line/)
- [Vindo do FL Studio](/wiki/coming-from-fl-studio/)
- [Vindo do Ableton Live](/wiki/coming-from-ableton/)

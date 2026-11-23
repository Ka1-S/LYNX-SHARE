---
source: e1ff018baf
---
O LYNX SHARE salva projetos como arquivos `.lynx`. Ele também abre projetos de outros programas, carrega os arquivos de áudio comuns e exporta áudio, MIDI e projetos para outros programas. A tabela lista todos os formatos da 0.1.31 alpha.

Um projeto aberto a partir do formato de outro programa é uma importação. `Arquivo › Salvar` então pergunta onde gravar um arquivo `.lynx`, e o arquivo original nunca é alterado.

## Abrir um projeto de outro programa

1. Escolha `Arquivo › Abrir...` (`Ctrl+O`).
2. Selecione um arquivo `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid` ou `.zip`.
3. Escolha `Ajuda › Último relatório de importação de projeto` para ler o que foi aproximado ou deixado de fora.
4. Escolha `Arquivo › Salvar` e indique onde gravar o arquivo `.lynx`.

## Formatos

| Formato | Abrir | Salvar / Exportar | Notas |
|---|---|---|---|
| `.lynx` | `Arquivo › Abrir...` | `Arquivo › Salvar`, com salvamento automático | O formato de projeto próprio do LYNX SHARE (JSON). `Arquivo › Salvar uma cópia para um LYNX mais antigo...` grava uma cópia que o 0.1.27 ou o 0.1.28 abre, e lista o que essa versão não consegue manter |
| `.zip` | `Arquivo › Abrir...` extrai para uma pasta e abre o projeto que está dentro | `Arquivo › Exportar › Pacote de loops zipado...` | Também abre o pacote zipado de loop do FL Studio |
| `.lynxpack` | Abre depois de um resumo. Uma parte é mesclada ao projeto de quem criou a sessão | `Arquivo › Sessão online › Enviar projeto...` | Um projeto ou uma parte enviada em uma [sessão online](/wiki/online-sessions/). As partes com plugins de terceiros viajam como renders de áudio mais as configurações do plugin |
| `.flp` | `Arquivo › Abrir...` | `Arquivo › Exportar › Projeto do FL Studio...`, um projeto do FL Studio 2026 ou um pacote zipado de loop | Os plugins nativos tocam em motores recriados, com exatidão variável. Os pacotes FLEX nunca são abertos. Na exportação, instrumentos e efeitos só do LYNX são renderizados em áudio ou trocados pelo plugin do FL mais próximo, e os moduladores ficam de fora. Veja [Vindo do FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `Arquivo › Abrir...` | `Arquivo › Salvar uma cópia como projeto do FL Studio Mobile (.flm)...` | Projeto do FL Studio Mobile. Parcial: instrumentos que não sejam MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, o Slicer e DirectWave tocam no synth embutido. A exportação é uma cópia: ela mantém as notas, o andamento, o mixer, os instrumentos próprios do FL Studio Mobile, o DW Sampler, o 3x Osc, os drum kits, os efeitos e a automação do nível do mixer, do pan, dos efeitos, do DW Sampler e do GMSynth, com os samples ao lado. Na cópia, os outros instrumentos mantêm suas notas no MiniSynth, e os efeitos sem módulo FLM e a automação de outros destinos (os knobs dos próprios synths, plugins de terceiros) ficam de fora; uma janela lista tudo antes de salvar. |
| `.als` | `Arquivo › Abrir...`, sets do Live 11 e 12 | `Arquivo › Exportar › Set do Ableton Live (.als)...`, para o Live 12 ou Live 11 | Na importação, os devices do Live sem equivalente no LYNX SHARE são descartados. Na exportação, os instrumentos e efeitos próprios do LYNX SHARE não são convertidos (o Helix vira Wavetable), e a automação de parâmetros de efeito e as rotas de insert para insert são descartadas. Veja [Vindo do Ableton Live](/wiki/coming-from-ableton/) |
| `.dawproject` | `Arquivo › Abrir...` | `Arquivo › Exportar › DAWproject (.dawproject)...` | O formato aberto de intercâmbio do Bitwig, do Studio One, do Cubase e de outros programas: faixas, pastas, clipes, notas, áudio com seus marcadores de warp, automação, andamento, marcadores, o mixer com sends e plugins VST3, VST2 e CLAP com seu estado. Efeitos de nota e expressões MIDI ficam de fora na importação; a grade de clipes, outros arranjos e alguns detalhes por nota na exportação. As duas direções listam o que ficou de fora |
| `.band` | `Arquivo › Abrir...` (o pacote, o arquivo `projectData` dele ou um zip dele) | Não | Projeto do GarageBand 10 ou do GarageBand para iOS, como faixas do layout Simple. Parcial: as faixas de instrumento tocam no sintetizador e na bateria embutidos, exceto instrumentos Sampler feitos no GarageBand para iOS. Projetos do GarageBand 11 e mais antigos não são legíveis |
| `.blx` | `Arquivo › Abrir...`, ou a pasta dele com a importação `Pasta...` do layout Simple | Não | Projeto do BandLab baixado pelo Cakewalk ou Sonar, como faixas do layout Simple com regiões, andamento, tom, o mix, automação de volume e pan e os efeitos comuns. Parcial: os sons de instrumento do BandLab e outros pedais são listados no relatório de importação |
| `.mid`, `.midi` | `Arquivo › Abrir...` cria um projeto novo. `Arquivo › Importar › Arquivo MIDI...` e o Piano Roll importam para o projeto atual | `Arquivo › Exportar › Arquivo MIDI...` (`Ctrl+Shift+M`) | |
| `.mod`, `.xm`, `.s3m`, `.it` | `Arquivo › Abrir...` | Não | Módulos de tracker (ProTracker, FastTracker 2, Scream Tracker 3, Impulse Tracker). Os instrumentos viram canais Sampler com seus samples, a música vira clipes de pattern com suas mudanças de andamento, e `Exibir › Tracker` edita as notas. Parcial: efeitos como slides, vibrato e arpejos, envelopes e mudanças de volume numa nota que está soando aparecem no relatório de importação |
| `.wav` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo Wave...`, 16 bits, 24 bits ou 32 bits float | Dither, informação de tempo, marcadores de loop e marcadores da Playlist opcionais |
| `.mp3` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo MP3...` | Taxa de bits constante |
| `.ogg` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo OGG...` | |
| `.flac` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo FLAC...`, compressão de 0 a 8 | Sem perdas em todos os níveis |
| `.aif`, `.aiff` | Como sample ou clipe de áudio | `Arquivo › Exportar › Arquivo AIFF...`, 16 bits ou 24 bits | |
| `.m4a` | – | `Arquivo › Exportar › Arquivo M4A...` | Só exportação. AAC com o codificador do sistema no Windows e no macOS; não disponível no Linux |
| `.wv` | Como sample | Não | Samples WavPack, como vêm nos pacotes do FL Studio |
| `.sf2`, `.sf3` | Tocado pelo LYNX Soundfont | Não | SoundFonts |
| `.dwp`, `.dwb` | Clique duplo no Browser, soltar no Channel Rack ou `Abrir programa / banco...` do DirectWave | Salvar programa e banco do DirectWave | Programas e bancos do DirectWave. Os samples são procurados onde o DirectWave os procura |
| `.gig` | Como canal do DirectWave | Não | Instrumentos do GigaStudio: camadas de velocity, round robin, loops, afinação e envelope de amplitude. Samples de release-trigger, keyswitches e samples comprimidos ainda não são lidos |
| `.ncw` | Como sample ou clipe de áudio | Não | Samples comprimidos do Kontakt. Instrumentos `.nki` do Kontakt precisam do próprio Kontakt como plugin; bibliotecas protegidas nunca são descriptografadas |
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
| `Arquivo › Exportar › Para outros apps...` | Uma pasta com stems, arquivos MIDI, um set do Ableton Live, um projeto do FL Studio, a mixagem final e um README, zipada se você quiser |
| `Arquivo › Exportar › Ossos do projeto...` | Grava presets e pontuações de canais e efeitos em uma pasta |

A janela de exportação renderiza a música, o pattern atual ou a seleção da Playlist.

## Pela linha de comando

No Windows o programa é `LYNX.exe`. No macOS e no Linux é `LYNX`.

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` e `--export-flp` também aceitam um `.als` como entrada.

## Bom saber

- Arquivos de áudio com mais de 30 segundos são lidos em streaming de um cache em disco.
- Um `.flp`, `.als`, `.flm`, `.dawproject`, `.band` ou `.blx` nunca é gravado quando você salva. Os comandos de exportação gravam arquivos novos.
- Os arquivos `.wv` são só de leitura.
- Um `.zip` é extraído primeiro para uma pasta, e depois o projeto que está dentro é aberto.

## Veja também

- [Importar projetos](/wiki/importing-projects/)
- [Exportação](/wiki/exporting/)
- [Linha de comando](/wiki/command-line/)
- [Vindo do FL Studio](/wiki/coming-from-fl-studio/)
- [Vindo do Ableton Live](/wiki/coming-from-ableton/)

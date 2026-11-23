---
source: 532b2eee04
---
`Arquivo › Abrir...` (`Ctrl+O`) abre os projetos `.lynx` do próprio LYNX SHARE e também arquivos `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx` e `.mid`. Um projeto de outro programa é uma importação: ele vira um projeto novo do LYNX SHARE, ainda não salvo, e o arquivo original nunca é gravado.

## Abrir um projeto de outro programa

1. Escolha `Arquivo › Abrir...` e selecione o arquivo `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, `.zip` ou de módulo tracker (`.mod`, `.xm`, `.s3m`, `.it`).
2. Espere enquanto o LYNX SHARE procura os plugins e samples do projeto.
3. Se algo não foi encontrado, a janela `Arquivos ausentes` se abre. Aponte-a para os seus samples ou feche-a.
4. A barra de título passa a mostrar de onde o projeto veio, por exemplo "(imported from FL Studio)".
5. Escolha `Arquivo › Salvar`. O LYNX SHARE pergunta onde gravar um arquivo `.lynx`.

## O que vem junto

| Formato | Convertido | Não convertido |
|---|---|---|
| `.flp` (FL Studio) | Canais, samples, patterns, notas de slide e porta, todos os arranjos, marcadores, mudanças de fórmula de compasso, automação, vínculos de controlador, o Mixer com roteamento, sidechains, EQ de faixa e efeitos nativos | Os pacotes FLEX são criptografados e nunca são abertos. Um canal FLEX recebe um patch de sintetizador escolhido pelo nome do preset |
| `.als` (Live 11 e 12) | Faixas MIDI, de áudio, de grupo e de retorno, tempo, fórmula de compasso, locators, o loop, volume, pan, mudo, solo e sends com a automação deles, clipes de Session e cenas, warp markers, racks e Drum Racks, e muitos dos devices do próprio Live (EQ Eight, Compressor, Glue Compressor, Reverb, Hybrid Reverb, Delay, Grain Delay, Auto Filter, Saturator, Roar, Utility, Simpler, Drift, Operator, Wavetable e mais) como os do LYNX SHARE | Os devices do Live sem equivalente no LYNX SHARE (Amp, Cabinet, Beat Repeat, Vocoder e outros) são descartados e listados |
| `.flm` (FL Studio Mobile) | Tempo, fórmula de compasso, um insert de mixer por canal, uma faixa da Playlist por lane, patterns, clipes de áudio com offset e pitch, kits de bateria, DW Sampler e programas `.dwp`, o Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth e SuperSaw, os efeitos, a automação e os conjuntos de acordes | Os outros geradores do FLM tocam no sintetizador embutido |
| `.dawproject` (Bitwig Studio, Studio One, Cubase e outros programas) | Faixas, pastas, clipes, notas, áudio com seus marcadores de warp, automação, andamento, marcadores, o mixer com sends, cenas na grade de clipes e plugins VST3, VST2 e CLAP com o estado deles. O EQ, o compressor, o gate e o limiter do formato viram os do próprio LYNX SHARE | Efeitos de nota, expressões por nota e os devices próprios do outro programa são listados no relatório de importação |
| `.band` (GarageBand 10, GarageBand para iOS) | Faixas, regiões MIDI e de áudio, andamento, fórmula de compasso, tom, seções como marcadores, nível, pan e mudo, como faixas do layout Simple. Instrumentos Sampler feitos no GarageBand para iOS | As faixas de instrumento tocam no sintetizador e na bateria embutidos. Regiões do Drummer, pastas de takes e mudanças de andamento são listadas. Projetos do GarageBand 11 e mais antigos não são legíveis |
| `.blx` (BandLab) | Faixas, regiões MIDI e de áudio com seus loops, andamento, fórmula de compasso, tom, volume, pan e mudo, automação de volume e pan e os efeitos comuns, como faixas do layout Simple | As faixas de instrumento tocam no sintetizador e na bateria embutidos. Outros efeitos e o preset de Mastering ficam de fora e são listados. Testado só com projetos fictícios |
| `.mid` | Notas, fórmulas de compasso e todas as mudanças de tempo | Um arquivo MIDI não traz sons: os canais recebem o sintetizador do LYNX SHARE ou um canal MIDI Out |
| `.mod`, `.xm`, `.s3m`, `.it` (módulos tracker) | Instrumentos como canais Sampler com seus samples, patterns, a lista de ordem como a Playlist, velocidade e andamento, volume da nota, pan, delay e corte; mudanças de volume e pan como eventos do canal (MOD e XM), e slides, vibrato, arpejos e envelopes de pitch como as curvas de pitch das notas | Outros efeitos e envelopes de instrumento são contados no relatório de importação. `Exibir › Tracker` edita as notas do jeito tracker |
| `.zip` | O pacote zipado de loop do FL Studio, ou um projeto exportado com seus samples: extraído para uma pasta própria, depois o projeto de dentro abre com seus samples | |

Plugins VST2, VST3 e CLAP de terceiros em um `.flp` ou um `.dawproject`, e plugins VST2 e VST3 em um `.als`, carregam com o estado salvo se o mesmo plugin estiver instalado no seu computador.

## Os plugins do próprio FL Studio

Os geradores e efeitos do FL Studio não são carregados. O LYNX SHARE lê as configurações salvas deles e as toca em motores que recriou. Cada motor tem um nível de qualidade, de `Exact` (idêntico bit a bit ao original em todos os estados testados) até `Placeholder` (as configurações são mantidas, mas o plugin fica mudo ou deixa o áudio passar direto). Os níveis são explicados em [Vindo do FL Studio](/wiki/coming-from-fl-studio/).

Abrir um `.flp` funciona sem o FL Studio instalado. Alguns motores precisam de ondas, samples ou impulsos que são lidos da sua própria instalação do FL Studio durante a execução.

Um `.flp` que usa o "FL Studio Mobile Rack" do FL Studio (o "Save as FLP" do próprio FL Studio Mobile grava um por faixa de instrumento) toca o instrumento e os efeitos nos mesmos motores de um `.flm`, e o efeito "FL Studio Mobile Rack FX" também. Um rack com um kit de bateria ou vários instrumentos ainda toca o sintetizador embutido.

## Sons de fábrica do FL Studio Mobile

Os sons de fábrica que um `.flm` usa (kits de bateria, programas do DirectWave, SoundFonts, as ondas do GMSynth) pertencem ao FL Studio Mobile e nunca são distribuídos com o LYNX SHARE. O LYNX SHARE os procura na sua própria cópia da pasta `Resources` do app: adicione-a em `Configurações › Arquivos › Recursos do FL Studio Mobile` (escolher a pasta acima dela, ou uma pasta de instalação do FL Studio, também funciona). A instalação do FL Studio guarda só uma parte deles e é pesquisada depois das suas pastas. Os arquivos que ainda faltarem aparecem na janela Arquivos ausentes.

## Samples ausentes

![A janela Arquivos ausentes na aba Samples: arquivos encontrados em verde, ausentes em âmbar, com Search folder, Locate e Skip](/assets/img/wiki/missing-files.webp)

A aba `Amostras` lista todos os samples que não foram encontrados, com o caminho salvo e o que os usa.

| Comando | O que faz |
|---|---|
| `Procurar em pasta...` | Procura todos os samples ausentes em uma pasta e nas subpastas dela |
| `Localizar...` | Pergunta onde está um sample. Os outros encontrados na mesma pasta também são aproveitados |
| `Pular` | Deixa o sample de fora. O canal dele fica mudo |

`Aplicar` usa os arquivos que foram encontrados.

A aba `Encontrados automaticamente` lista arquivos que o LYNX SHARE achou sozinho em outra pasta. Salve o projeto para guardar os novos locais.

## Plugins ausentes

O LYNX SHARE procura cada plugin de terceiros pelo identificador, depois pelo arquivo e depois pelo nome. Um projeto salvo com o VST2 abre com o VST3 do mesmo produto, e vice-versa, mas com as configurações padrão.

Quando um efeito não está instalado, um efeito do LYNX SHARE com a mesma função assume o slot: um reverb no lugar de um reverb, um compressor no lugar de um compressor.

- O slot mostra o nome do plugin original com um selo LYNX SHARE.
- Quando o estado salvo pode ser lido, as configurações são traduzidas. Caso contrário, o substituto começa de um preset padrão. A tabela de produtos, seus substitutos e o que é traduzido em cada um está em [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- A automação dos parâmetros do original é mantida, mas não controla o substituto.
- A referência e o estado do plugin original permanecem no projeto. Depois que o plugin for instalado, o projeto volta a abrir com ele.
- `Desativar todos os substitutos LYNX SHARE deste projeto`, no menu de clique direito de um slot substituto, desliga todos de uma vez.

A chave é `Substituir plugins ausentes por equivalentes do LYNX SHARE` em `Opções › Configurações gerais...`, ativada por padrão. Com ela desligada, o slot de um efeito ausente fica de fora do mixer. Um instrumento ausente não é substituído. Ele vira um sintetizador embutido com um preset escolhido pelo nome do canal.

Para recuperar o plugin de verdade, instale-o, escolha `Opções › Procurar plugins` e abra o projeto de novo.

## Arquivos MIDI

![A janela Import MIDI data: faixas, canais, tipo de canal e opções](/assets/img/wiki/midi-import-dialog.webp)

`Arquivo › Abrir...` cria um projeto novo a partir de um arquivo MIDI. `Arquivo › Importar › Arquivo MIDI...` abre primeiro a janela `Importar dados MIDI`, onde você escolhe o que trazer.

| Configuração | O que faz |
|---|---|
| `Quais faixas importar?` | Todas as faixas, ou uma |
| `Quais canais importar?` | Clique em um canal MIDI para deixá-lo de fora. Clique com o botão direito mantém só esse |
| `Tipo de canal` | `Synth do LYNX SHARE` ou `MIDI Out` |
| `Iniciar novo projeto` | Começa um projeto em branco. Desligado: adiciona ao projeto aberto |
| `Criar um canal por trilha` | Desligado: um canal por canal MIDI |
| `Definir trilhas do mixer para os novos canais` | Roteia os novos canais para inserts consecutivos do mixer |
| `Importar fórmulas de compasso` | A fórmula de compasso do arquivo passa a ser a do projeto |

`Ctrl+M` no [Piano Roll](/wiki/piano-roll/) importa um arquivo MIDI para o canal atual.

## Importar faixas de outro projeto

`Arquivo › Importar › Trilhas de outro projeto...` traz faixas de outro projeto para o aberto, sem fechá-lo. Ele lê todos os formatos que `Arquivo › Abrir...` lê.

1. Escolha o projeto. Uma janela lista as faixas da Playlist dele e o que cada uma contém.
2. Marque as faixas que você quer.
3. Escolha se `Com inserts do mixer e efeitos` e `Com automação` vêm junto, e `Colocar em` o início da música ou o início da seleção de tempo.
4. Clique para importar. As novas faixas, canais, patterns e inserts chegam como um só passo, então um único desfazer leva tudo de volta.

## Bom saber

- `Ajuda › Arquivos ausentes...` abre a janela de novo mais tarde. `Ajuda › Último relatório de importação de projeto` lista o que foi aproximado, deixado de fora ou não está instalado.
- Os clipes de Session de um set do Live vão para a grade de clipes do layout de lançamento de clipes. `Clipes de sessão de sets do Live` em `Opções › Configurações gerais...` os coloca na Playlist.

## Veja também

- [Formatos de arquivo](/wiki/file-formats/)
- [Vindo do FL Studio](/wiki/coming-from-fl-studio/)
- [Vindo do Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Exportação](/wiki/exporting/)

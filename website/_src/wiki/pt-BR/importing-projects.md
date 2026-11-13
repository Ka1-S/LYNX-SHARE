---
source: 358890815a
---
`Arquivo › Abrir...` (`Ctrl+O`) abre os projetos `.lynx` do próprio LYNX SHARE e também arquivos `.flp`, `.als`, `.flm` e `.mid`. Um projeto de outro programa é uma importação: ele vira um projeto novo do LYNX SHARE, ainda não salvo, e o arquivo original nunca é gravado.

## Abrir um projeto de outro programa

1. Escolha `Arquivo › Abrir...` e selecione o arquivo `.flp`, `.als`, `.flm` ou `.mid`.
2. Espere enquanto o LYNX SHARE procura os plugins e samples do projeto.
3. Se algo não foi encontrado, a janela `Arquivos ausentes` se abre. Aponte-a para os seus samples ou feche-a.
4. A barra de título passa a mostrar de onde o projeto veio, por exemplo "(imported from FL Studio)".
5. Escolha `Arquivo › Salvar`. O LYNX SHARE pergunta onde gravar um arquivo `.lynx`.

## O que vem junto

| Formato | Convertido | Não convertido |
|---|---|---|
| `.flp` (FL Studio) | Canais, samples, patterns, notas de slide e porta, todos os arranjos, marcadores, mudanças de fórmula de compasso, automação, vínculos de controlador, o Mixer com roteamento, sidechains, EQ de faixa e efeitos nativos | Os pacotes FLEX são criptografados e nunca são abertos. Um canal FLEX recebe um patch de sintetizador escolhido pelo nome do preset |
| `.als` (Live 11 e 12) | Faixas MIDI, de áudio, de grupo e de retorno, tempo, fórmula de compasso, locators, o loop, volume, pan, mudo, solo e sends com a automação deles, clipes de Session e cenas, warp markers, racks e Drum Racks, e muitos dos devices do próprio Live (EQ Eight, Compressor, Glue Compressor, Reverb, Delay, Auto Filter, Saturator, Utility, Simpler, Drift e mais) como os do LYNX SHARE | Os devices do Live sem equivalente no LYNX SHARE (Hybrid Reverb, Grain Delay, Amp, Roar, patches de FM Operator e outros) são descartados e listados. Os MIDI Effect Racks não são convertidos |
| `.flm` (FL Studio Mobile) | Tempo, fórmula de compasso, um insert de mixer por canal, uma faixa da Playlist por lane, patterns, clipes de áudio com offset e pitch, kits de bateria, DW Sampler e programas `.dwp`, o Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth e SuperSaw, os efeitos, a automação e os conjuntos de acordes | Os outros geradores do FLM tocam no sintetizador embutido |
| `.mid` | Notas, fórmulas de compasso e todas as mudanças de tempo | Um arquivo MIDI não traz sons: os canais recebem o sintetizador do LYNX SHARE ou um canal MIDI Out |

Plugins VST2, VST3 e CLAP de terceiros em um `.flp`, e plugins VST2 e VST3 em um `.als`, carregam com o estado salvo se o mesmo plugin estiver instalado no seu computador.

## Os plugins do próprio FL Studio

Os geradores e efeitos do FL Studio não são carregados. O LYNX SHARE lê as configurações salvas deles e as toca em motores que recriou. Cada motor tem um nível de qualidade, de `Exact` (idêntico bit a bit ao original em todos os estados testados) até `Placeholder` (as configurações são mantidas, mas o plugin fica mudo ou deixa o áudio passar direto). Os níveis são explicados em [Vindo do FL Studio](/wiki/coming-from-fl-studio/).

Abrir um `.flp` funciona sem o FL Studio instalado. Alguns motores precisam de ondas, samples ou impulsos que são lidos da sua própria instalação do FL Studio durante a execução.

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

## Bom saber

- `Ajuda › Arquivos ausentes...` abre a janela de novo mais tarde. `Ajuda › Last FL Studio import report` lista o que foi aproximado, deixado de fora ou não está instalado.
- Os clipes de Session de um set do Live vão para a grade de clipes do fluxo por disparo de clipes. `Clipes de sessão de sets do Live` em `Opções › Configurações gerais...` os coloca na Playlist.

## Veja também

- [Formatos de arquivo](/wiki/file-formats/)
- [Vindo do FL Studio](/wiki/coming-from-fl-studio/)
- [Vindo do Ableton Live](/wiki/coming-from-ableton/)
- [Plugins](/wiki/plugins/)
- [Exportação](/wiki/exporting/)

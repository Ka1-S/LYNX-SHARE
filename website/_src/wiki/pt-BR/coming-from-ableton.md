---
source: 37920663db
---
![O layout de disparo de clipes: uma grade de clipes com cenas ao lado da Playlist](/assets/img/cliplaunch.webp)

O LYNX SHARE começa no layout Pattern: um Channel Rack, patterns e uma Playlist. O layout de lançamento de clipes é opcional e acrescenta uma grade de clipes com cenas ao lado da Playlist. O LYNX SHARE lê e grava sets `.als`. Não tem nenhuma ligação com a Ableton.

Esta página serve também para quem usa o Bitwig Studio: a grade de clipes funciona do mesmo jeito, e os arquivos DAWproject do Bitwig (`.dawproject`) abrem com `Arquivo › Abrir...` e são gravados por `Arquivo › Exportar › DAWproject (.dawproject)...`.

## Seus primeiros passos

1. **Escolha o layout.** Na primeira vez que o LYNX SHARE abre, um cartão de boas-vindas pergunta de onde você vem. Clique em `Ableton Live ou Bitwig`: o Layout de lançamento de clipes abre. Depois, `Exibir › Estilo de layout › Layout de lançamento de clipes` troca para ele a qualquer momento.
2. **Preencha um slot.** Arraste um sample ou um pattern do Browser para um slot, ou clique com o botão direito num slot vazio e escolha `Colocar um padrão aqui`.
3. **Dispare.** Clique no triângulo à esquerda de um clipe, ou no triângulo de uma cena na coluna `Cenas` para disparar a linha toda. Ou abra um dos seus sets `.als` (abaixo).

O que é diferente do Live:

- A Playlist é o arranjo, e as faixas dela são as colunas da grade. Uma faixa que disparou um clipe fica na grade até você apertar `Voltar à Playlist`.
- Fora da grade, as teclas seguem o FL Studio. `Ctrl+Z` desfaz um passo e, pressionado de novo, refaz.
- Só alguns dos devices próprios do Live são convertidos (veja Bom saber). Plugins de terceiros vêm com o estado deles.
- O Layout Pattern, o jeito de trabalhar do FL Studio com Channel Rack e patterns, funciona no mesmo projeto.

## Abrir um set do Live

1. Escolha `Arquivo › Abrir...` (`Ctrl+O`) e selecione o `.als`. Sets salvos pelo Live 11 e pelo Live 12 são lidos.
2. Escolha `Exibir › Estilo de layout › Layout de lançamento de clipes` para trabalhar com a grade de clipes, para onde vão os clipes de Session e as cenas do set.
3. Use a chave `Grid / Both / Song` na barra de título da Playlist para mostrar a grade, a Playlist ou as duas.
4. Escolha `Arquivo › Salvar`. Ele pergunta onde gravar um arquivo `.lynx`. O `.als` não é alterado.

`Exibir › Estilo de layout › Layout Pattern (padrão)` volta ao anterior. A mesma escolha está em `Opções › Configurações gerais...`. Patterns e o Channel Rack continuam disponíveis nos dois layouts.

## O que corresponde a quê

| No Live | No LYNX SHARE |
|---|---|
| Session view | A grade de clipes do layout de lançamento de clipes |
| Vista de arranjo | A Playlist |
| Faixas | Faixas da Playlist, que são as colunas da grade de clipes |
| Cenas | Cenas, as linhas da grade de clipes. Uma cena pode definir o tempo ao ser disparada |
| Quantização de disparo | `Global`, `Nenhum`, de 8 compassos até 1 compasso, ou um valor de nota de 1/2 a 1/32, por clipe se quiser |
| Modos de disparo | `Gatilho`, `Gate`, `Alternar` e `Repetições` |
| Follow actions | Action A e Action B, cada uma com uma chance |
| Botões de parada de clipe | Um slot vazio pode ter um botão de parada |
| Voltar ao arranjo | `Voltar à Playlist` |
| Modos de warp | `Beats`, `Tons`, `Textura` e `Complexo` em `Configurações do canal › Esticar o tempo` |
| Simpler, Sampler e drum racks | Samplers do LYNX SHARE |
| Set `.als` | Arquivo de projeto `.lynx` |

## A grade de clipes

Um slot guarda um clipe de pattern ou um clipe de áudio.

- Uma faixa toca um clipe por vez. Um clipe disparado substitui o que estava tocando.
- Um disparo espera o próximo passo da quantização de disparo.
- `Legato` inicia o novo clipe na posição a que o clipe substituído tinha chegado. `Loop` é por clipe.
- Cenas podem ser inseridas, excluídas e renomeadas.
- Uma faixa que disparou um clipe fica em silêncio na Playlist até você pressionar `Voltar à Playlist`. Uma opção devolve as faixas quando o transporte para.

As follow actions rodam depois de um número de compassos ou de passagens do clipe. As ações são `Nenhuma ação`, `Parar`, `Tocar de novo`, `Anterior`, `Próx.`, `Primeiro`, `Último`, `Qualquer` e `Outros`. Uma chave liga ou desliga as follow actions para a grade inteira.

## O que é importado de um .als

- Faixas MIDI, de áudio, de grupo e de retorno, tempo, fórmula de compasso, locators, o loop, configurações do mixer (volume, pan, mudo, solo, sends) e a automação delas.
- Plugins VST2 e VST3 com o estado deles, se o mesmo plugin estiver instalado. Veja [Plugins de terceiros](/wiki/plugins/).
- Clipes de Session e cenas, na grade de clipes, com o modo de disparo, a quantização, o legato, o loop, as follow actions e os botões de parada. Para colocá-los na Playlist, uma seção por cena, mude `Clipes de sessão de sets do Live` em `Opções › Configurações gerais...`.
- Marcadores de warp.

## Exportar um .als

1. Escolha `Arquivo › Exportar › Set do Ableton Live (.als)...`.
2. Em `Salvar para`, escolha `Live 12` ou `Live 11 (também abre no Live 12)`.
3. Em `Amostras`, copie-os para a pasta de projeto do set ou aponte para onde estão.
4. Escolha uma pasta. O set é salvo como `<name> Project/<name>.als`.

| Gravado | Não gravado |
|---|---|
| Uma faixa MIDI por canal, com as notas como clipes MIDI | Os instrumentos e efeitos próprios do LYNX SHARE, exceto o Helix, que vira Wavetable. As faixas e as notas deles são mantidas |
| Faixas de áudio com stretch, transposição, ganho, fades e modo de warp | Automação de parâmetros de efeitos |
| Grupos, retornos e sends | Rotas de insert para insert |
| Plugins VST2 e VST3 com o estado deles | |
| Marcadores e automação de volume, pan e tempo | |
| A grade de clipes como clipes de Session e cenas, racks e Drum Racks | |

Um relatório depois da exportação lista o que ficou de fora.

## Bom saber

- Muitos dos devices do próprio Live entram como os do LYNX SHARE, com suas configurações e automação: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Hybrid Reverb, Delay, Echo, Grain Delay, Auto Filter, Saturator, Overdrive, Roar, Erosion, Vinyl Distortion, Corpus, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Spectral Time, Spectral Resonator, Looper, Simpler, Sampler, Drift, Operator, Wavetable e Meld. Os outros são descartados e listados no relatório de importação.
- Os Instrument Racks e Audio Effect Racks entram com suas cadeias, zonas e macros, e um Drum Rack vira o Drum Rack do LYNX SHARE. O Arpeggiator do Live vira o arpejador do canal. Pitch, Velocity, Chord, Note Length e os MIDI Effect Racks são gravados nas notas.
- O relatório de importação lista o que ficou de fora.
- No Device View, um rack mostra os devices de suas cadeias na própria linha, e o modo Macro Map (os botões `Map` do rack) entrega o parâmetro de um device a uma macro, como no Live.
- O mixer Session tem o interruptor Solo / Cue do Live, com um `Cue Out` para os seus fones e um `Volume do cue`.
- O Live corta os clipes de áudio que se sobrepõem em um set exportado.
- Os warp markers de um set do Live são mantidos na importação e gravados de volta na exportação.

## Veja também

- [Playlist](/wiki/playlist/)
- [Layouts](/wiki/layouts/)
- [Importar projetos](/wiki/importing-projects/)
- [Formatos de arquivo](/wiki/file-formats/)
- [Dois layouts em um só programa](/why/#workflows)
- [Aprenda: tutoriais](/learn/)

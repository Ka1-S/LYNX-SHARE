---
source: d5cc625221
---
![O layout de disparo de clipes: uma grade de clipes com cenas ao lado da Playlist](/assets/img/cliplaunch.webp)

O LYNX SHARE começa no fluxo por padrões: um Channel Rack, patterns e uma Playlist. O fluxo por disparo de clipes é opcional e acrescenta uma grade de clipes com cenas ao lado da Playlist. O LYNX SHARE lê e grava sets `.als`. Não tem nenhuma ligação com a Ableton.

## Abrir um set do Live

1. Escolha `Arquivo › Abrir...` (`Ctrl+O`) e selecione o `.als`. Sets salvos pelo Live 11 e pelo Live 12 são lidos.
2. Escolha `Exibir › Estilo de layout › Fluxo por disparo de clipes` para trabalhar com a grade de clipes, para onde vão os clipes de Session e as cenas do set.
3. Use a chave `Grid / Both / Song` na barra de título da Playlist para mostrar a grade, a Playlist ou as duas.
4. Escolha `Arquivo › Salvar`. Ele pergunta onde gravar um arquivo `.lynx`. O `.als` não é alterado.

`Exibir › Estilo de layout › Fluxo por padrões (padrão)` volta ao anterior. A mesma escolha está em `Opções › Configurações gerais...`. Patterns e o Channel Rack continuam disponíveis nos dois layouts.

## O que corresponde a quê

| No Live | No LYNX SHARE |
|---|---|
| Session view | A grade de clipes do fluxo por disparo de clipes |
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
| Uma faixa MIDI por canal, com as notas como clipes MIDI | Os instrumentos e efeitos próprios do LYNX SHARE. As faixas e as notas deles são mantidas |
| Faixas de áudio com stretch, transposição, ganho, fades e modo de warp | Automação de parâmetros de efeitos |
| Grupos, retornos e sends | Rotas de insert para insert |
| Plugins VST2 e VST3 com o estado deles | |
| Marcadores e automação de volume, pan e tempo | |
| A grade de clipes como clipes de Session e cenas, racks e Drum Racks | |

Um relatório depois da exportação lista o que ficou de fora.

## Bom saber

- Muitos dos devices do próprio Live entram como os do LYNX SHARE, com suas configurações e automação: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift e Operator (sem FM). Os outros são descartados e listados no relatório de importação.
- Os Instrument Racks e Audio Effect Racks entram com suas cadeias, zonas e macros, e um Drum Rack vira o Drum Rack do LYNX SHARE. Os MIDI Effect Racks do Live ainda não são convertidos e aparecem no relatório de importação.
- O relatório de importação lista o que ficou de fora.
- O Live corta os clipes de áudio que se sobrepõem em um set exportado.
- Os warp markers de um set do Live são mantidos na importação e gravados de volta na exportação.

## Veja também

- [Playlist](/wiki/playlist/)
- [Layouts](/wiki/layouts/)
- [Importar projetos](/wiki/importing-projects/)
- [Formatos de arquivo](/wiki/file-formats/)
- [Dois fluxos de trabalho em um só programa](/why/#workflows)

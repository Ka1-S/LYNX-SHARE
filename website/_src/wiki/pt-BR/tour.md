---
source: edf15b371e
---
![A janela principal com o projeto de demonstração: barra de ferramentas no topo, Browser à esquerda, Playlist ao fundo e o Channel Rack flutuando sobre ela](/assets/img/wiki/desktop.webp)

O LYNX SHARE tem uma janela principal. Uma barra de ferramentas corre pelo topo. Abaixo dela fica uma área de trabalho com cinco janelas: o Browser, a Playlist, o Channel Rack, o Piano Roll e o Mixer. Em uma primeira execução, o Browser fica à esquerda, a Playlist ocupa o resto e o Channel Rack flutua sobre ela. O Piano Roll e o Mixer ficam ocultos até você chamá-los.

## Se orientar

1. Aponte o mouse para qualquer controle. A barra de dicas sob os menus diz o que ele faz e quais botões do mouse e teclas ele aceita.
2. Pressione `F6` para ocultar o Channel Rack e `F6` de novo para mostrá-lo.
3. Pressione `F9` para mostrar o Mixer e depois `F7` para o Piano Roll.
4. Arraste uma janela pela faixa de título para movê-la. Dê um clique duplo no título para maximizá-la e de novo para restaurá-la.
5. Escolha `Exibir › Restaurar layout padrão` para colocar tudo de volta.

## A barra de ferramentas

![A barra de ferramentas: menus e barra de dicas, transporte, knobs master, opções de gravação, grade e pattern, botões de janela, botões de arquivo, exibição de acordes, medidores e monitor de saída](/assets/img/wiki/toolbar-1920.webp)

Da esquerda para a direita, com o layout padrão:

| Grupo | O que contém |
|---|---|
| Menu e barra de dicas | Os menus `Arquivo`, `Editar`, `Adicionar`, `Patterns`, `Exibir`, `Opções`, `Ferramentas` e `Ajuda`, com a barra de dicas embaixo deles. Sempre primeiro |
| Transporte | Os botões de modo `Pattern` e `Song`, Tocar, Parar e Gravar, o tempo em BPM e a posição como compasso, tempo e step |
| Volume e pitch master | O knob de volume `MASTER` e o knob `AFINAÇÃO`, em cents |
| Opções de gravação | Metrônomo, contagem, gravação em loop, edição por steps e as outras chaves de gravação |
| Snap e pattern | A grade principal (`Linha (segue o zoom)` por padrão), o pattern atual, um botão de novo pattern e o menu de patterns |
| Atalhos das janelas | Um botão por janela, e o seletor de plugins |
| Arquivo e desfazer | Novo, Abrir, Salvar, Exportar, Desfazer e Refazer |
| Detecção de acordes | Nomeia o acorde que está sendo tocado em um dispositivo MIDI ou no teclado de digitação |
| CPU, memória e polifonia | Carga de CPU do áudio, memória em uso e número de vozes |
| Monitor de saída | Um osciloscópio da saída. Um clique percorre os outros modos dele |

Existe também um grupo chamado `Multilink para controladores`, oculto por padrão.

`Opções › Barra de ferramentas` muda a barra de ferramentas: `Editar barra de ferramentas (arraste painéis para mover, incluir ou remover)`, `Separadores`, `Esconder os painéis da barra`, `Presets`, uma chave para cada grupo e `Tamanho dos ícones` (pequeno, médio ou grande).

## Transporte

| Controle | Tecla | O que faz |
|---|---|---|
| Modo Pattern | `L` | Toca o pattern atual em loop. Clique com o botão direito no botão para mostrar o Channel Rack |
| Modo Song | `L` | Toca o arranjo da Playlist. Clique com o botão direito no botão para mostrar a Playlist |
| Tocar | `Space` | Toca. Pressionado de novo, para e volta ao ponto de partida. `Ctrl+Space` pausa |
| Parar | | Para e volta ao início. O clique duplo também envia all notes off |
| Gravar | `R` | Liga ou desliga a gravação. Veja [Gravação](/wiki/recording/) |

## A barra de dicas

Em repouso, a barra de dicas mostra `Point at anything for help`, com `F1` para a folha de atalhos, `F5` a `F9` para as janelas e `Space` para tocar e parar. Sobre um controle, ela nomeia o controle e lista o que arrastar, `Ctrl`, `Alt` e o clique com o botão direito fazem ali. Tarefas longas, como o exame de plugins em segundo plano, informam o progresso nela.

## As cinco janelas

| Janela | Tecla | Para que serve |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Arranjar clipes de pattern, áudio e automação em uma música |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Uma linha por instrumento, com os steps do pattern atual |
| [Piano Roll](/wiki/piano-roll/) | `F7` | As notas de um canal no pattern atual |
| [Mixer](/wiki/mixer/) | `F9` | Inserts com faders, efeitos e roteamento |
| [Browser](/wiki/browser/) | `Alt+F8` | Samples, presets, o conteúdo do projeto e o banco de dados de plugins |

Cada tecla mostra a janela e lhe dá o foco, ou a oculta quando ela já tem o foco. `F8` sozinho abre o seletor de plugins, que não é uma das cinco. Os botões de janela da barra de ferramentas fazem o mesmo que as teclas. Clicar com o botão direito em um deles mostra aquela janela maximizada. `Enter` maximiza ou restaura a Playlist.

## Como as janelas se movem e se encaixam

Toda janela tem uma faixa de título. À esquerda dela há um botão de menu com as opções da própria janela, depois `Destacada (janela própria)`, `Maximizada` e `Fechar`. À direita ficam os botões de maximizar e fechar.

- Arraste a faixa de título para mover uma janela. Ela se encaixa nas outras janelas e nas bordas. Mantenha `Alt` pressionado para mover sem encaixar.
- Arraste uma borda para redimensionar.
- `Destacada (janela própria)` transforma a janela em uma janela separada do sistema, para um segundo monitor. Uma janela destacada tem um botão `Encaixar` que a devolve ao lugar. `Exibir › Painéis destacados (janela própria)` lista as cinco.
- `Exibir › Trazer de volta painéis fora da tela` recupera uma janela que ficou fora da área de trabalho.
- `Exibir › Salvar layout como...` guarda o arranjo com um nome, e `Exibir › Carregar layout` traz um de volta.

O Browser é o fundo da área de trabalho: ele fica encaixado na lateral e as outras janelas sempre ficam acima dele.

## Bom saber

- Até você mover, redimensionar ou destacar alguma coisa, as janelas voltam às posições padrão sempre que a janela principal muda de tamanho. Depois disso, o seu arranjo é lembrado e escala com a janela.
- `F12` fecha as janelas de plugin, de configurações de canal e de ferramentas. Não fecha as cinco janelas principais.
- Enquanto o piano do teclado de digitação está ligado (`Ctrl+T`), as teclas de letra tocam notas e a maioria dos atalhos de uma letra fica em pausa.
- `Exibir › Estilo de layout` alterna entre o fluxo por padrões (o padrão) e o fluxo por disparo de clipes, que acrescenta uma grade de clipes ao lado da Playlist.

## Veja também

- [Sua primeira música](/wiki/first-song/)
- [Layouts](/wiki/layouts/)
- [Atalhos](/wiki/shortcuts/)
- [Configurações](/wiki/settings/)

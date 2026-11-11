---
source: ab2c2f14eb
---
![A página General da janela Configurações, com as abas de página ao longo do topo](/assets/img/wiki/settings-general.webp)

A janela Configurações tem oito páginas: Audio, MIDI, General, Theme, Files, LYNX extras, Project e Info. As seis primeiras pertencem ao programa. Project e Info pertencem ao projeto aberto e são salvas com ele. Cada página tem a própria entrada no alto do menu `Opções`.

## Mudar uma configuração

1. Abra o menu `Opções` e escolha uma página, por exemplo `Configurações gerais...`.
2. Clique em outra aba ao longo do topo para ir a outra página.
3. Mude a configuração. Ela vale na hora, exceto o idioma e a fonte da interface, que valem quando o LYNX SHARE reinicia.

`F10` abre a página MIDI e `F11` a página Info.

## Áudio

`Opções › Configurações de áudio...`. Driver, dispositivos, taxa de amostragem, tamanho do buffer e gravação. [Configuração de áudio](/wiki/audio-setup/) descreve esta página. Em `Gravação`, `Formato do arquivo` agora também oferece `WAV 16 bits (a taxa de amostragem do dispositivo)`.

## MIDI

![A página MIDI: dispositivos de entrada, dispositivos de saída com sincronização mestre e sincronização externa](/assets/img/wiki/settings-midi.webp)

`Opções › Configurações de MIDI...`

| Opção | O que faz |
|---|---|
| Dispositivos de entrada | Cada dispositivo pode ser desligado, escutar só um canal MIDI e tocar um canal fixo do Channel Rack em vez dos selecionados |
| `Tipo de controlador` | Para cada dispositivo de entrada: `Detectar automaticamente`, `Controlador genérico` ou um perfil de controlador, como o Mackie Control. A linha ao lado informa a porta que recebe as luzes e o display |
| `Controlador MPE` | No fim da lista de canais MIDI de um dispositivo. Cada nota vem com seu próprio bend, timbre e pressão. Uma segunda caixa define os canais membros |
| `Atualizar lista de dispositivos` | Procura os dispositivos de novo e lê de novo os seus perfis de controlador |
| `Pasta de perfis de controlador` | Abre a pasta para os seus próprios perfis de controlador (`*.lynxctl`) |
| `Enviar sincronização mestre` | Envia MIDI clock com Song Position Pointer, ou MIDI Time Code, a um dispositivo de saída. `Deslocamento (ms)` envia mais tarde ou mais cedo |
| `Enviar all notes off` | Envia all notes off ao dispositivo na inicialização e depois de uma exportação |
| `Sincronizar com clock externo` | O LYNX SHARE segue o clock MIDI da entrada escolhida: tempo, start, continue, stop e posição |
| `Velocity` | Para notas de dispositivos MIDI: como tocada, suave, dura ou fixa |
| `Canal do modo performance` | Notas neste canal disparam clipes da Playlist no modo Performance |
| `Salto para marcador da música` | Notas neste canal levam a música a um marcador em vez de tocar |
| `Captura (modo takeover)` | Um controlador só move um parâmetro depois de alcançar o valor atual dele |
| `O pedal controla o fim das notas` | Ligado por padrão: os pedais de sustain e de sostenuto seguram as notas. Desligado, o pedal vai para os plugins |
| `Usar um controle de videogame` | Um controle de jogo comanda o transporte e pode ser vinculado como um controlador MIDI. Desligado por padrão |
| Vínculos | Os vínculos de controlador deste projeto, depois os globais, com `Remover vínculo` e `Remover todos` |

## Geral

`Opções › Configurações gerais...`

| Opção | O que faz |
|---|---|
| `Estilo de layout` | Fluxo por patterns (padrão), fluxo por disparo de clipes ou fluxo simples |
| `Modelo padrão`, `Projeto ao iniciar` | O template que abre com `Arquivo › Novo`, e o que abre na inicialização: `Projeto vazio`, `Modelo padrão` ou `Último projeto usado`. `Arquivo › Salvar como modelo...` adiciona os seus |
| `Clipes de sessão de sets do Live` | Para onde vão os clipes de sessão de um set do Live importado: a grade de clipes, ou a Playlist depois do arranjo |
| `Idioma`, `Fonte da interface` | Valem na próxima inicialização |
| `Tamanho da interface`, `Densidade dos menus` | Escala da interface inteira, e menus compactos ou confortáveis |
| `Formas de onda` | Limpas (a cor do clipe) ou em cores espectrais, opcionalmente com os canais estéreo separados |
| `Nomes das notas`, `Teclas pretas` | Nomes em inglês, germânicos ou em solfejo, com sustenidos ou bemóis |
| `Estilo dos plugins` | O visual das janelas de plugins embutidos: automático, ou um só estilo para todos os plugins |
| `Animações`, `Taxa de animação` | Animações da interface, e com que frequência o cursor de reprodução e os medidores se movem |
| `Desenhar com a GPU (Direct2D)` | Desligue se o driver gráfico travar ou a tela piscar |
| Inicialização | Tela de abertura, som de abertura e busca de plugins novos em segundo plano a cada início |
| Edição | Teclado do computador como piano, manter abertos os editores de canal, lembrar o tempo de busca, repetir em loop clipes de pattern esticados |
| `Destacar todos os plugins novos` | Ligado: a janela de um plugin que você adiciona abre destacada, sobre os painéis. Desligado (padrão): abre acoplada na janela principal |
| `Pintura monofônica no piano roll` | Ligado: a ferramenta Pincel substitui as notas sob cada nota nova, e Shift pinta por cima delas. Desligado: o contrário |
| `Selecionar automaticamente os módulos ligados` | Dar foco a uma janela de plugin seleciona a faixa dela no Mixer. Ligado por padrão |
| `Desativar a prioridade do teclado de digitação por padrão` | Para os plugins que você adicionar daqui em diante. Desligado por padrão |
| `Máximo de níveis de desfazer` | 25 a 1000 |
| `Gerenciar mensagens` | Em `Dicas e avisos`. As mensagens marcadas são mostradas. Desmarque uma para pular a pergunta dela |
| Cópia de segurança | Com que frequência uma cópia de segurança é salva, quantas são mantidas por projeto, e `Abrir pasta das cópias` |
| `Rodar plugins em processo separado` | Um plugin que trava não derruba o LYNX SHARE. Desligado por padrão |
| `Substituir plugins ausentes por equivalentes do LYNX SHARE` | Ligado por padrão. Quando um projeto usa um plugin de terceiros que não está instalado, o substituto do LYNX SHARE carrega com configurações equivalentes, se houver um. O projeto mantém o plugin original e volta a usá-lo quando ele for instalado |
| `Limitar o número de editores de plugin abertos` | No máximo 25 janelas de editor de plugin ficam abertas. Ligado por padrão |
| `Relatórios de falha` | Perguntar depois de uma falha ou travamento (padrão), sempre enviar ou nunca enviar |
| `Procurar atualizações automaticamente` | A busca de atualizações na inicialização, com `Procurar agora` |

## Tema

`Opções › Tema (cores, fundo)...`

| Opção | O que faz |
|---|---|
| Caixa de tema | Escolhe um preset. `Importar...` e `Exportar...` leem e gravam arquivos `.lynxtheme`, `Salvar preset...` guarda o tema atual, `Padrão` o restaura |
| `Cor de destaque`, `Fundo`, `Área de trabalho` | As três cores de um tema. A cor da área de trabalho pode seguir o fundo automaticamente |
| Imagem da área de trabalho | Um PNG, JPEG ou GIF atrás das janelas, com `Ajuste da imagem`, `Opacidade da imagem` e `Escurecer` |
| Som de abertura | Um som seu de 30 segundos ou menos, ou o próprio do LYNX SHARE |

## Arquivos

`Opções › Configurações de arquivos (pastas de busca)...`

| Opção | O que faz |
|---|---|
| Pastas de busca de samples ausentes | A janela Arquivos ausentes procura nestas pastas e nas subpastas por conta própria |
| Pastas do Browser | Aparecem na lista de pastas do Browser e também são consultadas na busca de samples ausentes |
| Pastas de plugins | Examinadas em busca de plugins por `Opções › Procurar plugins` |

## LYNX extras

`Opções › Extras do LYNX...`. As adições próprias do LYNX SHARE num só lugar. Todas vêm desligadas por padrão. Cada chave é a mesma configuração do menu ou da página que a linha dela cita.

| Configuração | O que faz |
|---|---|
| `Medidor de loudness ao lado do Master (LYNX)` | Loudness de curto prazo e integrado (LUFS) e true peak do Master, com um botão Reset |
| `Mostrar só os inserts usados` | Esconde os inserts do Mixer que nada usa |
| `Manter ligações para inserts excluídos, em cinza (LYNX)` | Excluir um insert mantém os vínculos que iam para ele, para poderem ser conectados de novo |
| `Grupos do Mixer seguem a Playlist (LYNX)` | Os grupos do Mixer espelham os grupos de faixas da Playlist |
| `Faixas de automação` | A automação pode ficar em faixas sob uma faixa da Playlist |
| `Régua: compassos da música (LYNX)` | A régua do Piano Roll mostra os números dos compassos do ponto da música onde o pattern toca |
| `Mostrar o valor de um controle ao lado do mouse ao passar sobre ele (LYNX)` | O valor aparece ao lado do cursor, e não só na barra de dicas |
| `Após gravar em loop, só a última passagem toca` | As passadas anteriores ficam por baixo, mudas |
| `W mostra ou oculta a janela do dispositivo selecionado` | Uma tecla para a Device view |
| `Selecionar um dispositivo seleciona o insert dele no Mixer` | O Mixer acompanha o dispositivo escolhido na Device view |
| `Usar um controle de videogame` | A mesma chave da página MIDI |

## Projeto

![A página Projeto: configurações de tempo, groove e afinação, base de tempo e opções da Playlist](/assets/img/wiki/settings-project.webp)

`Opções › Configurações do projeto (andamento, fórmula de compasso)...`

| Opção | O que faz |
|---|---|
| `Tempo e compasso` | Divisão de tempo (tempos por compasso, steps por tempo) ou uma fórmula de compasso tradicional (numerador e denominador) |
| `Tempo`, `Tempos por compasso` | O andamento do projeto e os tempos em um compasso |
| `Swing`, `Afinação master` | A quantidade global de swing e o pitch master em cents |
| `Crossfades automáticos` | Clipes de áudio sobrepostos em uma faixa fazem crossfade na sobreposição |
| `Crossfade do comp` | A duração do crossfade usada ao fazer comp de takes, em milissegundos |
| `Tocar notas truncadas nos clipes` | Uma nota que começa antes de um clipe de pattern toca a partir do início do clipe |
| `Declick rápido para grupos de corte` | Rampa mais rápida quando grupos de corte cortam notas |
| `Lei de panorâmica` | Circular (centro em 0 dB, +3 dB no lado para onde se faz o pan) ou Triangular (o lado mais alto mantém o nível). Para inserts do Mixer e canais Sampler |

## Info

`Opções › Informações do projeto...`. Título, autor, gênero, link da web e comentários do projeto, e `Mostrar esta página ao abrir o projeto`. O rodapé mostra quando o projeto foi criado e o tempo de trabalho.

## Bom saber

- A base de tempo é fixa em 960 ticks por semínima. Os projetos do FL Studio são convertidos para ela.
- As mudanças de fórmula de compasso ao longo da música não estão na página Projeto. Adicione um marcador de fórmula de compasso na régua da Playlist.
- `Rodar plugins em processo separado` vale para os plugins abertos dali em diante. Reabra o projeto para mover os outros.
- As configurações de atualização não existem na versão da Microsoft Store, porque a Store a atualiza.
- Os atalhos ficam em `Opções › Gerenciar atalhos...`, não em uma página de Configurações.
- Algumas chaves estão em menus, não nesta janela. `Baixa latência durante a gravação` fica no menu de opções de gravação ao lado do botão de gravar e no menu `Gravação em disco` do Mixer. `Estilo do teclado` fica no menu View do Piano Roll.
- A Plugin database é uma pasta, `Banco de plugins`, dentro da pasta `LYNX` dos seus documentos. Você pode organizá-la no gerenciador de arquivos.

## Veja também

- [Configuração de áudio](/wiki/audio-setup/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Atalhos](/wiki/shortcuts/)
- [Plugins](/wiki/plugins/)
- [Layouts](/wiki/layouts/)

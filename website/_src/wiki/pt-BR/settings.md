---
source: 841fdd7d67
---
![A página Geral da janela Configurações, com as sete abas de página ao longo do topo](/assets/img/wiki/settings-general.webp)

A janela Configurações tem sete páginas: Áudio, MIDI, Geral, Tema, Arquivos, Projeto e Info. As cinco primeiras pertencem ao programa. Projeto e Info pertencem ao projeto aberto e são salvas com ele. Cada página tem a própria entrada no alto do menu `Opções`.

## Mudar uma configuração

1. Abra o menu `Opções` e escolha uma página, por exemplo `Configurações gerais...`.
2. Clique em outra aba ao longo do topo para ir a outra página.
3. Mude a configuração. Ela vale na hora, exceto o idioma e a fonte da interface, que valem quando o LYNX SHARE reinicia.

`F10` abre a página MIDI e `F11` a página Info.

## Áudio

`Opções › Configurações de áudio...`. Driver, dispositivos, taxa de amostragem, tamanho do buffer e gravação. [Configuração de áudio](/wiki/audio-setup/) descreve esta página.

## MIDI

![A página MIDI: dispositivos de entrada, dispositivos de saída com sincronização mestre e sincronização externa](/assets/img/wiki/settings-midi.webp)

`Opções › Configurações de MIDI...`

| Opção | O que faz |
|---|---|
| Dispositivos de entrada | Cada dispositivo pode ser desligado, escutar só um canal MIDI e tocar um canal fixo do Channel Rack em vez dos selecionados |
| `Atualizar lista de dispositivos` | Procura os dispositivos de novo |
| `Enviar sincronização mestre` | Envia MIDI clock com Song Position Pointer, ou MIDI Time Code, a um dispositivo de saída. `Deslocamento (ms)` envia mais tarde ou mais cedo |
| `Enviar all notes off` | Envia all notes off ao dispositivo na inicialização e depois de uma exportação |
| `Sincronizar com clock externo` | O LYNX SHARE segue o clock MIDI da entrada escolhida: tempo, start, continue, stop e posição |
| `Velocity` | Para notas de dispositivos MIDI: como tocada, suave, dura ou fixa |
| `Canal do modo performance` | Notas neste canal disparam clipes da Playlist no modo Performance |
| `Salto para marcador da música` | Notas neste canal levam a música a um marcador em vez de tocar |
| `Captura (modo takeover)` | Um controlador só move um parâmetro depois de alcançar o valor atual dele |
| Vínculos | Os vínculos de controlador deste projeto, depois os globais, com `Remover vínculo` e `Remover todos` |

## Geral

`Opções › Configurações gerais...`

| Opção | O que faz |
|---|---|
| `Estilo de layout` | Fluxo por padrões (padrão) ou fluxo por disparo de clipes |
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
| `Máximo de níveis de desfazer` | 25 a 1000 |
| Cópia de segurança | Com que frequência uma cópia de segurança é salva, quantas são mantidas por projeto, e `Abrir pasta das cópias` |
| `Rodar plugins em processo separado` | Um plugin que trava não derruba o LYNX SHARE. Desligado por padrão |
| `Substituir plugins ausentes por equivalentes do LYNX SHARE` | Ligado por padrão. O projeto mantém o plugin original |
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

## Veja também

- [Configuração de áudio](/wiki/audio-setup/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Atalhos](/wiki/shortcuts/)
- [Plugins](/wiki/plugins/)
- [Layouts](/wiki/layouts/)

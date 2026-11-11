---
source: 7ca5e689db
---
![A paleta de inserção rápida: instrumentos à esquerda, efeitos à direita, cada um marcado como embutido ou motor FL embutido](/assets/img/wiki/quickadd.webp)

O LYNX SHARE traz seus próprios instrumentos e efeitos, então um projeto toca sem nenhum plugin de terceiros. Há três tipos: originais do LYNX SHARE (um sintetizador, um sampler, um player de SoundFont, baterias, equalizadores, compressores, limitadores, reverbs, delays), motores escritos para tocar as configurações dos plugins nativos do FL Studio e efeitos que substituem plugins conhecidos de terceiros.

Cada um deles tem sua própria página no [catálogo](/instruments-and-effects/). `Ajuda › Plugins nativos (lista online)...` abre a lista com o status de cada motor.

## Adicionar um instrumento e um efeito

1. Pressione `F6` para mostrar o Channel Rack.
2. Clique na caixa `+ adicionar canal` abaixo da última linha e escolha um instrumento. O menu os agrupa por categoria.
3. Pressione `F9` para mostrar o Mixer e selecione o insert por onde o canal toca.
4. Clique em um slot vazio no painel de efeitos e escolha um efeito. A janela dele se abre.
5. Clique no nome do slot para ocultar ou mostrar essa janela de novo.

## O seletor de plugins

Pressione `F8` para abrir o seletor de plugins. Ele cobre a tela e mostra cada gerador e efeito como uma imagem do próprio editor, com as categorias como abas na parte de baixo.

| Comando | Tecla | O que faz |
|---|---|---|
| Busca | Digite | Escurece os plugins que não coincidem |
| Percorrer os plugins | `Up` / `Down` | Move o destaque |
| Adicionar | `Enter`, clique duplo ou clique com o botão direito | Um gerador vira um novo canal. Um efeito vai para o insert selecionado do Mixer |
| Arrastar uma imagem | | Para o Channel Rack, cria um novo canal; sobre um canal, substitui o instrumento dele; sobre um insert ou slot do Mixer, adiciona o efeito |
| Fechar | `Esc` ou `F8` | Fecha o seletor sem adicionar nada |

As imagens dos plugins do próprio LYNX SHARE são desenhadas pelo LYNX SHARE em segundo plano. Um plugin de terceiros mostra um bloco simples com o nome do fabricante até que o editor dele seja aberto uma vez.

`Editar › Paleta de inserção rápida...` abre uma lista simples: instrumentos à esquerda, efeitos à direita, filtrados conforme você digita. `Tab` alterna entre as listas e `Enter` adiciona o item destacado. O Banco de plugins do [Browser](/wiki/browser/) (`Shift+F8`) lista os mesmos plugins em forma de árvore.

![O Banco de plugins do Browser com a pasta FL engines aberta](/assets/img/wiki/browser-plugins.webp)

## Presets

A janela de um efeito embutido tem uma barra de presets no cabeçalho.

| Controle | O que faz |
|---|---|
| Setas | Preset anterior e seguinte |
| Nome do preset | Abre o menu: `Presets do LYNX SHARE`, `Seus presets`, `Presets do FL Studio` onde existirem, `Salvar preset...` e `Mostrar a pasta de presets` |
| `A` / `B` | Duas configurações para comparar. O botão ao lado delas copia a que você está ouvindo para a outra |

O nome do preset e as configurações A e B são salvos com o projeto.

## O que significa "abre as configurações do FL"

Os plugins nativos do FL Studio nunca são carregados. O LYNX SHARE tem um motor próprio para cada um deles, e esse motor lê as configurações que o plugin do FL salvou. Isso funciona em dois lugares:

- Um projeto `.flp`. Cada gerador e efeito do FL abre no motor correspondente do LYNX SHARE com as configurações salvas, e os controles continuam editáveis e automatizáveis.
- Um preset `.fst`. Dê um clique duplo em um preset de sintetizador no Browser para criar um novo canal, ou arraste um preset de efeito para um insert do Mixer. Clique com o botão direito em um slot de efeito preenchido para ver `Presets do FL`, que lista os presets encontrados na sua instalação do FL Studio.

O quanto cada motor se aproxima do original é indicado por motor, de Exact a Placeholder. Os níveis são explicados em [Vindo do FL Studio](/wiki/coming-from-fl-studio/).

## Motores que precisam da sua instalação do FL Studio

O LYNX SHARE não contém código, áudio nem dados de onda da Image-Line. Alguns motores precisam das ondas, samples, impulsos ou tabelas que pertencem ao plugin original e os leem da sua própria instalação do FL Studio enquanto rodam.

| Tipo | Motores |
|---|---|
| Instrumentos | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Efeitos | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Se os arquivos não forem encontrados, adicionar um desses motores pode falhar com uma mensagem dizendo que os arquivos de dados de uma instalação do FL Studio não foram encontrados ou não puderam ser lidos.

## Substitutos de plugins de terceiros

Quando um projeto usa um efeito de terceiros que não está instalado, um efeito do LYNX SHARE com a mesma função assume o slot: um reverb no lugar de um reverb, um delay no lugar de um delay.

- O slot mostra o nome do plugin original com um selo LYNX SHARE.
- Quando o estado salvo pode ser lido, as configurações são traduzidas. Caso contrário, o substituto começa de um preset padrão.
- O plugin original e suas configurações permanecem no projeto. Depois que o plugin for instalado, o projeto volta a abrir com ele.
- Clique com o botão direito em um slot substituto e escolha `Desativar todos os substitutos LYNX SHARE deste projeto` para ouvir o projeto sem eles.

A chave é `Substituir plugins ausentes por equivalentes do LYNX SHARE` em `Opções › Configurações gerais...`, ativada por padrão. Em um projeto novo, os mesmos efeitos ficam no menu do slot vazio, em `Equivalentes LYNX SHARE`.

## Bom saber

- Instrumentos ausentes ainda não são substituídos. Um plugin de instrumento ausente vira um sintetizador embutido, escolhido pelo nome do canal.
- A automação dos parâmetros do plugin original é mantida, mas não controla o substituto.
- Os pacotes FLEX são criptografados e nunca são abertos. Um canal FLEX recebe um patch de sintetizador escolhido pelo nome do preset.
- Um insert tem 10 slots de efeito.

## Veja também

- [Catálogo de instrumentos e efeitos](/instruments-and-effects/)
- [Plugins de terceiros](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Vindo do FL Studio](/wiki/coming-from-fl-studio/)

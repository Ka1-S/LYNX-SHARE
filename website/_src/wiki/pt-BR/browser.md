---
source: 8df354b49e
---
![O Browser à esquerda, com as três abas, a caixa de busca, a árvore de pastas e a faixa de pré-escuta](/assets/img/wiki/browser-tree.webp)

O Browser lista suas pastas de samples, o conteúdo do projeto atual e os plugins que o LYNX SHARE conhece. Pressione `Alt+F8` para mostrar ou ocultar. `F8` sozinho abre o seletor de plugins, uma vista separada em tela cheia com todos os instrumentos e efeitos.

## Carregar um sample no projeto

1. Pressione `Alt+F8` para mostrar o Browser.
2. Na aba `TUDO`, clique em uma pasta para abri-la.
3. Clique em um arquivo de áudio para ouvi-lo. `Up` e `Down` percorrem os arquivos e tocam cada um.
4. Dê um clique duplo no arquivo para criar um novo canal com o sample no [Channel Rack](/wiki/channel-rack/).
5. Ou arraste o arquivo para a [Playlist](/wiki/playlist/) para criar um clipe de áudio.

## Abas

| Aba | Mostra |
|---|---|
| ALL | O projeto atual, o Banco de plugins e todas as pastas de samples |
| PROJECT | Só o que o projeto atual usa: patterns, efeitos, geradores, automação, arquivos de áudio |
| STARRED | Os arquivos e pastas que você marcou com estrela (clique com o botão direito e escolha `Favorito`) |

## Navegar e pré-ouvir

- Clique em uma pasta para abri-la ou fechá-la. Clique com o botão direito para ver um menu (recolher tudo, mostrar no gerenciador de arquivos).
- Clique em um arquivo de áudio para ouvi-lo. A faixa de pré-escuta abaixo da lista mostra a forma de onda. Clique na faixa para ouvir o arquivo de novo, ou arraste a partir dela como se fosse o próprio arquivo.
- O painel acima da faixa de pré-escuta mostra a data, o tamanho e o caminho do arquivo selecionado.

## Usar um sample

| Ação | O que faz |
|---|---|
| Clique duplo | Novo canal com o sample |
| Arrastar para o Channel Rack | Novo canal, ou solte sobre uma linha para carregar o sample ali |
| Arrastar para a Playlist | Clipe de áudio |
| `Enter` | Carrega o arquivo no canal selecionado |
| `Shift+Up` / `Shift+Down` | Vai para o arquivo anterior ou seguinte e o carrega no canal selecionado |

O último atalho permite testar samples diferentes no próprio lugar enquanto o pattern toca.

## Outros tipos de arquivo

- **Projeto**: o clique duplo o abre.
- **Arquivo MIDI**: o clique duplo o abre como projeto.
- **SoundFont ou SFZ**: o clique faz a pré-escuta, o clique duplo cria um canal LYNX Soundfont (ou o carrega no canal selecionado).
- **Preset de Mixer**: o clique duplo o carrega nos inserts selecionados do Mixer, ou arraste-o para um insert.
- **Preset de efeito**: arraste-o para um insert do Mixer.

## Banco de plugins

A entrada Banco de plugins lista os instrumentos e efeitos embutidos do LYNX SHARE e os plugins instalados. `Shift+F8` vai direto a ela.

- Dê um clique duplo em um instrumento para adicioná-lo como novo canal, ou arraste-o para o Channel Rack.
- Dê um clique duplo em um efeito para adicioná-lo ao insert selecionado do [Mixer](/wiki/mixer/), ou arraste-o para um insert ou um slot de efeito.
- `F2` renomeia o plugin selecionado.
- Clique com o botão direito em um plugin e escolha `Mover para a pasta` para organizar os plugins em pastas. O menu de slot do Mixer e o menu de adicionar do Channel Rack passam então a listar cada pasta de nível superior.

## A aba PROJECT

Os patterns listados aqui podem ser clicados para torná-los o pattern atual, abertos com clique duplo no [Piano Roll](/wiki/piano-roll/), ou arrastados para a Playlist. A seção do projeto atual também lista o histórico de desfazer: clique em um passo para voltar a ele.

## Busca

Pressione `Ctrl+F` para colocar o cursor na caixa de busca. Ela pesquisa na pasta selecionada (ou em todas as pastas) e abaixo. Na aba PROJECT, pesquisa os itens do projeto.

Todas as palavras precisam coincidir. A caixa também aceita `OR`, um sinal de menos para excluir uma palavra e curingas `*`:

```
kick -loop
snare OR clap
hat*.wav
```

As pastas são indexadas em segundo plano. `Atualizar o índice de busca` no menu do Browser reconstrói o índice.

## Suas próprias pastas

Use `Adicionar pasta` na lista de pastas do Browser para adicionar uma pasta de samples, projetos ou SoundFonts. As pastas de busca ficam em `Opções › Configurações de arquivos (pastas de busca)`.

## Teclas

As setas, `Enter` e `F2` exigem que o Browser esteja com o foco.

| Comando | Tecla | O que faz |
|---|---|---|
| Mostrar o Browser | `Alt+F8` | Mostra ou oculta o Browser |
| Seletor de plugins | `F8` | Abre o seletor de plugins em tela cheia |
| Banco de plugins | `Shift+F8` | Vai direto à entrada Banco de plugins |
| Busca | `Ctrl+F` | Coloca o cursor na caixa de busca |
| Percorrer e pré-ouvir | `Up` / `Down` | Seleciona o arquivo anterior ou seguinte e o toca |
| Fechar pasta | `Left` | Fecha uma pasta ou vai para a pasta pai |
| Abrir pasta | `Right` | Abre uma pasta |
| Carregar | `Enter` | Carrega o arquivo no canal selecionado |
| Percorrer e carregar | `Shift+Up` / `Shift+Down` | Vai para o arquivo anterior ou seguinte e o carrega no canal selecionado |
| Renomear plugin | `F2` | Renomeia o plugin selecionado no Banco de plugins |

## Bom saber

- Remover uma pasta da lista não a apaga do disco.
- `F8` e `Alt+F8` são coisas diferentes. `F8` é o seletor de plugins, `Alt+F8` é o Browser.
- Os resultados da busca vêm de um índice construído em segundo plano. `Atualizar o índice de busca` no menu do Browser o reconstrói.
- `Enter` carrega o arquivo no canal selecionado. O clique duplo cria um novo canal.

## Veja também

- [Channel Rack](/wiki/channel-rack/)
- [Plugins de terceiros](/wiki/plugins/)
- [Formatos de arquivo](/wiki/file-formats/)
- [Desfazer e histórico](/wiki/undo-history/)
- [Instrumentos e efeitos](/instruments-and-effects/)

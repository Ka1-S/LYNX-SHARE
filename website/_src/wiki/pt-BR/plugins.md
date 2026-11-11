---
source: be0fc22373
---
![A parte de baixo do Gerenciador de plugins, com a nota de que cada arquivo de plugin é examinado em um processo separado](/assets/img/wiki/plugin-manager-scan.webp)

O LYNX SHARE hospeda plugins de instrumento e de efeito de outros fabricantes. Ele os encontra examinando as pastas de plugins, e `Opções › Gerenciador de plugins...` mostra o que foi encontrado. Os instrumentos e efeitos do próprio LYNX SHARE estão descritos em [Instrumentos e efeitos embutidos](/wiki/built-in/).

## Examinar e adicionar um plugin

1. Escolha `Opções › Procurar plugins`. O progresso aparece na barra de dicas e você pode continuar trabalhando.
2. Se o plugin está em uma pasta só dele, abra `Opções › Gerenciador de plugins...`, clique em `Pastas de plugins...`, adicione a pasta e depois clique em `Buscar plugins novos`.
3. Pressione `F8` para o seletor de plugins e digite parte do nome do plugin.
4. Dê um clique duplo nele ou pressione `Enter`. Um instrumento vira um novo canal. Um efeito vai para o insert selecionado do Mixer.

Na primeira execução, o assistente de configuração lista as pastas de plugins e as examina em segundo plano. `Opções › Assistente de configuração (idioma, áudio, pastas de plugins)...` o abre de novo. O LYNX SHARE também procura plugins novos alguns segundos depois de cada início.

## Formatos por sistema

| Sistema | Formatos hospedados |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

O VST2 não é hospedado no macOS. No Linux, plugins do Windows funcionam por meio do [yabridge](https://github.com/robbert-vdh/yabridge) e do Wine, que você mesmo instala: adicione a pasta que o yabridge sincroniza como pasta de plugins.

## O Gerenciador de plugins

| Controle | O que faz |
|---|---|
| Caixa de busca | Filtra por nome, fabricante, formato ou arquivo |
| `Todos`, `Instrumentos`, `Efeitos`, `Favoritos`, `Com falha` | Escolhem quais plugins são listados |
| Coluna da estrela | Marca um favorito |
| Coluna `Bits` | 32, 64 ou 32+64 (Windows) |
| Coluna `Estado` | `OK`, `Arquivo não encontrado` ou `Falhou ao carregar` |
| `Buscar plugins novos` | Procura arquivos que ainda não estão na lista |
| `Examinar tudo de novo...` | Esquece os arquivos com falha e examina todos os arquivos de novo |
| `Pastas de plugins...` | Edita as pastas que são examinadas |
| `Adicionar arquivo de plugin...` | Adiciona um arquivo de plugin |

Cada arquivo de plugin é examinado em um processo separado. Um arquivo que trava ou fica parado por 30 segundos é marcado como falho e ignorado depois. No Windows, os arquivos com falha são listados com o motivo em `%APPDATA%\LYNX\scan-failures.log`.

As pastas padrão são examinadas, mais as que você adicionar:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` e `Steinberg\VSTPlugins` em Program Files, e as pastas que o FL Studio usa, se estiver instalado.
- macOS: `VST3`, `Components` e `CLAP` em `~/Library/Audio/Plug-Ins` e `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst` e as mesmas pastas em `/usr/lib` e `/usr/local/lib`.

## Janelas de plugin acopladas e destacadas

O editor de um plugin abre dentro da janela principal, como os outros painéis. `Destacada` no menu da janela do plugin dá a esse plugin uma janela própria, e `Fixar no topo` no mesmo menu mantém uma janela destacada acima das outras janelas do LYNX SHARE.

`Destacar todos os plugins novos` em `Opções › Configurações gerais...` abre destacado todo plugin que você adiciona. `Ferramentas › Alternar todos os editores de plugins para` define `Encaixada` ou `Destacada` para todos os plugins do projeto.

## Rodar plugins em um processo separado

Por padrão, um plugin roda dentro do LYNX SHARE, então um plugin que trava pode derrubar o programa. Como opção, cada plugin roda no seu próprio processo.

1. Abra `Opções › Configurações gerais...`.
2. Ative `Rodar plugins em processo separado (protege o LYNX SHARE de travamentos)`.
3. Reabra o projeto para aplicar isso aos plugins que já estão abertos.

Se um plugin assim trava ou para de responder, ele é silenciado, uma mensagem aparece e o LYNX SHARE continua rodando. `Recarregar` nessa mensagem inicia o plugin de novo com as configurações do último salvamento ou backup, sem reabrir o projeto. O editor do plugin abre em uma janela própria e não pode ser acoplado nem fixado.

Para fazer isso com um plugin só, use `Rodar em um processo separado` ou `Always run <plugin> in a separate process` no menu wrapper da janela do plugin. Quando o editor de um plugin trava, a mensagem que aparece oferece também a segunda opção.

Isso funciona no Windows e no Linux, e no macOS desde a 0.1.25.

Um plugin que trava dentro do LYNX SHARE é desligado, e a mensagem dele também oferece `Recarregar`. Depois, use `Recarregar o plugin que travou` no menu do canal ou no menu do slot de efeito.

## Plugins de 32 bits (só no Windows)

No Windows, plugins VST2 e VST3 de 32 bits rodam por uma ponte (`LYNX-bridge32.exe`). A ponte não acrescenta latência. Um plugin que trava nela para sozinho enquanto o LYNX SHARE continua tocando. O editor de um plugin em ponte abre em uma janela própria e não pode ser acoplado nem fixado. Quando há uma build de 32 bits e uma de 64 bits do mesmo plugin instaladas, `Tamanho em bits` em `Configurações do wrapper`, na janela do plugin, escolhe qual delas carrega.

## Monitor de desempenho de plugins

![O Monitor de desempenho dos plugins: uma linha por instrumento e efeito com sua parte do tempo de processamento](/assets/img/wiki/plugin-monitor.webp)

`Ferramentas › Monitor de desempenho dos plugins...` lista cada instrumento e efeito com o tempo de processamento que consome: agora, na média e no pico. 100% significa um núcleo do processador ocupado durante todo o tempo que o áudio dura.

| Configuração | O que faz |
|---|---|
| `Buscar` | Filtra por plugin, canal ou insert |
| `Só plugins` | Oculta os instrumentos e efeitos do próprio LYNX SHARE |
| `Congelar` | Para de atualizar os números |
| `Zerar ao tocar` | Recomeça médias e picos cada vez que a reprodução começa |
| `Restaurar` | Zera médias e picos agora |

## Enviar as saídas de um instrumento para inserts separados

Um plugin de instrumento com várias saídas pode tocar cada uma em um insert próprio do Mixer. Na janela do plugin, clique em `Configurações do wrapper` e depois na aba `Processamento`. Em `Conexões`, cada saída tem uma luz que a liga ou desliga e um número: em quantos inserts depois do insert do próprio canal ela toca. `Mapear saídas automaticamente` dá a cada saída o insert seguinte, e `Restaurar` volta ao padrão.

## A pasta Plugin database

As pastas da Plugin database do [Browser](/wiki/browser/), o seletor de plugins e os menus de plugins são uma pasta no disco: `LYNX/Plugin database` na sua pasta Documentos. Para abri-la, clique com o botão direito em `Banco de plugins` no Browser e escolha `Abrir`.

- `Efeitos` e `Geradores` guardam um arquivo `.lynxplugin` por plugin. Crie pastas e subpastas (até 4 níveis) e mova os arquivos para dentro delas.
- Renomeie um arquivo para renomear o plugin nos menus. Exclua um arquivo para tirar o plugin dos menus. Ele não é desinstalado.
- `Instalados` tem uma entrada para cada plugin. Copie uma para `Efeitos` ou `Geradores` para trazer um plugin de volta.

## Abrir um projeto sem os plugins

`Arquivo › Abrir sem plugins de terceiros...` abre um projeto do LYNX SHARE com todos os plugins de terceiros desligados, para um projeto que não abre ou trava ao carregar. As configurações salvas dos plugins são mantidas. `Arquivo › Reativar plugins...` liga todos de novo.

## Quando um plugin não está instalado

O LYNX SHARE procura primeiro o mesmo plugin pelo identificador, depois pelo arquivo e depois pelo nome. Um projeto salvo com o VST2 abre com o VST3 do mesmo produto, e vice-versa, mas então o plugin toca com as configurações padrão.

Se um efeito não está instalado de jeito nenhum, um efeito do LYNX SHARE com a mesma função assume o slot e o original permanece no projeto. Quando as configurações salvas do plugin podem ser lidas, o substituto começa com configurações equivalentes. A chave é `Substituir plugins ausentes por equivalentes do LYNX SHARE` em `Opções › Configurações gerais...`. Veja [Instrumentos e efeitos embutidos](/wiki/built-in/). `Ajuda › Arquivos ausentes...` lista os plugins que foram substituídos.

## Bom saber

- A janela de um plugin hospedado tem uma barra de presets: os programas do próprio plugin, seus presets, `Salvar preset...`, `Carregar arquivo de preset...` e `Exportar arquivo de preset...`. Arquivos `.fxp`, `.fxb` e `.vstpreset` só carregam em um plugin que roda dentro do LYNX SHARE, não em um processo separado.
- O monitor mede só enquanto a janela dele está aberta.
- Enquanto a janela de um plugin tem o foco, uma tecla que o plugin não usa toca o teclado de piano de digitação. No Windows, com o botão de teclado do cabeçalho da janela desligado, o LYNX SHARE trata as teclas antes do plugin, atalhos inclusive.
- O menu de parâmetros de um plugin VST3 começa com os itens do LYNX SHARE, como `Criar clipe de automação`. Veja [Automação](/wiki/automation/).
- `MPE` em `MIDI`, nas `Configurações do wrapper` de um plugin, envia cada nota em seu próprio canal MIDI. Veja [Controladores MIDI](/wiki/midi-controllers/).
- Instrumentos ausentes ainda não são substituídos. Um plugin de instrumento ausente vira um sintetizador embutido, escolhido pelo nome do canal.

## Veja também

- [Instrumentos e efeitos embutidos](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Solução de problemas](/wiki/troubleshooting/)

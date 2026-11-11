---
source: a6d9063ec3
---
O LYNX SHARE é uma DAW gratuita e de código aberto para Windows, macOS e Linux. Ele trabalha com patterns: você os programa no Channel Rack e no Piano Roll, os arranja na Playlist e os mixa no Mixer. Abre projetos do FL Studio (`.flp`) e sets do Ableton Live (`.als`) e salva no seu próprio formato `.lynx`.

É uma versão alfa. Espere bugs e salve com frequência.

## Colocar para rodar

1. Confira a tabela abaixo para ver se o seu sistema é compatível.
2. Baixe e instale. [Instalação](/wiki/install/) tem os passos para cada sistema.
3. Inicie o programa. Um assistente de configuração curto escolhe o idioma, testa a saída de áudio e lista as pastas de plugins, que são então examinadas em segundo plano. Você pode pulá-lo e reabri-lo depois com `Opções › Assistente de configuração (idioma, áudio, pastas de plugins)...`.
4. Se você não ouvir nada, vá para [Configuração de áudio](/wiki/audio-setup/).
5. Leia [Um passeio pela janela](/wiki/tour/) e depois faça [Sua primeira música](/wiki/first-song/).

## O que você precisa

| Sistema | Requisito |
|---|---|
| Windows | Windows 10 ou 11, 64 bits |
| macOS | macOS 11 Big Sur ou mais recente, Intel ou Apple silicon |
| Linux | x86_64 com glibc 2.35 ou mais recente (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch ou Manjaro atuais), X11 ou XWayland |

Você não precisa de uma interface de áudio. O LYNX SHARE toca pelo dispositivo de som que o seu sistema já usa. Também não precisa do FL Studio. Se ele estiver instalado no mesmo computador, o LYNX SHARE encontra os samples dele e usa os mesmos arquivos de plugin para projetos `.flp`.

Plugins de terceiros são opcionais. O LYNX SHARE tem seus próprios instrumentos e efeitos, listados no [catálogo](/instruments-and-effects/).

## Salvar o seu trabalho

`Ctrl+S` salva, `Ctrl+Shift+S` salva com um novo nome e `Ctrl+Alt+S` salva uma nova versão (Song_2, Song_3 e assim por diante). Há um salvamento automático, acessível por `Arquivo › Abrir pasta de salvamento automático` e `Arquivo › Voltar ao último backup...`.

`Arquivo › Novo a partir de modelo` tem quatro pontos de partida: Empty, Basic beat, Trap with 808 e House.

## O projeto de demonstração

O instalador e o zip do Windows, o `.dmg` do macOS e o `.tar.gz` do Linux incluem um projeto de demonstração, `Demo.lynx`. Abra-o com `Arquivo › Abrir...` (`Ctrl+O`). No Windows ele fica na pasta de instalação, ao lado de `LYNX.exe`.

Você também pode gerá-lo a partir de um terminal. Veja [Linha de comando](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Para onde ir depois

| Você quer | Leia |
|---|---|
| Aprender cada janela | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Abrir um projeto de outro programa | [Vindo do FL Studio](/wiki/coming-from-fl-studio/), [Vindo do Ableton Live](/wiki/coming-from-ableton/) |
| Usar seus plugins VST, CLAP, LV2 ou AU | [Plugins](/wiki/plugins/) |
| Consultar uma tecla | [Atalhos](/wiki/shortcuts/) |
| Resolver um problema | [Solução de problemas](/wiki/troubleshooting/) e o [FAQ](/faq/) |

## Bom saber

- O arquivo do programa se chama `LYNX.exe` no Windows e `LYNX` no macOS e no Linux. A pasta de configurações também se chama `LYNX`.
- No Windows e no macOS, a primeira execução pode mostrar um aviso do sistema, porque a versão para Windows ainda não tem assinatura de código e a versão para macOS não é notarizada. [Instalação](/wiki/install/) explica em que clicar.
- A interface fica em inglês por padrão em um sistema cujo idioma o LYNX SHARE não tem. Você pode mudar em `Opções › Configurações gerais...`. Um novo idioma passa a valer após reiniciar.
- Os pacotes FLEX em projetos do FL Studio são criptografados e nunca são abertos. Esses canais recebem um patch de sintetizador escolhido pelo nome do preset.

## Veja também

- [Instalação](/wiki/install/)
- [Configuração de áudio](/wiki/audio-setup/)
- [Um passeio pela janela](/wiki/tour/)
- [Sua primeira música](/wiki/first-song/)
- [Download](/download/)

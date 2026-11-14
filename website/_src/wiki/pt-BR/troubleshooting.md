---
source: a3748d4a2a
---
Cada título abaixo é um problema. Se o seu não está aqui, relate-o como descrito no final.

## Iniciar o programa

### Windows: o SmartScreen avisa na primeira execução

O executável ainda não tem assinatura de código, então o SmartScreen pode avisar na primeira vez que você o executar.

1. Clique em `More info`.
2. Clique em `Run anyway`.

O instalador não precisa de direitos de administrador.

### macOS: "não pode ser aberto"

O app não é notarizado pela Apple e não tem assinatura de Developer ID, então a primeira execução exige um passo extra.

1. Arraste o LYNX SHARE para `Applications`.
2. Clique com o botão direito (ou com Control) nele e escolha `Abrir`.
3. Clique em `Abrir` de novo.

No macOS 15 Sequoia ou mais recente, tente abrir uma vez, depois vá a `System Settings › Privacy & Security` e clique em `Open Anyway`. Ou, no Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

O macOS pede acesso ao microfone na primeira vez que você grava.

### Linux: o AppImage não inicia

O LYNX SHARE precisa de um sistema x86_64 com glibc 2.35 ou mais recente (Ubuntu 22.04, Debian 12, Fedora 36, Arch ou Manjaro atuais) e X11 ou XWayland.

1. Torne o arquivo executável e execute-o, com os dois comandos abaixo.
2. Se ainda não iniciar, instale o libfuse2. O pacote é `libfuse2` no Ubuntu 22.04, `libfuse2t64` no Ubuntu 24.04, `fuse2` no Arch e `fuse-libs` no Fedora.
3. Se preferir não instalá-lo, baixe o `.tar.gz`, extraia e execute `./install.sh`.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` instala o LYNX SHARE para o seu usuário em `~/.local`, com uma entrada de menu. `./install.sh --uninstall` o remove.

## Som

### Sem som, ou o dispositivo de áudio errado

1. Abra `Opções › Configurações de áudio...`.
2. Escolha um driver.
3. Escolha um dispositivo de saída.

| Sistema | Drivers |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO e o driver LYNX ASIO embutido |
| macOS | Core Audio |
| Linux | PipeWire (nativo), JACK e ALSA. Na primeira execução, o LYNX SHARE abre o primeiro deles que tiver dispositivos |

ASIO é um modelo de driver do Windows e existe só no Windows. `Opções › Assistente de configuração (idioma, áudio, pastas de plugins)...` também testa o dispositivo de áudio. Veja [Configuração de áudio](/wiki/audio-setup/).

### Linux: o som falha

A thread de áudio deve rodar com prioridade de tempo real.

1. Abra `Opções › Configurações de áudio...` e leia `Estado`. Ele mostra se a thread obteve prioridade de tempo real.
2. Se não obteve, instale o rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit` ou `sudo pacman -S rtkit`), ou entre no grupo `áudio` com um limite de tempo real, como nas configurações de JACK.
3. Saia da sessão e entre de novo.

Os detalhes estão em [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

### O som estala ou falha

O LYNX SHARE conta cada falha (buffer underrun) e diz por que ela aconteceu. Aponte para o medidor `CPU` na barra de ferramentas, ou abra `Ferramentas › Monitor de desempenho dos plugins...`. Um número vermelho ao lado da porcentagem de CPU é a contagem.

A dica diz a causa da última falha:

| Causa | O que significa | O que tentar |
|---|---|---|
| O processamento levou mais tempo que o buffer | Um bloco de áudio levou mais tempo para ser calculado do que o tempo que ele dura. A dica diz o canal ou efeito mais lento daquele bloco | Aumente o tamanho do buffer em `Opções › Configurações de áudio...`, congele esse canal ou baixe a qualidade do plugin |
| O processamento chegou perto do limite do buffer | A carga estava perto de 100 % na hora da falha | Aumente o tamanho do buffer ou congele os canais mais pesados |
| Latência do driver ou do sistema | O driver ou o sistema atrasou enquanto a carga estava baixa. Nenhum plugin causou a falha | Aumente o tamanho do buffer, tente outro driver (no Windows, o driver ASIO da interface) e, no Linux, verifique a prioridade de tempo real (acima) |

Cada falha também é registrada em `audio-glitches.log` na pasta de configurações, com o dispositivo, o buffer e os canais mais pesados.

## Plugins

### Um plugin trava

A maioria dos travamentos de plugin desliga só aquele plugin: um efeito fica em bypass, um instrumento fica mudo, e o LYNX SHARE e o projeto continuam rodando. A mensagem `O plugin travou` então oferece:

- `Recarregar`, que inicia o plugin de novo com as configurações do último salvamento ou backup.
- `Sempre rodar <plugin> em um processo separado`, que o inicia de novo em um processo próprio, agora e em todo projeto. Um travamento lá não derruba o LYNX SHARE. Um plugin em processo separado abre a janela dele sozinha (ela não pode ser encaixada).

A janela do plugin mostra os mesmos dois botões no lugar do editor. `Opções › Gerenciador de plugins...` lista na coluna `Processo separado` os plugins que sempre rodam em processo separado; clique na caixa para ligar ou desligar um deles.

Para rodar todos os plugins no próprio processo:

1. Abra `Opções › Configurações gerais...`.
2. Ative `Rodar plugins em processo separado (protege o LYNX SHARE de travamentos)`.
3. Reabra o projeto.

Um plugin que travou ou parou de responder no próprio processo é silenciado. Salve o projeto e reabra-o para carregar o plugin de novo. Veja [Plugins de terceiros](/wiki/plugins/).

### Um plugin não está na lista

1. Escolha `Opções › Procurar plugins`.
2. Se ainda estiver faltando, abra `Opções › Gerenciador de plugins...`, clique em `Pastas de plugins...` e adicione a pasta do plugin.
3. Olhe a aba `Com falha`. Um arquivo de plugin que falha no exame é ignorado. No Windows, o motivo está em `%APPDATA%\LYNX\scan-failures.log`.
4. Clique em `Examinar tudo de novo...` para tentar os arquivos com falha de novo.

### Um projeto usa um plugin que não está instalado

Um efeito é substituído por um efeito do LYNX SHARE com a mesma função, e as configurações dele são mantidas no projeto. `Ajuda › Arquivos ausentes...` lista o que está faltando. Veja [Instrumentos e efeitos embutidos](/wiki/built-in/).

### O vídeo não funciona no Linux

Faixas de vídeo e `Export with video` precisam do FFmpeg. O LYNX SHARE diz como instalá-lo.

## Relatórios de falha

![A pergunta sobre o relatório de falha mostrada na próxima execução, com Send, Show what will be sent e Don't send](/assets/img/wiki/crash-report.webp)

Os relatórios de falha são opcionais. Depois de uma falha ou de um congelamento, a próxima execução pergunta se deve enviar um relatório.

| Comando | O que faz |
|---|---|
| `Mostrar o que será enviado` | Mostra o texto exato |
| `Relatar no GitHub em vez disso` | Abre o formulário de issue do GitHub, já preenchido. Você anexa os arquivos por conta própria |
| `Abrir a pasta com o relatório` | Mostra os arquivos do relatório no seu computador |
| `Include file names (projects, samples)` | Desligado, só a extensão de cada arquivo é enviada |
| `Sempre enviar sem perguntar` | Envia os relatórios futuros sem a pergunta |

`Relatórios de falha` em `Opções › Configurações gerais...` define `Ask` (o padrão), `Sempre enviar` ou `Nunca enviar`.

Um relatório contém a versão do LYNX SHARE, o sistema operacional, a CPU, as placas de vídeo e seus drivers, o tamanho da tela, o driver e o dispositivo de áudio, o arquivo da falha ou do congelamento, os últimos 60 passos da sessão e os nomes e versões dos plugins de terceiros que estavam carregados.

Não contém áudio, projetos, samples, o nome do seu computador nem o seu nome de usuário. O servidor não guarda nenhum endereço IP. Os ids enviados são listados em `crash-reports\sent.log` na pasta de configurações. Para que um relatório seja apagado, abra uma issue com o id dele. O texto completo está na [política de privacidade](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Relatar um problema

![A janela Report a problem: uma caixa de descrição, um endereço de e-mail opcional e o botão Send](/assets/img/wiki/report-problem.webp)

1. Escolha `Ajuda › Relatar um problema...`.
2. Descreva o que aconteceu. O endereço de e-mail é opcional, para quando você quiser uma resposta.
3. Deixe `Anexar os arquivos de falha / travamento mais recentes e os passos desta sessão` marcado se o problema foi uma falha.
4. Clique em `Mostrar o que será enviado` para conferir o texto.
5. Clique em `Enviar`. Nada é enviado antes disso.

Você também pode abrir uma issue em [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## Bom saber

- Depois de uma falha, cópias de segurança do seu projeto ficam na pasta de salvamento automático do LYNX SHARE.
- Quando um plugin esteve envolvido na falha, a pergunta oferece rodar esse plugin em um processo separado daqui em diante.
- A pasta de configurações é `%APPDATA%\LYNX` no Windows, `~/Library/Application Support/LYNX` no macOS e `~/.config/LYNX` no Linux.

## Veja também

- [Configuração de áudio](/wiki/audio-setup/)
- [Plugins de terceiros](/wiki/plugins/)
- [Instalação](/wiki/install/)
- [FAQ](/faq/)
- [Proteção contra falhas](/why/#crash)

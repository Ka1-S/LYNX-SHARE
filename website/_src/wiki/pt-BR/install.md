---
source: 8a120ab7de
---
Cada versão tem builds para Windows, macOS e Linux na [página de releases](https://github.com/Ka1-S/LYNX-SHARE/releases). Os arquivos são compilados a partir do código-fonte marcado com a tag pelo GitHub Actions e listados com o SHA-256 em `SHA256SUMS.txt`. A página [Download](/download/) leva aos atuais.

## Instalar no Windows

Windows 10 ou 11, 64 bits.

1. Baixe `LYNX-<version>-setup-x64.exe`.
2. Execute-o. Ele instala só para o seu usuário e não precisa de direitos de administrador.
3. O SmartScreen pode avisar, porque o executável ainda não tem assinatura de código. Escolha `More info` e depois `Run anyway`.
4. Inicie o LYNX SHARE pelo menu Iniciar ou pelo atalho da área de trabalho.

O instalador faz do LYNX SHARE o programa que abre projetos `.lynx`. Ele oferece o LYNX SHARE em *Abrir com* para arquivos `.flp`, sem mudar o padrão do FL Studio.

Há duas outras formas:

| Forma | Arquivo ou lugar | O que faz |
|---|---|---|
| Zip portátil | `LYNX-<version>-windows-x64.zip` | Extraia em qualquer lugar e execute `LYNX.exe`. Nada é instalado. |
| Microsoft Store | [LYNX SHARE na Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | A Store instala e mantém atualizado. |

## Instalar no macOS

macOS 11 Big Sur ou mais recente. Um único `LYNX-<version>-macos-universal.dmg` cobre Intel e Apple silicon.

1. Abra o `.dmg` e arraste o LYNX SHARE para *Aplicativos*.
2. O app não é notarizado pela Apple e não tem assinatura de Developer ID, então a primeira execução exige um passo extra. Clique com o botão direito (ou com Control) no LYNX SHARE em *Aplicativos*, escolha *Abrir* e depois *Abrir* de novo.
3. No macOS 15 Sequoia ou mais recente, tente abrir uma vez, depois vá a *Ajustes do Sistema › Privacidade e Segurança* e clique em *Abrir Mesmo Assim*.

A alternativa pelo Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

O macOS pede acesso ao microfone na primeira vez que você grava.

## Instalar no Linux

x86_64 com glibc 2.35 ou mais recente (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch ou Manjaro atuais). O LYNX SHARE precisa de X11 ou XWayland. O áudio passa pelo PipeWire, JACK ou ALSA, e nada extra precisa ser instalado para isso.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Algumas distribuições precisam do libfuse2 para executar AppImages (`libfuse2` no Ubuntu 22.04, `libfuse2t64` no 24.04, `fuse2` no Arch, `fuse-libs` no Fedora). Na primeira execução, o assistente de configuração se oferece para adicionar o LYNX SHARE ao menu de aplicativos e para abrir arquivos `.lynx` com ele. Você pode mudar isso depois em `Opções › Configurações gerais...`, ou por um terminal com `./LYNX-*.AppImage --menu-entry on` (ou `desligado`).

Se preferir não instalar o libfuse2, use `LYNX-<version>-linux-x86_64.tar.gz`: extraia e execute `./install.sh`. Isso instala o LYNX SHARE para o seu usuário em `~/.local`, com uma entrada de menu, um ícone e o tipo de arquivo `.lynx`.

## Onde as configurações ficam

| Sistema | Pasta de configurações |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (ou sob `$XDG_CONFIG_HOME`) |

A pasta guarda suas opções, o idioma escolhido (`language.txt`), seus atalhos (`keys.json`), suas pastas de plugins (`plugin-folders.txt`) e a lista de plugins examinados (`plugins.xml`).

## Atualizar

Ao iniciar, o LYNX SHARE pergunta ao GitHub o número da versão mais nova. Nada sobre você é enviado. Quando sai uma versão nova, uma barra mostra `LYNX SHARE <version> is available` com um botão `Novidades` e um botão de atualizar. `Ajuda › Procurar atualizações...` consulta a qualquer momento.

| Build | Como atualiza |
|---|---|
| Instalador do Windows | `Atualizar` baixa o novo instalador, pergunta sobre alterações não salvas, instala e reinicia |
| Zip portátil do Windows, macOS | `Atualizar` baixa o novo arquivo e o mostra na pasta dele |
| AppImage do Linux | `Atualizar` atualiza o arquivo AppImage no lugar. Gear Lever e AppImageUpdate fazem o mesmo por fora |
| `.tar.gz` do Linux | `Baixar...` abre a página de download |
| Microsoft Store | A Store atualiza. Esta build não tem verificação de atualização própria |

Para desligar a verificação, abra `Opções › Configurações gerais...` e desmarque `Procurar atualizações automaticamente`.

## Remover

- Instalador do Windows: desinstale o LYNX SHARE pela lista *Aplicativos e recursos* do Windows.
- Zip portátil: apague a pasta que você extraiu.
- `.tar.gz` do Linux: execute `./install.sh --uninstall`.

## Bom saber

- As builds do Windows não são assinadas até que o pedido de assinatura de código do projeto seja aceito. O app do macOS tem só uma assinatura ad hoc. Os pacotes do Linux não são assinados.
- Para verificar um download, compare o SHA-256 dele com a linha em `SHA256SUMS.txt` na página da release.
- O instalador, o zip, o `.dmg` e o `.tar.gz` incluem o projeto de demonstração `Demo.lynx`.
- Fechar a barra de atualização com o botão de fechar ignora aquela versão.

## Veja também

- [Comece aqui](/wiki/getting-started/)
- [Configuração de áudio](/wiki/audio-setup/)
- [Solução de problemas](/wiki/troubleshooting/)
- [Download](/download/)
- [Atualizações](/updates/)

---
source: 9f36c60cc7
---
![A janela do histórico de desfazer: seis passos de Start a Add notes, com Mudo: Kick como o atual](/assets/img/wiki/history.webp)

Toda edição no projeto é um passo no histórico de desfazer: uma nota que você adiciona, um clipe que você move, um fader, uma mudança de roteamento, o tempo. `Editar › Histórico de desfazer...` (`Ctrl+Alt+H`) mostra os passos em uma lista. Separadamente, o LYNX SHARE salva cópias de segurança do projeto enquanto você trabalha.

## Voltar a um passo anterior

1. Escolha `Editar › Histórico de desfazer...`, ou aperte `Ctrl+Alt+H`.
2. A lista mostra os passos com o mais novo embaixo. A primeira linha, `Início`, é o projeto como foi aberto.
3. Clique em um passo. O projeto volta ao estado logo depois dele, e os passos seguintes ficam cinza.
4. Clique em um passo cinza para avançar de novo.
5. Faça uma nova edição para continuar a partir do passo escolhido. Os passos cinza então desaparecem.

## As teclas de desfazer

`Ctrl+Z` não funciona como na maioria dos programas. Ele desfaz o último passo e, apertado de novo, o refaz, alternando entre os dois últimos estados. Para voltar mais, use `Ctrl+Alt+Z`.

| Comando | Tecla | O que faz |
|---|---|---|
| `Desfazer` | `Ctrl+Z` | Desfaz o último passo. Depois de um desfazer, refaz o passo seguinte, e o item do menu passa a mostrar `Desfazer (refazer)` |
| `Desfazer para trás` | `Ctrl+Alt+Z` | Sempre volta, um passo por toque |
| `Refazer` | `Ctrl+Y` ou `Ctrl+Shift+Z` | Avança um passo |
| `Histórico de desfazer...` | `Ctrl+Alt+H` | Abre a lista |

Depois de cada desfazer ou refazer, a barra de dicas nomeia o passo e mostra o nível, por exemplo `Level 2/3`.

Se você prefere o comportamento usual, ligue `Modo de desfazer alternativo (desfazer clássico, em vez de desfazer/refazer no mesmo atalho)` em `Opções › Configurações gerais...`. `Ctrl+Z` então sempre volta um passo.

## O que é um passo e o que não é

- Edições no projeto são passos: notas, patterns, clipes, canais, níveis e roteamento do mixer, efeitos, automação, tempo, mutes.
- Uma gravação é um passo. Também é mesclar uma parte em uma [sessão online](/wiki/online-sessions/) e `Fazer comp destas tomadas`.
- O que não altera o projeto não é passo: zoom, rolagem, mover janelas e as configurações do próprio programa.
- Salvar e exportar não são desfeitos. Os arquivos que eles gravaram continuam lá.
- As janelas dos plugins próprios do LYNX SHARE têm um desfazer e refazer separado para as alterações daquele plugin, ao lado das configurações A e B.

## Configurações

As duas estão em `Opções › Configurações gerais...`.

| Opção | O que faz |
|---|---|
| `Máximo de níveis de desfazer` | Quantos passos são mantidos: 25, 50, 100, 200, 500 ou 1000. O padrão é 100 |
| `Modo de desfazer alternativo` | Faz de `Ctrl+Z` um desfazer clássico |

## Cópias de segurança

O LYNX SHARE salva uma cópia de segurança do projeto aberto a cada 2 minutos por padrão e mantém as 10 mais novas por projeto. Uma cópia é um arquivo `.lynx` normal na pasta `autosave`, dentro da pasta de configurações, com o nome do projeto seguido de `(autosaved on ...)` e a data e a hora.

Cada vez que você salva por cima de um arquivo de projeto existente, a versão substituída é copiada para a mesma pasta com `(overwritten on ...)` no nome.

| Comando | O que faz |
|---|---|
| `Arquivo › Voltar ao último backup...` | Substitui o projeto aberto pela cópia mais nova dele, depois de perguntar. O arquivo do projeto só muda quando você salva |
| `Arquivo › Abrir pasta de salvamento automático` | Mostra as cópias no gerenciador de arquivos |
| `Arquivo › Salvar nova versão` (`Ctrl+Alt+S`) | Salva como `Song_2`, `Song_3` e assim por diante, ao lado do arquivo atual |

A seção `Cópia de segurança` de `Opções › Configurações gerais...` tem estas configurações:

| Opção | O que faz |
|---|---|
| `Salvar uma cópia de segurança` | `Nunca`, ou um intervalo em minutos |
| `Cópias mantidas por projeto` | 1, 3, 5, 10, 20 ou 50 |
| `Esperar para fazer backups enquanto a música toca ou um teclado MIDI está sendo tocado` | Segura uma cópia para não interromper você |
| `Espera máxima entre backups` | 5 a 60 minutos. Uma cópia é feita depois desse tempo mesmo enquanto você toca |

Se a última sessão terminou de forma inesperada, na próxima abertura o programa oferece abrir a cópia mais nova que ela salvou.

## Bom saber

- O histórico não é salvo com o projeto. Abrir um projeto ou começar um novo o esvazia.
- Quando o histórico enche, os passos mais antigos são descartados.
- Em uma sessão online ao vivo, desfazer e refazer trazem de volta só as suas edições, nunca as de outras pessoas.
- Com `Salvar uma cópia de segurança` em `Nunca`, as cópias feitas quando você salva por cima de um arquivo continuam sendo mantidas.
- A pasta de configurações é `%APPDATA%\LYNX` no Windows, `~/Library/Application Support/LYNX` no macOS e `~/.config/LYNX` no Linux.

## Veja também

- [Atalhos](/wiki/shortcuts/)
- [Configurações](/wiki/settings/)
- [Formatos de arquivo](/wiki/file-formats/)
- [Solução de problemas](/wiki/troubleshooting/)
- [O que só o LYNX SHARE faz: segurança contra falhas](/why/#crash)

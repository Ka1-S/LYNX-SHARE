---
source: 5efc5c8254
---
![A janela Manage shortcuts: uma lista pesquisável de ações com as teclas de cada uma](/assets/img/wiki/shortcuts.webp)

Estas são as teclas padrão na versão 0.1.25 alfa. `Ajuda › Atalhos` (`F1`) mostra a folha de atalhos dentro do app. `Opções › Gerenciar atalhos...` abre o editor da imagem, onde você os altera.

> No macOS o app mostra as teclas com os nomes do Mac: Ctrl aparece como ⌘ e Alt como ⌥.

## Alterar um atalho

1. Escolha `Opções › Gerenciar atalhos...`.
2. Digite uma ação ou uma tecla, como `Ctrl+L`, na caixa de busca. A lista ao lado limita a busca a um lugar, e `Só alterados` mostra os que você alterou.
3. Clique na ação.
4. Clique em `Alterar` para substituir as teclas dela, ou em `Adicionar atalho` para dar a ela uma segunda, e então pressione as novas teclas. `Esc` cancela.
5. Se as teclas já são usadas no mesmo lugar, uma mensagem nomeia a outra ação e permite que você as mova.

`Remover` apaga as teclas da ação. `Restaurar o padrão` restaura uma ação e `Restaurar tudo...` restaura todas. Suas alterações são salvas em `keys.json` na pasta de configurações (`%APPDATA%\LYNX` no Windows, `~/Library/Application Support/LYNX` no macOS, `~/.config/LYNX` no Linux). `Abrir keys.json` abre esse arquivo.

## Transporte e gravação

| Tecla | O que faz |
|---|---|
| `Space` | Toca, ou para e volta ao ponto onde a reprodução começou |
| `Ctrl+Space` | Toca ou pausa |
| `Shift+Space` | Toca, ou para no início da música ou do pattern |
| `L` | Alterna entre o modo Pattern e o modo Song |
| `R` | Liga ou desliga a gravação |
| `Shift+R` | Grava áudio agora: arma o insert selecionado e grava |
| `Ctrl+M` | Liga ou desliga o metrônomo |
| `Ctrl+P` | Liga ou desliga a contagem antes de gravar |
| `Ctrl+I` | Liga ou desliga a espera da gravação pela primeira nota |
| `Ctrl+B` | Liga ou desliga a mistura das notas gravadas |
| `Ctrl+E` | Liga ou desliga a edição por steps |
| `Ctrl+H` | Interrompe o som (all notes off) |
| `Ctrl+T` | Liga ou desliga o teclado de digitação como piano |
| `Alt+Numpad *` / `Alt+Numpad /` | Salta para o marcador de música seguinte ou anterior |

Algumas dessas teclas fazem outra coisa dentro de um editor que tem o foco. No Piano Roll, `Ctrl+M` importa um arquivo MIDI e `Ctrl+B` duplica. Na Playlist, `Ctrl+P` é o modo Performance.

## Arquivos e edição

| Tecla | O que faz |
|---|---|
| `Ctrl+N` | Novo projeto |
| `Ctrl+O` | Abrir projeto |
| `Ctrl+S` | Salvar |
| `Ctrl+Shift+S` | Salvar como |
| `Ctrl+Alt+S` | Salvar nova versão |
| `Ctrl+R` | Exportar áudio |
| `Ctrl+Shift+R` | Exportar um arquivo MP3 |
| `Ctrl+Shift+M` | Exportar um arquivo MIDI |
| `Ctrl+Z` | Desfazer. Pressionado de novo, refaz aquele passo |
| `Ctrl+Alt+Z` | Desfazer para trás, passo após passo |
| `Ctrl+Y` ou `Ctrl+Shift+Z` | Refazer |
| `Ctrl+Alt+H` | Histórico de desfazer |
| `Ctrl+F` | Buscar no Browser |

## Janelas

| Tecla | O que faz |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Seletor de plugins |
| `Shift+F8` | Banco de plugins (no Browser) |
| `Enter` | Maximiza ou restaura a Playlist |
| `F10` | Configurações de MIDI |
| `F11` | Informações do projeto |
| `F12` | Fecha todas as janelas (plugins, configurações de canal, ferramentas) |
| `Alt+F12` | Fecha todas as janelas de plugin |
| `Ctrl+F12` | Fecha todas as janelas, exceto a que tem o foco |
| `Alt+F7` | Teclado na tela |
| `Ctrl+F5` | Tap de tempo |
| `F1` | Folha de atalhos |

## Patterns

| Tecla | O que faz |
|---|---|
| `Numpad +` / `Numpad -` | Pattern seguinte, anterior |
| `Numpad 1` a `Numpad 9` | Seleciona o pattern 1 a 9 |
| `F4` | Próximo pattern vazio, e o nomeia |
| `Ctrl+F4` | Próximo pattern vazio, sem nomear |
| `Shift+F4` | Primeiro pattern vazio, e o nomeia |
| `F2` | Renomeia e colore o pattern |
| `Ctrl+Shift+C` | Clona o pattern |
| `Ctrl+Shift+Ins` | Insere um pattern vazio antes do atual |
| `Ctrl+Shift+Del` | Exclui o pattern |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Move o pattern para cima, para baixo |

## Channel Rack

| Tecla | O que faz |
|---|---|
| `Up` / `Down` | Seleciona o canal acima, abaixo |
| `Shift+Up` / `Shift+Down` | Estende a seleção |
| `Alt+Up` / `Alt+Down` | Move os canais selecionados |
| `Ctrl+A` | Seleciona todos os canais |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Recorta, copia, cola os steps dos canais selecionados |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotaciona os steps para a esquerda, para a direita |
| `Alt+C` | Clona os canais selecionados |
| `Alt+Del` | Exclui os canais selecionados |
| `Alt+G` | Agrupa os canais selecionados |
| `Ctrl+L` | Envia os canais selecionados para inserts livres do Mixer |
| `Ctrl+K` | Liga ou desliga as faixas de gráfico dos steps |
| `1` a `0` | Silencia ou reativa os canais 1 a 10 |
| `Ctrl+1` a `Ctrl+0` | Coloca em solo os canais 1 a 10 |

## Piano Roll

| Tecla | O que faz |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Lápis, Pincel, Pincel de bateria, Borracha, Mudo, Faca, Laço, Zoom, Ouvir |
| `S` | Liga ou desliga notas slide (novas notas) |
| `G` / `K` | Canal anterior, seguinte |
| `H` / `J` | Canal anterior, seguinte com notas |
| `Ctrl+A` | Seleciona tudo |
| `Ctrl+D` ou `Esc` | Desmarca |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, recorta, cola |
| `Ctrl+B` | Duplica |
| `Del` | Exclui a seleção |
| `Up` / `Down` | Move a seleção um semitom |
| `Ctrl+Up` / `Ctrl+Down` | Move a seleção uma oitava |
| `Left` / `Right` | Move a seleção um passo da grade |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Rotaciona as notas dentro do compasso |
| `Backspace` | Desliga ou liga a grade |
| `Ctrl+Q` | Quantização rápida (inícios e durações) |
| `Shift+Q` | Quantiza os inícios |
| `Ctrl+U` | Picota na grade |
| `Ctrl+G` | Une notas encostadas |
| `Ctrl+L` | Legato |
| `Shift+D` | Encurta as notas até a grade |
| `Ctrl+P` | Liga ou desliga o porta para a seleção |
| `Alt+M` / `Shift+Alt+M` | Silencia, reativa a seleção |
| `Alt+V` | Liga ou desliga as notas fantasma |
| `Shift+I` | Inverte a seleção |
| `Shift+G` / `Alt+G` | Agrupa, desagrupa as notas selecionadas |
| `Alt+C` | Colore as notas selecionadas com a cor da nota |
| `Shift+C` | Seleciona as notas da mesma cor |
| `Shift+N` | Carimbo de acorde: nenhum (notas simples) |
| `Ctrl+Ins` / `Ctrl+Del` | Insere, exclui espaço (a seleção de tempo) |
| `Ctrl+M` | Importa um arquivo MIDI |
| `Ctrl+Shift+M` | Exporta como arquivo MIDI |
| `PageUp` / `PageDown` | Aproxima, afasta |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom para 1, 4, 16 compassos |
| `Shift+4` | Zoom para mostrar tudo |
| `Shift+5` | Zoom para a seleção |
| `Shift+0` | Centraliza a vista no cursor de reprodução |

### Ferramentas de notas

Cada tecla abre a caixa de diálogo de uma ferramenta de notas no Piano Roll.

| Tecla | O que faz |
|---|---|
| `Alt+A` | Arpejar |
| `Alt+S` | Dedilhar (strum) |
| `Alt+Y` | Espelhar |
| `Alt+R` | Aleatorizar |
| `Alt+X` | Escalar níveis |
| `Alt+K` | Limitar a um intervalo |
| `Alt+Q` | Quantizar |
| `Alt+U` | Picotar |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Articular |
| `Alt+E` | Riff machine |
| `Alt+P` | Gerar progressão de acordes |

## Playlist

| Tecla | O que faz |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Lápis, Pincel, Borracha, Mudo, Faca, Laço, Zoom, Ouvir, Deslizar |
| `Ctrl+A` | Seleciona tudo |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copia, recorta, cola |
| `Ctrl+B` | Duplica |
| `Del` | Exclui os clipes selecionados |
| `Shift+Del` | Exclui os clipes selecionados e fecha os vãos |
| `Shift+S` | Liga ou desliga a edição ripple |
| `Backspace` | Desliga ou liga a grade |
| `Alt+M` / `Shift+Alt+M` | Silencia, reativa os clipes selecionados |
| `Shift+M` | Liga ou desliga o stretch de áudio ao redimensionar clipes de áudio |
| `Ctrl+Shift+V` | Torna únicos os clipes selecionados |
| `Ctrl+G` | Mescla clipes de pattern |
| `Ctrl+Shift+G` | Mescla clipes de pattern semelhantes |
| `Ctrl+Alt+G` | Mescla clipes de automação |
| `Ctrl+Shift+B` | Clona as faixas selecionadas da Playlist |
| `Ctrl+Q` | Quantiza os inícios dos clipes |
| `7` / `8` | Baixa, sobe um semitom nos clipes de áudio selecionados |
| `9` | Inverte os clipes de áudio selecionados |
| `Ctrl+Ins` / `Ctrl+Del` | Insere, remove a duração da seleção de tempo em todas as faixas |
| `Ctrl+Left` / `Ctrl+Right` | Seleciona o tempo anterior, seguinte |
| `Ctrl+Enter` | Seleciona o tempo em volta da seleção |
| `Alt+T` | Adiciona um marcador de tempo |
| `Ctrl+T` | Adiciona um marcador automaticamente |
| `Shift+Alt+T` | Adiciona uma mudança de fórmula de compasso |
| `Shift+T` | Posiciona o marcador de loop |
| `Ctrl+Alt+C` | Consolida a seleção da Playlist |
| `Ctrl+P` | Liga ou desliga o modo Performance |
| `PageUp` / `PageDown` | Aproxima, afasta |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom para 1, 4, 16 compassos |
| `Shift+4` | Zoom para mostrar tudo |
| `Shift+5` | Zoom para a seleção |
| `Shift+0` | Centraliza a vista no cursor de reprodução |

## Mixer

| Tecla | O que faz |
|---|---|
| `Alt+Left` / `Alt+Right` | Move os inserts selecionados para a esquerda, para a direita |
| `F2` | Renomeia e colore o insert selecionado |
| `S` | Coloca em solo os inserts selecionados |
| `Alt+S` | Coloca em solo os inserts selecionados junto com os inserts que os alimentam |
| `Ctrl+L` | Vincula os canais selecionados ao insert selecionado |
| `Ctrl+Shift+L` | Vincula os canais selecionados a inserts a partir do selecionado |
| `Alt+L` | Seleciona os canais vinculados ao insert selecionado |
| `Del` | Exclui o plugin no slot de efeito selecionado |
| `Ctrl+E` | Abre o editor de áudio |
| `Shift+E` | Abre o editor de áudio e grava |
| `Alt+R` | Renderiza os inserts armados em arquivos wave |
| `Shift+Alt+R` | Renderiza os inserts selecionados em arquivos wave |

## Browser

| Tecla | O que faz |
|---|---|
| `Up` / `Down` | Arquivo anterior, seguinte (com pré-escuta) |
| `Shift+Up` / `Shift+Down` | Arquivo anterior, seguinte no canal selecionado |
| `Enter` | Carrega o arquivo no canal selecionado |
| `Left` | Fecha a pasta, ou vai para a pasta pai |
| `Right` | Abre a pasta |
| `F2` | Renomeia o plugin selecionado (Banco de plugins) |

## Bom saber

- Enquanto o piano do teclado de digitação está ligado, as teclas dele tocam notas e a maioria dos atalhos de uma letra fica em pausa (`L` continua funcionando). Desligue com `Ctrl+T`.
- Um atalho funciona em qualquer lugar ou em uma só janela. Um que pertence a uma janela prevalece sobre as mesmas teclas que valem em qualquer lugar, enquanto essa janela tem o foco.
- Algumas ações não têm tecla padrão, por exemplo a paleta de inserção rápida. Dê uma a elas no editor.

## Veja também

- [Um passeio pela janela](/wiki/tour/)
- [Configurações](/wiki/settings/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Vindo do FL Studio](/wiki/coming-from-fl-studio/)

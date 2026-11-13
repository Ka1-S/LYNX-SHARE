---
source: 094cbefd18
---
![A janela Gravação pergunta o que gravar: áudio, notas e automação, tudo, ou áudio no editor de áudio](/assets/img/wiki/record-dialog.webp)

O botão de gravar no transporte, ou a tecla `R`, liga a gravação. Na primeira vez, uma janela pergunta o que você quer gravar. O áudio vai para a [Playlist](/wiki/playlist/) como clipes de áudio. As notas vão para o pattern atual. Os controles que você mexer viram automação.

## Gravar um microfone ou um instrumento

1. Abra `Opções › Configurações de áudio...` e escolha um dispositivo de entrada.
2. Abra o [Mixer](/wiki/mixer/) (`F9`) e selecione um insert vazio.
3. Clique na caixa de entrada acima dos slots de efeito do insert e escolha uma entrada. Os pares estéreo vêm primeiro; os canais únicos são marcados com `(mono)`.
4. Clique no botão de armar gravação ao lado da caixa de entrada.
5. Aperte `R` e escolha `Áudio, na Playlist como clipe de áudio`.
6. Aperte play se a reprodução não começou sozinha e toque.
7. Aperte stop. O take é um clipe de áudio na Playlist.

`Shift+R` faz os passos 4 a 6 com uma só tecla: arma o insert selecionado, grava só áudio e inicia a reprodução. Aperte de novo para parar.

Quando você escolhe uma entrada para um insert que grava mais do que essa entrada (outros canais que enviam para ele também são gravados), o LYNX SHARE pergunta, como o FL Studio faz, se deve trocar para `Só a entrada externa`. Responda `Sim` para gravar só a entrada nesse insert.

## Gravação de áudio com um clique

`Ferramentas › Gravação de áudio com um clique` inicia um take em um passo, como no FL Studio. Ele atua no insert do mixer selecionado. Com o Master ou nada selecionado, usa o primeiro insert com uma entrada ou, se não houver, um insert livre.

| Comando | O que faz |
|---|---|
| `No editor/gravador de áudio Edison` | Põe o Edison no insert, configurado para gravar quando a reprodução começa, e inicia a reprodução. Veja [Mixer](/wiki/mixer/) |
| `Na playlist como clipe de áudio` | Troca para o modo Song, arma o insert, pede a entrada dele se não tiver nenhuma, e inicia a gravação e a reprodução. Inserts que já estão armados gravam como estão |

## O que a janela Gravação oferece

| Escolha | O que faz |
|---|---|
| `Áudio, na Playlist como clipe de áudio` | Arma o insert do mixer selecionado e pede a entrada dele se não tiver nenhuma |
| `Notas e automação` | Grava notas de um teclado MIDI ou do teclado do computador, e os controles que você mexer |
| `Tudo` | Notas, automação e áudio dos inserts armados |
| `Áudio, no editor de áudio` | Grava a entrada em um novo editor de áudio enquanto a música toca |

Marque `Não perguntar de novo` para manter a sua escolha. Depois disso, clique com o botão direito no botão de gravar para ver o filtro de gravação: `Automação`, `Notas`, `Áudio`, `Clipes` e `Perguntar o que gravar ao iniciar a gravação` para trazer a janela de volta.

## A caixa de entrada de um insert

![O Mixer com o insert 3 armado e a caixa de entrada nas entradas 1 e 2](/assets/img/wiki/mixer-input.webp)

| Opção | O que faz |
|---|---|
| `Fonte de entrada de áudio` | A entrada do dispositivo que este insert toca e grava, ou `(nenhum)` |
| `Gravar` | Onde o take é captado: `Só a entrada externa`, `Entrada externa e do mixer`, `Pós-efeitos`, `Pós-EQ`, `Pós-volume e pan` ou `Pós-faixa` |
| `Gravar também a entrada seca como segundo take (LYNX)` | Guarda a entrada sem processamento ao lado de um take processado |
| `Monitorar entrada externa` | `Desligado`, `Quando armado`, `Auto (quando armado, não durante a reprodução)` ou `Ligado` |
| `Protegido contra gravação (não pode ser armado)` | Protege o insert de ser armado por engano |

## Gravar notas

As notas vêm de um teclado MIDI, do teclado do computador (`Ctrl+T`) ou do teclado na tela, e vão para os canais selecionados no pattern atual. Veja [Controladores MIDI](/wiki/midi-controllers/).

- `Mesclar notas gravadas` (`Ctrl+B`) decide entre overdub e substituição. Ligado, as notas novas se juntam às notas do pattern. Desligado, as notas antigas de um canal gravado são apagadas.
- `Quantizar as notas gravadas no snap global` está nas opções de gravação.
- `Edição por passos` (`Ctrl+E`) funciona com a música parada: cada nota que você toca entra no pattern, um passo da grade depois do outro.
- `Capturar MIDI` (`Ctrl+Shift+Alt+R`) põe no pattern as notas que você acabou de tocar, mesmo com a gravação desligada.
- O pedal de sustain (CC 64) e o pedal de sostenuto (CC 66) seguram as notas que você toca, e uma nota gravada dura até o pedal subir. Um canal Layer repassa o pedal para os canais dele. Com `O pedal controla o fim das notas` desligado em `Opções › Configurações de MIDI...`, o pedal vai para os plugins como um controlador.
- No modo Song, as notas gravadas em um clipe de pattern mais longo que o pattern ficam onde você as tocou, e o pattern cresce. O take não é dobrado de volta sobre os primeiros compassos do pattern. Com `Clipes de pattern esticados repetem o pattern (LYNX)` ligado, só as notas tocadas sobre compassos que já tinham notas entram no loop.

Os controles que você mexe durante a gravação são escritos como clipes de automação no modo Song e como eventos do pattern no modo Pattern. Veja [Automação](/wiki/automation/).

## Opções de gravação

As opções estão em `Opções › Gravação` e no menu de clique direito do botão de gravar.

| Comando | Tecla | O que faz |
|---|---|---|
| `Contagem` | `Ctrl+P` | Desligado, 1 compasso, 2 compassos ou 4 compassos de metrônomo antes de a gravação começar. `Ctrl+P` alterna entre desligado e a duração que você escolheu por último. A escolha é guardada quando você fecha o programa; uma instalação nova começa com 1 compasso, como o FL Studio |
| `Metrônomo` | `Ctrl+M` | Clique durante a reprodução |
| `Esperar a primeira nota para começar` | `Ctrl+I` | A gravação espera até você tocar |
| `Gravação em loop` | nenhuma | A gravação entra em loop em vez de seguir adiante |
| `Áudio: punch in/out na região de loop` | nenhuma | Toca pela região de loop e guarda só o áudio dentro dela |
| `Áudio: pre-roll` e `post-roll` | nenhuma | Desligado, 1, 2 ou 4 compassos tocados antes do punch-in e depois do punch-out |
| `Gravar inicia a reprodução` | nenhuma | Ligar a gravação também aperta play |
| `Desarmar os inserts quando a gravação parar` | nenhuma | Os inserts armados são liberados depois de cada take |

`Baixa latência durante a gravação` fica no menu do botão direito do botão de gravar e no menu do insert do Mixer, em `Gravação em disco`. Vem desligado por padrão. Enquanto um insert com entrada está armado, ou o monitoramento dele está em `Ligado`, os efeitos que acrescentam latência no caminho até o Master são ignorados. Isso muda só o que você ouve: um insert mantém todos os seus efeitos quando o take é captado depois deles, por exemplo com `Pós-efeitos`. Os efeitos voltam quando o insert é desarmado.

`Compensação de latência` está no mesmo menu `Gravação em disco` e vem ligado por padrão, como no FL Studio. Ele move cada take para trás pelo tempo de ida e volta do dispositivo de áudio, pelo valor `Ajuste fino (amostras)` das configurações de áudio e pela compensação de atraso dos plugins. Desligue para manter os takes onde foram gravados.

`Ferramentas › Cancelar gravação` descarta o que a passada de gravação atual escreveu (notas, automação e takes de áudio) e desliga a gravação. A reprodução continua e os inserts armados continuam armados.

## Gravação em slots de clipe

No [fluxo por disparo de clipes](/wiki/layouts/), os takes podem ir para a grade de clipes em vez da Playlist, como no Ableton Live.

- Arme uma faixa com o botão de armar dela no mixer Session. Os slots vazios dela mostram então um botão redondo de gravar no lugar do quadrado de parar. Clique em um para gravar nesse slot.
- `Gravação de sessão`, sob a grade, grava as faixas armadas na cena selecionada. Enquanto os clipes delas tocam, notas novas são acrescentadas a eles (overdub).
- `Duração fixa de gravação`, no menu de um slot vazio, encerra uma gravação em um slot depois de 1 ou mais compassos, e o clipe continua tocando. `Desligado` grava até você parar.

## Gravação em loop e takes

Com `Gravação em loop` ligado, a gravação repete a região de loop no modo Song, ou o pattern no modo Pattern. Desligado, a gravação segue direto.

Cada passagem de uma gravação de áudio em loop cai na próxima faixa livre da Playlist, e as faixas formam um grupo. `Mesclar notas gravadas` decide o que toca depois que você para: desligado silencia todas as passagens, ligado toca todas. Clique com o botão direito em uma faixa do grupo e escolha `Fazer comp destas tomadas` para montar um único take com as passagens. `Após gravar em loop, só a última passagem toca`, nas opções de gravação, volta a uma só faixa.

## Onde as gravações são salvas

- Um projeto salvo guarda os takes dele em `Audio/Recorded`, ao lado do arquivo do projeto.
- Um projeto não salvo grava em `LYNX/Recordings`, na sua pasta Música.
- Os takes são arquivos WAV na taxa de amostragem do dispositivo de áudio, em 16 bits, 24 bits ou 32 bits float. O formato (`Formato do arquivo`) e os nomes dos takes são definidos em `Gravação`, em `Opções › Configurações de áudio...`.
- `Abrir pasta de gravações` está no menu de clique direito do botão de gravar. `Arquivo › Excluir gravações não usadas e salvar...` remove os takes que o projeto não usa mais.

## Bom saber

- Um insert armado sem entrada não grava nada. O LYNX SHARE avisa e aponta para a caixa de entrada.
- A gravação se recusa a começar quando o disco tem menos de 30 segundos de espaço, e avisa abaixo de 10 minutos.
- `Opções › Gravar áudio da entrada` grava os dois primeiros canais de entrada em um novo insert quando nada está armado.
- No macOS, o sistema pede acesso ao microfone na primeira vez que você grava.
- Se o LYNX SHARE fechar durante a gravação, na próxima abertura ele oferece pôr os takes recuperados na Playlist.

## Veja também

- [Configuração de áudio](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Automação](/wiki/automation/)
- [Controladores MIDI](/wiki/midi-controllers/)
- [Solução de problemas](/wiki/troubleshooting/)

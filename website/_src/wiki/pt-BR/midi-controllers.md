---
source: 3b54a39c97
---
![A página MIDI das configurações: dispositivos de entrada, dispositivos de saída com sincronização mestre e sincronização externa](/assets/img/wiki/settings-midi.webp)

O LYNX SHARE recebe notas e movimentos de controlador de qualquer dispositivo de entrada MIDI. Os dispositivos são configurados na página MIDI das configurações: `Opções › Configurações de MIDI...`, ou `F10`. Sem hardware, o teclado do computador e o teclado na tela também tocam notas.

## Conectar um teclado

1. Ligue o teclado e abra `Opções › Configurações de MIDI...` (`F10`).
2. Procure-o em `Dispositivos de entrada` e marque-o. Se não estiver na lista, clique em `Atualizar lista de dispositivos`.
3. Deixe as duas caixas em `Qualquer canal MIDI` e `Toca os canais selecionados`.
4. Feche as configurações e selecione um canal no [Channel Rack](/wiki/channel-rack/).
5. Toque. O canal selecionado soa.

## Dispositivos de entrada

| Opção | O que faz |
|---|---|
| Caixa de seleção | Liga ou desliga o dispositivo |
| `Qualquer canal MIDI` | Escuta todos os canais MIDI, ou só um canal |
| `Toca os canais selecionados` | As notas vão para os canais selecionados no Channel Rack, ou sempre para um canal fixo |
| `Velocity` | `Como tocado`, `Suave (tocar leve soa mais forte)`, `Duro (pede toque mais forte)` ou `Fixo (toda nota com velocidade máxima)` |
| `Canal do modo performance` | Notas neste canal MIDI disparam clipes da Playlist enquanto o modo Performance está ligado |
| `Salto para marcador da música` | Notas neste canal MIDI levam a música a um marcador em vez de tocar |

## Vincular um knob a um controle

1. Clique com o botão direito no knob ou fader no LYNX SHARE.
2. Escolha `Vincular ao controlador MIDI (mova um knob no controlador)`.
3. Mova o knob ou fader no seu hardware. O vínculo está feito.

O vínculo é salvo com o projeto. Para removê-lo, clique de novo com o botão direito no controle e escolha `Desvincular controlador`. O item do menu mostra o número do CC em uso.

| Comando | Tecla | O que faz |
|---|---|---|
| `Override global link...` | nenhuma | Cria um vínculo que vale em todos os projetos. Fica guardado nas configurações, não no projeto |
| `Remover vínculo global` | nenhuma | Remove-o de novo |
| `Multilink para controladores` | `Ctrl+J` | Enquanto estiver ligado, os controles que você mover são vinculados, em ordem, aos controles de hardware que você mover em seguida. Fica no menu `Opções` |
| `Captura (modo takeover)` | nenhuma | Na página MIDI. Um controlador só move um parâmetro depois de alcançar o valor atual dele, então nada dá saltos |

A página MIDI lista os vínculos em `Vínculos (deste projeto, depois os globais)`. `Tipo de controlador`, na mesma página, mostra `Controlador genérico`.

Os parâmetros de um plugin de terceiros são acessados por `Automatizar parâmetro do plugin` no menu do canal ou no menu do slot de efeito. Cada parâmetro ali tem os mesmos itens de vínculo. Veja [Automação](/wiki/automation/).

## O teclado do computador

`Ctrl+T`, ou `Opções › Teclado do PC como piano`, transforma o teclado do computador em um piano. As fileiras de `Z` a `M` e de `Q` a `P` tocam os canais selecionados.

- Enquanto estiver ligado, as teclas de letras e números são notas. As teclas de ferramenta de uma letra só do Piano Roll e da Playlist funcionam apenas com ele desligado.
- Ele sempre toca com a velocity do Piano Roll. A opção `Velocity` da página MIDI é para dispositivos MIDI.
- O mesmo interruptor está em `Opções › Configurações gerais...`.

## O teclado na tela

![O teclado na tela: botões de oitava, tamanho das teclas, modo de velocity e uma fileira de teclas de piano a partir do C4](/assets/img/wiki/touch-keyboard.webp)

`Exibir › Teclado na tela` (`Alt+F7`) abre um piano que você toca com o mouse ou uma tela de toque. Ele toca o canal selecionado no Channel Rack e mostra o nome dele à direita.

| Opção | O que faz |
|---|---|
| `-` e `+` | Uma oitava para baixo ou para cima |
| Tamanho das teclas | `Teclas pequenas`, `Teclas médias` ou `Teclas grandes` |
| `Velocity: onde você toca` | Perto do topo de uma tecla toca suave, perto da base toca forte |
| `Velocity: fixa` | Toda nota na velocity definida com o controle deslizante |
| `Nomes das notas` | Mostra os nomes nas teclas |

## Sincronização MIDI

`Dispositivos de saída`, na página MIDI, envia o transporte para hardware e outros programas. Marque `Enviar sincronização mestre` para um dispositivo e escolha `MIDI clock` (com Start, Continue, Stop e Song Position Pointer) ou MIDI Time Code. `Deslocamento (ms)` envia a sincronização mais tarde com um valor positivo e mais cedo com um negativo. `Enviar all notes off` envia essa mensagem ao dispositivo na inicialização e depois de uma exportação.

`Sincronizar com clock externo` funciona no sentido oposto: o LYNX SHARE segue o clock MIDI da entrada que você escolher, com o tempo, Start, Continue, Stop e Song Position Pointer dela. `Deslocamento da sincronização externa (milissegundos)` faz o LYNX SHARE tocar mais tarde com um valor positivo e mais cedo com um negativo. `Estado` mostra se há um clock em andamento.

## Bom saber

- Escolher uma entrada para `Sincronizar com clock externo` liga esse dispositivo em `Dispositivos de entrada`.
- Um tempo movido por um controlador MIDI vinculado não redimensiona os clipes de áudio. Digite ou defina o tempo final.
- Para gravar o que você toca, veja [Gravação](/wiki/recording/). As notas que chegam podem ser ajustadas a uma escala no [Piano Roll](/wiki/piano-roll/).
- CC MIDI, aftertouch de canal e pitch bend enviados a um plugin podem ser automatizados com `Automatizar controlador MIDI (CC, aftertouch, pitch bend)`.

## Veja também

- [Gravação](/wiki/recording/)
- [Automação](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Configurações](/wiki/settings/)
- [Atalhos](/wiki/shortcuts/)

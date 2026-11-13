---
source: 3bf7e0c0ab
---
O LYNX SHARE mantém o fluxo de trabalho que você conhece: um Channel Rack, patterns, uma Playlist e um Mixer, com as mesmas teclas. Ele abre projetos `.flp` diretamente e pode gravar um de volta.

O LYNX SHARE é um programa independente. Ele não é afiliado, endossado nem patrocinado pela Image-Line e não contém código da Image-Line. FL Studio é marca registrada da Image-Line Software.

## Abrir um projeto .flp

1. Escolha `Arquivo › Abrir...` (`Ctrl+O`) e selecione o `.flp`.
2. O LYNX SHARE resolve os plugins e samples do projeto e depois o abre. O título mostra "(imported from FL Studio)".
3. Escolha `Ajuda › Last FL Studio import report` para ler o que foi aproximado, deixado de fora ou não encontrado.
4. Escolha `Ajuda › Arquivos ausentes...` para ver os samples e plugins ausentes.
5. Escolha `Arquivo › Salvar`. Ele pergunta onde gravar um arquivo `.lynx`. O próprio `.flp` nunca é gravado.

## O que corresponde a quê

| No FL Studio | No LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Mesmos nomes, em `F5`, `F6`, `F7` e `F9` |
| Seletor de plugins | `F8` |
| Browser | `Alt+F8` |
| Modo Pattern / Song | `L` |
| Desfazer | `Ctrl+Z` desfaz, ou refaz depois de um desfazer. `Ctrl+Alt+Z` desfaz para trás |
| Salvar nova versão | `Ctrl+Alt+S` |
| Exportar | `Ctrl+R`, MP3 em `Ctrl+Shift+R`, MIDI em `Ctrl+Shift+M` |
| Inserts, envios e sidechain do Mixer | Os mesmos, com 10 slots de efeito por insert |
| Fruity Wrapper | Plugins de terceiros são hospedados diretamente, veja [Plugins de terceiros](/wiki/plugins/) |
| Geradores e efeitos nativos | Tocam em motores que o LYNX SHARE recriou, veja abaixo |
| Arquivo de projeto `.flp` | Arquivo de projeto `.lynx` |

`Ajuda › Atalhos` lista todas as teclas.

## O que é importado

- Canais, samples, patterns, notas slide e porta, todos os arranjos, marcadores, mudanças de fórmula de compasso, automação e vínculos de controlador.
- O Mixer: roteamento, níveis, sidechains, EQ de faixa e efeitos nativos com as configurações salvas.
- Plugins VST3, VST2 e CLAP de terceiros dentro do Fruity Wrapper: substituídos pelo mesmo plugin instalado no seu computador, com o estado salvo.

## Plugins nativos do FL Studio

Os geradores e efeitos próprios do FL Studio não são carregados. O LYNX SHARE os toca em motores que ele recriou, e cada um tem um nível de qualidade:

| Nível | Significado |
|---|---|
| Exact | Idêntico bit a bit ao original em todos os estados testados |
| Near-exact | Idêntico bit a bit na maioria dos estados; nos demais a diferença fica abaixo de cerca de -100 dB |
| Close | O mesmo algoritmo, dentro de -60 dB do original em quase todos os estados testados |
| Approximate | As configurações salvas são mapeadas para um synth ou efeito embutido do LYNX SHARE |
| Placeholder | O estado é mantido e o plugin fica mudo ou deixa o áudio passar direto |

O nível de cada motor, e o que ainda falta em cada um, está em [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Veja também [Instrumentos e efeitos embutidos](/wiki/built-in/).

## O que precisa da sua própria instalação do FL Studio

Abrir um `.flp` funciona sem o FL Studio instalado. Uma instalação no mesmo computador acrescenta duas coisas:

- Os samples de fábrica do FL Studio e o banco de dados de plugins dele, para que samples e plugins sejam encontrados sem uma busca.
- As ondas, samples, impulsos e tabelas embutidos de que alguns motores precisam (FL Keys, Soundgoodizer, Maximus e os outros marcados com um sinal de adaga no PLUGINS.md). Eles são lidos da sua instalação em tempo de execução e não vão junto com o LYNX SHARE.

No Linux, o LYNX SHARE procura um FL Studio do Windows no Wine. No macOS ele procura o pacote do app, mas isso não foi testado.

## Exportar de volta para .flp

1. Escolha `Arquivo › Exportar › Projeto do FL Studio...`.
2. Escolha `Projeto do FL Studio (.flp)` ou `Pacote zipado (.zip, com todos os samples)`.
3. Escolha o que acontece com o que o FL Studio não tem: renderizar em áudio (soa igual) ou usar o plugin do FL mais próximo (continua editável, pode soar diferente, e os instrumentos só do LYNX ficam de fora).

O arquivo é um projeto do FL Studio 2026. Samplers, clipes de áudio, geradores e efeitos próprios do FL, plugins de terceiros (dentro do Fruity Wrapper), patterns, a Playlist, o Mixer e a automação passam com as suas configurações. Um relatório lista o que passou exatamente e o que foi renderizado, substituído ou deixado de fora.

## FL Studio Mobile (.flm)

`Arquivo › Abrir...` também aceita projetos `.flm`. A importação é parcial.

- Importado: tempo, fórmula de compasso, um insert de mixer por canal FLM, uma faixa da Playlist por lane, patterns (cada passagem do loop como um clipe), clipes de áudio com deslocamento e afinação, kits de bateria, programas do DW Sampler e `.dwp` e o Slicer (nos motores do próprio FLM), os instrumentos MiniSynth, Transistor Bass, 3x Osc, GMSynth e SuperSaw, os efeitos do FLM (como efeitos Pocket), a automação de nível, pan, mute, solo, pitch bend, sends e parâmetros de instrumento, e os conjuntos de acordes e o key locker.
- Não importado: os outros instrumentos do FLM, que tocam no synth embutido.
- Os samples são procurados ao lado do `.flm` e na cópia que o FL Studio tem dos recursos do FLM. Um `.dwp` que não for encontrado toca um substituto.

## Bom saber

- Os pacotes FLEX são criptografados e nunca são abertos. Um canal FLEX recebe um patch de synth escolhido pelo nome do preset.
- Os moduladores ficam de fora de uma exportação `.flp`, e o relatório da exportação avisa.
- O relatório de importação lista o que foi aproximado pelo synth embutido, o que não teve equivalente, quais plugins foram encontrados ou não estão instalados, quais efeitos ficaram de fora ou foram reproduzidos em parte, e quantos samples estão ausentes.
- Um efeito de terceiros que não está instalado é substituído por um efeito do LYNX SHARE com a mesma função. O original continua no projeto.

## Veja também

- [Instrumentos e efeitos embutidos](/wiki/built-in/)
- [Plugins de terceiros](/wiki/plugins/)
- [Formatos de arquivo](/wiki/file-formats/)
- [Atalhos](/wiki/shortcuts/)
- [O que só o LYNX SHARE faz com as configurações do FL](/why/#fl-settings)

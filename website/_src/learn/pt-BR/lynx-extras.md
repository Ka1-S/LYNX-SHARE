---
source: dd7b0185df
---
## O que são os extras

![A página LYNX extras, agrupada por área](/assets/img/learn/lynx-extras-1.webp)

1. Abra `Opções › Extras do LYNX...`: uma chave por extra, agrupados por área (Mixer, Playlist, Piano roll, edição e mais).
2. Cada chave diz o que faz e onde mais você acha o extra (muitas vezes no menu da própria janela também).

## Ligue um

![O medidor de loudness ao lado do Master](/assets/img/learn/lynx-extras-2.webp)

1. Aperte `Ctrl+F` e digite parte do nome, aqui "loudness", e clique na chave.
2. A mudança é imediata: o Mixer agora mostra o loudness da música ao lado do Master.

## Igual ao FL ou Recomendado

![FL-exact e Recommended no topo da página](/assets/img/learn/lynx-extras-3.webp)

1. `Igual ao FL` desliga todos os extras, então o LYNX funciona exatamente como o FL Studio.
2. `Recomendado` devolve cada extra ao seu padrão. A maioria dos que vêm ligados muda a aparência ou acrescenta algo que o FL Studio não tem, e nenhum muda o que as teclas e os cliques do FL Studio fazem. Quatro deles mudam o som ou seus arquivos, e as linhas deles dizem isso.

- `Clipes de pattern esticados repetem o pattern (LYNX)` toca o pattern de novo onde o FL Studio fica em silêncio depois dele.
- `Loops soltos recebem o andamento e o tom encontrados no áudio`, no layout Simple, muda como um loop solto toca.
- `Arquivos MIDI soltos na Playlist viram clipes de pattern` acrescenta um pattern à música quando você solta um arquivo (o FL Studio recusa o arquivo).
- `Uma linha por plugin instalado em vários formatos` escolhe o formato em que um plugin novo é carregado, e o projeto guarda esse formato.

## Mais um que muda o som

`Compensar também as automações de fader e de rota (LYNX)`, em `Compensação de latência de plugins` no menu de um insert do mixer, vem ligado em todo projeto novo. Ele não está na página de extras do LYNX, então `Igual ao FL` o deixa ligado.

1. Com plugins que adicionam latência (um EQ de fase linear, um limiter com lookahead), ele toca a automação de faders, pan, envios e faders VCA tão tarde quanto o áudio em que ela age, então um fade cai onde você o ouve.
2. O FL Studio atrasa só a automação dos knobs de plugins, então lá o mesmo movimento de fader soa um pouco antes. Projetos abertos do FL Studio e projetos feitos antes ficam com ele desligado.
3. Para o timing do FL Studio, desligue-o nesse menu: é uma configuração do projeto inteiro.

Veja também [Configurações](/wiki/settings/) e [Um tour pelas Configurações](/learn/settings-tour/).

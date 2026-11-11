---
source: 98829adcb8
---
O LYNX SHARE também roda sem abrir a janela. Uma opção renderiza um projeto em áudio, outras o convertem em um projeto do FL Studio ou em um set do Ableton Live, e várias existem para testes. As verificações do próprio projeto usam as mesmas opções.

O programa é `LYNX.exe` no Windows e `LYNX` no macOS e no Linux. No macOS o caminho completo é `/Applications/LYNX.app/Contents/MacOS/LYNX`. As opções são as mesmas em todos os sistemas.

## Renderizar um projeto em um arquivo

1. Abra um terminal na pasta que contém o programa. No Windows, é a pasta de instalação ou a pasta onde você extraiu o zip.
2. Execute `LYNX.exe --export project.flp out.flac`, com o seu projeto e o seu arquivo de saída.
3. Espere o programa terminar. Nenhuma janela se abre.
4. Leia `out.flac.txt`, gravado ao lado da saída. Para um projeto importado, ele lista o que foi convertido, aproximado e deixado de fora, e quais plugins foram encontrados ou estão faltando.

## Abrir um projeto

```
LYNX.exe project.lynx
```

Inicia o LYNX SHARE com a janela e abre o arquivo. Aceita `.lynx`, `.flp`, `.als`, `.flm` e `.mid`. Um caminho relativo é tomado a partir da pasta atual.

## Opções para o uso diário

| Opção | O que faz |
|---|---|
| `--export <project> <out>` | Renderiza a música inteira com a cauda, como a janela de exportação. A extensão de `<out>` escolhe o formato: `.wav` (24 bits), `.flac`, `.ogg` ou `.mp3` (320 kbps) |
| `--export ... --pattern` | Renderiza só o pattern atual |
| `--export ... --insert N` | Renderiza só a saída do insert N do Mixer |
| `--export ... --rate N` | Renderiza com outra taxa de amostragem, em Hz |
| `--stems <project> <base.wav>` | Grava um WAV por insert do Mixer que tenha canais: `base - 1.wav`, `base - 2.wav` e assim por diante |
| `--export-flp <project> <out.flp>` | Grava um projeto do FL Studio. Com `out.zip`, grava um pacote de loops compactado |
| `--export-flp ... --substitute` | Troca os plugins exclusivos do LYNX pelo plugin do FL mais próximo, em vez de renderizá-los em áudio |
| `--export-als <project> <folder>` | Grava um set do Ableton Live em `<folder>/<name> Project/<name>.als`, para o Live 12 |
| `--export-als ... --live11` | Grava o set para o Live 11 |
| `--devices <report.txt>` | Lista os tipos de driver de áudio e os dispositivos deste computador |
| `--showcase <out.lynx>` | Grava o projeto de demonstração do próprio LYNX SHARE |
| `--lang <id>` | Usa outro idioma de interface nesta execução, por exemplo `--lang pt-BR` |
| `--menu-entry on` ou `desligado` | Só no AppImage do Linux: adiciona o LYNX SHARE ao menu de aplicativos, ou o remove |

`--export`, `--stems` e `--export-flp` aceitam um `.lynx`, `.flp` ou `.als` como projeto. `--export-als` também aceita `.flm`.

Exemplos:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Opções para testes

Existem para verificar o próprio LYNX SHARE. São úteis em um relatório de bug.

| Opção | O que faz |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Roda o autoteste sem dispositivo de áudio. O relatório termina com `result=OK` quando passa |
| `--snapshot <prefix> [project]` | Desenha a interface e suas janelas em arquivos PNG nomeados a partir do prefixo. As imagens desta wiki são feitas assim |
| `--render <project> <out.wav> <seconds>` | Renderiza essa quantidade de segundos do pattern atual em um WAV de 24 bits |
| `--plugintest <plugin> <report.txt>` | Carrega e testa um plugin VST3 ou CLAP |
| `--sandboxtest <plugin> <report.txt>` | Testa um plugin em um processo separado, inclusive um que trave |
| `--scantest <folder> <report.txt>` | Examina uma pasta em busca de plugins, em processos separados |
| `--fldb <report.txt> [project.flp]` | Informa a instalação do FL Studio encontrada aqui, o banco de dados de plugins dela e as correspondências de plugins de um projeto |
| `--devicetest <type> <device> <report.txt>` | Abre um driver de áudio por um segundo de silêncio e informa o resultado |
| `--uiperf <project> <report.txt>` | Mede o tempo de desenho do Mixer, da Playlist e do Channel Rack |

Exemplos:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Bom saber

- Os resultados vão para arquivos, não para o terminal. `--export` grava `<out>.txt`, `--export-flp` grava `<out.flp>.txt`, `--export-als` grava `export-als.txt` na pasta e `--stems` grava `<base.wav>.txt`.
- O programa lê a entrada padrão. Em um laço de shell, acrescente `< /dev/null` a cada chamada.
- `--snapshot` desenha em 1280x720, a menos que a variável de ambiente `LYNX_SNAPSHOT_SIZE` diga outra coisa, por exemplo `1920x1080`.
- `--lang` aceita os ids dos idiomas listados em `Opções › Configurações gerais...`, como `en`, `pt-BR`, `de`, `es`, `fr` ou `ja`. Um id desconhecido é ignorado.
- `--export` usa o mesmo caminho de renderização da janela de exportação, mas nenhuma das outras opções da janela. Para dither, profundidade de bits ou normalização, use a janela.
- O código-fonte tem muitas outras opções de teste. São internas e podem mudar entre versões.

## Veja também

- [Exportação](/wiki/exporting/)
- [Formatos de arquivo](/wiki/file-formats/)
- [Compilar a partir do código-fonte](/wiki/build-from-source/)
- [Solução de problemas](/wiki/troubleshooting/)

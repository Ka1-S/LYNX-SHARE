---
source: 7db9a73959
---
Há duas formas de trabalhar em um projeto com outras pessoas, e as duas estão em `Arquivo › Sessão online`. Um pacote é um arquivo que você manda a um amigo, que devolve uma parte. Não precisa de rede, conta nem servidor. Uma sessão ao vivo conecta todo mundo pela internet por meio do relay do LYNX SHARE, e cada alteração chega a todos. Até 4 pessoas participam de um projeto, você incluído.

## Enviar um projeto a um amigo

![Enviar projeto](/assets/img/wiki/session-send.webp)

1. Abra o projeto e escolha `Arquivo › Sessão online › Enviar projeto...`.
2. Digite o seu nome e o do seu amigo.
3. Escolha com o que o seu amigo fica quando a sessão termina: `Só a própria parte (recomendado)` ou `Uma cópia completa do projeto`.
4. Salve o arquivo `.lynxpack`.
5. Envie o arquivo como preferir: e-mail, pasta compartilhada, pen drive.

O pacote contém o projeto, todos os samples e gravações que ele usa e renders de áudio ("substitutos") das partes que usam um plugin de terceiros. Seu amigo ouve essas partes sem ter os seus plugins.

## Gravar uma parte e devolvê-la

O amigo faz isto:

1. Abra o `.lynxpack` com `Arquivo › Abrir...`. Um resumo mostra quem enviou e quais inserts do mixer e faixas da Playlist são seus. Clique em `Abrir`.
2. Adicione os seus próprios canais, patterns e áudio nos seus inserts e faixas da Playlist. O material de quem criou o projeto fica como está.
3. Escolha `Arquivo › Sessão online › Devolver minha parte...` e envie o novo pacote a quem criou o projeto.

Cada convidado recebe uma cor, 20 inserts do mixer e 16 faixas da Playlist.

## Mesclar uma parte

Quem criou o projeto faz isto:

1. Abra o projeto que foi enviado.
2. Escolha `Arquivo › Sessão online › Mesclar uma parte...` e selecione o pacote que o seu amigo devolveu.
3. Os canais, patterns, notas, clipes e inserts do amigo são adicionados na cor dele. Nada do que é seu é alterado.

Mesclar é um só passo de desfazer. Uma parte mais nova do mesmo amigo substitui a anterior.

## Iniciar ou entrar em uma sessão ao vivo

![Iniciar sessão ao vivo](/assets/img/wiki/session-start.webp)

Quem cria a sessão a inicia:

1. Abra o projeto e escolha `Arquivo › Sessão online › Iniciar sessão ao vivo...`.
2. Digite o seu nome.
3. Em `Quem entrar pode editar`, escolha `Só os próprios canais, patterns, inserts e linhas (recomendado)` ou `Tudo`. Em `Quando a sessão terminar, quem entrou fica com`, escolha a própria parte deles ou uma cópia completa.
4. Clique em `Início`, depois copie o código de convite e envie a até 3 pessoas.
5. Quando alguém pedir para entrar, clique em `Deixar entrar`.

Um convidado entra:

1. Escolha `Arquivo › Sessão online › Entrar em sessão ao vivo...`.
2. Digite o seu nome, cole o código de convite e clique em `Entrar`.
3. Espere quem criou a sessão deixar você entrar. O projeto dessa pessoa abre.

## Durante uma sessão ao vivo

![Chat da sessão](/assets/img/wiki/session-chat.webp)

O menu `Arquivo › Sessão online` tem estes comandos enquanto uma sessão está em andamento:

| Comando | O que faz |
|---|---|
| `Chat da sessão...` | Abre o chat. O item do menu mostra o número de mensagens não lidas |
| `Seguir o transporte de quem criou a sessão` | Para convidados: tocar, parar e a posição seguem os de quem criou a sessão |
| `Pessoas e permissões...` | Para quem criou a sessão: o que cada pessoa pode alterar e com o que cada uma fica no fim |
| `Compartilhar plugins com quem entrar...` | Para o host: quais dos seus plugins os convidados podem usar. O item do menu mostra quantos estão compartilhados |
| `Mostrar código de convite...` | Para quem criou a sessão: mostra o código de novo, com um botão `Copia` |
| `Encerrar sessão ao vivo` | Para quem criou a sessão. Todos saem e o código deixa de funcionar |
| `Sair da sessão ao vivo` | Para um convidado. Sua parte do projeto fica com você |

Em `Pessoas e permissões...`, cada pessoa recebe um de quatro papéis: `Os próprios canais, patterns, inserts e linhas`, `As próprias partes + tempo e configurações do projeto`, `Tudo` ou `Só ouvir (sem edições)`. Desligar uma permissão nunca apaga nada.

## Plugins que os outros não têm

Um canal ou insert cujo plugin não está instalado no seu computador toca um substituto e mostra um selo âmbar `R` no Channel Rack e no Mixer.

- Em um pacote, os substitutos são renders de áudio feitos quando o pacote é salvo. Eles acompanham os clipes que você move, mas não as edições nas notas de um plugin que você não tem. Peça um pacote novo a quem o tem.
- Em uma sessão ao vivo, as partes que usam um plugin que alguém não tem são renderizadas por quem o tem e enviadas aos outros como áudio por streaming.

## Compartilhar seus plugins com os convidados

Em uma sessão ao vivo, o host pode deixar os convidados usarem plugins que só o host tem.

O host faz assim:

1. Escolha `Arquivo › Sessão online › Compartilhar plugins com quem entrar...`.
2. Marque os plugins a compartilhar. `Buscar nos seus plugins` reduz a lista. Nada é compartilhado até você marcar.

Um convidado faz assim:

1. Abra o menu de plugins de um dos seus canais ou slots de efeito. Os plugins compartilhados vêm primeiro, em `On <host>'s computer`.
2. Escolha um. A janela dele lista os parâmetros do plugin como sliders, com `Buscar parâmetros`.
3. Adicione notas e mova os sliders. A janela mostra `Updating...` enquanto o computador do host renderiza, e depois `Atualizado`.

- O plugin fica no computador do host e roda com a licença do host. O LYNX SHARE nunca envia o plugin, seus arquivos, seus presets nem sua licença.
- O convidado ouve um render que acompanha as notas e os valores depois de um instante. Não é tocado ao vivo, e o convidado não recebe a janela do próprio plugin.
- Enquanto o host está ausente, o último render toca. O compartilhamento termina quando a sessão acaba, e o último render permanece.
- Um convidado com o papel `Só ouvir (sem edições)` não pode usar plugins compartilhados.
- Algumas licenças de plugin não permitem que outras pessoas operem o plugin, nem remotamente. Confira cada licença antes de compartilhar.

## O que passa pela rede

Os pacotes são arquivos. Nada é enviado para lugar nenhum.

Uma sessão ao vivo só se conecta enquanto você está em uma. Tudo passa pelo relay `relay.lynx-share.com`, com criptografia de ponta a ponta usando uma chave derivada do código de convite. O relay repassa os dados sem poder lê-los. Ele vê o seu endereço IP, quando você esteve conectado e quantos bytes passaram. As outras pessoas da sessão veem o seu nome, as suas edições, os samples que a sua parte usa e se você está tocando ou gravando. Elas não veem o seu endereço IP. Os detalhes estão na [política de privacidade](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Bom saber

- `Só a própria parte` é uma cortesia entre músicos, não proteção contra cópia. O pacote é um zip comum.
- Os dois lados devem usar a mesma versão do LYNX SHARE. O resumo do pacote avisa quando elas diferem.
- Um convidado cuja conexão cai reconecta sozinho e mantém as edições dele.
- Se o LYNX SHARE de quem criou a sessão fechar durante ela, na próxima abertura ele oferece retomá-la, e os convidados reconectam sozinhos.
- Se o LYNX SHARE de um convidado fecha durante uma sessão, na próxima abertura ele pergunta se quer voltar. `Voltar` leva você de volta como a mesma pessoa, com a sua parte e tudo o que você tinha mudado. Isso funciona por 12 horas.
- A versão para Linux pode hospedar uma sessão ao vivo desde a 0.1.26.
- Em uma sessão ao vivo, desfazer e refazer trazem de volta só as suas próprias edições.
- Qualquer pessoa com o código de convite pode pedir para entrar, mas só depois que quem criou a sessão a deixa entrar.

## Veja também

- [O que só o LYNX SHARE faz: sessões online](/why/#online)
- [Formatos de arquivo](/wiki/file-formats/)
- [Plugins](/wiki/plugins/)
- [Desfazer e histórico](/wiki/undo-history/)
- [Solução de problemas](/wiki/troubleshooting/)

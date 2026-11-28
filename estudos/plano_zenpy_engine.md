# Plano: melhorar o zenpy para embedding em engine

A Light é o destino; o zenpy é o que o Radion corre hoje e vai continuar a
correr durante a construção da Light. Estas são as melhorias ao zenpy que
pagam já — cada uma resolve uma dor concreta encontrada nas bindings do Radion
(ver `licoes_radion_zen.md`) — e que ao mesmo tempo aproximam a API do zenpy do
desenho da Light, para a migração ser um `sed` e não uma reescrita.

Fonte de verdade: `zenpy/libzen`. Propagar por cópia para Kinetix2D e Radion
(`vendor/zen/UPSTREAM.md` explica a cadeia). Duas correcções já feitas do lado
do Radion/Kinetix2D e **ainda não no zenpy** vão primeiro, senão a próxima
sincronização apaga-as.

Ordem = retorno por esforço. Cada item diz o que muda na API, o que muda nas
bindings do Radion, e como se verifica.

## 0. Subir o que já está corrigido a jusante

- `bytecode.cpp`: `can_write_global_class()` — classes nativas fora do
  bytecode serializado.
- `platform_time.h`: `#include <cstdint>`.
- `run_nested()` em `vm.cpp` — reentrância nativo→script sem sequestrar o fiber
  principal.
- GC a marcar `field_names` das classes nativas.

Verificação: `diff -rq` entre `zenpy/libzen` e `Radion/vendor/zenpy/libzen` só
mostra reformatação.

## 1. Assinaturas tipadas nos nativos

Hoje: `method(name, fn, arity)`. O nativo valida tudo à mão e um argumento do
tipo errado é UB (instância com zero campos lida como Vec3).

Proposta, compatível: `method_sig(name, fn, "ifsV?", ...)` ou um array de
`ArgKind` — `Int`, `Float`, `Bool`, `String`, `Instance(ObjClass*)`, `Any`,
`Optional(...)`. A VM valida em `call_native` e devolve erro de runtime com
o nome do método e a posição do argumento antes de entrar no nativo. `method()`
antigo continua a existir sem validação.

Radion: as ~150 bindings perdem os `if (nargs >= 1 && isVec3Instance(...))`;
`readVec3` deixa de precisar de guarda. Metade das linhas.

Verificação: teste que passa `self.node` a `set_position` e recebe erro com
mensagem em vez de crash.

## 2. Tipos de valor matemáticos no core

`Vec2`, `Vec3`, `Vec4`/`Quat`, `Color` e `Rect` definidos pela VM, não por
cada embedder. Duas opções, por ordem de preferência:

a) **Imediatos**: `VAL_VEC3` com 3×`f32` no payload de 16 bytes do `Value`
   (`value.h:31-49` já tem união com `sso` de 8 bytes; 12 bytes de floats + tag
   cabem). Zero GC, operadores no dispatch (`+ - * /`, `dot`, `length` como
   métodos intrínsecos). `Vec4`/`Quat` não cabem — vão para b).

b) **Pool não-GC**: classe nativa com `native_data` para um bloco de 16 floats
   numa freelist da VM, reciclado por contagem de referências simples ou por
   frame (`vm.reset_temporaries()` chamado pelo engine no fim do frame).

Radion: `makeVec3`/`readVec3` desaparecem; `get_position` devolve imediato;
`Beam.set_color` recebe `Color` em vez de `u32`; `Input.get_mouse_delta`
devolve `Vec2`. Remove a dependência que impedia `radion_script` de devolver
Vec3.

Verificação: benchmark de 500 `get_position()` por frame — alocações do GC = 0.

## 3. Métodos estáticos e constantes de classe

`ClassBuilder::static_method(name, fn, sig)` e `ClassBuilder::constant(name, Value)`.
`Input.is_key_down(...)` deixa de ser uma instância singleton disfarçada;
`KEY_W` vira `Key.W` em vez de 66 globais soltas.

## 4. Lookup de classe cacheado e proibido no hot path

`vm.class_ref("Vec3")` devolve um `ObjClass*` estável (rooted) para guardar
num `static`. Documentar: `get_global` por string dentro de um nativo é bug.
Com o item 2 a maior parte dos casos desaparece; este cobre `RaycastHit`,
`MoveResult` e afins.

## 5. `native_data` com tipo

`zen_instance_data<T>(value)` faz cast cego. Acrescentar `TypeId` na
`ObjClass` nativa (`ClassBuilder::type_id(u32)`) e
`zen_instance_as<T>(value, typeId)` que devolve `nullptr` se a classe (ou uma
ancestral) não tiver esse id. Custo: uma comparação.

## 6. Invalidação de handles dentro da VM

`vm.invalidate_native(void* pointer)`: marca toda a instância cuja
`native_data == pointer` como morta (`native_data = nullptr` + flag). Os
nativos já tratam `nullptr` como "vazio". Hoje o Radion tem de manter
`ScriptCache::mInstances` + `forgetInstance` fora da VM; com isto a cache
`ponteiro → instância` (`instanceFor`) passa para dentro da VM
(`vm.instance_for(klass, pointer)`), que é onde o GC a pode ver.

É o `PersistentStore` da Light em versão mínima. Migrar depois é renomear.

## 7. Registo de libs: uma vez, e só uma

- `resolve_native_globals()` não volta a correr `init_fn` de uma lib já
  aberta (flag `opened` por lib).
- `NativeLib::init_fn` documentado como "corre uma vez por VM".
- `open_lib` expõe também os nomes **não** qualificados, ou ganha uma variante
  `open_lib_unqualified`; e o comentário de `vm.h:132-135` diz o que cada uma
  faz de facto (import vs globals vs qualificação).

Radion: cai a guarda manual em `scriptModuleInit`.

## 8. Hash de bindings no bytecode

Header do `.bc` ganha `bindings_hash` = hash de (nomes de libs, funções,
classes, métodos, na ordem de registo). Bytecode com hash diferente é
recompilado em vez de carregado. Elimina a classe de bugs "adicionei um nativo,
a cache antiga chama o índice errado".

## 9. Reflexão de classe compilada

`vm.class_fields(klass, out)` → nome, valor por omissão, tipo inferido do
default; `vm.class_methods(klass, out)` → nome, slot, aridade. O compilador já
sabe tudo isto. `ScriptCache` deixa de ler o texto fonte para descobrir campos
do `__init__`, e o inspector do Radion passa a mostrar também os tipos.

## 10. Erros com contexto de chamada

`vm.invoke(...)` que falha deve deixar em `last_error()` o caminho do script,
a linha, e a pilha (frames com nome de função e linha). Hoje `ZenBehaviour`
guarda `lastError` mas a mensagem nem sempre traz a linha do script quando o
erro nasce dentro de um nativo. Formato estável para o Console do editor
apontar ao ficheiro.

## 11. GC incremental com orçamento

`vm.gc_step(budget_bytes)` para o engine chamar no fim do frame, em vez de
colectas completas disparadas por limiar. É o modelo da Light; ter-lo no zenpy
primeiro dá o benchmark de pausas com um jogo real (Radion) antes de a Light
existir. Medir p95 da pausa com 500 `ZenBehaviour`.

## 12. Build de stress do GC

`ZEN_GC_STRESS`: colecta antes de cada alocação + verificação de invariantes.
Teria apanhado o bug dos `field_names` no primeiro teste. Correr a suite do
zenpy e os `ZenBehaviourTests` do Radion com ele ligado.

## Ordem sugerida

| Fase | Itens | Porquê primeiro |
| --- | --- | --- |
| A | 0, 7, 8 | Correcções e invariantes; sem código novo de risco. Desbloqueiam sincronizar sem medo. |
| B | 1, 5, 4 | Segurança das bindings; corta metade do código do Radion e elimina UB. |
| C | 2, 3 | Performance e ergonomia; o benchmark de `get_position` é o critério. |
| D | 6, 9, 10 | Aproximam a API da Light (`PersistentStore`, reflexão, `Error`). |
| E | 11, 12 | GC; medir antes de desenhar o da Light. |

Critério para cada fase: os testes do zenpy passam, `ZenBehaviourTests` do
Radion passa com a cópia nova, e o exemplo `zombie_encounter` em script (quando
existir) corre sem regressão de frame time.

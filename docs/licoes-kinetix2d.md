# Licoes do Kinetix2D + Zen para os bindings da Light

Este documento e o que aprendemos a embeber o Zen no Kinetix2D (motor + editor 2D), escrito
para ser lido quando a Light chegar a fase de bindings de engine (M4/M5 do `plano.md`).
Tudo o que aqui esta foi lido no codigo ou medido; onde ha numero, ha medicao. Onde o
`plano.md` ja decide bem, digo-o e dou o numero que o justifica; o resto e o que o plano
ainda nao cobre.

Referencias de codigo apontam para o Kinetix2D em `scripting/src/ZenScriptComponent.cpp`
salvo indicacao. Numeros da camada de bindings, para escala:

| Metrica | Valor |
| --- | --- |
| Ficheiro de bindings | 7527 linhas, um so ficheiro |
| Natives `int nat*(VM*, Value*, int)` | 512 |
| Classes `def_class` (Node + componentes) | 51 |
| Natives globais `def_native` (draw_*, audio_*, nav_*, get_number...) | 96 |
| Entradas na tabela de aliases de componentes | 48 |
| Chamadas a `valueToCString` com buffer local | 71 (24 de 16 bytes, 3 de 32, 5 de 128) |
| Scanner de propriedades exportadas (regex sobre o fonte) | 318 linhas |

## A. Decisoes do plano confirmadas por medicao

### PersistentStore com `{index, generation}` e `vm.invalidate()` — certo

O Zen guarda um `void* native_data` cru no `ObjInstance`. O motor compensou com tres
estruturas por fora: um `HashMap<void*, handle>` para devolver sempre o mesmo handle
(`a == b` em script), um `forgetInstance(ptr)` chamado do destrutor do `GameObject`, e uma
lista `liveInstances` como raiz do GC. Mesmo assim o ASan apanhou um heap-use-after-free
em Play: `forgetInstance` apagava a entrada do mapa mas o handle que um script ja tinha na
mao ficava a apontar para memoria libertada (`Scene::flushDisposed` -> `delete` ->
`natNodeIsActiveInHierarchy` no frame seguinte). A correccao foi por `native_data = nullptr`
no `forgetInstance` — um remendo, porque o VM nao tem conceito de "o dono morreu".

Medido em Release, 2M chamadas: `HashMap find` 2.3 ns, leitura de campo 0.2 ns. O custo do
mapa e irrelevante; **o problema e de correccao, nao de tempo**. O `PersistentStore` do plano
resolve as duas coisas de uma vez: identidade e `==` vem do `PersistentId`, e um acesso
depois de `invalidate()` falha com `Error` em vez de desreferenciar. Nao precisa de mapa,
nao precisa de `forget`, nao precisa de marcar N handles no GC (ver A.6).

Regra a manter: **o host chama `invalidate()` no destrutor do objecto**, nunca "quando se
lembra". No Kinetix o gancho e `GameObject::SetRemovedCallback` + `Component::SetRemovedCallback`;
na Light e o mesmo sitio.

### `<T>` resolvido para `TypeId` no compilador — certo, e o numero e grande

`node.get_component<RigidBody>()` no Zen e baixado para `get_component(RigidBody)` com a
classe como argumento. O binding le `requested->name->chars` e faz `strcmp` linear numa
tabela de 48 entradas x 3 aliases (`findComponentBinding`). Medido: **250.8 ns** para a
ultima entrada. No Kinetix salva-se porque os scripts cacheiam handles no `on_start` (25
chamadas, zero no `on_update`) — mas e uma regra de disciplina, nao de desenho. Com `<T>`
a chegar como `TypeId` imediato, a chamada e um indice; a tabela de aliases desaparece.

### Natives por descriptor indexado, nao por nome — certo

Os 96 globais do Kinetix sao um namespace plano (`draw_rect`, `audio_play_at`,
`nav_point_free`, `set_number`...). Funciona, mas o `ModuleBuilder` do plano (`gfx.draw_rect`,
`audio.play_at`) e melhor por tres razoes vistas na pratica: o IDE consegue completar,
o stub para o editor de texto gera-se por modulo, e desaparecem as colisoes de nome quando o
jogo define as suas proprias funcoes.

### Assinaturas estritas verificadas no compilador — certo

No Zen de `main` (o que o Kinetix vendoriza) `f(name=value)` compila e liga mal, em silencio;
a correccao existe mas esta num branch por fazer merge (`vm-fiber-error-and-len`). Perdemos
tempo com isso mais do que uma vez. Com assinaturas estritas isto e erro de compilacao — e a
aridade tambem: hoje 118 natives de joints repetem `if (joint && nargs >= 2)` a mao.

Nota sobre GC, porque afecta o orcamento de frame: no zen o `invoke()` a partir de C++ nao
pausa o GC, e a colecta e um mark-and-sweep completo disparado pelo alocador quando
`bytes_allocated > next_gc` — ou seja, cai a meio de um `on_update` qualquer, sem limite de
duracao. O `gc_step(budget)` incremental do plano e a resposta certa; ate la, a politica
de engine e `gc_pause` no inicio do frame e `gc_resume` + colecta num ponto fixo no fim.

### Retornos multiplos com aridade declarada — certo

E usado por todo o lado no Kinetix: `x, y = node.get_global_position()`,
`found, x, y = anim.get_real_point(0)`, `hit, hx, hy = raycast(...)`,
`enabled, speed, torque = joint.get_motor()`. Sem isto os bindings devolviam arrays
temporarios. O `_` para descartar (`ok, _, _ = ...`) tambem faz falta — hoje escreve-se
`hit, hit_x, hit_y` e o IDE avisa que `hit_x` nao e usado.

### `Error` por chamada, VM continua utilizavel — certo, e falta hoje

No Zen `had_error` e um estado do VM. O editor, ao ve-lo, **pausa o Play inteiro**
(`EditorApplication.cpp`, `mScriptErrorPaused = true`, toast "Script error - Play paused").
Um inimigo com um bug para o jogo todo. O plano ja tem `Error` como valor de retorno de
`vm.execute()` e das natives; a consequencia para a engine e a que interessa: **quarentena por
instancia de script**, nao por VM. O erro desactiva aquele componente (e regista-o), o resto
do frame corre.

### Strings tipadas e `u8` — certo, e ha um caso concreto

Fazer word-wrap de um texto em script (`thought_bubble.py`) exigiu `text[i]`, que no Zen
aloca **uma string nova de 1 byte por caracter** (`new_string(&s->chars[idx], 1)`). Com `u8`
como tipo real, `s[i]` devolve o byte e nao aloca. Vale para qualquer parsing em script.

## B. O que o plano ainda nao cobre

### B.1 Um componente, um registo — hoje sao cinco sitios

Adicionar **um** componente ao scripting do Kinetix toca em:

1. `#include` do header;
2. tabela de aliases `kComponentBindings[]` (para `get/add/has/remove_component`);
3. bloco `vm.def_class(...)` com os metodos;
4. tabela `def_global` de aliases longos (`{"ActionSequence2D", actionSequenceClass}`);
5. `Serializer::RegisterType(...)` com create/write/read/match;
6. `switch` do Inspector no editor (8 referencias so para um componente).

Sao seis fontes de verdade para o mesmo facto. A Light tem `TypeBuilder`; a licao e que o
**descriptor de tipo tem de ser consumivel por mais do que o compilador**: serializador,
inspector do editor, gerador de stubs para IDE e documentacao devem todos ler a mesma tabela.
Proposta: `TypeBuilder` guarda, por metodo e por campo, nome, assinatura, `TypeId`, e um
`flags` (`Inspectable`, `Serialized`, `ReadOnly`). O motor regista uma vez; o resto e
iteracao sobre o registry depois de `freeze_bindings()`.

### B.2 Reflexao de classes de script para o editor

O Inspector do Kinetix mostra propriedades exportadas de um script. Como o Zen nao expoe
metadados de classe, isso e feito com um **scanner textual de 318 linhas** que procura
`self.x = literal` dentro de `__init__` e `name = literal` ao nivel da classe
(`ZenScriptProperties.cpp`). Fragil por construcao: um `self.x = a + b` nao e propriedade,
uma constante de modulo so e resolvida se estiver no mesmo ficheiro.

A Light tem tipagem estrita e slots de campo conhecidos em compile-time. Expor isso pela API
(`ClassInfo { fields[] { SymbolId name; TypeId type; Value default; u32 slot; } }`) faz o
Inspector e o serializador de overrides lerem a classe compilada, nao o fonte. Falta decidir
uma anotacao de export (`export f32 speed = 160.0f;` dentro da classe, ou tudo o que e campo
publico com default e exportavel). O Radion usa o mesmo padrao (`ScriptProperty`), portanto o
formato deve servir os dois motores.

Licao dentro da licao: o zen **ja tem** isto — `zen_script_info.h` (`script_class_properties`,
`check_script_contract`, `find_script_class`), no commit mais recente de `main` e no submodulo
vendorizado do Kinetix. Nem o Kinetix nem o Radion o usam; ambos mantiveram o scanner de texto.
Expor metadados no VM nao chega: o host tem de ser levado a usa-los — o que na Light significa
que o `TypeBuilder`/`ClassInfo` e **a unica** forma de o editor obter propriedades, sem
caminho paralelo pelo fonte. E o `check_script_contract` (typo `on_updte`, aridade errada,
hook nunca chamado) e diagnostico de load-time que a Light deve ter desde o primeiro host.

### B.3 Mensagens entre scripts no mesmo no

Um `GameObject` pode ter varios componentes de script (a camara do tokenkay tem 4). O padrao
de comunicacao e `node.call("take_damage", value)`, que **percorre todos os scripts do no e
invoca o metodo em cada um**. No Zen, invocar um metodo que a classe nao define e erro de
runtime (`vtable slot is nil`) — por isso cada script precisa de **stubs vazios** para as
mensagens dos vizinhos (`rojas.py` ganhou 5 stubs, `thought_bubble.py` 2). E o `call()` so
passa um `double`.

Para a Light: ou `try_call(instance, symbol, args)` barato que devolve "nao existe" sem
`Error`, ou — melhor, com tipagem estrita — eventos declarados (`event take_damage(f32)`) que
o compilador resolve para um slot e a engine despacha por indice. O `has_method` ja existe no
motor (`hasFunction`, via `find_selector` + vtable) mas o caminho de `call` nao o usa.

O host anterior (zenengine, em `zenpy_projects`) tinha isto melhor resolvido: **sinais** com
argumentos tipados (`emit_signal("hit", {int, float, ptr})`, `connect`, `_on_signal(name,
args)`) e um bitmask de hooks presentes por instancia, calculado uma vez com `find_selector`,
para nunca invocar o que nao existe. O Kinetix regrediu para `call(name, um_double)` mais um
blackboard com chaves de string. O desenho dos sinais e o que deve passar para a Light; o
codigo nao (e STL).

### B.4 Ciclo de vida do host e callbacks: onde e que um native pode mutar a cena

Ordem do frame no Kinetix: `Scene::update` (scripts) -> `flushDisposed` (delete) ->
`compactComponentLists` -> `stepBodies` (fisica, que despacha `on_collision`). Isto significa
que um script em `on_collision` pode chamar `spawn()` (que faz `push_back` em
`mAllComponents` a meio do passo de fisica) ou `queue_destroy()` (que so e honrado no frame
seguinte porque o flush ja passou). E `removeBodyContactEvents` dispara callbacks `End`
enquanto o corpo esta a ser desmontado.

Licao para os bindings da Light, independente do VM: **cada native que muta topologia deve
saber em que fase esta**. O mais simples e robusto: natives de criar/destruir enfileiram
sempre e a engine aplica num ponto unico do frame; natives que so leem/escrevem valores podem
correr em qualquer callback. A Light pode ajudar com um `CallContext` (ver B.5) que diga a
native em que fase do frame foi chamada, mas a politica e da engine.

### B.5 Contexto de execucao por globais e reentrancia

O Kinetix passa contexto as natives por globais: `gZenCallbackNode` (o no que esta a correr,
para `spawn`/`raycast` acharem a `Scene`), `gZenDrawContext` (fila de render de `on_draw`),
`gZenAssets`, `gZenGameCamera`. Um `ZenCallbackScope` RAII salva/repoe. Funciona ate um
native reentrar no VM (um `call()` dentro de `on_collision` dentro de `stepBodies`).

A Light ja tem `void* userdata` na `VmConfig`. Falta o **contexto por chamada**: a native
recebe `(Vm&, CallContext&, args)` onde `CallContext` traz `Value self`, `PersistentId`
do dono e um `void* frame_ud` que a engine define ao invocar (`vm.invoke(fn, args, ctx)`).
Sem globais, reentrancia fica correcta por construcao.

### B.6 Unidades na fronteira: decidir uma vez, antes do primeiro binding

Encontrado no review dos joints: toda a API de script do Kinetix e em **graus**
(`set_rotation` -> `setRotationDegrees`, scripts fazem `math.degrees()`), mas os joints
expostos passavam **radianos** crus porque o C++ e em radianos. `set_limits(True, -90, 90)`
dava 14 voltas, sem erro. A politica nunca tinha sido escrita.

Para a Light: escrever a politica de unidades (angulos, tempo, cor 0..1 vs 0..255,
pixels vs unidades de mundo) **no mesmo documento que regista o primeiro modulo**, e
codificar no nome quando houver ambiguidade (`set_rotation_deg`). Com tipos estritos e
`LightData`, `Degrees`/`Radians` como structs distintos e possivel, mas provavelmente
excessivo; nome + politica chega.

### B.7 Setters C++ tem de registar intencao

Tres bugs do mesmo tipo nos joints: `DistanceJoint::setLength` nao colapsa `min/max`
(default 100/100), por isso `set_length(300)` da uma barra rigida de 100; `RevoluteJoint::
setLocalAnchorB` nao marca `mAnchorsConfigured`, por isso `onConnected` sobrescreve o valor
do script; `WheelJoint::setLocalAxisA` nao normaliza. O serializador **mascarava** os tres
com `setLengthConfigured(true)`/`setAnchorsConfigured(true)` explicitos no fim do read, e o
Inspector tambem — so o script os apanhava, porque o script chama o setter cru.

Licao: o binding expoe o setter C++ tal como e. Se o setter nao deixa o objecto num estado
completo, o bug aparece primeiro em script. Antes de expor um tipo, ler os setters com a
pergunta "e se so chamarem este?".

### B.8 Strings na fronteira: view, nunca copia truncada

`valueToCString(vm, v, small, sizeof(small))` e chamado 71 vezes com buffers de 16, 32 ou
128 bytes; strings pequenas do Zen sao copiadas e **truncadas em silencio** ao tamanho do
buffer. Um caminho de asset com mais de 128 caracteres num `spawn()` ou `set_texture()` falha
sem aviso. O `ObjString` da Light guarda `length`; a API de natives deve dar
`StringView { const char* bytes; u32 length; }` sem copia, e o `print` da `VmConfig` ja
recebe bytes+length — manter isso em toda a fronteira.

### B.9 Referencias entre objectos: nao por nome, nao por frame

Medido com 401 objectos: `scene.find("Player")` custa **7204 ns** (travessia recursiva com
`strcmp`). `rojas.py` chamava-o ate 4x por inimigo por frame: 20 inimigos = **0.576 ms =
3.5% de um frame de 60 fps**, gasto a procurar sempre o mesmo no. O blackboard
(`set_number`/`get_number`) e um `ct::Vector` com comparacao de string por chamada.

Para a Light/engine: (1) referencias a objectos como **propriedade exportada de primeira
classe** (arrastar no editor, resolvida uma vez para `PersistentId`), (2) chaves de
blackboard como `SymbolId` internado no compilador, nao string em runtime, (3) `find` por
nome existe mas e API de arranque, e por id e O(1) com `PersistentStore`.

### B.10 Cache de classes compiladas e obrigatorio; medido

Teste de 256 objectos criados e destruidos em ondas: com uma classe partilhada por caminho
(`loadFile`), **0 bytes** de crescimento na arena do VM; com compilacao por objecto
(`loadSource`), **8466 bytes por objecto que nunca voltam** (2.1 MB em 256). O cache de
modulos do plano (M5) cobre isto — a regra e que **a engine nunca compila por instancia**,
e um prefab com script referencia o modulo por caminho canonico. O `ScriptCache` do Radion
(cache por ficheiro, versao de classe, geracao no reset) e a referencia de desenho.

Reload a quente: o Kinetix guarda `classVersion` e `generation` por instancia e recria a
instancia de script quando a classe muda; `on_start` volta a correr. Isso funciona e os
scripts ja assumem idempotencia no `on_start`. Vale escrever como contrato.

### B.11 Coroutines: o caso de uso e AI

`rojas.py` e uma maquina de 14 estados com temporizadores (`state_timer`), 560 linhas, e
o bug de "fica preso em CHASE" veio de um estado que nao verificava a condicao de saida. Com
coroutines (M6) o mesmo comportamento escreve-se como
`chase(); wait(0.3); shoot(); wait(0.5);`, e o compilador garante que se sai. O plano ja
proibe `&` atraves de `yield`; falta so garantir que o custo de `resume` por frame por
entidade e medido antes de recomendar isto como padrao.

### B.12 Ferramentas que fizeram diferenca (e as que faltaram)

- **Profiler com contadores do VM** (`vm.update`, `vm.render`, `vm.other` em ms e chamadas,
  ligados ao profiler da engine): encontrou um Inspector a gastar 137.5 ms de um frame de
  141.3 ms (63 serializacoes da cena por frame) numa unica captura. Manter na Light um
  hook barato de tempo por chamada native e por `invoke`.
- **ASan+UBSan em Debug**: apanhou em uma execucao o use-after-free que dias de leitura nao
  tinham apanhado. O plano ja o tem no CI. Armadilha encontrada: `option()` do CMake so
  aplica o default na primeira configuracao — ficou `ON` num cache que passou a Release e o
  editor correu a 6 fps sem nada no output a dizer porque. A Light ja separa
  `LIGHT_ENABLE_ASAN`; acrescentar uma `message(STATUS ...)` quando estiver ligado.
- **Faltou `check`**: para validar sintaxe de scripts sem abrir o editor tive de escrever
  um harness C++ contra a `libzen.a`. Uma CLI `light check ficheiro.light` (compila, nao
  executa, erros com linha:coluna e codigo estavel) deve existir desde M1 — o plano ja tem o
  disassembler desde M1, e a mesma familia.
- **Faltou stub para o IDE**: o editor de texto marca 40 avisos "not defined" por ficheiro
  (`ScriptComponent`, `set_flag`, `raycast`...) porque nao sabe o que a engine regista.
  Depois de `freeze_bindings()` a VM tem tudo para emitir um ficheiro de declaracoes;
  fazer parte do `TypeBuilder` (B.1).

### B.13 Limites dimensionados para uma engine real

Um teste de bundle de bytecode do Kinetix falha com `too many selectors in bytecode`. A
escala real de uma engine 2D pequena, para dimensionar `limits.h`: 51 classes, ~600 metodos
registados, 96 funcoes globais, 512 natives, scripts de 500-600 linhas, cenas com 400
objectos e 20-30 scripts activos por frame. Os limites do plano falham com `Error` — bom;
os defaults tem de acomodar isto sem tocar em nada.

### B.14 GC: a store e a raiz, nao os handles

O `markRuntimeRoots` do Kinetix percorre o mapa de handles inteiro a cada colecta para
marcar objectos que, sendo `persistent`, **nem estao na lista do GC**. Trabalho nulo por
handle. Com `PersistentStore`, o GC marca a store (uma raiz) e os wrappers `Userdata`
morrem sozinhos. Confirmar que os campos de um `Userdata` (o Kinetix da ao script um campo
`node` no `ScriptComponent`) sao marcados pelo wrapper, nao pela store.

## C. Como ficaria um componente na Light

Esboco com os nomes do plano, para o primeiro binding de engine servir de molde:

```cpp
// Uma vez, no arranque da engine. Tudo o resto le esta tabela.
ModuleBuilder physics = vm.begin_module("physics");

TypeBuilder distance = physics.external_type("DistanceJoint", ComponentTypeId::Joint);
distance.parent(joint);                                   // herda is_connected, anchors
distance.method("set_length", nat_distance_set_length, NativeSignature::fixed(1))
        .doc("Rest length in world units; also collapses an unconfigured range");
distance.method("set_length_range", nat_distance_set_length_range, NativeSignature::fixed(2));
distance.method("set_spring", nat_distance_set_spring, NativeSignature::fixed(2))
        .doc("frequency_hz, damping_ratio");
distance.field("length", TypeId::F32, Flags::Inspectable | Flags::Serialized);
distance.field("spring_frequency", TypeId::F32, Flags::Inspectable | Flags::Serialized);

vm.freeze_bindings();
```

Depois do freeze: o serializador itera `fields` com `Serialized`; o Inspector itera
`Inspectable`; `light stubs` emite `class DistanceJoint extends Joint { ... }` para o IDE;
`entity.add<DistanceJoint>()` chega a VM como `TypeId`. Um sitio.

Native com contexto, sem globais e sem `nargs` a mao (a assinatura ja validou):

```cpp
static Status nat_distance_set_length(Vm& vm, CallContext& ctx, Args args)
{
    DistanceJoint2D* joint = ctx.self_as<DistanceJoint2D>();   // Error se invalidado
    if (!joint) return Status::Error;
    joint->setLength(args.f32(0));
    return Status::Ok;
}
```

## D. Checklist para o primeiro binding de engine

- [ ] Politica de unidades escrita (angulos, tempo, cor, espaco) antes do primeiro metodo.
- [ ] `invalidate()` chamado nos destrutores de `GameObject` e `Component` da engine.
- [ ] Natives de criar/destruir enfileiram; a engine aplica num ponto unico do frame.
- [ ] Nenhum global de contexto: `CallContext` por chamada.
- [ ] Strings so por `StringView`; nenhum buffer local com truncagem.
- [ ] Um descriptor por tipo alimenta compilador, serializador, inspector e stubs.
- [ ] Erro de script desactiva a instancia, nao o VM nem o Play.
- [ ] Cache de modulos por caminho; nunca compilar por instancia.
- [ ] `light check` e `light stubs` na CLI antes de haver editor.
- [ ] Profiler: tempo e contagem por native e por `invoke`, exportados para a engine.
- [ ] Teste de destruicao sob ASan: pai destruido com filho scriptado; handle guardado por
      outro script; auto-destruicao em callback; dois scripts no mesmo no; reutilizacao de
      slot apos `invalidate()` — os cinco cenarios que escrevi para o Kinetix, mais o que
      la falhou e aqui nao pode falhar: acesso a `PersistentId` de geracao antiga.

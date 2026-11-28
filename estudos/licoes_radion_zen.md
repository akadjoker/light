# Lições do Radion × Zen para a Light

O Radion embebe o Zen (`vendor/zenpy/libzen`) como linguagem de gameplay: um
componente `ZenBehaviour` por GameObject, uma classe de script por componente,
uma VM partilhada por todo o processo, e ~3200 linhas de bindings em
`runtime/scene/src/SceneScriptBindings.cpp` + `runtime/script/src/ScriptModule.cpp`.
Isto é o que esse embedding ensinou — o que resultou e deve ser desenho de raiz
na Light, e o que doeu e a Light tem de tornar impossível. Cada ponto aponta
para a linha onde se vê.

Onde é que um ponto já está no `plano.md`, digo-o; o objectivo não é repetir o
plano, é dar-lhe as provas.

## O que resultou (levar para a Light)

### 1. Uma VM, uma compilação por script, uma instância por objecto

`ScriptCache` (`runtime/scene/include/ScriptCache.h`) compila cada ficheiro uma
vez, chave = caminho (ou o texto, para scripts inline), e cada `ZenBehaviour`
recebe uma instância barata dessa classe. 500 balas com o mesmo script = 1
compilação + 500 instâncias. Os slots da vtable de `__init__`/`on_start`/
`on_update`/... resolvem-se **uma vez por compilação** (`ScriptCache::Entry`,
`initSlot..onEventSlot`) e a chamada por frame é `vm.invoke(instance, slot, args)`
(`ZenBehaviour.cpp:485`) — por índice, nunca por nome.

Light: já está no plano (slots resolvidos no compilador). O que acrescentar: o
host precisa de uma API "dá-me o slot do método X desta classe, ou -1" no
momento do bind, e uma garantia de que o slot é estável enquanto a classe viver.

### 2. Handles por id, não ponteiros — e a validade não depende da árvore

Um `GameObject` em script é `(Scene*, id)`; cada acesso resolve `findGameObject(id)`
(`SceneScriptBindings.cpp`, `resolveGameObjectById`). Um objecto destruído deixa
de resolver em todo o lado no instante em que a Scene esquece o id, sem passo de
invalidação. E um objecto ainda em fila (criado mas antes do flush do
`Scene::update`) já resolve, porque o id é carimbado no `createGameObject`, não
no flush.

Light: é exactamente o `PersistentId {index, generation}` do plano — isto é a
prova de que funciona em jogo real. A regra "validade do handle não depende do
estado da hierarquia" tem de ficar escrita na API.

### 3. Instâncias-handle cacheadas por ponteiro nativo

`ScriptCache::instanceFor(klass, pointer)` (`ScriptCache.cpp:424`) devolve
sempre a mesma instância de script para o mesmo componente C++ — `get_component`
por frame não aloca. Mas o host tem de chamar `forgetInstance(pointer)` quando o
componente morre — lifecycle a cargo do embedder, fácil de esquecer.

Light: o `PersistentStore` com `invalidate()` é a generalização certa. O que o
Radion mostra é que a cache "ponteiro → wrapper" tem de estar **dentro** da VM,
não no host: o host só chama `persist`/`invalidate`.

### 4. Classes persistentes e não-construíveis

`persistent(true).constructable(false)` (`vm.h:152-153`) marca uma classe cujas
instâncias o GC não gere e que o script não pode criar — é assim que
`Camera`, `RigidBody`, etc. são "coisas do engine que o script só toca".

Light: `TypeBuilder` + `external_type` do plano cobre isto. Manter os dois bits
separados (não-GC vs não-construível): há classes de valor (RaycastHit) que são
GC mas não construíveis.

### 5. Propriedades declaradas no corpo da classe → inspector

Um campo com valor no corpo da classe (`speed = 1.5`) vira propriedade que o
inspector do editor edita e a cena serializa **por instância** (overrides), sem
o script saber (`ZenBehaviour::declaredProperty*`, `setNumberOverride`).

Light: o compilador conhece os campos e os defaults estaticamente — expor isso
como metadata da classe (nome, tipo, default) na API do host. Ver "o que doeu"
nº 9: hoje o Radion lê o **texto fonte** para descobrir campos declarados no
`__init__`, o que é o sintoma de a VM não dar essa metadata.

### 6. Multi-retorno funciona e é o que faltava para Vec2

`x, y = Input.get_mouse_delta()` — o compilador remenda `nresults`
(`compiler_statements.cpp:1938`) e o dispatch copia N resultados
(`vm_dispatch.cpp`, `copy_native_results`, também no caminho de método
`OP_INVOKE` ~3359). Light já o tem no plano (`result_count: u8`, `_`).

### 7. GC pausado à volta de cada nativo

`call_native` (`vm_dispatch.cpp:26-32`) pausa o GC — um nativo pode fazer
`new_array` + `push` sem um objecto intermédio ficar sem raiz. Simples e
suficiente para bindings de engine.

Light: equivalente. Alternativa (rooting explícito por handle) é mais
"correcta" mas foi precisamente o que o quickjs mostra ser fonte de bugs; o
custo de pausar o GC durante um nativo de engine é zero na prática.

### 8. Bytecode: classes nativas não se serializam

O patch local `can_write_global_class()` em `bytecode.cpp` (ver
`vendor/zen/UPSTREAM.md`) existe porque sem ele a cache de bytecode escrevia
callbacks C++ e corrompia a imagem. Light já diz "natives não são serializados"
— acrescentar ao header `.lbc` um **hash do conjunto de bindings** para rejeitar
bytecode compilado contra outra tabela de natives (índices diferentes = crash
silencioso).

## O que doeu (tornar impossível na Light)

### 1. `self` em `args[-1]`

Toda a binding começa por `zen::as_instance(args[-1])`. É convenção do
`ClassBuilder`, não está no tipo, e um nativo registado como função livre em
vez de método lê lixo. Light: `this` no slot 0, explícito na assinatura do
descriptor — já no plano.

### 2. Validação de tipo à mão, em cada nativo, ×200

Cada nativo repete `if (nargs >= 1 && isVec3Instance(args[0]))`. E
`is_instance()` não chega: as instâncias-handle têm zero campos, e um script
que passe `self.node` onde vai um `Vec3` dereferencia `fields[0]` a null. Daí
os guardas ad hoc `num_fields >= 3` e `klass == gGameObjectClass`
(`SceneScriptBindings.cpp`, `isVec3Instance`, `readGameObject`). Isto é o
grosso das 3200 linhas: 150 métodos × 15 linhas de boilerplate.

Light: descriptor com assinatura tipada, VM valida `TypeId` **antes** de chamar
— o nativo recebe argumentos já certos, corpo de 3 linhas. É a decisão com maior
retorno de todo o plano. Não a adiar para "depois da API manual".

### 3. Sem métodos estáticos

`Input.is_key_down(KEY_W)` só foi possível criando uma **instância** singleton
e pondo-a na global `Input` (`ScriptModule.cpp`, `scriptModuleInit`) — o zen só
chama métodos em instâncias. Light: funções de módulo (`gfx.function`) cobrem o
caso; permitir também `static` em classe para que `Input` e `Math` não sejam
módulos disfarçados.

### 4. Cada `get_position()` aloca um Vec3 no heap do GC

`makeVec3` (`SceneScriptBindings.cpp`) faz `make_instance` a cada chamada, e
antes disso `findClass(vm, "Vec3")` = `get_global` com **lookup por string** no
hot path. Um script com 500 agentes a ler posição por frame produz 500 objectos
de lixo por frame e 500 hashes.

Light: é o argumento mais forte a favor de tipos de valor matemáticos
**imediatos**. `Value` tem 16 bytes; tag `u8` + 3×`f32` = 13 bytes — cabe.
`Vec2`, `Vec3`, `Color` (4×`u8`) como `ValueKind` próprios, sem GC, com
operadores no dispatch. Isto não está no plano e merece um benchmark antes de
M4: é a diferença entre "script pode correr a lógica de 500 zombies" e não.
Fallback se não couber: `Vec4`/`Quat` como `LightData` numa pool do host.

### 5. Cores como `u32` empacotado, Vec2 inexistente

Sem tipo de cor no script, `Beam.set_color(0xFFRRGGBB)` — funciona, é opaco.
Sem Vec2, `get_size()` devolve dois floats. Consequência directa do nº 4.

### 6. Módulo base não pode devolver tipos do módulo de cima

`Input` vive em `radion_script` (liga só `radion_core`); `Vec3` vive nas
bindings de `radion_scene`. Resultado: `get_mouse_delta` não pode devolver Vec3.
Lição de organização, não da VM: os **tipos de valor** (math, cor) pertencem à
camada mais baixa dos bindings, registados antes de tudo o resto.

### 7. `init_fn` corre mais de uma vez

`VM::resolve_native_globals()` (`vm.cpp:631-635`) volta a correr o `init_fn`
de **todas** as libs sempre que carrega bytecode — e o ScriptCache carrega
bytecode. Um `init_fn` que faça `def_class("Input")` redefine a classe a cada
load. Teve de levar guarda manual. E `open_lib` vs `register_lib` vs
`open_lib_globals` (`vm.cpp:542-622`) têm semânticas que só se percebem a ler o
fonte (`open_lib` guarda os nomes qualificados `"radion.version"`, o import é
outro caminho).

Light: `freeze_bindings()` resolve por desenho — registo acontece uma vez,
antes de tudo, e depois é imutável. Escrever isso como invariante verificado:
`register` depois de `freeze` = erro.

### 8. Argumento a mais ignorado em silêncio

`on_collision(self, other)` antigo continuou a funcionar quando se passou a
chamar com `(self, other, began)` porque "o zen não verifica a contagem numa
chamada invocada de nativo — escreve o registo extra e nunca o lê"
(`ZenBehaviour.h`, `callCollision`). Conveniente, mas é comportamento
acidental.

Light: com assinaturas estritas isto **quebra** scripts antigos quando o engine
evolui um callback. Precisa de regra explícita: o host resolve callbacks por
nome **e aridade** no bind e adapta (chama a forma que o script declarou). Não
deixar ao acaso.

### 9. Metadata de campos lida do texto fonte

Para saber que campos um `__init__` declara, `ScriptCache` **lê o ficheiro
fonte** (`Entry::properties`, "reading the source for anything a constructor
declares"). É o sintoma do nº 5 de "o que resultou": a VM sabe os campos e não
os expõe. Light: API de reflexão de classe compilada (campos, tipos, defaults,
métodos e slots) — barata, porque tudo isto já existe no compilador.

### 10. Reentrância: `VM::run` sequestrava o fiber principal

Script que chamava nativo que chamava script corrompia as frames do chamador;
corrigido com `run_nested()` (memória do projecto: `zen-vm-run-reutiliza-main-fiber`).
Light: nativo→script→nativo é o caso **normal** num engine (`on_collision`
dispara de dentro do `stepPhysics`, que foi chamado de `on_update`). Testar
reentrância a 3 níveis desde M2, não descobrir em produção.

### 11. GC não marcava `field_names` das classes nativas

Classes nativas perdiam os nomes dos campos à primeira colecta (memória:
`zen-gc-nao-marca-field-names`). Light: o `gc-stress.yml` do plano (colecta
antes de cada alocação) apanha exactamente isto — e a raiz do bug foi "objecto
criado pelo host sem passar pelo caminho normal de alocação". Todo o objecto
criado pelo host passa pelo mesmo `alloc` da VM, sem excepção.

### 12. Registar um componente são três sítios

`def_class` + `cache.setComponentClass(ComponentType, klass)` + linha na tabela
`kScriptComponents` (para `get_component(Tipo)`). Esquecer um = `get_component`
devolve nil sem erro. Light: `entity.get<Sprite>()` com `TypeId` resolvido no
compilador (já no plano) reduz a um sítio. Manter assim.

### 13. Camada de compatibilidade de aridade por inteiro

`method(name, fn, arity)` com `-1` = variádico, e opcionais por convenção
"`nargs >= 2 ? ... : nullptr`". Light: min/max no descriptor (já no plano).

## Números que valem a pena medir na Light (com o Radion como baseline)

- Custo de `invoke(instance, slot, 1 arg)` por componente por frame — o Radion
  faz 1 por `ZenBehaviour`; 500 objectos é o caso de teste.
- Alocações por frame de um script que lê `get_position()` de 500 objectos —
  no Radion são 500 instâncias GC + 500 `get_global("Vec3")`. Na Light o alvo é
  zero.
- Lookup de classe por nome vs. índice fixo.
- Tempo de `get_component` com cache (Radion: hash em `unordered_map<void*>`)
  vs. `TypeId` directo.

# BlueprintLisp semantic fidelity and data access

Source integration includes the three repair rounds. These updates do not replace already published ZIPs.

Multicast delegate calls now round-trip as `(broadcast :delegate OnDamage :args (:Result result))`, with optional `:owner`/`:object` target expressions. Import resolves the existing delegate property without creating a signature function graph. Explicitly missing `break-struct :field` names fail instead of selecting another output. See the [broadcast guide](DelegateBroadcasts.md) and `BlueprintLisp.Delegate.*` runtime fixtures.

## 语义保真扩展

源码中的语义保真扩展（不代表已发布二进制的能力）：

- 外部属性读取使用 `(get-member :owner "/Script/Engine.LightComponent" :name Intensity :object Target)`，保留目标对象及所属类；可嵌套，并与 `break-struct` 组合。
- `class</Script/UMG.UserWidget>` 表示带基类约束的 class reference，区别于实例 object reference，也可用于 array/set/map 的元素和值类型。
- 已执行调用的数据输出使用 `(node-output :node "12345678" :pin "OutHits")` 引用该调用的 `:id`。同名输出不会从其他调用中猜测；关闭可选稳定 ID 时仍保留数据依赖必需的 ID。
- 无法表达的纯节点、缺少值的关键字、多余位置参数、未知成员、悬空输出引用和内嵌 impure 调用会返回错误。导入错误不代表通用资产回滚；调用方必须检查结果。
- 最小复现和运行回归见 `BlueprintLisp.Semantic.*` 以及 [回归测试说明](../Tests/Regression/README.md)。

### Semantic fidelity extensions

Source-only semantic fidelity extensions (not a claim about released binaries):

- `(get-member :owner "/Script/Engine.LightComponent" :name Intensity :object Target)` retains the owning class and target object. Reads can nest and compose with `break-struct`.
- `class</Script/UMG.UserWidget>` preserves the class-reference category and base constraint, including array/set/map element and value types.
- `(node-output :node "12345678" :pin "OutHits")` references an already executed call's `:id`. Output names are never guessed across calls. IDs required by data dependencies remain even when optional stable IDs are disabled.
- Unsupported pure nodes, missing keyword values, excess positional arguments, unknown members, dangling output references, and nested impure calls return errors. A failed import does not imply general asset rollback; callers must check the result.
- Minimal reproductions and runtime checks are under `BlueprintLisp.Semantic.*`; see the [regression guide](../Tests/Regression/README.md).

## Map 与 DataTable 数据访问


Map 调用继续使用普通函数语法：`Map_Add` 的 `:TargetMap` 会先建立键/值类型，因此关键字顺序不会清空先前的字面量键。`Map_Find` 可用 `:out-pin "ReturnValue"` 读取 Found，用 `:out-pin "Value"` 读取值，两者也可分别绑定到 `let`；存在且值为零与缺少键是不同结果。

数据表行使用独立执行形态，并保留两个出口与同一次查行的 OutRow：

~~~lisp
(get-data-table-row
  :table (asset "/Game/Data/DT_Items.DT_Items")
  :row RowName
  :row-struct "/Script/MyGame.ItemRow"
  :id "1badc0de"
  :found (ConsumeRow :Row (node-output :node "1badc0de" :pin "ReturnValue"))
  :not-found (HandleMissingRow))
~~~

示例中的资产、行类型和处理函数由调用工程提供。动态 `:table` 表达式必须声明 `:row-struct`；固定表资产可推断行类型，显式声明时须与资产一致。OutRow 可与 `break-struct` 组合读取字段。后续执行放在 `:found` / `:not-found` 内。缺表、类型不符或无效输出引用会返回错误。`BlueprintLisp.DataAccess.*` 覆盖 Map 与数据表的运行回归。

### Map and DataTable access

Map calls retain ordinary function syntax. `Map_Add` establishes `:TargetMap` key/value types before assigning other arguments, preserving literal keys regardless of keyword order. `Map_Find` exposes Found as `:out-pin "ReturnValue"` and the value as `:out-pin "Value"`; explicit `let` bindings honor that selection. A present zero value remains distinct from a missing key.

`get-data-table-row` retains `:table`, `:row`, `:row-struct`, `:found`, `:not-found`, and `:id`. Branches reference that lookup's row using `(node-output :node "1badc0de" :pin "ReturnValue")`, optionally composed with `break-struct`. Dynamic table expressions require `:row-struct`; a fixed table can infer it, and an explicit type must match the asset. Continuations belong inside the two branch bodies. Missing tables, incompatible types, and invalid output references fail explicitly. Runtime coverage is under `BlueprintLisp.DataAccess.*`.

## Saved graph maintenance

The explicit maintenance API is available on UE 5.8+; older engines report unsupported. See the [maintenance and regression guide](../Tests/Regression/Maintenance.md).

## Integration validation

Integrated Blueprint2DSL source through commit `1e66c573c3a1a2dcc828172ace24e220af15c9e9`, retaining GeneralBlueprint's existing UE 5.0 ImportText compatibility guard and module layout. The UE 5.8 integration host passed 90 related Automation tests and 68 fresh-process fixture reload checks. Older-engine packages were not rebuilt in this synchronization; the maintenance API explicitly reports unsupported before UE 5.8.

# BlueprintLisp delegate broadcasts

源码新增多播委托广播语法（已发布 ZIP 未更新），对应 `K2Node_CallDelegate`：

~~~lisp
(broadcast :delegate OnRecvDamageFromEnemy)
(broadcast :delegate OnTakeDamageType
  :args (:ResultType result :DamageRule (self.DamageRule)))
(broadcast :delegate OnPlayerEnterDeath
  :owner "/Script/MyGame.InsideMsgRouterComponent"
  :object (get-member :owner "/Script/MyGame.PlayerModel"
    :name InsideMsgRouter :object (GetModel)))
~~~

示例中的委托、参数名、类和函数由宿主工程提供。省略 `:owner`/`:object` 时广播 self 上的委托；外部目标必须同时提供两者，`:owner "self"` 表示当前 Blueprint 的类。`:args` 为参数 Pin 名与表达式的关键字列表，可省略无参列表。目标与参数支持嵌套纯表达式、`get-member` 和已执行节点的 `node-output`；impure 目标函数须先在执行链中调用，再引用其输出。该形态直接解析现有的 Blueprint-callable 多播委托属性，不创建 `__DelegateSignature` 同名函数图，也不声明委托或建立订阅。错误的委托、参数或目标会使导入失败；`validate_dsl` 只检查语法，属性与类型在导入时检查。订阅者接收及导出/导入回归见 `BlueprintLisp.Delegate.*`。

## English

Source also supports `K2Node_CallDelegate` as `(broadcast :delegate OnDamage :args (:Result result :DamageRule rule))`; a no-argument broadcast can omit `:args`. Omit `:owner` and `:object` for self, or provide both for an external target: `(broadcast :delegate OnDeath :owner "/Script/MyGame.Router" :object RouterExpression)`. Targets compose with nested `get-member` reads, pure calls and `node-output` references to previously executed impure producers. Payload keywords live inside `:args` to avoid collisions with metadata. Import resolves an existing Blueprint-callable multicast property and never creates a signature function graph. This does not declare delegates or bind subscribers. `validate_dsl` checks syntax; import checks properties and types. Subscriber delivery and native graph roundtrips are covered by `BlueprintLisp.Delegate.*`. Published ZIPs are unchanged.

## Validation

UE 5.8 fixtures cover native and Blueprint-defined dispatchers, enum/GameplayTag payloads, nested object targets, previously executed impure producers, subscriber delivery and removal, and stable node reuse. Explicitly missing `break-struct :field` names fail instead of selecting another compatible output. Use `--filter BlueprintLisp.Delegate` or run the full `BlueprintLisp` suite; see [the regression guide](../Tests/Regression/README.md). These fixtures do not qualify a complete game or other engine/platform combinations.

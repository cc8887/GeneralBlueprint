# 让 Unreal 蓝图成为 AI 可读写的语言：类 Lisp 风格 DSL 插件的设计与实现

当 AI 开始进入 Unreal Editor，真正昂贵的往往不是“理解需求”，而是“理解资产”。Blueprint、Animation Blueprint 和 Material 都以图形化资产存在于编辑器中，原始 `.uasset` 是二进制；如果改用 T3D 一类通用文本导出，虽然终于可以读取，却会带来大量对象声明、类路径、引脚描述、GUID、编辑器坐标和重复连接信息。模型需要先消耗很长的上下文还原编辑器对象，才能看到其中真正的逻辑。

[BlueprintLisp](https://github.com/cc8887/Blueprint2DSL)、[AnimBP2FP](https://github.com/cc8887/AnimationBP2FP-N) 和 [MatBP2FP](https://github.com/cc8887/MaterialBP2DSL) 采用了另一条路线：不把 Unreal 对象逐字段摊平，而是把资产翻译成 Lisp 风格的领域专用语言。据当前内部接入测试所采用的资产样本和 tokenizer 口径，这组 DSL 相比 T3D 可节省约 90% 的 token。换句话说，过去用于描述一个图的十份上下文，现在大约一份就能表达其主要语义。

这个数字应被理解为当前测试口径下的工程结果，而不是对所有资产都恒定成立的理论比例。资产复杂度、是否包含布局、使用哪一种 tokenizer，都会影响最终数值；目前三个代码仓库也尚未收录这项对照实验的可复现基准脚本。但它揭示了更重要的一点：最大的压缩并不来自缩短字段名，而是来自提高表示层级。T3D 主要描述“编辑器里有哪些对象以及它们怎样连接”，DSL 则直接描述“这段逻辑、动画或材质在做什么”。

## 三个插件概览

这三个插件可以先理解为同一套“AI 资产语言体系”的三个领域前端。它们都使用 S-expression、AST 和双向转换，但面对的不是同一种图：BlueprintLisp 处理程序逻辑，AnimBP2FP 处理动画行为，MatBP2FP 处理着色表达式。三者的主要特点和差异如下。

| 项目            | 面向的资产                                              | 原图的核心特点                                                      | DSL 的建模策略                                                                    | 最突出的优势                                    |
| ------------- | -------------------------------------------------- | ------------------------------------------------------------ | ---------------------------------------------------------------------------- | ----------------------------------------- |
| [BlueprintLisp](https://github.com/cc8887/Blueprint2DSL) | `UBlueprint` 的 EventGraph、FunctionGraph、MacroGraph | 执行流与纯数据流混合，具有事件入口、分支、函数边界和宏出口                                | 将执行链还原为 `seq`、`branch`、`cast` 等程序结构，将纯节点嵌套为表达式                               | 生成结果接近源码，AI 可以直接阅读和修改业务逻辑，而不必逐条追踪 K2 连线   |
| [AnimBP2FP](https://github.com/cc8887/AnimationBP2FP-N)     | `UAnimBlueprint`、状态机、动画层，以及关联的 Control Rig         | 既有以 Root Pose 为汇点的 Pose 图，又有 Cached Pose 共享、状态拓扑、K2 逻辑和跨资产依赖 | AnimLang 表达 Pose 与状态机，嵌入 BlueprintLisp 保存 K2 子图，并通过 RigLang 模块引用 Control Rig | 保留动画领域语义和依赖顺序，能够分层校验、编译和重建复合动画资产          |
| [MatBP2FP](https://github.com/cc8887/MaterialBP2DSL)      | `UMaterial` 与 Material Function                    | 无执行流的表达式 DAG，节点可以共享、拥有多个输出，并连接多个材质结果槽                        | 用显式 `$id`、`(connect $id output-index)` 和 `outputs` 精确表示 DAG                  | 在不复制共享子图的前提下保持文本紧凑，并适合做悬空引用、环检测和 AST Diff |

因此，这组项目并不是三个名称不同的图序列化器。BlueprintLisp 的关键词是“程序化”，AnimBP2FP 的关键词是“分层组合”，MatBP2FP 的关键词是“显式 DAG”。后文先介绍它们共同选择 Lisp DSL 的原因，再分别展开三条实现路线。

## 从通用序列化到语义表达

YAML 和 JSON 已经比 T3D 更适合机器处理，但它们首先是通用数据容器。要描述一个节点，通常仍需定义 `type`、`id`、`inputs`、`connection`、`target`、`output` 等层层字段。以材质乘法节点为例，一个通用 YAML schema 可能写成：

```yaml
node:
  type: multiply
  id: mul1
  inputs:
    a:
      connection:
        target: tex1
        output: 0
    b:
      connection:
        target: tint
        output: 0
```

同样的信息在 MatLang 中可以直接写成：

```lisp
(multiply $mul1
  :a (connect $tex1)
  :b (connect $tint))
```

这里的压缩来自语言本身的组合能力：表单首符号同时承担节点类型和操作语义；括号同时表达边界与嵌套关系；高频参数可以使用位置参数，可选参数使用 `:keyword`；原子符号不必普遍加引号；默认值、无语义的编辑器信息和零号输出索引还可以按语法约定省略。对于普通 Blueprint，纯节点可以直接嵌套为表达式，控制流可以写成 `branch`、`seq`、`switch-int` 或宏调用，不必再为每个节点和每条边重复声明端点。

因此，DSL 相比 YAML/JSON 的优势并不只是少几个标点，而是能够用“语言结构”承载信息。YAML 和 JSON 当然也能保存同样的数据，但一旦为它们设计出节点类型、引用规则、控制结构和校验约束，实际上同样是在发明一门语言，只是把语言藏在了通用容器里。

## 专用 DSL 的工程优势

专门设计 DSL 的第一个优势，是语法可以成为明确的契约。解析层能够识别括号、字符串、数字、布尔值、关键字、标识符和注释，并把文本变成 AST。三个项目的实现细节和诊断精度并不完全相同：AnimLang 与 MatLang 拥有独立 Tokenizer、Parser 和源码 span，BlueprintLisp 使用直接递归下降 Parser，并在 AST 节点上保留起始行列。至少在括号、字符串和顶层结构出错时，系统可以给出明确位置，而不必等到 Unreal 完成资产创建后才发现问题。

第二个优势，是错误可以分层处理。语法分析先回答“这是不是合法的语言”；各插件再按自身覆盖范围检查 ID、引用、图不变量和部分领域约束；最后由 K2 Schema、资产自身的校验机制和 Unreal 编译器回答“它是否真能成为有效资产”。这三层的完整度因项目而异：MatLang 能在写入前发现重复或悬空 ID、DAG 环和未知材质槽；BlueprintLisp 当前的独立 `Validate` 主要覆盖解析与顶层表单，更深的引脚兼容性通常要到导入和编译阶段才能确认。这种分层反馈仍然非常适合 AI 的工作方式：生成文本，先做可用的静态检查，再根据诊断修正局部并写回测试资产。这里的“容易纠错”不是指插件已经能自动修复一切，而是错误被结构化、定位并缩小到可迭代处理的范围。

第三个优势，是可以利用模型已有的语言先验。Lisp、Scheme、Clojure 及大量 S-expression 代码具有极其规则的基本语法：少量原子类型、平衡括号、首元素表示操作，其余元素表示参数。相比某个项目临时约定的多层 YAML/JSON schema，这组插件作出的工程判断是：模型更有机会复用已有的函数调用与表达式组合先验，从而减少结构错误。领域表单仍然需要文档、示例和节点 stub，但模型不必同时学习一套任意的容器层级和一套隐藏在容器中的语义。这个判断符合当前设计经验，但仍值得通过统一错误率基准进一步量化。

第四个优势，是 AST 把文本格式与 Unreal 实现解耦。三个项目的主链路都可以概括为：

```text
Unreal 资产
  -> Exporter
  -> 领域 AST
  -> Printer
  -> Lisp DSL

Lisp DSL
  -> Tokenizer / Parser
  -> 领域 AST
  -> 语法与语义检查
  -> 可选的 Diff / Patch
  -> Importer
  -> Unreal 资产
```

一旦各项目的语义进入各自 AST，相应的中间表示就能成为统一输出、静态分析、依赖查询、语义 diff、增量更新和往返验证的基础，各项目按自身成熟度选用这些能力。未来还可以增加其他后端，将 AST 转为 JSON、YAML、Graphviz、文档、测试代码或另一种脚本语言，而不必再次从复杂的 Unreal 对象图中提取语义。当前三个项目的主要后端仍是各自 DSL 与 Unreal 资产，其他语言生成属于架构已经打开、但尚未普遍实现的扩展能力。

三者共享的是语言工程基础设施，使用的却是各自的领域 AST、图不变量和导入后端。共同基础到这里已经清楚，下面分别看三个插件如何把这些原则落实到不同类型的 Unreal 资产上。

## [BlueprintLisp](https://github.com/cc8887/Blueprint2DSL)：把 K2 图还原成程序

普通 Blueprint 图同时包含两种关系。白色执行线决定语句的先后顺序，数据引脚则形成纯表达式的依赖网络。若只是逐节点导出，就会得到一个充满节点 ID 和边记录的通用图；BlueprintLisp 的核心做法，是从事件、函数入口或宏入口出发，把图重新结构化成程序。

导出器沿执行引脚向下遍历。连续语句折叠成 `seq`；Branch、Sequence、Switch 和 Cast 等节点转为带嵌套 body 的控制表单，ForEachLoop 等复合行为则可表示为宏调用；没有执行引脚的纯节点从输入引脚反向递归，成为调用参数中的表达式。无语义的 reroute 节点可以被穿透，编辑器坐标默认省略，完整 GUID 则压缩为图内仍保持唯一的短稳定 ID。于是模型看到的不再是“节点 A 的 Then 连到节点 B 的 Execute”，而是接近源码的表达：

```lisp
(event BeginPlay
  (set Health 100)
  (branch (IsValid Target)
    :true (PrintString "ready")
    :false (PrintString "missing")))
```

底层 AST 保持 Lisp 的通用结构：Nil、Symbol、Keyword、Number、String 和 List。目标 Blueprint 与图名由调用方指定，顶层的 `event`、`function` 或 `macro` 决定该图中的导入语义。Importer 创建或复用 `UK2Node`，恢复字面量和纯表达式，并通过 `UEdGraphSchema_K2` 连接引脚。Wildcard 类型在连接完成后通过节点重建传播，最后再编译 Blueprint，并把编译错误和警告返回给调用方。

这种设计覆盖的不只是 EventGraph。函数图需要保留 Entry、Result、参数、返回值、局部变量和函数元数据；宏图需要处理 Tunnel 与多个出口；输入事件、组件绑定事件、父类调用和多输出宏也都有不同的语义表单。插件还可以导出 `UK2Node` stub，供 AI 或 lint 工具查询节点签名、引脚方向、类型和是否为纯节点。

稳定 `:event-id` 和 `:id` 已用于 MergeAppend 模式下复用兼容的现有节点，从而保留一部分对象身份和编辑器状态。当前独立的 semantic-diff `Update` 公共接口仍待完成，因此更准确的说法是：BlueprintLisp 已具备基于稳定 ID 的合并复用能力，而完整语义增量更新仍在演进中。它的 `Validate` 也不等于完整的写入前语义检查，实际工作流仍应在测试资产上执行导入、Schema 连线与编译验证。

## [AnimBP2FP](https://github.com/cc8887/AnimationBP2FP-N)：把复合动画资产分层表达

Animation Blueprint 远比“带几个动画节点的 Blueprint”复杂。一个资产中同时存在目标 Skeleton、AnimInstance 变量、Root Motion 设置、主 AnimGraph、Cached Pose、状态机、Transition Rule、Animation Layer、普通 EventGraph/FunctionGraph，以及 AnimSequence、Montage、BlendSpace、Chooser、PoseSearch、Control Rig 等外部依赖。

主 Pose 图有一个很有价值的特点：它通常从最终 Root Pose 向输入方向展开，天然接近一棵嵌套的函数表达式树。因此 AnimLang 可以把 Sequence Player、Blend Space、Layered Blend、Additive、State Machine 等高频动画节点写成领域表单，Pose 输入直接成为命名子表达式。对于暂未专门适配的动画节点，Exporter 还会保存精确类路径、非 Pose 属性和命名 Pose 子节点，并标记 `exact`、`reflected`、`lossy` 或 `unsupported` 覆盖等级；Importer 可以对不可靠的降级结果选择硬失败，而不是静默创建一个看似成功、实际错误的资产。

Pose 图又不完全是树。Save Cached Pose 可能被多个 Use Cached Pose 共享，因此导出器把缓存提升为顶层 `(define ...)`，使用处变成引用，并按依赖关系排序定义。这既保留共享语义，也避免把同一大段 Pose 子图重复展开。

状态机则需要另一层专门表示。Exporter 读取初始状态、普通 State、Alias、Conduit 和 Transition；每个 State 内部仍是一棵 Pose 子图，Transition 还带来源、目标、优先级、混合时长以及条件规则。Importer 必须先创建状态节点和内部图，再恢复 Alias 目标、Entry 连接和 Transition，最后重建条件图及相关 AnimGetter 绑定。它不是普通节点列表能够可靠覆盖的结构。

AnimBP2FP 没有重复发明 K2 逻辑语言。普通 EventGraph、FunctionGraph，以及复杂的 Transition/Conduit Rule Graph，会以嵌入的 BlueprintLisp 子语言保存；AnimLang 专注 Pose、状态机、动画层和动画资产元数据。非 Pose 引脚的属性绑定、变量引用和复杂纯表达式分别编码为 `bind-path`、`bind-var` 和 `value-expr`；更复杂的值计算可以提升为 BlueprintLisp helper graph，再经生成变量和 `subgraph-ref` 桥回 Pose 图。这是一种语言组合，而不是把所有内容强塞进一个 AST。

Control Rig 又形成了第三个边界。Rig 资产内部拥有 bone、control、null、curve hierarchy，多套 transform、变量、RigVM 节点与连线、函数和公开执行入口。项目因此使用独立的 `.riglang` 模块保存 Rig 的权威结构；AnimLang 只通过带 hash 的 `import-rig`、`rig-ref`、`rig-entry` 和有精确类型的输入绑定引用它。严格 bundle 模式会先检查模块 hash、依赖 DAG、公开入口和输入类型，再按依赖顺序在 transient staging 中编译并复核全部 Rig 与 Anim；只有通过重新导出的文本门禁后才创建和保存持久资产，失败则阻止或回滚整组导入。

动画资产的导入顺序同样是领域知识的一部分：先建立接口与生成类，再创建变量和辅助逻辑图，随后处理动画层和 Cached Pose，最后重建 Root Pose 树并编译。更新时，Differ 按命名 Pose 引脚、稳定 NodeId，并在必要时按同类型位置匹配节点，生成路径化差异；只有属性变化时可以原位 Patch，结构变化或 Patch 失败则回退到完整重建。AnimBP2FP 因而不仅是文本序列化器，更像一个面向动画资产的编译与装配管线。

## [MatBP2FP](https://github.com/cc8887/MaterialBP2DSL)：用显式引用保存材质 DAG

所谓“材质蓝图”在 Unreal 类型系统中实际是 `UMaterial`，并不继承 `UBlueprint`。它使用 `UMaterialExpression`、材质输入槽和自己的编译生命周期，也没有 K2 执行引脚。这已经决定了它不能直接复用 BlueprintLisp 的 Importer。

材质图的核心结构是有向无环图。一个 Texture Coordinate、Fresnel 或参数节点可以同时供多个下游节点使用；许多表达式还具有多个输出。若把它强行递归展开为树，共享子图会被复制，语义和 token 都会变差。因此 MatLang 为每个表达式分配显式 `$id`，连接同时记录目标和输出序号：

```lisp
(material "M_SimplePBR"
  :domain surface
  :blend-mode opaque
  :shading-model default-lit

  (expressions
    (texture-sample $tex :texture (asset "/Game/Textures/T_Brick"))
    (vector-parameter $tint :name "Tint" :default (1 1 1 1))
    (multiply $base
      :a (connect $tex 0)
      :b (connect $tint 0)))

  (outputs
    :base-color (connect $base 0)))
```

材质的终点也不是单一 Root，而是 Base Color、Metallic、Roughness、Normal、Emissive Color 等多个具有固定含义的输出槽，所以它们被集中表达在 `outputs` 中。

Exporter 采用两遍扫描。第一遍为所有表达式分配 ID，第二遍再导出节点属性和连接；这样无论引用目标在文本中的前后顺序如何，都能正确表示共享 DAG。常量、参数、纹理、UV、Fresnel、Custom HLSL、Material Function Call 和 Named Reroute 等节点有专门适配，其余节点可通过反射只保存相对类默认对象发生变化的属性；部分可选专用属性也会省略默认值。这种有选择地消除默认信息，是 MatLang 保持紧凑的另一个重要原因。

Importer 同样分为两个阶段：先创建所有 `UMaterialExpression` 并建立 `$id -> UObject` 映射，再恢复节点输入、输出序号和材质结果槽。这一策略天然支持前向引用和共享节点。写入前的 Linter 会检查重复 ID、悬空连接、负输出序号、未知材质槽和 DAG 环，并给出规则号与源码位置；这些已覆盖的错误可以在资产发生修改前被拒绝。表达式类、具体输入引脚名和输出序号上界等问题仍可能到导入阶段才暴露，这是当前静态检查的边界。

AST Differ 以 `$id` 匹配表达式。属性、输入连接或输出槽变化通常可以原位 Patch；节点增删、节点类型或 Material Domain 等结构变化则触发完整重建。目前 `$id` 并未作为独立语义字段永久写入 `UMaterialExpression`，因此最可靠的增量工作流是“先导出、保留 ID、就地编辑、再导入”。如果 AI 从头生成一套完全不同的 ID，结果仍可通过全量重建实现，但不会获得增量 Patch 的收益。

Material Function 可以作为独立文档导出，函数调用在材质中保留资产引用，批量导出服务还能建立包含调用位置的依赖索引。当前主要双向目标是基础 `UMaterial`；Material Instance 尚不在支持范围，独立 Material Function 也仍以导出、签名和依赖管理能力为主。当前文本同样以语义图为主，不承诺节点坐标、注释框等编辑器布局元数据往返保真，布局可以在导入后交给 AutoLayout 重建。这些边界使 DSL 的可重建承诺保持清晰。

## 插件拆分的语义边界

从表面看，三类资产都有“节点”和“连线”，似乎可以统一成一份 JSON 图结构。但这种统一只发生在最低、也是最冗长的层级。到了真正需要导入和验证时，差异会立刻出现：

- 普通 Blueprint 必须区分执行流、纯表达式、事件入口、函数边界和宏出口。
- Animation Blueprint 必须理解 Pose 类型、状态机嵌套、Cached Pose、动画层、Skeleton 生成类和 Control Rig 依赖顺序。
- Material 必须维护表达式 DAG、多输出索引、多个材质结果槽以及材质自己的编译与函数引用规则。

如果把这些语义都放进一个通用插件，最终不是得到一个简单统一的系统，而是得到大量 `assetKind` 分支、可选字段和互相冲突的不变量。现在的拆分更接近编译器的做法：共享 S-expression 和工具思想，但为每一种源语言建立合适的 AST、语义检查和后端。BlueprintLisp 还可以作为通用 K2 子语言被 AnimBP2FP 复用，说明模块化边界并没有阻碍复用，反而让复用发生在正确的层次。

## 引擎版本范围与局部图修改

三个插件目前的引擎版本口径并不完全相同。这里需要区分“代码中已经做了兼容处理”和“仓库明确记录为已验证”两件事，不能仅凭存在版本宏就把一个引擎版本视为完整支持。

| 插件 | 当前版本范围 | 验证口径与边界 |
| --- | --- | --- |
| [BlueprintLisp](https://github.com/cc8887/Blueprint2DSL) | 代码面向 UE 4.27，以及 UE 5.1-5.8 做了兼容处理 | 公开文档当前明确完成验证的是 UE 5.5；UE 5.0 未在支持范围内，其余目标版本应在实际工程中重新编译并运行回归测试 |
| [AnimBP2FP](https://github.com/cc8887/AnimationBP2FP-N) | 对外 README 当前声明 UE 5.6+ | UE 5.6 是公开版本明确展示的安装与验证基线；接入更高 UE5 小版本时，仍应重新验证 AnimGraph、BlueprintLisp 和 Control Rig/RigVM 相关流程 |
| [MatBP2FP](https://github.com/cc8887/MaterialBP2DSL) | UE 4.27，以及 UE 5.0-5.8 的每个小版本 | 仓库记录了 Windows 下逐版本的 Editor 与 Game target 通过矩阵，并通过兼容层处理材质 API 在 UE4/UE5 之间以及 UE5 各小版本中的变化 |

其中 MatBP2FP 的跨版本边界最明确：兼容层集中处理 UE 4.27 的裸指针与 UE5 的 `TObjectPtr`、UE 5.1 的材质编辑器数据迁移、UE 5.2 的头文件拆分、UE 5.3/5.5 的输入枚举变化，以及 UE 5.8 的初始化委托差异。BlueprintLisp 和 AnimBP2FP 在用于正式项目时，则更适合把仓库声明的版本作为起点，在目标引擎上重新执行编译、导入、编译资产和往返测试，而不是把一个宽泛的版本号直接等同于所有节点与资产组合都已验证。

文本 DSL 带来的另一个直接收益，是局部修改和代码评审变得可行。`.uasset` 最终仍然是二进制文件，但同版本保存的 DSL 副本可以提供可读的 Git diff。当导出时省略编辑器坐标等非语义信息，并保留节点 ID 与结构时，一次参数修改通常只表现为对应 S-expression 的少量行变化：

```diff
 (scalar-parameter $roughness
   :name "Roughness"
-  :default 0.35)
+  :default 0.50)
```

这比比较整个 T3D 对象块或二进制资产更容易审查：评审者能够直接判断变化发生在哪个节点、哪个属性或哪条连接上，合并冲突也更接近普通源码冲突。这里的“diff 更友好”指 DSL 文本的变化记录，而不是让 `.uasset` 本身变成可读文本；它也依赖稳定的 ID 和导出顺序，因此更准确的表述是“通常把差异收敛到相关语义”，而不是保证任何一次导出都产生绝对最小 diff。

从 DSL 局部编辑到 Unreal 资产局部写回，三个插件的实现成熟度有所不同：

| 插件 | 当前局部写回方式 | 回退与限制 |
| --- | --- | --- |
| BlueprintLisp | 通过稳定的 `:event-id` 和 `:id`，在 `MergeAppend` 中复用类型与语义兼容的现有节点，可以针对事件、函数或宏的目标子图写回，并尽量保留节点身份与其他入口 | 目标子图仍可能被断开并重新连线；独立的 `UpdateSemantic` / AST semantic diff 接口尚未完成，因此它不是逐引脚的通用 Patch |
| AnimBP2FP | 先导出现有 AST，再与新 AnimLang 做结构化 Diff；仅包含 AnimGraph 节点属性变化时，在原节点上应用 Patch 并保留节点位置 | 节点增删、变量、Cached Pose 定义、辅助图、逻辑图或元数据等结构变化，以及 Patch 失败，都会自动回退到完整重建；嵌入的 K2 图和 RigLang 也不应笼统视为已支持原位 Patch |
| MatBP2FP | 以 `$id` 匹配表达式，可原位修改材质属性、表达式属性、输入连接和输出槽 | 节点增删、节点类型或 Material Domain 等结构变化会完整重建；当前 `$id` 未永久写回 `UMaterialExpression`，所以最可靠的流程是导出后保留 ID、就地编辑再导入 |

因此，从使用者视角看，三者都支持“只修改现有 DSL 中与需求相关的一小段”；从写回机制看，AnimBP2FP 和 MatBP2FP 已经具备属性级 Diff/Patch 与结构变化回退，BlueprintLisp 当前则以稳定 ID 驱动的局部复用式合并为主。即使某次修改最终需要全量重建，语义 DSL 仍然保留了更短、更清晰的审查记录；而在能够命中增量路径时，还可以减少无关节点重建、GUID 漂移和布局变化带来的噪声。

## 从文本导出走向 AI 资产工程

这组三个项目的价值，不只是让二进制资产“可以复制成文本”。它们建立了一层面向 AI 的语义接口：文本足够短，可以节省上下文；语法足够规则，便于稳定生成；AST 足够明确，可以承载检查、diff 和后续转换；Importer 又能把结果带回 Unreal，并接受各项目已经实现的静态检查、资产校验、编译和往返测试。

实践中的推荐闭环是：先导出目标资产或目标图，保留稳定 ID；让 AI 在已有 DSL 上做局部修改；先运行该插件现有的 parser、validate 或 lint；再导入测试资产并编译；最后重新导出，比较 AST 或 Printer 输出文本。这个闭环把“AI 猜测编辑器操作”变成“AI 修改一门可验证的语言”。

下一步值得继续统一的，并不是三类资产的领域 AST，而是它们周围的语言工具：一致的诊断协议、编辑器补全、格式化器、Language Server、基准测试和更多 AST 后端。尤其是 90% token 节省这一结果，未来可以固定资产集合、布局选项和 tokenizer，持续发布 T3D、YAML/JSON 与各 DSL 的可复现实验数据。

当图形化资产有了稳定的文本语言，AI 与 Unreal 的关系也随之改变：模型不再只是远程点击编辑器，而是能够阅读、生成、检查和重构资产语义。这正是 Lisp 风格 DSL 在这组插件中的核心意义。

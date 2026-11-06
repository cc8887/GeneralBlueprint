# BlueprintLisp Regression Tests

## 资产无关的语义保真回归（UE 5.8）

`Source/BlueprintLisp/Private/Tests/BlueprintLispSemanticTests.cpp` 从原生 K2 节点构造最小源图，再导出、导入到全新 Blueprint、编译并执行。覆盖外部对象及结构体拆分读取、class 参数和容器、同名调用的多输出归属、纯节点多输出共享以及静默损失拒绝。测试不依赖 WUCHANG 语料或生产资产。

先在宿主工程编译插件，再运行：

```powershell
python .\Tests\Regression\run_semantic_regression.py `
  --editor "<UnrealEditor-Cmd.exe>" --project "<Host.uproject>" `
  --output "<一个新的结果目录>"
```

`--filter BlueprintLisp` 可运行整个 BlueprintLisp Automation 测试集。`--save-fixtures` 会把测试资产保存到宿主的 `/Game/BlueprintLispTests/Round1/<本次唯一编号>/`，路径记录在结果目录的 `fixtures.json`；随后设置 `BLUEPRINTLISP_FIXTURE_MANIFEST` 为该文件绝对路径，通过新 Editor 进程运行 `verify_semantic_reload.py`，验证磁盘重载后的类型和行为。可用 `BLUEPRINTLISP_RELOAD_RESULT` 指定重载报告路径。输出目录保留 Automation 报告、Editor 日志与 JSON 摘要。宿主插件若在退出阶段阻塞，运行器只会在完整测试报告落盘后终止自己启动的进程，并在摘要中单独记录。

这些测试证明对应夹具的编译和运行行为，不证明完整游戏逻辑、所有 K2 节点或所有引擎版本的兼容性。

第二轮数据访问回归位于 `BlueprintLispDataAccessTests.cpp`，可用同一运行器加 `--filter BlueprintLisp.DataAccess` 执行。覆盖 Map 的 Found/Value 显式绑定、关键字顺序、结构体值、零值与缺键、覆盖写入，以及 GetDataTableRow 的固定/动态表、拆分字段、found/not-found 分支、切换表、未消费 OutRow 和错误拒绝。ReplaceGraph 与 MergeAppend 均有运行断言。

加 `--save-fixtures` 后，第二轮资产保存在 `/Game/BlueprintLispTests/Round2/<本次唯一编号>/`，同样记录于 `fixtures.json`。使用新 Editor 进程运行 `verify_data_access_reload.py`，并设置 `BLUEPRINTLISP_FIXTURE_MANIFEST` 与可选的 `BLUEPRINTLISP_RELOAD_RESULT`，验证原 Map 的变更以及数据表重载后的行内容和两个执行出口。完整 `--filter BlueprintLisp` 同时覆盖两轮测试；对应的两个重载脚本分别检查自己的夹具。

保存图迁移、旧数组字面量、引用参数和执行链缺失的最小复现，以及 UE 5.8+ 维护 API 的使用边界，见 [Maintenance.md](Maintenance.md)。

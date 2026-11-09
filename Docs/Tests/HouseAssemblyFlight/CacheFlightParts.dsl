; 仅供 /Game/test/AssemblyEffect/BP_HouseAssemblyReveal 使用的蓝图源逻辑。
; 先缓存每个零件的完整世界变换与包围盒，之后的飞行动画不会反过来污染归位数据。
; 此函数只在 BeginPlay 执行一次，所有数组下标都对应 PartComponents 中的同一个零件。
(fn CacheFlightParts ()
  (bind parts (Actor|GetComponentsByClass :self self :ComponentClass "/Script/Engine.StaticMeshComponent"))
  (Variables|运行数据|SetPartComponents parts)
  (bind (center extent) (Collision|GetActorBounds :self self :bOnlyCollidingComponents false :bIncludeFromChildActors false))
  (Variables|运行数据|SetAssemblyCenter center)
  (Variables|运行数据|SetAssemblyExtent extent)
  (Variables|运行数据|SetAssemblyBottomZ (- (.z center) (.z extent)))
  (Variables|运行数据|SetAssemblyTopZ (+ (.z center) (.z extent)))
  (Utilities|Array|Clear (Variables|飞入运行数据|GetPartTransforms))
  (Utilities|Array|Clear (Variables|飞入运行数据|GetPartCenters))
  (Utilities|Array|Clear (Variables|飞入运行数据|GetPartBottomHeights))
  (Utilities|Array|Clear (Variables|飞入运行数据|GetFlightOrder))
  ; 无零件时明确停止并报错，不切换成整体显现之类的其他行为。
  (if (== (Utilities|Array|Length parts) 0)
    (Variables|飞入运行数据|SetFlightReady false)
    (Actor|Tick|SetActorTickEnabled :self self :bEnabled false)
    (Utilities|Debugging|RaiseScriptError "飞入装配失败：该测试蓝图没有静态网格零件。"))
  (for part parts
    (bind mesh (Utilities|Casting|CastToStaticMeshComponent :Object part)
      (:then
        (bind target (Transformation|GetWorldTransform :self mesh))
        (bind (partCenter partExtent radius) (Collision|GetComponentBounds :Component mesh))
        (Utilities|Array|Add (Variables|飞入运行数据|GetPartTransforms) target)
        (Utilities|Array|Add (Variables|飞入运行数据|GetPartCenters) partCenter)
        (Utilities|Array|Add (Variables|飞入运行数据|GetPartBottomHeights) (- (.z partCenter) (.z partExtent)))
        ; 只在运行时修改继承组件的可移动性，原始模型蓝图资产不参与修改。
        (Transformation|SetMobility :self mesh :NewMobility "Movable")
        ; 尚未轮到的零件保持隐藏，到飞入起点后才显示，不在最终位置提前露出。
        (Rendering|SetVisibility :self mesh :bNewVisibility false :bPropagateToChildren false))
      (:CastFailed
        (Variables|飞入运行数据|SetFlightReady false)
        (Actor|Tick|SetActorTickEnabled :self self :bEnabled false)
        (Utilities|Debugging|RaiseScriptError "飞入装配失败：缓存的零件不是静态网格组件。")))))

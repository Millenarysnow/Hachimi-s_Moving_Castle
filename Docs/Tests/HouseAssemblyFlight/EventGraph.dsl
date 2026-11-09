; 专用测试蓝图的入口与逐帧驱动，不包含正式城堡、房间或商店代码。
; 默认单件飞行 1.2 秒、每隔 0.08 秒发射一件，稳定阶段约 15 件同时飞行。
; 123 件的理论完整演示时长为 1 + 122 * 0.08 + 1.2 = 11.96 秒。
(event EventBeginPlay
  (Variables|飞入运行数据|SetFlightReady false)
  (Variables|飞入运行数据|SetActiveOrderIndex 0)
  (Variables|飞入运行数据|SetCompletedPartCount 0)
  (Variables|飞入运行数据|SetFlyingPartCount 0)
  (Variables|运行数据|SetAssemblyElapsed 0.0)
  (Variables|飞入运行数据|SetFlightPartTime 0.0)
  ; 不擅自修正错误参数：非法配置明确报错并停止特效。
  (if (and (> (Variables|飞入参数|GetFlightDuration) 0.0)
           (and (> (Variables|飞入参数|GetFlightDistance) 0.0)
                (and (>= (Variables|飞入参数|GetFlightArcHeight) 0.0)
                     (and (>= (Variables|飞入参数|GetStartDelay) 0.0) (> (Variables|飞入参数|GetLaunchInterval) 0.0)))))
    (Variables|飞入运行数据|SetFlightReady true)
    (Variables|飞入运行数据|SetOriginalCollisionEnabled (Collision|GetActorEnableCollision :self self))
    (CallFunction|CacheFlightParts :self self)
    (if (Variables|飞入运行数据|GetFlightReady)
      (CallFunction|SortFlightParts :self self)
      ; 飞行阶段关闭整个测试 Actor 的碰撞，完工时恢复原值，避免隐藏部件阻挡玩家。
      (Collision|SetActorEnableCollision :self self :bNewActorEnableCollision false)
      (Development|PrintString :InString "房屋连续飞入装配开始：多个零件从四周汇入，逐层堆叠建成。" :Duration 4.0 :Key "HouseFlightStatus"))
    (else
      (Actor|Tick|SetActorTickEnabled :self self :bEnabled false)
      (Utilities|Debugging|RaiseScriptError "飞入装配参数错误：单件时长、发射间隔和飞入距离须大于零，弧高和开始等待不得小于零。"))))

(event EventTick (DeltaSeconds)
  (if (Variables|飞入运行数据|GetFlightReady)
    (Variables|运行数据|SetAssemblyElapsed (+ (Variables|运行数据|GetAssemblyElapsed) DeltaSeconds))
    ; 记录本帧实际更新的在途零件数，便于在 PIE Details 面板核对同时飞入的数量。
    (Variables|飞入运行数据|SetFlyingPartCount 0)
    ; 用全局时间计算每件自己的飞行年龄，发射节奏与上一件是否完成无关。
    ; 已完成的前缀不再遍历；遇到尚未到发射时间的零件即可结束本帧循环。
    (for orderIndex (range (Variables|飞入运行数据|GetCompletedPartCount) (Utilities|Array|Length (Variables|运行数据|GetPartComponents)))
      (bind age (- (- (Variables|运行数据|GetAssemblyElapsed) (Variables|飞入参数|GetStartDelay)) (* orderIndex (Variables|飞入参数|GetLaunchInterval))))
      (if (>= age 0.0)
        (Variables|飞入运行数据|SetActiveOrderIndex orderIndex)
        (Variables|飞入运行数据|SetFlightPartTime age)
        (CallFunction|UpdateFlyingPart :self self)
        (else (break))))
    (if (>= (Variables|飞入运行数据|GetCompletedPartCount) (Utilities|Array|Length (Variables|运行数据|GetPartComponents)))
      (Variables|飞入运行数据|SetFlightReady false)
      (Collision|SetActorEnableCollision :self self :bNewActorEnableCollision (Variables|飞入运行数据|GetOriginalCollisionEnabled))
      ; 完工关闭 Tick，完整建筑保留原材质、原缩放和精确拼装位置。
      (Actor|Tick|SetActorTickEnabled :self self :bEnabled false)
      (Development|PrintString :InString "房屋连续飞入装配完成：全部零件已精确归位。" :Duration 5.0 :Key "HouseFlightStatus"))))

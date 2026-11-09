; 调度器在一帧内为多个在途零件依次调用此函数，每件使用自己的飞行年龄。
; ActiveOrderIndex 和 FlightPartTime 仅表示本次调用的零件，不再等待上一件落定。
; 已落定的前缀由 CompletedPartCount 跳过，隐藏中的未来零件也不参与飞行计算。
(fn UpdateFlyingPart ()
  (bind orderIndex (Variables|飞入运行数据|GetActiveOrderIndex))
  (bind partIndex (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetFlightOrder) orderIndex))
  (bind part (Utilities|Array|Get(acopy) (Variables|运行数据|GetPartComponents) partIndex))
  (bind mesh (Utilities|Casting|CastToStaticMeshComponent :Object part)
    (:then
      (bind target (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetPartTransforms) partIndex))
      (if (>= (Variables|飞入运行数据|GetFlightPartTime) (Variables|飞入参数|GetFlightDuration))
        ; 结束帧直接恢复缓存变换，消除插值误差并精确接合原模型。
        (Transformation|SetWorldTransform :self mesh :NewTransform target :bSweep false :bTeleport true)
        (Rendering|SetVisibility :self mesh :bNewVisibility true :bPropagateToChildren false)
        ; 所有零件的飞行时长相同，完成的零件构成高度排序数组的连续前缀。
        ; 每件只在首次完成时计数一次，调度器下一帧会从这个前缀之后开始更新。
        (Variables|飞入运行数据|SetCompletedPartCount (+ (Variables|飞入运行数据|GetCompletedPartCount) 1))
        (else
          (Variables|飞入运行数据|SetFlyingPartCount (+ (Variables|飞入运行数据|GetFlyingPartCount) 1))
          (bind u (Math|Float|Clamp(Float) (/ (Variables|飞入运行数据|GetFlightPartTime) (Variables|飞入参数|GetFlightDuration)) 0.0 1.0))
          ; Smoothstep 令起飞和落定速度平滑；抬升和侧向弯曲在两端均为零。
          (bind eased (* (* u u) (- 3.0 (* 2.0 u))))
          (bind arch (* 4.0 (* u (- 1.0 u))))
          (bind targetCenter (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetPartCenters) partIndex))
          ; 黄金角让连续零件从房屋四周轮流飞来；不依赖随机数，重播路径可复现。
          (bind angle (+ 30.0 (* orderIndex 137.508)))
          (bind cx (Math|Trig|Cos(Degrees) angle))
          (bind sy (Math|Trig|Sin(Degrees) angle))
          (bind extent (Variables|运行数据|GetAssemblyExtent))
          ; 起点半径超出整座房屋的水平包围圆，再额外增加 FlightDistance。
          (bind orbitRadius (+ (Math|Float|Sqrt (+ (* (.x extent) (.x extent)) (* (.y extent) (.y extent)))) (Variables|飞入参数|GetFlightDistance)))
          (bind center (Variables|运行数据|GetAssemblyCenter))
          (bind bottom (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetPartBottomHeights) partIndex))
          (bind startCenter (Math|Vector|MakeVector
            :X (+ (.x center) (* cx orbitRadius))
            :Y (+ (.y center) (* sy orbitRadius))
            :Z (+ (Variables|运行数据|GetAssemblyBottomZ) (+ (- (.z targetCenter) bottom) 80.0))))
          (bind travelCenter (Math|Vector|Lerp(Vector) startCenter targetCenter eased))
          (bind arcHeight (Variables|飞入参数|GetFlightArcHeight))
          (bind curvedCenter (+ travelCenter (Math|Vector|MakeVector
            :X (* (- sy) (* arch (* arcHeight 0.35)))
            :Y (* cx (* arch (* arcHeight 0.35)))
            :Z (* arch arcHeight))))
          (bind targetRotation (.rotation target))
          (bind tilt (Math|Rotator|MakeRotator :Roll (* sy 18.0) :Pitch (* cx 26.0) :Yaw (* sy 55.0)))
          (bind startRotation (Math|Rotator|CombineRotators targetRotation tilt))
          (bind flightRotation (Math|Rotator|Lerp(Rotator) startRotation targetRotation eased true))
          ; SketchUp 零件往往共享房屋原点。先求包围盒中心的局部坐标，
          ; 再扣除当前旋转缩放后的中心偏移，使旋转围绕该零件自身，而非房屋原点。
          (bind localCenter (Math|Transform|InverseTransformLocation target targetCenter))
          (bind rotatedCenterOffset (Math|Transform|TransformLocation
            (Math|Transform|MakeTransform :Rotation flightRotation :Scale (.scale target)) localCenter))
          (bind animatedTransform (Math|Transform|MakeTransform
            :Location (- curvedCenter rotatedCenterOffset) :Rotation flightRotation :Scale (.scale target)))
          (Transformation|SetWorldTransform :self mesh :NewTransform animatedTransform :bSweep false :bTeleport true)
          (Rendering|SetVisibility :self mesh :bNewVisibility true :bPropagateToChildren false))))
    (:CastFailed
      (Variables|飞入运行数据|SetFlightReady false)
      (Actor|Tick|SetActorTickEnabled :self self :bEnabled false)
      (Utilities|Debugging|RaiseScriptError "飞入装配失败：当前飞行零件不是静态网格组件。"))))

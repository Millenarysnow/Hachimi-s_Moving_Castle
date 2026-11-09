; 按最终包围盒底部高度排序，确保从基础到上层结构逐件装配。
; 底部高度相同则按中心高度排序，仍相同则按原始下标排序，保证每件拥有唯一序号。
; 这是初始化阶段的排名算法，123 个零件只比较约 1.5 万次；运行时不再排序。
(fn SortFlightParts ()
  (bind count (Utilities|Array|Length (Variables|运行数据|GetPartComponents)))
  (Utilities|Array|Resize (Variables|飞入运行数据|GetFlightOrder) count)
  (for i (range count)
    (Variables|飞入运行数据|SetSortRank 0)
    (bind thisBottom (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetPartBottomHeights) i))
    (bind thisCenter (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetPartCenters) i))
    (for j (range count)
      (bind otherBottom (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetPartBottomHeights) j))
      (bind otherCenter (Utilities|Array|Get(acopy) (Variables|飞入运行数据|GetPartCenters) j))
      (if (or (< otherBottom thisBottom)
              (and (== otherBottom thisBottom)
                   (or (< (.z otherCenter) (.z thisCenter))
                       (and (== (.z otherCenter) (.z thisCenter)) (< j i)))))
        (Variables|飞入运行数据|SetSortRank (+ (Variables|飞入运行数据|GetSortRank) 1))))
    ; 最终 FlightOrder[排名] = 原始下标；该数组是原零件列表的完整排列。
    (Utilities|Array|SetArrayElem (Variables|飞入运行数据|GetFlightOrder) (Variables|飞入运行数据|GetSortRank) i false)))

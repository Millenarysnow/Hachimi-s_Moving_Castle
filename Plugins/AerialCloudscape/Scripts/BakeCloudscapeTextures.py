"""离线制作云海的无缝数据纹理；只写显式指定的制作目录，不需要 UE。"""

import argparse
from itertools import product
from pathlib import Path

import numpy as np
from PIL import Image


def grid(size, dims):
    """在 texel 中心采样，不在纹理首尾重复储存同一个点。"""
    return (np.indices((size,) * dims, dtype=np.float32) + 0.5) / size


def grad_noise(points, cells, seed):
    """梯度格点先取余再寻址；弯曲坐标后仍可横跨整个纹理周期。"""
    dims = len(points)
    rng = np.random.default_rng(seed)
    gradients = rng.standard_normal((cells,) * dims + (dims,), dtype=np.float32)
    gradients /= np.linalg.norm(gradients, axis=-1, keepdims=True)
    coord = points * cells
    cell = np.floor(coord).astype(np.int32)
    t = coord - cell
    blend = t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
    value = np.zeros(t.shape[1:], dtype=np.float32)
    for corner in product((0, 1), repeat=dims):
        directions = gradients[tuple((cell[i] + c) % cells for i, c in enumerate(corner))]
        dot = sum(directions[..., i] * (t[i] - c) for i, c in enumerate(corner))
        weight = np.prod([blend[i] if c else 1.0 - blend[i] for i, c in enumerate(corner)], axis=0)
        value += dot * weight
    return value


def cellular(points, cells, seed):
    """独立抖动各体元内部的种子；使用最近距离形成团块而不是网格状峰线。"""
    coord = points * cells
    cell = np.floor(coord).astype(np.int32)
    local = coord - cell
    sites = np.random.default_rng(seed).uniform(0.17, 0.83, (cells,) * 3 + (3,)).astype(np.float32)
    closest = np.full(cell.shape[1:], np.inf, dtype=np.float32)
    for offset in product((-1, 0, 1), repeat=3):
        neighbors = sites[tuple((cell[i] + n) % cells for i, n in enumerate(offset))]
        distance_sq = sum((local[i] - offset[i] - neighbors[..., i]) ** 2 for i in range(3))
        np.minimum(closest, distance_sq, out=closest)
    return np.clip(1.0 - np.sqrt(closest) / 0.96, 0.0, 1.0)


def normalize(value, first_percentile, last_percentile):
    """按分位数定义制作曲线，避免少数孤立极值决定整片天气的覆盖率。"""
    low, high = np.percentile(value, (first_percentile, last_percentile))
    assert high > low, '制作数据没有有效的数值范围'
    return np.clip((value - low) / (high - low), 0.0, 1.0)


def quantize(channels):
    # 数值纹理不是照片，按线性通道写入，无显示伽马与颜色配置文件。
    return np.round(np.stack(channels, axis=-1) * 255.0).astype(np.uint8)


def bake_coverage(output):
    points = grid(512, 2)
    warped = points + 0.105 * np.stack((grad_noise(points, 5, 41), grad_noise(points, 5, 53)))
    broad = 0.62 * grad_noise(warped, 6, 872) + 0.28 * grad_noise(warped, 11, 821) + 0.10 * grad_noise(warped, 21, 971)
    # 红色拉开云区与公里级缺口；绿色给相邻云团独立的厚度和顶部高度。
    coverage = normalize(broad, 14, 89)
    elevation = normalize(0.73 * grad_noise(warped, 9, 123) + 0.27 * grad_noise(warped, 17, 257), 7, 94)
    Image.fromarray(quantize((coverage, elevation, np.zeros_like(coverage)))).save(output)


def bake_shape(output):
    points = grid(64, 3)
    # 三维基底兼有宽阔的光照面和团块间的凹谷。小尺度场独立于主形，不使云边退化成锯齿纹。
    curl = np.stack([grad_noise(points, 4, seed) for seed in (54, 71, 93)])
    warped = points + 0.043 * curl
    wide = cellular(warped, 4, 188)
    medium = cellular(warped, 7, 337)
    fine = cellular(points, 11, 445)
    # 宽圆丘主导体积，较小的泡团只托起轮廓，不切碎正面的受光块面。
    base = normalize(0.74 * wide + 0.18 * medium + 0.08 * (0.5 + grad_noise(points, 5, 19)), 3, 98)
    shoulder = normalize(0.58 * medium + 0.42 * cellular(warped, 5, 789), 5, 97)
    erosion = normalize(0.80 * fine + 0.20 * cellular(points, 15, 929), 4, 97)
    voxels = quantize((base, shoulder, erosion))

    # UE 的体积纹理工厂从 8×8 的行优先图集中恢复 64 个正方形切片。
    atlas = voxels.reshape(8, 8, 64, 64, 3).transpose(0, 2, 1, 3, 4).reshape(512, 512, 3)
    Image.fromarray(np.ascontiguousarray(atlas)).save(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    bake_coverage(args.output / 'T_AerialCloudCoverage.png')
    bake_shape(args.output / 'T_AerialCloudShapeAtlas.png')

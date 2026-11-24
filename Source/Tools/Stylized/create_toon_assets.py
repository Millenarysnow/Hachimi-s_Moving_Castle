"""由 UE 编辑器 Python 创建风格化表面的 Toon Profile 与材质函数。"""

import unreal


ROOT = "/Game/MovingCastle/Rendering"
EDITING = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.CastleStylizedMaterialTools
FRONT = unreal.MaterialProperty.MP_FRONT_MATERIAL

# 色阶在线性空间中取样。单位的暗部略亮，金属同时保留窄而硬的高光。
PROFILES = {
    "Surface": ((0.45, 0.47, 0.58), (0.0, 0.0, 0.0), 0.0, 1.0),
    "Unit": ((0.56, 0.59, 0.69), (0.0, 0.0, 0.0), 0.0, 1.0),
    "Metal": ((0.45, 0.47, 0.58), (0.65, 0.53, 0.39), 0.35, 0.34),
}


def require(value, reason):
    """统一把资产路径和失败原因交还给命令行，避免保存不完整的节点图。"""
    if not value:
        raise RuntimeError(reason)
    return value


def load(path, asset_type):
    """严格检查工具的输入类型，避免不同类的同名资产参与生成。"""
    asset = ASSETS.load_asset(path)
    require(isinstance(asset, asset_type), f"{path} 不是可加载的 {asset_type.__name__}")
    return asset


def create(path, asset_type, factory):
    """通过 AssetTools 创建设计指定的长包路径。"""
    folder, name = path.rsplit("/", 1)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    return require(asset_tools.create_asset(name, folder, asset_type, factory()), f"{path} 创建失败")


def color(rgb):
    """把色阶的线性 RGB 变成 UE 资产可识别的颜色。"""
    return unreal.LinearColor(*rgb, 1.0)


def node(owner, kind, x, y):
    """材质和材质函数是不同容器，创建后由引擎把节点登记进对应节点图。"""
    if isinstance(owner, unreal.MaterialFunction):
        result = EDITING.create_material_expression_in_function(owner, kind, x, y)
    else:
        result = EDITING.create_material_expression(owner, kind, x, y)
    return require(result, f"{owner.get_path_name()} 创建 {kind.__name__} 失败")


def connect(source, target, pin="", output=""):
    """使用确切的引脚名接线，连接失败时不进入编译和保存。"""
    require(EDITING.connect_material_expressions(source, output, target, pin),
            f"{source.get_path_name()} 的 {output} 无法连接到 {target.get_name()} 的 {pin}")


def make_surface_function(path, profile, metallic, roughness):
    """只有共享函数拥有原生 Toon BSDF，材质根节点一律通过函数传递 Front Material。"""
    function = create(path, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew)
    toon = node(function, unreal.MaterialExpressionSubstrateToonBSDF, 0, 0)
    toon.set_editor_property("toon_profile", profile)
    # 如未接法线贴图，切线空间 +Z 由引擎转换为几何表面的世界法线。
    for index, (name, pin, preview) in enumerate((
        ("BaseColor", "BaseColor", None),
        ("Emissive", "EmissiveColor", (0.0, 0.0, 0.0)),
        ("Normal", "Normal", (0.0, 0.0, 1.0)),
    )):
        input_node = node(function, unreal.MaterialExpressionFunctionInput, -380, index * 130)
        input_node.set_editor_property("input_name", name)
        input_node.set_editor_property("input_type", unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3)
        input_node.set_editor_property("sort_priority", index)
        input_node.set_editor_property("use_preview_value_as_default", preview is not None)
        if preview is not None:
            default_value = node(function, unreal.MaterialExpressionConstant3Vector, -650, index * 130)
            default_value.set_editor_property("constant", color(preview))
            connect(default_value, input_node)
        connect(input_node, toon, pin)

    for index, (name, value) in enumerate((("Metallic", metallic), ("Roughness", roughness))):
        constant = node(function, unreal.MaterialExpressionConstant, -380, 390 + index * 90)
        constant.set_editor_property("r", value)
        connect(constant, toon, name)

    output = node(function, unreal.MaterialExpressionFunctionOutput, 300, 0)
    output.set_editor_property("output_name", "Surface")
    connect(toon, output)
    EDITING.update_material_function(function)
    return function


def build_shared_assets(root):
    """在空目录中生成成对的配置和函数。验证由调用方决定，函数自身不保存资产。"""
    functions = {}
    for name, (shadow, highlight, metallic, roughness) in PROFILES.items():
        profile_path = f"{root}/Profiles/TP_{name}"
        function_path = f"{root}/Functions/MF_ToonSurface_{name}"
        require(not ASSETS.does_asset_exist(profile_path) and not ASSETS.does_asset_exist(function_path),
                f"验证要求使用新包，{profile_path} 或 {function_path} 已存在")
        profile = create(profile_path, unreal.ToonProfile, unreal.ToonProfileFactory)
        require(TOOLS.configure_toon_profile(profile, color(shadow), color(highlight)),
                f"{profile_path} 写入色阶失败")
        functions[name] = make_surface_function(function_path, profile, metallic, roughness)
    return functions


def connect_front(material, call):
    """把函数产生的 Substrate 材质传给真正参与光照的材质根输入。"""
    require(EDITING.connect_material_property(call, "", FRONT),
            f"{material.get_path_name()} 无法连接 Front Material")


def function_call(material, function):
    """通过引擎更新函数调用的引脚及 GUID。"""
    call = node(material, unreal.MaterialExpressionMaterialFunctionCall, 180, 0)
    call.set_editor_property("material_function", function)
    return call


def connect_color_input(material, call, parameter, input_name, value):
    """使用原始线性颜色创建向量参数，让后续材质实例能继续覆色。"""
    vector = node(material, unreal.MaterialExpressionVectorParameter, -220, 0 if input_name == "BaseColor" else 140)
    vector.set_editor_property("parameter_name", parameter)
    vector.set_editor_property("default_value", value)
    connect(vector, call, input_name)


def compile_material(material):
    """使用当前 RHI 编译，保留编译器报告的完整错误。"""
    errors = EDITING.recompile_material(material)
    require(not errors, f"{material.get_path_name()} 编译失败\n" + "\n".join(errors))

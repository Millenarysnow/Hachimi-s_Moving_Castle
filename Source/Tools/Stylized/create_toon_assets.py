"""由 UE 编辑器 Python 创建风格化表面的 Toon Profile 与材质函数。"""

import unreal


ROOT = "/Game/MovingCastle/Rendering"
EDITING = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.CastleStylizedMaterialTools
FRONT = unreal.MaterialProperty.MP_FRONT_MATERIAL

# 线性暗阶给建筑投影一点冷蓝，单位暗阶单独提亮以保留剪影内的细节。
# 金属与建筑共用漫反射暗阶，只在亮部保留窄暖高光。
PROFILES = {
    "Surface": ((0.32, 0.37, 0.52), (0.0, 0.0, 0.0), 0.0, 1.0),
    "Unit": ((0.44, 0.49, 0.62), (0.0, 0.0, 0.0), 0.0, 1.0),
    "Metal": ((0.32, 0.37, 0.52), (0.65, 0.53, 0.39), 0.35, 0.34),
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


def get_or_create(path, asset_type, factory):
    """保持已保存的项目资产标识，首次构建时才创建包。"""
    return load(path, asset_type) if ASSETS.does_asset_exist(path) else create(path, asset_type, factory)


def save(asset):
    """只保存制作器负责的包，拒绝引擎报告的保存失败。"""
    require(ASSETS.save_loaded_asset(asset, False), f"{asset.get_path_name()} 保存失败")


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


def make_surface_function(function, profile, metallic, roughness):
    """只有共享函数拥有原生 Toon BSDF，材质根节点一律通过函数传递 Front Material。"""
    if EDITING.get_material_function_expressions(function):
        # 保留已有函数输入 GUID，避免重复执行后调用方丢失接线。
        toon, = [n for n in EDITING.get_material_function_expressions(function)
                 if isinstance(n, unreal.MaterialExpressionSubstrateToonBSDF)]
        require(toon.get_editor_property("toon_profile") == profile, f"{function.get_path_name()} 的 Toon Profile 无效")
        return function
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
    """按约定路径生成或更新三份共享配置，保持函数输入的连接身份。"""
    functions = {}
    for name, (shadow, highlight, metallic, roughness) in PROFILES.items():
        profile_path = f"{root}/Profiles/TP_{name}"
        function_path = f"{root}/Functions/MF_ToonSurface_{name}"
        profile = get_or_create(profile_path, unreal.ToonProfile, unreal.ToonProfileFactory)
        require(TOOLS.configure_toon_profile(profile, color(shadow), color(highlight)),
                f"{profile_path} 写入色阶失败")
        function = get_or_create(function_path, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew)
        functions[name] = make_surface_function(function, profile, metallic, roughness)
        save(profile)
        save(function)
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

"""在启用 Python Editor Script Plugin 的编辑器内执行：
py "<工作区>/Source/Moving_Castle/Castle/Audio/Editor/ImportCastleAudio.py" "<含 elevenlabs-v4 和 音乐 的目录>"
"""

import shutil
import subprocess
import sys
from pathlib import Path

import unreal


AUDIO_ROOT = "/Game/MovingCastle/Audio"
SCRIPT_ROOT = "/Game/MovingCastle/Data/Tutorial"
CONFIG_PATH = "/Game/MovingCastle/Data/Audio"
CONFIG_NAME = "DA_AudioConfig"

# 键和枚举名称直接对应，中文原名仅用于定位交付目录中的原始录音。
BGM = {
    "MAIN_MENU": ("MainMenu", "主界面"),
    "BUILD": ("Build", "建造"),
    "BATTLE": ("Battle", "战斗"),
    "SKY_ISLAND": ("SkyIsland", "空岛"),
}
SFX = {
    "INTERACT": ("Interact", "交互"),
    "CLOSE_PANEL": ("ClosePanel", "关闭界面"),
    "GAMBLE": ("Gamble", "和赌博机互动"),
    "DEFEAT": ("Defeat", "失败"),
    "BUILD": ("Build", "建造"),
    "VICTORY": ("Victory", "战斗胜利"),
    "DEMOLISH": ("Demolish", "拆除"),
    "BOW_SHOT": ("BowShot", "拉弓射箭"),
    "BUTTON": ("Button", "按按钮"),
    "ATTACK_MAIN_TOWER": ("MainTowerHit_01", "攻击主塔1"),
    "BALLOON_POP": ("BalloonPop", "气球破掉"),
    "CANNON_FIRE": ("CannonFire", "炮声"),
    "ARROW_FLIGHT": ("ArrowFlight", "箭飞行"),
    "CRATE_LANDED": ("CrateLanded", "箱子落地"),
    "PAGE_TURN": ("PageTurn", "翻页声"),
    "REWARD": ("Reward", "获得奖励（系统发物品）"),
    "GOLD": ("Gold", "获得金币（包括经过农田）"),
    "BEES": ("Bees", "蜜蜂"),
    "ENTER_GAME": ("EnterGame", "进入游戏"),
    "DRAGON_FIRE": ("DragonFire", "龙吐火"),
}
VOICE = {
    **{f"Tutorial_{index:02}": f"{index}.mp3" for index in (*range(1, 16), *range(17, 24))},
    "Arrival_NPC": "npc空岛.mp3",
    "Arrival_Shop": "商店空岛.mp3",
    "Arrival_Gamble_01": "赌博空岛1.mp3",
    "Arrival_Gamble_02": "赌博空岛2.mp3",
    "CryBubble": "呜呜呜气泡.mp3",
    "Wangcai_01": "旺财1.wav",
    "Wangcai_02": "旺财2.wav",
    "Wangcai_03": "旺财3.wav",
}

# 按剧本现有行序逐行绑定。W 表示播放旺财池；S09 保持资产存储顺序和原有跳转。
W = "Wangcai"
SCRIPTS = {
    "Tutorial_S01": (1, 2, W, 3, W, 4, 5, W, 6, W),
    "Tutorial_S03": (7, W),
    "Tutorial_S04": (8,),
    "Tutorial_S05": (9,),
    "Tutorial_S06": (10, W),
    "Tutorial_S07": (11,),
    "Tutorial_S08": (12,),
    "Tutorial_S09": (W, 13, W, 14, W, 15),
    "Tutorial_S10": (W,),
    "Tutorial_S11": (W,),
    "Tutorial_S12": (W, 18),
    "Tutorial_S13": (19,),
    "Tutorial_S14": (22,),
    "Tutorial_Claim": (17,),
    "Tutorial_OutOfStock": (20,),
    "Tutorial_FarmOffer": (21,),
    "Tutorial_Closing": (W, 23, W),
    "Arrival_Shop": ("Arrival_Shop",),
    "Arrival_NPC": ("Arrival_NPC",),
    "Arrival_Gamble": ("Arrival_Gamble_01", W, "Arrival_Gamble_02"),
}


def load_script_rows():
    """先检查全部 20 个教程和空岛剧本，避免漏绑正式剧本或错位后部分写入。"""
    dialogue_class = unreal.load_class(None, "/Script/Moving_Castle.CastleDialogueScriptDataAsset")
    if dialogue_class is None:
        raise RuntimeError("编辑器尚未加载 CastleDialogueScriptDataAsset 反射类型。")
    rows = []
    for name, bindings in SCRIPTS.items():
        path = f"{SCRIPT_ROOT}/DA_{name}"
        script = unreal.load_asset(path)
        if script is None or script.get_class().get_path_name() != dialogue_class.get_path_name():
            raise RuntimeError(f"正式剧本缺失或类无效：{path}")
        lines = script.get_editor_property("lines")
        if len(lines) != len(bindings):
            raise RuntimeError(f"{path} 台词行数错误：实际 {len(lines)}，预期 {len(bindings)}")
        for index, (line, binding) in enumerate(zip(lines, bindings), 1):
            expected_speaker = "旺财" if binding == W else "哈基米德"
            speaker = str(line.get_editor_property("speaker_name"))
            if speaker != expected_speaker:
                raise RuntimeError(f"{path} 第 {index} 行发言人为 {speaker}，预期 {expected_speaker}")
        rows.append((script, lines, bindings))
    return rows


def prepare_import_source(source):
    """只为实际是 MP4 容器的误标 MP3 建立临时 16-bit PCM WAV。"""
    with source.open("rb") as audio_file:
        header = audio_file.read(12)
    if source.suffix.lower() != ".mp3" or header[4:8] != b"ftyp":
        return source

    project_root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    bundled_exe = project_root / "Saved/Coordinator/worker-d-ffmpeg/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe"
    executable = shutil.which("ffmpeg") or (str(bundled_exe) if bundled_exe.is_file() else None)
    if not executable:
        raise RuntimeError(f"音频 {source} 扩展名为 MP3、内容却是 MP4 容器。安装 ffmpeg 后可重试。")

    project_root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    destination = project_root / "Saved/Coordinator/worker-d-audio-conversion" / f"{source.stem}.wav"
    destination.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [executable, "-nostdin", "-v", "error", "-y", "-i", str(source),
         "-vn", "-c:a", "pcm_s16le", "-ar", "48000", str(destination)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0 or not destination.is_file():
        raise RuntimeError(
            f"音频 {source} 转成 16-bit PCM WAV 失败，ffmpeg 退出码 {result.returncode}：{result.stderr}"
        )
    return destination


def import_waves(source_root):
    """使用 UE 原生 SoundFactory 导入源录音，资产命名全部为 ASCII。"""
    bgm = {name: f"{source}.mp3" for name, source in BGM.values()}
    sfx = {name: f"{source}.mp3" for name, source in SFX.values()}
    sfx["MainTowerHit_02"] = "攻击主塔2.mp3"
    groups = {
        "BGM": (source_root / "音乐" / "bgm", bgm),
        "SFX": (source_root / "音乐" / "音效", sfx),
        "Voice": (source_root / "elevenlabs-v4", VOICE),
    }
    tasks = []
    for group, (folder, files) in groups.items():
        for name, filename in files.items():
            source = folder / filename
            if not source.is_file():
                raise RuntimeError(f"正式音频源文件缺失：{source}")
            import_source = prepare_import_source(source)
            asset_name = f"SW_{group}_{name}"
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str(import_source))
            task.set_editor_property("destination_path", f"{AUDIO_ROOT}/{group}")
            task.set_editor_property("destination_name", asset_name)
            task.set_editor_property("factory", unreal.SoundFactory())
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            tasks.append((group, name, asset_name, task))

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task for _, _, _, task in tasks])
    sounds = {group: {} for group in groups}
    for group, name, asset_name, task in tasks:
        results = task.get_objects()
        expected_path = f"{AUDIO_ROOT}/{group}/{asset_name}.{asset_name}"
        if len(results) != 1 or not isinstance(results[0], unreal.SoundWave):
            raise RuntimeError(f"{expected_path} 导入失败，源文件 {task.get_editor_property('filename')}")
        sound = results[0]
        if sound.get_path_name() != expected_path:
            raise RuntimeError(f"音频导入得到 {sound.get_path_name()}，预期 {expected_path}")
        sound.set_editor_property("looping", group == "BGM")
        sounds[group][name] = sound
    return sounds


def load_existing_waves():
    """绑定修订时只加载已落盘的 55 个 SoundWave，不重导入音源。"""
    bgm = {name: f"SW_BGM_{name}" for name, _ in BGM.values()}
    sfx = {name: f"SW_SFX_{name}" for name, _ in SFX.values()}
    sfx["MainTowerHit_02"] = "SW_SFX_MainTowerHit_02"
    groups = {"BGM": bgm, "SFX": sfx, "Voice": {name: f"SW_Voice_{name}" for name in VOICE}}
    sounds = {group: {} for group in groups}
    for group, assets in groups.items():
        for name, asset_name in assets.items():
            path = f"{AUDIO_ROOT}/{group}/{asset_name}"
            sound = unreal.load_asset(path)
            if sound is None or not isinstance(sound, unreal.SoundWave):
                raise RuntimeError(f"已导入音频资产缺失或不是 SoundWave：{path}")
            sounds[group][name] = sound
    return sounds


def save_assets(assets):
    """只保存本命令明确生成或绑定的资产，任一保存失败时报告具体包名。"""
    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    for asset in assets:
        if not subsystem.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"音频资产保存失败：{asset.get_path_name()}")


def main(source_root, bImportAudio=True):
    script_rows = load_script_rows()
    if len(script_rows) != 20:
        raise RuntimeError(f"正式教程和空岛对话资产必须完整配置 20 个，实际找到 {len(script_rows)} 个。")
    config_class = unreal.load_class(None, "/Script/Moving_Castle.CastleAudioConfigDataAsset")
    if config_class is None:
        raise RuntimeError("编辑器尚未加载 CastleAudioConfigDataAsset 反射类型。")
    source_root = Path(source_root)
    # 所有源音频都在开始导入之前验证，缺少单项时不会留下半批资产。
    if bImportAudio:
        for folder, filenames in (
            (source_root / "音乐" / "bgm", [f"{source}.mp3" for _, source in BGM.values()]),
            (source_root / "音乐" / "音效", [f"{source}.mp3" for _, source in SFX.values()] + ["攻击主塔2.mp3"]),
            (source_root / "elevenlabs-v4", list(VOICE.values())),
        ):
            for filename in filenames:
                source = folder / filename
                if not source.is_file():
                    raise RuntimeError(f"正式音频源文件缺失：{source}")
        sounds = import_waves(source_root)
    else:
        sounds = load_existing_waves()
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if not assets:
        raise RuntimeError("当前编辑器无法取得 EditorAssetSubsystem。")
    config_path = f"{CONFIG_PATH}/{CONFIG_NAME}"
    if assets.does_asset_exist(config_path):
        config = assets.load_asset(config_path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", config_class)
        config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(CONFIG_NAME, CONFIG_PATH, config_class, factory)
    if config is None or config.get_class().get_path_name() != config_class.get_path_name():
        raise RuntimeError(f"无法加载或创建音频总表：{config_path}")
    # 按配置资产中的枚举顺序写成数组，避免依赖 Python 对 C++ UEnum 的命名封装。
    if not config.set_imported_audio_assets(
        [sounds["BGM"][name] for name, _ in BGM.values()],
        [sounds["SFX"][name] for name, _ in SFX.values()],
        sounds["SFX"]["MainTowerHit_02"],
        [sounds["Voice"][f"Wangcai_{index:02}"] for index in range(1, 4)],
        sounds["Voice"]["CryBubble"],
    ):
        raise RuntimeError(f"正式音频总表未通过 C++ 音频配置校验：{config_path}")
    for script, lines, bindings in script_rows:
        for index, binding in enumerate(bindings):
            line = lines[index]
            voice_key = f"Tutorial_{binding:02}" if isinstance(binding, int) else binding
            line.set_editor_property("voice_line", None if binding == W else sounds["Voice"][voice_key])
            line.set_editor_property("use_wangcai_voice", binding == W)
            # Unreal Python 将 USTRUCT 数组元素作为值返回，修改副本后必须写回数组槽。
            lines[index] = line
        script.set_editor_property("lines", lines)
        script.set_editor_property("require_voice_lines", True)
        saved_lines = script.get_editor_property("lines")
        if not script.get_editor_property("require_voice_lines") or len(saved_lines) != len(bindings):
            raise RuntimeError(f"{script.get_path_name()} 语音行数组或正式语音标记写回失败。")
        for index, binding in enumerate(bindings):
            line = saved_lines[index]
            uses_wangcai = line.get_editor_property("use_wangcai_voice")
            voice = line.get_editor_property("voice_line")
            expected_voice = None if binding == W else sounds["Voice"][f"Tutorial_{binding:02}"] if isinstance(binding, int) else sounds["Voice"][binding]
            if uses_wangcai != (binding == W) or (expected_voice and voice.get_path_name() != expected_voice.get_path_name()):
                raise RuntimeError(f"{script.get_path_name()} 第 {index + 1} 行配音写回校验失败。")
            if binding == W and voice is not None:
                raise RuntimeError(f"{script.get_path_name()} 第 {index + 1} 行旺财语音槽仍保留了固定语音。")
    if bImportAudio:
        save_assets([sound for group in sounds.values() for sound in group.values()])
    save_assets([config, *(script for script, _, _ in script_rows)])
    unreal.log(f"CastleAudio 导入完成：BGM {len(sounds['BGM'])}，SFX {len(sounds['SFX'])}，"
               f"Voice {len(sounds['Voice'])}，正式剧本 {len(script_rows)}。")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise RuntimeError("用法：py ImportCastleAudio.py <音频交付根目录>")
    main(sys.argv[1])

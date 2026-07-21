# A test double for UEFN's `unreal` module. This module object itself is passed to
# the serializer / capability probes in place of the real `unreal`, so `isinstance`
# and `hasattr` checks behave as they would inside UEFN.


class Vector:
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = x, y, z


class Vector2D:
    def __init__(self, x=0.0, y=0.0):
        self.x, self.y = x, y


class Rotator:
    def __init__(self, pitch=0.0, yaw=0.0, roll=0.0):
        self.pitch, self.yaw, self.roll = pitch, yaw, roll


class Quat:
    def __init__(self, x=0.0, y=0.0, z=0.0, w=1.0):
        self.x, self.y, self.z, self.w = x, y, z, w

    def rotator(self):
        return Rotator(0.0, 0.0, 0.0)


class LinearColor:
    def __init__(self, r=0.0, g=0.0, b=0.0, a=1.0):
        self.r, self.g, self.b, self.a = r, g, b, a


class Color:
    def __init__(self, r=0, g=0, b=0, a=255):
        self.r, self.g, self.b, self.a = r, g, b, a


class Transform:
    def __init__(self, translation=None, rotation=None, scale3d=None):
        self.translation = translation or Vector()
        self.rotation = rotation or Quat()
        self.scale3d = scale3d or Vector(1.0, 1.0, 1.0)


class EnumBase:
    def __init__(self, label="EnumType.VALUE"):
        self._label = label

    def __str__(self):
        return self._label


class _Class:
    def __init__(self, name):
        self._name = name

    def get_name(self):
        return self._name


class Object:
    def __init__(self, name="Obj", path="/Game/Obj.Obj", cls="StaticMesh"):
        self._name, self._path, self._cls = name, path, cls

    def get_name(self):
        return self._name

    def get_path_name(self):
        return self._path

    def get_class(self):
        return _Class(self._cls)


class AssetData:
    def __init__(self, asset_name="SM_Example", asset_class_path="StaticMesh",
                 package_name="/Game/Example/SM_Example"):
        self.asset_name = asset_name
        self.asset_class_path = asset_class_path
        self.package_name = package_name


class Paths:
    @staticmethod
    def project_dir():
        return "/Fake/Project/"

    @staticmethod
    def project_content_dir():
        return "/Fake/Project/Content/"

    @staticmethod
    def project_log_dir():
        return "/Fake/Project/Saved/Logs/"

    @staticmethod
    def get_project_file_path():
        return "/Fake/Project/Project.uefnproject"


class SystemLibrary:
    @staticmethod
    def get_engine_version():
        return "5.x-uefn-fake"


# Presence-only placeholders so capability probes report True.
EditorActorSubsystem = object
EditorAssetLibrary = object
LevelEditorSubsystem = object
EditorUtilityLibrary = object
AssetRegistryHelpers = object
EditorValidatorSubsystem = object

_tick_callbacks = {}
_tick_counter = [0]


def register_slate_post_tick_callback(cb):
    _tick_counter[0] += 1
    handle = _tick_counter[0]
    _tick_callbacks[handle] = cb
    return handle


def unregister_slate_post_tick_callback(handle):
    _tick_callbacks.pop(handle, None)

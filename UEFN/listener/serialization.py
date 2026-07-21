# UEFN MCP listener — serializer for unreal.* values -> JSON-safe structures.
#
# Portions adapted from KirChuvakov/uefn-mcp-server (MIT, (c) 2025 KirChuvakov);
# see UEFN/THIRD_PARTY_NOTICES.md. Reworked to accept an INJECTED unreal module so
# the module imports and tests cleanly outside UEFN, and to add recursion/collection
# limits and cycle detection (the reference had none).
#
# Pure stdlib. Never top-level imports `unreal`; the module is passed in.

DEFAULT_MAX_DEPTH = 6
DEFAULT_MAX_ITEMS = 1000


class Serializer:
    """
    Convert unreal.* objects and containers to JSON-safe values.

    unreal_module: the live `unreal` module inside UEFN, or a test double, or None.
                   When None, unreal-specific types fall back to a structured/string ref.
    """

    def __init__(self, unreal_module=None, max_depth: int = DEFAULT_MAX_DEPTH,
                 max_items: int = DEFAULT_MAX_ITEMS):
        self.u = unreal_module
        self.max_depth = max_depth
        self.max_items = max_items

    def serialize(self, obj):
        return self._ser(obj, depth=0, seen=set())

    # --- internals -------------------------------------------------------

    def _ser(self, obj, depth, seen):
        # Primitives pass through.
        if obj is None or isinstance(obj, (bool, int, float, str)):
            return obj

        if depth >= self.max_depth:
            return {"type": "truncated", "reason": "max_depth", "repr": _safe_repr(obj)}

        # Cycle detection for containers/objects.
        oid = id(obj)
        if oid in seen:
            return {"type": "cycle", "repr": _safe_repr(obj)}

        # Containers.
        if isinstance(obj, dict):
            seen = seen | {oid}
            out = {}
            for i, (k, v) in enumerate(obj.items()):
                if i >= self.max_items:
                    out["__truncated__"] = f"more than {self.max_items} items"
                    break
                out[str(k)] = self._ser(v, depth + 1, seen)
            return out

        if isinstance(obj, (list, tuple, set, frozenset)):
            seen = seen | {oid}
            out = []
            for i, v in enumerate(obj):
                if i >= self.max_items:
                    out.append({"__truncated__": f"more than {self.max_items} items"})
                    break
                out.append(self._ser(v, depth + 1, seen))
            return out

        # unreal.* structured types (only when a module is available).
        special = self._ser_unreal(obj)
        if special is not None:
            return special

        # Unknown: last-resort string.
        return _safe_repr(obj)

    def _ser_unreal(self, obj):
        u = self.u
        if u is None:
            return None

        def is_a(name):
            cls = getattr(u, name, None)
            return cls is not None and isinstance(obj, cls)

        if is_a("Vector"):
            return {"x": _f(obj.x), "y": _f(obj.y), "z": _f(obj.z)}
        if is_a("Vector2D"):
            return {"x": _f(obj.x), "y": _f(obj.y)}
        if is_a("Rotator"):
            return {"pitch": _f(obj.pitch), "yaw": _f(obj.yaw), "roll": _f(obj.roll)}
        if is_a("Quat"):
            return {"x": _f(obj.x), "y": _f(obj.y), "z": _f(obj.z), "w": _f(obj.w)}
        if is_a("LinearColor") or is_a("Color"):
            return {"r": _f(getattr(obj, "r", 0)), "g": _f(getattr(obj, "g", 0)),
                    "b": _f(getattr(obj, "b", 0)), "a": _f(getattr(obj, "a", 0))}
        if is_a("Transform"):
            return {
                "location": self._ser(getattr(obj, "translation", None), 1, set()),
                "rotation": self._ser(_rot_from_transform(obj), 1, set()),
                "scale": self._ser(getattr(obj, "scale3d", None), 1, set()),
            }
        if is_a("AssetData"):
            return {
                "type": "AssetData",
                "asset_name": _attr_str(obj, "asset_name"),
                "asset_class": _attr_str(obj, "asset_class_path", "asset_class"),
                "package_name": _attr_str(obj, "package_name"),
                "object_path": _attr_str(obj, "object_path", "package_name"),
            }
        # Enum values (unreal enums stringify usefully).
        enum_base = getattr(u, "EnumBase", None)
        if enum_base is not None and isinstance(obj, enum_base):
            return {"type": "Enum", "value": str(obj)}
        # Generic unreal.Object -> structured reference.
        obj_base = getattr(u, "Object", None)
        if obj_base is not None and isinstance(obj, obj_base):
            return {
                "type": "Object",
                "class": _class_name(obj),
                "name": _call_str(obj, "get_name"),
                "path": _call_str(obj, "get_path_name"),
            }
        return None


# --- helpers -------------------------------------------------------------

def _f(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return v


def _rot_from_transform(obj):
    # Transform stores a Quat; expose a rotator if the object provides one.
    rot = getattr(obj, "rotation", None)
    to_rot = getattr(rot, "rotator", None)
    if callable(to_rot):
        try:
            return to_rot()
        except Exception:  # noqa: BLE001 - serialization must never raise
            return rot
    return rot


def _attr_str(obj, *names):
    for n in names:
        if hasattr(obj, n):
            try:
                return str(getattr(obj, n))
            except Exception:  # noqa: BLE001
                continue
    return None


def _class_name(obj):
    getc = getattr(obj, "get_class", None)
    if callable(getc):
        try:
            return str(getc().get_name())
        except Exception:  # noqa: BLE001
            pass
    return type(obj).__name__


def _call_str(obj, method):
    fn = getattr(obj, method, None)
    if callable(fn):
        try:
            return str(fn())
        except Exception:  # noqa: BLE001
            return None
    return None


def _safe_repr(obj):
    try:
        return str(obj)
    except Exception:  # noqa: BLE001
        return f"<unserializable {type(obj).__name__}>"

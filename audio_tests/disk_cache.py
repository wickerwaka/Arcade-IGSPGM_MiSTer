#!/usr/bin/env python3
"""Small JSON-backed result cache decorator for audio test helpers."""

from __future__ import annotations

import dataclasses
import enum
import functools
import hashlib
import json
import marshal
import tempfile
from pathlib import Path
from typing import Any, Callable, TypeVar


F = TypeVar("F", bound=Callable[..., Any])

_DARK_GRAY = "\033[90m"
_RESET = "\033[0m"


def _jsonable(value: Any) -> Any:
    """Convert common Python values to deterministic JSON-compatible data."""
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, enum.Enum):
        return {"__enum__": f"{value.__class__.__module__}.{value.__class__.__qualname__}", "value": _jsonable(value.value)}
    if isinstance(value, Path):
        return {"__path__": str(value)}
    if isinstance(value, bytes):
        return {"__bytes__": value.hex()}
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        return {
            "__dataclass__": f"{value.__class__.__module__}.{value.__class__.__qualname__}",
            "fields": {field.name: _jsonable(getattr(value, field.name)) for field in dataclasses.fields(value)},
        }
    if isinstance(value, (list, tuple)):
        return [_jsonable(item) for item in value]
    if isinstance(value, dict):
        items = [(_jsonable(k), _jsonable(v)) for k, v in value.items()]
        items.sort(key=lambda item: json.dumps(item[0], sort_keys=True, separators=(",", ":")))
        return {"__dict_items__": items}
    raise TypeError(f"value is not JSON-cacheable: {type(value).__module__}.{type(value).__qualname__}")


def _function_bytecode_hash(func: Callable[..., Any]) -> str:
    """Hash the function code object so implementation changes invalidate cache."""
    return hashlib.sha256(marshal.dumps(func.__code__)).hexdigest()


def _make_cache_key(func_hash: str, args: tuple[Any, ...], kwargs: dict[str, Any]) -> str:
    """Return a stable key for function bytecode + JSON-cacheable arguments."""
    payload = json.dumps(
        {"function_bytecode_hash": func_hash, "args": _jsonable(args), "kwargs": _jsonable(kwargs)},
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _load_cache(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"version": 1, "entries": {}}
    with path.open("r", encoding="utf-8") as f:
        cache = json.load(f)
    if not isinstance(cache, dict) or cache.get("version") != 1 or not isinstance(cache.get("entries"), dict):
        raise ValueError(f"unsupported cache file format: {path}")
    return cache


def _save_cache_atomic(path: Path, cache: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.", delete=False) as f:
        tmp = Path(f.name)
        json.dump(cache, f, sort_keys=True, separators=(",", ":"))
        f.write("\n")
    tmp.replace(path)


def _default_cache_path(func: Callable[..., Any]) -> Path:
    module_file = getattr(__import__(func.__module__, fromlist=["__file__"]), "__file__", None)
    base_dir = Path(module_file).resolve().parent if module_file else Path.cwd()
    safe_name = f"{func.__module__}.{func.__qualname__}".replace("<", "_").replace(">", "_").replace(":", "_").replace("/", "_").replace("\\", "_")
    return base_dir / ".cache" / f"{safe_name}.json"


def _cache_path_for_key(base_path: Path, key: str) -> Path:
    return base_path.with_name(f"{base_path.stem}.{key}{base_path.suffix or '.json'}")


def disk_cache(func: F | None = None, *, path: str | Path | None = None) -> Callable[[F], F] | F:
    """Cache function results in JSON on disk, keyed by input args/kwargs.

    Use as either::

        @disk_cache
        def slow(...): ...

    or, if an explicit path is ever needed::

        @disk_cache(path="custom.json")
        def slow(...): ...

    By default the cache file is derived from the function's module and qualified
    name and stored under a `.cache/` directory next to the module file.  The
    function bytecode hash and argument hash are included in the filename, so
    each distinct function implementation/call gets its own JSON file.  The
    wrapped function's arguments and return value must be
    JSON-cacheable.  Common
    primitives, lists/tuples, dicts, pathlib paths, enums, bytes, and dataclass
    instances are supported for cache keys.  Results are stored as JSON-compatible
    data and returned as loaded from JSON on cache hits.
    """

    def decorator(inner: F) -> F:
        base_cache_path = Path(path) if path is not None else _default_cache_path(inner)
        function_hash = _function_bytecode_hash(inner)

        @functools.wraps(inner)
        def wrapper(*args: Any, **kwargs: Any) -> Any:
            key = _make_cache_key(function_hash, args, kwargs)
            cache_path = _cache_path_for_key(base_cache_path, key)
            cache = _load_cache(cache_path)
            entries = cache["entries"]
            if key in entries:
                print(f"{_DARK_GRAY}disk_cache hit: {inner.__qualname__} -> {cache_path}{_RESET}")
                return entries[key]["result"]

            print(f"{_DARK_GRAY}disk_cache miss: {inner.__qualname__} -> {cache_path}{_RESET}")
            result = inner(*args, **kwargs)
            entries[key] = {
                "function": f"{inner.__module__}.{inner.__qualname__}",
                "function_bytecode_hash": function_hash,
                "args": _jsonable(args),
                "kwargs": _jsonable(kwargs),
                "result": _jsonable(result),
            }
            _save_cache_atomic(cache_path, cache)
            return result

        return wrapper  # type: ignore[return-value]

    if func is not None:
        return decorator(func)
    return decorator

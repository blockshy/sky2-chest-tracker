"""《空之轨迹 the 2nd》静态资源读取器。

仅以只读模式访问原始资源；所有偏移、长度、循环引用都先验证。
FPAC 布局参考 https://github.com/coinkillerl/FPACker 。
BJSON 布局参考 ED9ModManager 的 bjson_decoder.cpp/h，并增加严格边界检查。
Required Notice: Copyright © 2026 lom2333 (https://github.com/lom2333/ED9ModManager)
相关派生部分遵循 https://polyformproject.org/licenses/noncommercial/1.0.0/ 。
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Any


@dataclass(frozen=True)
class Entry:
    """资源索引中的一项；offset 是包内绝对偏移，而非内存地址。"""

    name: str
    offset: int
    size: int


class Fpac:
    """读取 FPAC 索引及指定成员，不把整个大资源包载入内存。"""

    def __init__(self, path: Path):
        self.path = path
        self.size = path.stat().st_size
        self.entries: dict[str, Entry] = {}
        with path.open("rb") as stream:
            header = stream.read(16)
            if len(header) != 16:
                raise ValueError(f"FPAC 文件头不完整：{path}")
            magic, count, first_data, version = struct.unpack("<4sIII", header)
            if magic != b"FPAC" or version != 1:
                raise ValueError(f"不支持的 FPAC 格式：{path}")
            if count > 1_000_000 or 16 + count * 32 > self.size:
                raise ValueError("FPAC 索引数量越界")
            if first_data > self.size:
                raise ValueError("FPAC 数据起点越界")
            index = stream.read(count * 32)
            for _, _, name_offset, size, offset in struct.iter_unpack("<IIQQQ", index):
                if name_offset >= self.size or offset + size > self.size:
                    raise ValueError("FPAC 成员越界")
                stream.seek(name_offset)
                name_bytes = stream.read(min(4096, self.size - name_offset))
                end = name_bytes.find(b"\0")
                if end < 0:
                    raise ValueError("FPAC 路径缺少结束符")
                name = name_bytes[:end].decode("utf-8")
                if name in self.entries:
                    raise ValueError(f"FPAC 包含重复路径：{name}")
                self.entries[name] = Entry(name, offset, size)

    def read(self, name: str, max_size: int = 64 * 1024 * 1024) -> bytes:
        """读取单项资源；大小上限防止损坏索引导致意外内存分配。"""
        entry = self.entries[name]
        if entry.size > max_size:
            raise ValueError(f"资源超过当前读取上限：{name}")
        with self.path.open("rb") as stream:
            stream.seek(entry.offset)
            data = stream.read(entry.size)
        if len(data) != entry.size:
            raise ValueError(f"资源读取不完整：{name}")
        return data


class Bjson:
    """解码 Falcom 二进制 JSON，保留节点偏移供后续核对／定点补丁使用。"""

    def __init__(self, data: bytes):
        self.data = data
        self.names: dict[int, str] = {}
        self.cache: dict[int, dict[str, Any]] = {}
        if len(data) < 24 or data[:4] != b"JSON":
            raise ValueError("不是 Falcom BJSON")
        self.root_offset = self.unpack("<Q", 8) + 4
        self.bounds(self.root_offset, 5)
        cursor = 24
        index = 0
        while cursor < self.root_offset:
            value, end = self.string(cursor, self.root_offset)
            self.bounds(end, 4)
            self.names[cursor - 4] = value
            if index == 0:
                self.names[cursor - 3] = value
            cursor = end + 4
            index += 1
        if cursor != self.root_offset or data[self.root_offset] != 0:
            raise ValueError("BJSON 名称表或根节点损坏")

    def bounds(self, offset: int, size: int) -> None:
        """所有读取使用统一检查，拒绝负偏移和越过文件末尾的读取。"""
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f"BJSON 越界：offset={offset}, size={size}")

    def unpack(self, fmt: str, offset: int):
        self.bounds(offset, struct.calcsize(fmt))
        return struct.unpack_from(fmt, self.data, offset)[0]

    def string(self, offset: int, limit: int | None = None) -> tuple[str, int]:
        """返回字符串及结束零字节之后的位置，保留无法解码字节的替代标记。"""
        self.bounds(offset, 1)
        end = self.data.find(b"\0", offset, limit)
        if end < 0:
            raise ValueError("BJSON 字符串缺少结束符")
        return self.data[offset:end].decode("utf-8", errors="replace"), end + 1

    def node(self, offset: int) -> dict[str, Any]:
        """读取单节点，不递归展开；可用于查找节点及其原始偏移。"""
        if offset in self.cache:
            return self.cache[offset]
        kind = self.unpack("<B", offset)
        result: dict[str, Any] = {"offset": offset, "kind": kind}
        if kind in (2, 3, 4, 5, 6):
            token = self.unpack("<I", offset + 1)
            if token not in self.names:
                raise ValueError(f"未知 BJSON 字段 token：{token}")
            result["name"] = self.names[token]
        if kind in (0, 4, 5, 20):
            pos = offset + (5 if kind in (4, 5) else 1)
            count = self.unpack("<I", pos)
            self.bounds(pos + 4, count * 4)
            result["children"] = [self.unpack("<I", pos + 4 + i * 4) for i in range(count)]
        elif kind == 2:
            result["value"] = self.string(offset + 5)[0]
        elif kind == 3:
            result["value"] = self.unpack("<d", offset + 5)
        elif kind == 6:
            result["value"] = self.unpack("<B", offset + 5)
        elif kind == 18:
            result["label"], child = self.string(offset + 1)
            result["children"] = [child]
        elif kind == 19:
            result["value"] = {"id": self.unpack(">I", offset + 1), "aux": self.unpack("<I", offset + 5)}
        else:
            raise ValueError(f"不支持的 BJSON 节点类型：0x{kind:02x}，偏移 {offset}")
        self.cache[offset] = result
        return result

    def tree(self, offset: int | None = None, ancestors: frozenset[int] = frozenset()) -> dict[str, Any]:
        """生成带类型与偏移的完整节点树；检测环，防止坏文件无限递归。"""
        offset = self.root_offset if offset is None else offset
        if offset in ancestors or len(ancestors) > 128:
            raise ValueError("BJSON 存在循环引用或嵌套过深")
        result = dict(self.node(offset))
        if "children" in result:
            result["children"] = [self.tree(child, ancestors | {offset}) for child in result["children"]]
        return result


def walk(node: dict[str, Any]):
    """按深度优先顺序遍历已解码的树节点。"""
    yield node
    for child in node.get("children", []):
        yield from walk(child)


def plain(node: dict[str, Any]) -> Any:
    """将调试节点树转为可读对象；重复键用列表保留，避免静默丢失数据。"""
    if "value" in node:
        return node["value"]
    children = node.get("children", [])
    if node["kind"] == 18:
        return {"$label": node["label"], "$value": plain(children[0])}
    # COMPOUND 节点既可表达对象，也可表达无名称数组，依据子节点是否带字段名判断。
    if node["kind"] == 5 or (node["kind"] == 20 and not all("name" in c for c in children)):
        return [plain(child) for child in children]
    result: dict[str, Any] = {}
    for child in children:
        name = child.get("name", f"@{child['offset']}")
        if name in result:
            raise ValueError(f"对象字段重复，需要人工确认：{name}")
        result[name] = plain(child)
    return result

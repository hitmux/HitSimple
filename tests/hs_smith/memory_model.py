"""Independent checked-memory state model for HS-Smith profiles."""

from __future__ import annotations

from dataclasses import dataclass


class MemoryError(RuntimeError):
    """A modeled memory violation with a stable classification."""


@dataclass(frozen=True)
class AddressValue:
    object_id: int
    offset: int
    generation: int
    origin: str


@dataclass
class MemoryObject:
    object_id: int
    size: int
    bytes: bytearray
    initialized: set[int]
    generation: int = 1
    alive: bool = True


class MemoryModel:
    """Tracks allocation, bounds, initialization, aliases, and generations."""

    def __init__(self) -> None:
        self._next_id = 1
        self._objects: dict[int, MemoryObject] = {}

    def calloc(self, count: int, size: int) -> AddressValue:
        if count < 0 or size < 0:
            raise MemoryError("invalid-allocation-size")
        total = count * size
        if count and total // count != size:
            raise MemoryError("calloc-overflow")
        return self._allocate(total, zeroed=True)

    def alloc(self, size: int) -> AddressValue:
        if size < 0:
            raise MemoryError("invalid-allocation-size")
        return self._allocate(size, zeroed=False)

    def _allocate(self, size: int, zeroed: bool) -> AddressValue:
        object_id = self._next_id
        self._next_id += 1
        initialized = set(range(size)) if zeroed else set()
        self._objects[object_id] = MemoryObject(
            object_id, size, bytearray(size), initialized
        )
        return AddressValue(object_id, 0, 1, "dynamic-base")

    def offset(self, address: AddressValue, amount: int) -> AddressValue:
        return AddressValue(address.object_id, address.offset + amount, address.generation, "offset")

    def realloc(self, address: AddressValue, size: int) -> AddressValue:
        object_ = self._require_base(address)
        if size < 0:
            raise MemoryError("invalid-allocation-size")
        old_size = object_.size
        object_.bytes = object_.bytes[:size] + bytearray(max(0, size - old_size))
        object_.initialized = {index for index in object_.initialized if index < size}
        object_.size = size
        object_.generation += 1
        return AddressValue(object_.object_id, 0, object_.generation, "dynamic-base")

    def free(self, address: AddressValue) -> None:
        object_ = self._require_base(address)
        object_.alive = False

    def store(self, address: AddressValue, value: int) -> None:
        object_, index = self._require_access(address)
        object_.bytes[index] = value & 0xFF
        object_.initialized.add(index)

    def load(self, address: AddressValue) -> int:
        object_, index = self._require_access(address)
        if index not in object_.initialized:
            raise MemoryError("uninitialized-read")
        return object_.bytes[index]

    def _require_base(self, address: AddressValue) -> MemoryObject:
        object_ = self._require_live_generation(address)
        if address.offset != 0 or address.origin != "dynamic-base":
            raise MemoryError("invalid-free")
        return object_

    def _require_access(self, address: AddressValue) -> tuple[MemoryObject, int]:
        object_ = self._require_live_generation(address)
        if address.offset < 0 or address.offset >= object_.size:
            raise MemoryError("out-of-bounds")
        return object_, address.offset

    def _require_live_generation(self, address: AddressValue) -> MemoryObject:
        try:
            object_ = self._objects[address.object_id]
        except KeyError as error:
            raise MemoryError("unknown-address") from error
        if not object_.alive:
            raise MemoryError("use-after-free")
        if address.generation != object_.generation:
            raise MemoryError("stale-address")
        return object_


@dataclass(frozen=True)
class MemoryCase:
    name: str
    source: str
    expected_stdout: str | None
    expected_error: str | None
    provability: str
    detectability: str
    address_origin: str
    feature_tags: tuple[str, ...]


def safe_memory_case(first: int, second: int) -> MemoryCase:
    model = MemoryModel()
    pointer = model.calloc(1, 4)
    model.store(pointer, first)
    model.store(model.offset(pointer, 1), second)
    pointer = model.realloc(pointer, 6)
    total = model.load(pointer) + model.load(model.offset(pointer, 1))
    model.free(pointer)
    source = "\n".join(
        (
            "$include <stdio.hsh>",
            "$include <stdlib.hsh>",
            "",
            "func main() {",
            "    new pointer as addr = calloc(1, 4)",
            "    [1]*pointer = " + str(first),
            "    [1]*(pointer? + 1) = " + str(second),
            "    pointer = realloc(pointer, 6)",
            "    new first_value as u8 = [1]*pointer",
            "    new second_value as u8 = [1]*(pointer? + 1)",
            "    free(pointer)",
            '    printf("%d\\n", to_u32(first_value) + to_u32(second_value))',
            "    return 0",
            "}",
            "",
        )
    )
    return MemoryCase(
        "checked-memory-safe",
        source,
        str(total) + "\n",
        None,
        "not-applicable",
        "required",
        "tracked-dynamic-base",
        ("checked-memory", "calloc", "realloc", "free", "initialized-interval"),
    )


def invalid_memory_cases() -> tuple[MemoryCase, ...]:
    return (
        MemoryCase(
            "checked-memory-use-after-free",
            """$include <stdlib.hsh>

func main() -> i32 {
    new pointer as addr = alloc(1)
    free(pointer)
    return [1]*pointer
}
""",
            None,
            "use-after-free",
            "static",
            "required",
            "tracked-dynamic-base",
            ("checked-memory", "use-after-free", "single-rule-invalid"),
        ),
        MemoryCase(
            "checked-memory-interior-free",
            """$include <stdlib.hsh>

func main() {
    new pointer as addr = alloc(2)
    new interior as addr = pointer? + 1
    free(interior)
    return 0
}
""",
            None,
            "invalid-free",
            "static",
            "required",
            "tracked-dynamic-interior",
            ("checked-memory", "invalid-free", "single-rule-invalid"),
        ),
    )

from typing import cast, Any, TypeVar

T = TypeVar("T")

def safe_cast(typ: type[T], o: Any) -> T:
    """
    Bandaid around typing.cast: Shall be used to assert to static checkers that typing is correct. We therefore always cast, but at runtime we check if this cast is valid.
    """
    if type(o) != typ:
        raise TypeError(f"Cannot cast object to {typ}, because it is {type(o)}: {o}")

    return cast(typ, o)

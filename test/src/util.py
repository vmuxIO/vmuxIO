from typing import cast, Any, TypeVar, Iterator, cast, List, Dict, Callable, Tuple, Any

T = TypeVar("T")

def safe_cast(typ: type[T], o: Any) -> T:
    """
    Bandaid around typing.cast: Shall be used to assert to static checkers that typing is correct. We therefore always cast, but at runtime we check if this cast is valid.
    """
    if type(o) != typ:
        raise TypeError(f"Cannot cast object to {typ}, because it is {type(o)}: {o}")

    return cast(typ, o)

def product_dict(input_dict: Dict[str, List[Any]]) -> List[Dict[str, Any]]:
    ret = _product_dict(input_dict)
    assert ret is not None
    return ret

def _product_dict(input_dict: Dict[str, List[Any]], result=None, keys: List[str] | None = None, index=0, current={}) -> List[Dict[str, Any]] | None:
    """
    Generates all combinations of lists contained in a dictionary, where each combination is represented as a dictionary with the same keys.
    """
    # Initialize result list and keys on the first call
    if result is None:
        result = []
        keys = list(input_dict.keys())  # Extract keys to maintain order
    assert keys is not None

    # Base case: If current combination is complete, add it to the result
    if index == len(keys):
        result.append(current.copy())  # Use copy to avoid reference issues
        return

    # Recursive case: Iterate through the current list and recurse for the next list
    current_key = keys[index]
    for item in input_dict[current_key]:
        current[current_key] = item  # Assign current item to its key in the current combination
        _product_dict(input_dict, result=result, keys=keys, index=index + 1, current=current)

    return result



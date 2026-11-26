"""NSO patches for Sea of Stars (01008C0016544000).

Fixes crash when loading PC/Steam saves containing Update 2.0 / Dawn of Equinox
or DLC items/relics (e.g. GUID 866778a20ec11da4baad80bbe7540c56) on v1.0.45861.
In function 0x3dd58b0, when Dictionary.TryGetValue fails to find an item GUID,
instead of storing NULL and dereferencing x0 at 0x3bf0b8 (SIGSEGV), branch
directly to 0x3dd5a9c (loop continue) to skip unknown items.
"""

CUTS = {
    # main binary build-id: 092cd60ef7c038ef934191dbb6011809
    '092cd60ef7c038ef934191dbb6011809': [
        (0x3dd5bd4, '0c000014', 'b2ffff17', 'Inventory load: unknown item branch directly to continue'),
        (0x3dd5bfc, '1f00007200119f9a', '00f5ff34e00308aa', 'Inventory load: TryGetValue failure cbz to continue, else mov x0, x8'),
    ],
}

"""Firmware releases, compatibility matrix and test results (Releases page).

One document ("releases"/"book"): pump_types, releases, compat, tests.
Anyone signed in can read it; the "firmware" permission is needed to change it.
"""

import time

from fastapi import Depends, HTTPException

from ..container import Container
from .deps import container, current_user, need, protected

router = protected()

_COL, _DOC = "releases", "book"
_LISTS = ("pump_types", "releases", "compat", "tests")
_RESULTS = {"pass", "fail", "issue", "untested", "na"}


def _empty() -> dict:
    return {k: [] for k in _LISTS}


def _validate(book: dict) -> None:
    for k in _LISTS:
        if not isinstance(book.get(k), list):
            raise HTTPException(400, f"'{k}' must be a list")
    for i, r in enumerate(book["releases"]):
        if not isinstance(r, dict) or not r.get("product") or not r.get("version"):
            raise HTTPException(400, f"releases[{i}]: needs product and version")
    for i, row in enumerate(book["compat"]):
        if not isinstance(row, dict) or not row.get("main"):
            raise HTTPException(400, f"compat[{i}]: needs main")
        bad = [f"{t}={v}" for t, v in (row.get("results") or {}).items() if v not in _RESULTS]
        if bad:
            raise HTTPException(400, f"compat[{i}]: unknown result {', '.join(bad)} (use {', '.join(sorted(_RESULTS))})")
    for i, run in enumerate(book["tests"]):
        if not isinstance(run, dict) or not run.get("id") or not isinstance(run.get("items", []), list):
            raise HTTPException(400, f"tests[{i}]: needs id and an items list")
        for j, it in enumerate(run.get("items", [])):
            if it.get("result") not in _RESULTS:
                raise HTTPException(400, f"tests[{i}].items[{j}]: result must be one of {', '.join(sorted(_RESULTS))}")


@router.get("/releases")
def get_releases(c: Container = Depends(container)):
    return c.docs.get(_COL, _DOC) or _empty()


@router.put("/releases", dependencies=[need("firmware")])
def put_releases(body: dict, c: Container = Depends(container), user: str = Depends(current_user)):
    _validate(body)
    book = {k: body[k] for k in _LISTS}
    book["updated"], book["updated_by"] = time.time(), user
    c.docs.put(_COL, _DOC, book)
    c.audit.record(user, "releases.update", releases=len(book["releases"]), compat=len(book["compat"]),
                   tests=len(book["tests"]))
    return book

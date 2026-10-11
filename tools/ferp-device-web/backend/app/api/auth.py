"""Login / logout (public) and user accounts (admin)."""

from fastapi import APIRouter, Depends, HTTPException, Request, Response
from pydantic import BaseModel, Field

from ..adapters.auth import SESSION_COOKIE, session_token
from ..container import Container
from ..services.users import PERMISSIONS, SESSION_DAYS
from .deps import admin_user, container, current_user, guard, protected

public = APIRouter(prefix="/api")
router = protected()


class LoginIn(BaseModel):
    username: str = Field(..., min_length=1)
    password: str = Field(..., min_length=1)


class UserIn(BaseModel):
    username: str = Field(..., min_length=1)
    password: str = Field(..., min_length=8)
    admin: bool = False
    permissions: list[str] = []


class UserPatch(BaseModel):
    admin: bool | None = None
    permissions: list[str] | None = None
    disabled: bool | None = None
    password: str | None = None


def _users(c: Container):
    users = getattr(c.auth, "users", None)
    if users is None:
        raise HTTPException(400, f"User accounts are not used in auth mode '{c.auth.mode}'")
    return users


@public.post("/auth/login")
def login(body: LoginIn, response: Response, c: Container = Depends(container)):
    token = _users(c).login(body.username, body.password)
    if not token:
        c.audit.record(body.username.strip().lower() or "?", "auth.login.failed")
        raise HTTPException(401, "Wrong user name or password, or the account is disabled")
    c.audit.record(body.username.strip().lower(), "auth.login")
    response.set_cookie(SESSION_COOKIE, token, max_age=SESSION_DAYS * 86400, httponly=True, samesite="lax", path="/")
    return {"ok": True}


@public.post("/auth/logout")
def logout(request: Request, response: Response, c: Container = Depends(container)):
    users = getattr(c.auth, "users", None)
    if users is not None:
        users.logout(session_token(request.headers))
    response.delete_cookie(SESSION_COOKIE, path="/")
    return {"ok": True}


@router.get("/me")
def me(request: Request, user: str = Depends(current_user)):
    c = container(request)
    keys = [k for k, _, _ in PERMISSIONS]
    return {"user": user, "auth_mode": c.auth.mode, "login_required": user != "local",
            "admin": c.auth.is_admin(user), "permissions": [k for k in keys if c.auth.allows(user, k)]}


@router.get("/permissions")
def permissions():
    return [{"key": k, "label": label, "description": d} for k, label, d in PERMISSIONS]


@router.get("/users", dependencies=[Depends(admin_user)])
def list_users(c: Container = Depends(container)):
    return _users(c).list()


@router.post("/users")
def create_user(body: UserIn, c: Container = Depends(container), user: str = Depends(admin_user)):
    u = guard(_users(c).create, body.username, body.password, body.admin, body.permissions)
    c.audit.record(user, "user.create", username=u["username"], admin=u["admin"], permissions=u["permissions"])
    return u


@router.patch("/users/{username}")
def update_user(username: str, body: UserPatch, c: Container = Depends(container), user: str = Depends(admin_user)):
    u = guard(_users(c).update, username, acting=user, admin=body.admin, permissions=body.permissions,
              disabled=body.disabled, password=body.password)
    c.audit.record(user, "user.update", username=u["username"], admin=u["admin"], permissions=u["permissions"],
                   disabled=u["disabled"], password_changed=bool(body.password))
    return u


@router.delete("/users/{username}")
def delete_user(username: str, c: Container = Depends(container), user: str = Depends(admin_user)):
    if not guard(_users(c).delete, username, user):
        raise HTTPException(404, f"Unknown user {username}")
    c.audit.record(user, "user.delete", username=username)
    return {"ok": True}

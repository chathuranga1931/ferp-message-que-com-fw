import { createContext, useContext, type ReactNode } from "react";

/** Permission keys (server: services/users.py PERMISSIONS). */
export type Permission = "devices" | "workspace" | "config" | "ota" | "firmware" | "logs" | "history" | "settings";

export interface Me {
  user: string;
  auth_mode: string;
  login_required: boolean;     // false = open server (no accounts yet, or auth off)
  admin: boolean;
  permissions: Permission[];
}

const AuthContext = createContext<Me | null>(null);

export function AuthProvider({ me, children }: { me: Me; children: ReactNode }) {
  return <AuthContext.Provider value={me}>{children}</AuthContext.Provider>;
}

/** The logged-in user and `can(permission)` (admins can do everything). */
export function useAuth() {
  const me = useContext(AuthContext);
  return {
    me,
    admin: !!me?.admin,
    can: (p: Permission) => !!me && (me.admin || me.permissions.includes(p)),
  };
}

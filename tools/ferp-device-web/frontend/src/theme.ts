/**
 * Colour theme — a per-browser preference (localStorage), not a server setting,
 * so each person / device can pick their own. "system" follows the OS setting.
 * Applied as <html data-theme="light|dark">; styles.css holds both palettes.
 */
import { useEffect, useState } from "react";

export type ThemePref = "system" | "light" | "dark";

const LS_THEME = "ferp.theme";

export function loadTheme(): ThemePref {
  try {
    const v = localStorage.getItem(LS_THEME);
    return v === "light" || v === "dark" ? v : "system";
  } catch {
    return "system";
  }
}

const media = () => window.matchMedia?.("(prefers-color-scheme: dark)");

export function applyTheme(pref: ThemePref = loadTheme()): void {
  const dark = pref === "dark" || (pref === "system" && !!media()?.matches);
  document.documentElement.dataset.theme = dark ? "dark" : "light";
}

export function saveTheme(pref: ThemePref): void {
  try { localStorage.setItem(LS_THEME, pref); } catch { /* private mode: applies to this tab only */ }
  applyTheme(pref);
}

/** Call once at start-up: applies the saved theme and follows OS changes while on "system". */
export function initTheme(): void {
  applyTheme();
  media()?.addEventListener?.("change", () => { if (loadTheme() === "system") applyTheme("system"); });
}

export function useTheme(): [ThemePref, (p: ThemePref) => void] {
  const [pref, setPref] = useState<ThemePref>(loadTheme);
  useEffect(() => { saveTheme(pref); }, [pref]);
  return [pref, setPref];
}

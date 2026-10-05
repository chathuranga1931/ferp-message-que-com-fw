import { useEffect, useState } from "react";
/** Same normalisation as ferp_core.topics.topic_id (and the firmware's uuid_to_topic_id). */
export function topicId(id: string): string {
  return id.replace(/[:-]/g, "").toLowerCase();
}

export function fmtTime(ts: number): string {
  return new Date(ts * 1000).toLocaleTimeString([], { hour12: false });
}

export function fmtDateTime(ts: number): string {
  return new Date(ts * 1000).toLocaleString([], { hour12: false });
}

export function fmtBytes(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / 1024 / 1024).toFixed(2)} MB`;
}

export function hex(n: number, width = 4): string {
  return "0x" + n.toString(16).toUpperCase().padStart(width, "0");
}

export async function copyText(text: string): Promise<boolean> {
  try { await navigator.clipboard.writeText(text); return true; } catch { return false; }
}

export const EMPTY: Record<string, never> = Object.freeze({}) as Record<string, never>;

export function fmtAgo(ts: number | null, now: number): string {
  if (!ts) return "never";
  const d = Math.max(0, Math.round(now - ts));
  if (d < 60) return `${d}s ago`;
  if (d < 3600) return `${Math.floor(d / 60)}m ago`;
  if (d < 86400) return `${Math.floor(d / 3600)}h ago`;
  return `${Math.floor(d / 86400)}d ago`;
}

/** Re-render every `ms` (for relative times). */
export function useNow(ms = 5000): number {
  const [now, setNow] = useState(Date.now() / 1000);
  useEffect(() => { const t = setInterval(() => setNow(Date.now() / 1000), ms); return () => clearInterval(t); }, [ms]);
  return now;
}

/** Trigger a browser download of text content. */
export function downloadText(name: string, text: string, type = "application/json") {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = Object.assign(document.createElement("a"), { href: url, download: name });
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/** Config keys that exist on devices of the given type (keys without a type list apply to all). */
export function keysForType<T extends { device_types?: string[] }>(keys: T[], deviceType?: string | null): T[] {
  const t = (deviceType ?? "").trim().toLowerCase();
  if (!t) return keys;
  return keys.filter((k) => !k.device_types?.length || k.device_types.some((d) => d.toLowerCase() === t));
}

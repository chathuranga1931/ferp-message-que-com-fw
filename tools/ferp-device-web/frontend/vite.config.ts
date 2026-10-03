import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Dev: `npm run dev` proxies API + WebSocket to the backend (VITE_BACKEND, default :8700).
// Prod: `npm run build` → dist/, served by the FastAPI backend itself.
const backend = process.env.VITE_BACKEND ?? "127.0.0.1:8700";

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      "/api": `http://${backend}`,
      "/ws": { target: `ws://${backend}`, ws: true },
    },
  },
});

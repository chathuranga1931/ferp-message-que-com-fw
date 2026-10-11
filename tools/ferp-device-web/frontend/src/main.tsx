import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import { ApiError, api } from "./api";
import { AuthProvider } from "./auth";
import Login from "./pages/Login";
import { startLiveConnection } from "./store";
import { initTheme } from "./theme";
import "./styles.css";

initTheme();
const root = createRoot(document.getElementById("root")!);
// who is signed in decides what loads: the sign-in screen, or the app with that user's permissions
api.me().then((me) => {
  startLiveConnection();
  root.render(<StrictMode><AuthProvider me={me}><App /></AuthProvider></StrictMode>);
}).catch((e) => {
  if (e instanceof ApiError && e.status === 401) root.render(<StrictMode><Login /></StrictMode>);
  else root.render(<div className="banner error">Cannot reach the server: {String((e as Error).message)}. Reload the page to try again.</div>);
});

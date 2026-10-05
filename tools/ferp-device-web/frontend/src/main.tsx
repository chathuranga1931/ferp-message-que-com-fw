import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import { startLiveConnection } from "./store";
import { initTheme } from "./theme";
import "./styles.css";

initTheme();
startLiveConnection();
createRoot(document.getElementById("root")!).render(<StrictMode><App /></StrictMode>);

import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import { startLiveConnection } from "./store";
import "./styles.css";

startLiveConnection();
createRoot(document.getElementById("root")!).render(<StrictMode><App /></StrictMode>);

import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
import fs from "node:fs";
import path from "node:path";

const projectRoot = fs.realpathSync(path.resolve("."));

export default defineConfig({
  root: projectRoot,
  plugins: [react()],
  server: {
    port: 5173,
    strictPort: true
  }
});

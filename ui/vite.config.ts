import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// 引擎从任意路径静态托管 ui/dist，资源引用必须相对
export default defineConfig({
  base: './',
  plugins: [react()],
  build: {
    outDir: 'dist',
  },
});

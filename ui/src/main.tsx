import React from 'react';
import ReactDOM from 'react-dom/client';
import 'dockview/dist/styles/dockview.css';
import './styles.css';
import App from './App';
import { initTheme } from './theme';

// I6：令牌解析早于挂载（canvas/SVG 面板从 --chart-*/--rg-* 取色）
initTheme();

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);

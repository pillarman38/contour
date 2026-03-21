const express = require('express');
const path = require('path');
const fs = require('fs');
const { createProxyMiddleware } = require('http-proxy-middleware');

const app = express();
const PORT = process.env.PORT || 4012;
const API_BACKEND = process.env.API_BACKEND || 'http://localhost:8080';

// Proxy /api/* to the C++ backend FIRST (before body parsing, so POST body is forwarded intact)
// If contour and golf_sim run on different machines, set API_BACKEND (e.g. http://192.168.1.10:8080)
// When mounted at /api, req.url is e.g. /session-id; pathRewrite ensures backend gets /api/session-id
app.use('/api', createProxyMiddleware({
  target: API_BACKEND,
  changeOrigin: true,
  proxyTimeout: 30000,
  pathRewrite: { '^/': '/api/' },
}));

// Serve static files from Angular build output
const staticDir = path.join(__dirname, 'dist', 'contour', 'browser');
if (!fs.existsSync(staticDir)) {
  console.error('Build not found. Run "npm run build" first.');
  process.exit(1);
}
app.use(express.static(staticDir));

// SPA fallback: serve index.html for all other routes
app.get('*', (req, res) => {
  res.sendFile(path.join(staticDir, 'index.html'));
});

app.listen(PORT, () => {
  console.log(`Contour server at http://localhost:${PORT}`);
  console.log(`API proxy: /api/* -> ${API_BACKEND}`);
  console.log('(If 504, ensure golf_sim is running and API_BACKEND points to it)');
});

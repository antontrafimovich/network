const express = require("express");
const fs = require("fs");
const morgan = require("morgan");
const path = require("path");

const app = express();
const port = 8081;
const assetsDir = path.join(__dirname, "assets");
const useChunkedTransfer = process.argv.includes("--chunked");

const contentTypes = {
  ".css": "text/css; charset=utf-8",
  ".gif": "image/gif",
  ".html": "text/html; charset=utf-8",
  ".ico": "image/x-icon",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".txt": "text/plain; charset=utf-8",
};

function getFilePath(reqPath) {
  const pathname = decodeURIComponent(new URL(reqPath, "http://localhost").pathname);
  const relativePath = pathname === "/" ? "index.html" : pathname.slice(1);
  const filePath = path.resolve(assetsDir, relativePath);

  if (!filePath.startsWith(`${assetsDir}${path.sep}`) && filePath !== assetsDir) {
    return null;
  }

  return filePath;
}

app.use(morgan("combined"));

app.use((req, res, next) => {
  if (req.method !== "GET" && req.method !== "HEAD") {
    next();
    return;
  }

  let filePath;
  try {
    filePath = getFilePath(req.url);
  } catch {
    res.sendStatus(400);
    return;
  }

  if (!filePath) {
    res.sendStatus(403);
    return;
  }

  fs.stat(filePath, (statError, stat) => {
    if (statError || !stat.isFile()) {
      next();
      return;
    }

    res.setHeader("Content-Type", contentTypes[path.extname(filePath).toLowerCase()] || "application/octet-stream");

    if (!useChunkedTransfer) {
      res.setHeader("Content-Length", stat.size);
    }

    if (req.method === "HEAD") {
      res.end();
      return;
    }

    fs.createReadStream(filePath).pipe(res);
  });
});

const server = app.listen(port, () => {
  console.log(`Serving ${assetsDir} on http://localhost:${port} (${useChunkedTransfer ? "chunked" : "content-length"} mode)`);
});
server.keepAliveTimeout = 60000;

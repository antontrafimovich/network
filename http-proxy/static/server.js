const express = require("express");
const fs = require("fs");
const http = require("http");
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
  const pathname = decodeURIComponent(
    new URL(reqPath, "http://localhost").pathname,
  );
  const relativePath = pathname === "/" ? "index.html" : pathname.slice(1);
  const filePath = path.resolve(assetsDir, relativePath);

  if (
    !filePath.startsWith(`${assetsDir}${path.sep}`) &&
    filePath !== assetsDir
  ) {
    return null;
  }

  return filePath;
}

function getRawPacketText(err) {
  if (!err.rawPacket) return "";

  return err.rawPacket
    .toString("latin1")
    .replace(/\r/g, "\\r")
    .replace(/\n/g, "\\n\n");
}

function getRequestLine(rawPacketText) {
  return rawPacketText.split("\\r\\n")[0] || "<unparseable request>";
}

function getChunkText(chunk, encoding) {
  if (Buffer.isBuffer(chunk)) return chunk.toString("latin1");
  if (typeof chunk === "string") {
    return Buffer.from(chunk, encoding).toString("latin1");
  }

  return "";
}

function observeNodeHttpErrors(server) {
  server.prependListener("request", (req) => {
    req.socket._reachedExpress = true;
  });

  server.on("connection", (socket) => {
    let rawRequest = "";
    let logged = false;
    const originalWrite = socket.write.bind(socket);

    socket.prependListener("data", (chunk) => {
      if (rawRequest.length < 4096) {
        rawRequest += chunk.toString("latin1", 0, 4096 - rawRequest.length);
      }
    });

    socket.write = (chunk, encoding, callback) => {
      if (typeof encoding === "function") {
        callback = encoding;
        encoding = undefined;
      }

      const responseText = getChunkText(chunk, encoding);
      const statusMatch = responseText.match(
        /^HTTP\/1\.1 ([45]\d\d) ([^\r\n]+)/,
      );

      if (statusMatch && !socket._reachedExpress && !logged) {
        logged = true;

        const rawPacketText = rawRequest
          .replace(/\r/g, "\\r")
          .replace(/\n/g, "\\n\n");

        console.error("HTTP server generated error response");
        console.error(`remoteAddress: ${socket.remoteAddress || "-"}`);
        console.error(`status: ${statusMatch[1]} ${statusMatch[2]}`);

        if (rawPacketText) {
          console.error("rawPacket:");
          console.error(rawPacketText);
        }
      }

      return encoding === undefined
        ? originalWrite(chunk, callback)
        : originalWrite(chunk, encoding, callback);
    };
  });
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

    res.setHeader(
      "Content-Type",
      contentTypes[path.extname(filePath).toLowerCase()] ||
        "application/octet-stream",
    );

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

const server = http.createServer(app);
observeNodeHttpErrors(server);

server.on("error", (err) => {
  console.error("HTTP server error");
  console.error(err);
});

server.listen(port, () => {
  console.log(
    `Serving ${assetsDir} on http://localhost:${port} (${useChunkedTransfer ? "chunked" : "content-length"} mode)`,
  );
});
server.keepAliveTimeout = 60000 * 6;

server.on("clientError", (err, socket) => {
  const rawPacketText = getRawPacketText(err);
  const requestLine = getRequestLine(rawPacketText);
  const statusCode = err.code === "HPE_HEADER_OVERFLOW" ? 431 : 400;
  const statusText =
    statusCode === 431 ? "Request Header Fields Too Large" : "Bad Request";
  const remoteAddress = socket.remoteAddress || "-";
  const now = new Date().toISOString();

  console.log(`${remoteAddress} - - [${now}] "${requestLine}" ${statusCode} -`);

  console.error("HTTP request parse error");
  console.error(`remoteAddress: ${remoteAddress}`);
  console.error(`code: ${err.code || "UNKNOWN"}`);
  console.error(`message: ${err.message}`);

  if (typeof err.bytesParsed === "number") {
    console.error(`bytesParsed: ${err.bytesParsed}`);
  }

  if (rawPacketText) {
    console.error("rawPacket:");
    console.error(rawPacketText);
  }

  if (!socket.writable) {
    socket.destroy();
    return;
  }

  socket.end(
    `HTTP/1.1 ${statusCode} ${statusText}\r\nConnection: close\r\n\r\n`,
  );
});

const express = require("express");
const morgan = require("morgan");
const path = require("path");

const app = express();
const port = 8081;

app.use(morgan("combined"));
app.use(express.static(path.join(__dirname, "assets")));

app.listen(port);

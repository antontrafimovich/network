Implement a very simple Express static-file server with request logging.

Context:
- The project already has all static files needed for the server located in ['assets'](./assets/) directory.
- Do not create, modify, generate, or rebuild any static files.
- This should replace the current `pm2 serve` usage.
- The goal is only to log every request made to the static files.

Task:
1. Add a `server.js` file.
2. Use Express to serve the existing static files directory.
3. Use `morgan` to log every HTTP request to stdout.
4. Use a fixed port: `8080`.
5. Keep the code minimal. No extra configuration, no health endpoint, no custom logging system, no unnecessary abstractions.
7. Add a package script:
   - `"start": "node server.js"`

Expected behavior:
- Running `node server.js` starts the static file server on port `8080`.
- Running it with PM2 allows logs to be viewed with:

  `pm2 logs`

Acceptance criteria:
- Existing static files are served successfully.
- Every request is logged.
- Missing files return 404 and are logged.
- Static files are not changed.
- `pm2 serve` is no longer needed.
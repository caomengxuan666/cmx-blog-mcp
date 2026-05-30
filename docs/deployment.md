# Deployment Notes

## Required Programs

- `cmx-blog-mcp`
- `git`
- `gh`
- a configured git identity: `git config user.name` and `git config user.email`
- either an SSH deploy key with `github.com` in `known_hosts`, or an HTTPS repo URL with token auth
- GitHub write access for the blog repo, from either `gh auth login` for the service user or `GH_TOKEN`

Before running it under a service manager, verify:

```bash
gh auth status
git ls-remote git@github.com:caomengxuan666/caomengxuan666.github.io.git main
```

`GH_TOKEN` is optional when `gh auth status` works for the same OS user that
runs the service. It is useful for systemd, Docker, or other headless
deployments because the credential is explicit and can be scoped to only
`caomengxuan666/caomengxuan666.github.io`.

`BLOG_MCP_AUTH_TOKEN` is separate. It protects the MCP HTTP endpoint itself.
Anyone who can reach the HTTP endpoint must present this bearer token before
the server will process MCP requests.

The default transport is stdio, which is normally launched by the MCP client.
For a long-running server process, set `BLOG_MCP_HTTP_PORT`; the executable then
uses cxxmcp's Streamable HTTP transport. Bind to `127.0.0.1` unless a reverse
proxy or tunnel is handling authentication.

## Systemd Example

```ini
[Unit]
Description=CMX Blog MCP Server
After=network.target

[Service]
Type=simple
User=cmx
WorkingDirectory=/srv/cmx-blog-mcp
Environment=BLOG_MCP_WORKDIR=/srv/cmx-blog-mcp/work
Environment=BLOG_REPO_URL=git@github.com:caomengxuan666/caomengxuan666.github.io.git
Environment=BLOG_REPO_SLUG=caomengxuan666/caomengxuan666.github.io
Environment=BLOG_MCP_HTTP_HOST=127.0.0.1
Environment=BLOG_MCP_HTTP_PORT=7331
Environment=BLOG_MCP_HTTP_PATH=/mcp
Environment=BLOG_MCP_AUTH_TOKEN=replace-with-long-random-token
# Optional if gh auth status works for User=cmx.
Environment=GH_TOKEN=replace-me
ExecStart=/srv/cmx-blog-mcp/cmx-blog-mcp
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

For production, put `GH_TOKEN` and `BLOG_MCP_AUTH_TOKEN` in an environment file
with restricted permissions instead of in the unit file.

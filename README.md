# cmx-blog-mcp

MCP server for publishing posts to `caomengxuan666.github.io`.

This repository is intentionally separate from the GitHub Pages repository.
The blog repo owns content and templates; this server owns runtime publishing
tools, GitHub credentials, logs, and deployment.

## Tools

- `generate_post_markdown`: build a Jekyll `_posts/YYYY-MM-DD-slug.md` file body.
- `validate_post_markdown`: check front matter and basic blog rules.
- `create_post_pr`: create a branch in the blog repo, add the generated post, and open a PR.

`create_post_pr` accepts the same fields as `generate_post_markdown`, plus:

- `dry_run`: return the planned file, branch, and Markdown without touching git.
- `overwrite`: allow replacing an existing `_posts/YYYY-MM-DD-slug.md` file.
- `branch`: override the default `blog-post/YYYY-MM-DD-slug` branch name.

## Build

Windows / Visual Studio:

```powershell
cmake -S . -B build -DCMX_BLOG_MCP_ENABLE_HTTP=ON
cmake --build build --target cmx-blog-mcp --config Release
```

Linux / single-config generators:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cmx-blog-mcp
```

By default CMake fetches `caomengxuan666/cxxmcp` from GitHub. To use an installed
package instead:

```bash
cmake -S . -B build -DCMX_BLOG_MCP_FETCH_CXXMCP=OFF
```

## Run

Default stdio transport:

```bash
export BLOG_MCP_WORKDIR=/srv/cmx-blog-mcp/work
export BLOG_REPO_URL=git@github.com:caomengxuan666/caomengxuan666.github.io.git
export BLOG_REPO_SLUG=caomengxuan666/caomengxuan666.github.io
export GH_TOKEN=...
./build/cmx-blog-mcp
```

PowerShell:

```powershell
$env:BLOG_MCP_WORKDIR="C:\srv\cmx-blog-mcp\work"
$env:BLOG_REPO_URL="git@github.com:caomengxuan666/caomengxuan666.github.io.git"
$env:BLOG_REPO_SLUG="caomengxuan666/caomengxuan666.github.io"
$env:GH_TOKEN="..."
.\build\Release\cmx-blog-mcp.exe
```

The server speaks MCP over stdio unless `BLOG_MCP_HTTP_PORT` is set. For a
server-hosted Streamable HTTP endpoint, bind it locally and put authentication
or SSH tunneling in front of it:

```bash
export BLOG_MCP_HTTP_HOST=127.0.0.1
export BLOG_MCP_HTTP_PORT=7331
export BLOG_MCP_HTTP_PATH=/mcp
export BLOG_MCP_AUTH_TOKEN=use-a-long-random-token
./build/cmx-blog-mcp
```

HTTP mode refuses to start without `BLOG_MCP_AUTH_TOKEN`. Clients must send:

```http
Authorization: Bearer use-a-long-random-token
```

Example tool input:

```json
{
  "title": "cxxmcp progress notes",
  "slug": "cxxmcp-progress-notes",
  "description": "Notes from recent commits, issues, and docs work in cxxmcp.",
  "date": "2026-05-30",
  "tags": ["cxxmcp", "mcp"],
  "body": "Write from commits, PRs, issues, and README content. Link the source items directly.",
  "dry_run": true
}
```

## Publishing Flow

1. An agent calls `create_post_pr`.
2. The server validates metadata and Markdown.
3. The server clones or updates the blog repository under `BLOG_MCP_WORKDIR`.
4. The server creates a branch, writes `_posts/YYYY-MM-DD-slug.md`, commits, pushes, and opens a PR.
5. The blog repository's normal GitHub Pages workflow publishes after merge.

The server only accepts `blog-post/*` branches and pushes `HEAD` to that branch;
it does not push directly to `main`.

## Client Config

For a local MCP client, point the command at the built executable:

```json
{
  "mcpServers": {
    "cmx-blog": {
      "command": "C:\\Users\\cmx\\repo\\cmx-blog-mcp\\build\\Release\\cmx-blog-mcp.exe",
      "env": {
        "BLOG_MCP_WORKDIR": "C:\\Users\\cmx\\repo\\cmx-blog-mcp\\.cache",
        "BLOG_REPO_URL": "git@github.com:caomengxuan666/caomengxuan666.github.io.git",
        "BLOG_REPO_SLUG": "caomengxuan666/caomengxuan666.github.io"
      }
    }
  }
}
```

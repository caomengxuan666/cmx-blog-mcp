#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/run.hpp"
#include "cxxmcp/server/auth.hpp"

namespace fs = std::filesystem;
using Json = mcp::protocol::Json;

namespace {

struct CommandResult {
  int code = 0;
  std::string output;
};

class DirectoryLock {
 public:
  explicit DirectoryLock(fs::path path) : path_(std::move(path)) {
    fs::create_directories(path_.parent_path());
    if (!fs::create_directory(path_)) {
      throw std::runtime_error("worktree lock is already held: " +
                               path_.string());
    }
  }

  ~DirectoryLock() {
    std::error_code ignored;
    fs::remove(path_, ignored);
  }

  DirectoryLock(const DirectoryLock&) = delete;
  DirectoryLock& operator=(const DirectoryLock&) = delete;

 private:
  fs::path path_;
};

std::string getenv_or(std::string_view name, std::string fallback) {
  if (const char* value = std::getenv(std::string(name).c_str())) {
    if (*value != '\0') {
      return value;
    }
  }
  return fallback;
}

std::string today_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  char buffer[11]{};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
  return buffer;
}

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
  std::string out = "\"";
  for (const char ch : value) {
    if (ch == '"') {
      out += "\\\"";
    } else {
      out += ch;
    }
  }
  out += "\"";
  return out;
#else
  std::string out = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out += ch;
    }
  }
  out += "'";
  return out;
#endif
}

CommandResult run_command(const std::string& command) {
  const std::string full = command + " 2>&1";
#ifdef _WIN32
  FILE* pipe = _popen(full.c_str(), "r");
#else
  FILE* pipe = popen(full.c_str(), "r");
#endif
  if (!pipe) {
    return {127, "failed to start command"};
  }

  std::string output;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

#ifdef _WIN32
  const int code = _pclose(pipe);
#else
  const int code = pclose(pipe);
#endif
  return {code, output};
}

std::string yaml_escape(std::string value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (const char ch : value) {
    if (ch == '\\' || ch == '"') {
      out += '\\';
    }
    out += ch;
  }
  return "\"" + out + "\"";
}

bool valid_slug(const std::string& slug) {
  static const std::regex pattern("^[a-z0-9]+(-[a-z0-9]+)*$");
  return std::regex_match(slug, pattern);
}

bool valid_date(const std::string& date) {
  static const std::regex pattern("^\\d{4}-\\d{2}-\\d{2}$");
  return std::regex_match(date, pattern);
}

bool valid_branch_name(const std::string& branch) {
  if (branch.rfind("blog-post/", 0) != 0) {
    return false;
  }
  if (branch == "blog-post/" || branch == "main" || branch == "master" ||
      branch == "HEAD") {
    return false;
  }
  if (branch.rfind("refs/", 0) == 0 || branch.front() == '/' ||
      branch.back() == '/' || branch.back() == '.') {
    return false;
  }
  if (branch.find("..") != std::string::npos ||
      branch.find("//") != std::string::npos ||
      branch.find("@{") != std::string::npos) {
    return false;
  }

  for (const unsigned char ch : branch) {
    if (std::isspace(ch) || ch < 0x21 || ch == ':' || ch == '?' || ch == '*' ||
        ch == '[' || ch == '\\' || ch == '^' || ch == '~') {
      return false;
    }
  }
  return true;
}

std::vector<std::string> read_tags(const Json& input) {
  std::vector<std::string> tags;
  if (!input.contains("tags")) {
    return tags;
  }

  const auto& raw = input.at("tags");
  if (raw.is_array()) {
    for (const auto& tag : raw) {
      tags.push_back(tag.get<std::string>());
    }
  } else if (raw.is_string()) {
    std::stringstream stream(raw.get<std::string>());
    std::string item;
    while (std::getline(stream, item, ',')) {
      item.erase(item.begin(),
                 std::find_if(item.begin(), item.end(), [](unsigned char ch) {
                   return !std::isspace(ch);
                 }));
      item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char ch) {
                   return !std::isspace(ch);
                 }).base(),
                 item.end());
      if (!item.empty()) {
        tags.push_back(item);
      }
    }
  }
  return tags;
}

std::string tags_yaml(const std::vector<std::string>& tags) {
  std::string out = "[";
  for (std::size_t index = 0; index < tags.size(); ++index) {
    if (index != 0) {
      out += ", ";
    }
    out += yaml_escape(tags[index]);
  }
  out += "]";
  return out;
}

Json validation_errors_for_post(const std::string& title,
                                const std::string& slug,
                                const std::string& description,
                                const std::string& date,
                                const std::string& body,
                                const std::vector<std::string>& tags) {
  Json errors = Json::array();
  if (title.size() < 6) {
    errors.push_back("title is too short");
  }
  if (!valid_slug(slug)) {
    errors.push_back("slug must use lowercase letters, numbers, and hyphens");
  }
  if (!valid_date(date)) {
    errors.push_back("date must be YYYY-MM-DD");
  }
  if (description.size() < 20) {
    errors.push_back("description is too short");
  }
  if (body.size() < 160) {
    errors.push_back("body is too short");
  }
  if (tags.empty()) {
    errors.push_back("at least one tag is required");
  }
  return errors;
}

std::string build_markdown(const std::string& title,
                           const std::string& slug,
                           const std::string& description,
                           const std::string& date,
                           const std::string& body,
                           const std::vector<std::string>& tags) {
  (void)slug;
  std::ostringstream out;
  out << "---\n";
  out << "title: " << yaml_escape(title) << "\n";
  out << "description: " << yaml_escape(description) << "\n";
  out << "date: " << date << "\n";
  out << "updated: " << date << "\n";
  out << "tags: " << tags_yaml(tags) << "\n";
  out << "---\n\n";
  out << body;
  if (!body.empty() && body.back() != '\n') {
    out << '\n';
  }
  return out.str();
}

Json generate_post_markdown(const Json& input) {
  const std::string title = input.value("title", "");
  const std::string slug = input.value("slug", "");
  const std::string description = input.value("description", "");
  const std::string date = input.value("date", today_utc());
  const std::string body = input.value("body", "");
  const auto tags = read_tags(input);

  const Json errors =
      validation_errors_for_post(title, slug, description, date, body, tags);
  const std::string markdown =
      build_markdown(title, slug, description, date, body, tags);
  const std::string filename = "_posts/" + date + "-" + slug + ".md";

  return Json{{"ok", errors.empty()},
              {"filename", filename},
              {"markdown", markdown},
              {"errors", errors}};
}

Json validate_post_markdown(const Json& input) {
  const std::string markdown = input.value("markdown", "");
  Json errors = Json::array();
  if (markdown.rfind("---\n", 0) != 0) {
    errors.push_back("missing opening front matter delimiter");
  }
  const auto close = markdown.find("\n---\n", 4);
  if (close == std::string::npos) {
    errors.push_back("missing closing front matter delimiter");
  }
  for (const std::string key :
       {"title:", "description:", "date:", "updated:", "tags:"}) {
    if (markdown.find(key) == std::string::npos) {
      errors.push_back("missing " + key);
    }
  }
  if (markdown.find("github.com/") == std::string::npos &&
      markdown.find("http://") == std::string::npos &&
      markdown.find("https://") == std::string::npos) {
    errors.push_back("post should link at least one commit, release, issue, PR, or source");
  }
  return Json{{"ok", errors.empty()}, {"errors", errors}};
}

void ensure_blog_checkout(const fs::path& repo_dir,
                          const std::string& repo_url,
                          Json& log) {
  fs::create_directories(repo_dir.parent_path());
  const fs::path marker_path = repo_dir / ".cmx-blog-mcp-worktree";
  if (!fs::exists(repo_dir / ".git")) {
    const auto clone = run_command("git clone " + shell_quote(repo_url) + " " +
                                   shell_quote(repo_dir.string()));
    log.push_back(Json{{"cmd", "git clone"}, {"code", clone.code},
                       {"output", clone.output}});
    if (clone.code != 0) {
      throw std::runtime_error("git clone failed");
    }
    std::ofstream marker(marker_path, std::ios::binary);
    marker << "managed by cmx-blog-mcp\n";
  }

  if (!fs::exists(marker_path)) {
    throw std::runtime_error(
        "refusing to modify checkout without .cmx-blog-mcp-worktree marker");
  }

  const auto remote =
      run_command("git -C " + shell_quote(repo_dir.string()) +
                  " remote get-url origin");
  log.push_back(Json{{"cmd", "git remote get-url origin"},
                     {"code", remote.code},
                     {"output", remote.output}});
  if (remote.code != 0) {
    throw std::runtime_error("git remote get-url failed");
  }
  std::string remote_url = remote.output;
  while (!remote_url.empty() &&
         (remote_url.back() == '\n' || remote_url.back() == '\r')) {
    remote_url.pop_back();
  }
  if (remote_url != repo_url) {
    throw std::runtime_error("checkout origin does not match BLOG_REPO_URL");
  }

  const auto fetch =
      run_command("git -C " + shell_quote(repo_dir.string()) +
                  " fetch origin main");
  log.push_back(Json{{"cmd", "git fetch"}, {"code", fetch.code},
                     {"output", fetch.output}});
  if (fetch.code != 0) {
    throw std::runtime_error("git fetch failed");
  }

  const auto checkout =
      run_command("git -C " + shell_quote(repo_dir.string()) + " checkout main");
  log.push_back(Json{{"cmd", "git checkout main"}, {"code", checkout.code},
                     {"output", checkout.output}});
  if (checkout.code != 0) {
    throw std::runtime_error("git checkout main failed");
  }

  const auto reset =
      run_command("git -C " + shell_quote(repo_dir.string()) +
                  " reset --hard origin/main");
  log.push_back(Json{{"cmd", "git reset"}, {"code", reset.code},
                     {"output", reset.output}});
  if (reset.code != 0) {
    throw std::runtime_error("git reset failed");
  }
}

Json create_post_pr(const Json& input) {
  Json generated = generate_post_markdown(input);
  if (!generated.value("ok", false)) {
    return generated;
  }

  const Json markdown_check =
      validate_post_markdown(Json{{"markdown", generated.value("markdown", "")}});
  if (!markdown_check.value("ok", false)) {
    return Json{{"ok", false},
                {"filename", generated.value("filename", "")},
                {"errors", markdown_check.value("errors", Json::array())}};
  }

  const std::string date = input.value("date", today_utc());
  const std::string slug = input.value("slug", "");
  const std::string title = input.value("title", "");
  const std::string markdown = generated.value("markdown", "");
  const std::string filename = generated.value("filename", "");
  const std::string repo_url = getenv_or(
      "BLOG_REPO_URL",
      "git@github.com:caomengxuan666/caomengxuan666.github.io.git");
  const std::string repo_slug = getenv_or(
      "BLOG_REPO_SLUG", "caomengxuan666/caomengxuan666.github.io");
  const fs::path workdir = getenv_or("BLOG_MCP_WORKDIR", ".cache");
  const fs::path repo_dir = workdir / "blog";
  const std::string branch =
      input.value("branch", "blog-post/" + date + "-" + slug);
  const bool dry_run = input.value("dry_run", false);
  const bool overwrite = input.value("overwrite", false);

  if (!valid_branch_name(branch)) {
    return Json{{"ok", false},
                {"filename", filename},
                {"branch", branch},
                {"error", "branch must be a safe blog-post/* branch name"}};
  }

  if (dry_run) {
    return Json{{"ok", true},
                {"dry_run", true},
                {"filename", filename},
                {"branch", branch},
                {"repo", repo_slug},
                {"markdown", markdown}};
  }

  Json log = Json::array();
  try {
    const DirectoryLock lock(workdir / "blog.lock");
    ensure_blog_checkout(repo_dir, repo_url, log);

    const auto branch_cmd =
        run_command("git -C " + shell_quote(repo_dir.string()) +
                    " checkout -B " + shell_quote(branch));
    log.push_back(Json{{"cmd", "git checkout -B"}, {"code", branch_cmd.code},
                       {"output", branch_cmd.output}});
    if (branch_cmd.code != 0) {
      throw std::runtime_error("git branch creation failed");
    }

    const fs::path output_path = repo_dir / filename;
    if (fs::exists(output_path) && !overwrite) {
      return Json{{"ok", false},
                  {"filename", filename},
                  {"branch", branch},
                  {"error", "post already exists; pass overwrite=true to update it"},
                  {"log", log}};
    }
    fs::create_directories(output_path.parent_path());
    {
      std::ofstream out(output_path, std::ios::binary);
      out << markdown;
    }

    const auto add = run_command("git -C " + shell_quote(repo_dir.string()) +
                                 " add " + shell_quote(filename));
    log.push_back(Json{{"cmd", "git add"}, {"code", add.code},
                       {"output", add.output}});
    if (add.code != 0) {
      throw std::runtime_error("git add failed");
    }

    const auto commit =
        run_command("git -C " + shell_quote(repo_dir.string()) +
                    " commit -m " +
                    shell_quote("Add blog post: " + date + "-" + slug));
    log.push_back(Json{{"cmd", "git commit"}, {"code", commit.code},
                       {"output", commit.output}});
    if (commit.code != 0) {
      throw std::runtime_error("git commit failed");
    }

    const auto push =
        run_command("git -C " + shell_quote(repo_dir.string()) +
                    " push -u origin " +
                    shell_quote("HEAD:refs/heads/" + branch) +
                    " --force-with-lease");
    log.push_back(Json{{"cmd", "git push"}, {"code", push.code},
                       {"output", push.output}});
    if (push.code != 0) {
      throw std::runtime_error("git push failed");
    }

    const std::string pr_body =
        "Generated by cmx-blog-mcp.\n\nFile: `" + filename + "`";
    const auto pr = run_command("gh pr create --repo " + shell_quote(repo_slug) +
                                " --head " + shell_quote(branch) +
                                " --base main --title " +
                                shell_quote("Add blog post: " + date + "-" + slug) +
                                " --body " + shell_quote(pr_body));
    log.push_back(Json{{"cmd", "gh pr create"}, {"code", pr.code},
                       {"output", pr.output}});
    if (pr.code != 0) {
      throw std::runtime_error("gh pr create failed");
    }

    return Json{{"ok", true},
                {"filename", filename},
                {"branch", branch},
                {"pr_output", pr.output},
                {"log", log}};
  } catch (const std::exception& error) {
    return Json{{"ok", false},
                {"filename", filename},
                {"branch", branch},
                {"error", error.what()},
                {"log", log}};
  }
}

Json describe_schema(Json schema, std::string description) {
  schema["description"] = std::move(description);
  return schema;
}

Json tags_schema() {
  return Json{{"description", "Tags as an array or comma-separated string"},
              {"oneOf",
               Json::array({mcp::protocol::JsonSchema::array(
                                mcp::protocol::JsonSchema::string()),
                            mcp::protocol::JsonSchema::string()})}};
}

Json post_input_schema(bool include_pr_options) {
  auto builder = mcp::protocol::object_schema();
  builder.required_property(
      "title",
      describe_schema(mcp::protocol::JsonSchema::string(), "Blog post title"));
  builder.required_property(
      "slug", describe_schema(mcp::protocol::JsonSchema::string(),
                               "Lowercase URL slug using letters, numbers, and hyphens"));
  builder.required_property(
      "description",
      describe_schema(mcp::protocol::JsonSchema::string(),
                      "Short front matter description"));
  builder.optional_property(
      "date", describe_schema(mcp::protocol::JsonSchema::string(),
                               "Post date in YYYY-MM-DD format"));
  builder.required_property("tags", tags_schema());
  builder.required_property(
      "body", describe_schema(mcp::protocol::JsonSchema::string(), "Markdown body"));

  if (include_pr_options) {
    builder.optional_property(
        "dry_run",
        describe_schema(mcp::protocol::JsonSchema::boolean(),
                        "Return planned file, branch, and Markdown only"));
    builder.optional_property(
        "overwrite",
        describe_schema(mcp::protocol::JsonSchema::boolean(),
                        "Allow replacing an existing post file"));
    builder.optional_property(
        "branch",
        describe_schema(mcp::protocol::JsonSchema::string(),
                        "Override the blog-post/* branch name"));
  }

  return builder.additional_properties(false).build();
}

Json markdown_validation_schema() {
  auto builder = mcp::protocol::object_schema();
  builder.required_property(
      "markdown", describe_schema(mcp::protocol::JsonSchema::string(),
                                   "Markdown document to validate"));
  return builder.additional_properties(false).build();
}

}  // namespace

int main() {
  auto builder = mcp::ServerPeer::builder();
  builder.name("cmx-blog-mcp")
      .version("0.1.0")
      .instructions("Publish posts to caomengxuan666.github.io through PRs.");

  const std::string http_port = getenv_or("BLOG_MCP_HTTP_PORT", "");
  if (!http_port.empty()) {
#if defined(CXXMCP_ENABLE_HTTP)
    const std::string auth_token = getenv_or("BLOG_MCP_AUTH_TOKEN", "");
    if (auth_token.empty()) {
      std::cerr << "BLOG_MCP_HTTP_PORT requires BLOG_MCP_AUTH_TOKEN\n";
      return 1;
    }
    std::vector<mcp::server::StaticBearerAuthProvider::Entry> auth_entries;
    auth_entries.push_back(
        {.token = auth_token,
         .identity = {.subject = "blog-publisher",
                      .claims = {{"repository", "caomengxuan666.github.io"}}}});
    builder.auth_provider(
        std::make_unique<mcp::server::StaticBearerAuthProvider>(
            std::move(auth_entries)));
    builder.streamable_http(getenv_or("BLOG_MCP_HTTP_HOST", "127.0.0.1"),
                            std::stoi(http_port),
                            getenv_or("BLOG_MCP_HTTP_PATH", "/mcp"));
#else
    std::cerr << "BLOG_MCP_HTTP_PORT requires CMX_BLOG_MCP_ENABLE_HTTP=ON\n";
    return 1;
#endif
  } else {
    builder.stdio();
  }

  return builder
      .tool(mcp::server::tool<Json, Json>("generate_post_markdown")
                .description("Generate a Jekyll blog post Markdown file.")
                .input_schema(post_input_schema(false))
                .handler([](const Json& input) {
                  return generate_post_markdown(input);
                }))
      .tool(mcp::server::tool<Json, Json>("validate_post_markdown")
                .description("Validate generated blog post Markdown.")
                .input_schema(markdown_validation_schema())
                .handler([](const Json& input) {
                  return validate_post_markdown(input);
                }))
      .tool(mcp::server::tool<Json, Json>("create_post_pr")
                .description("Create a blog post branch and pull request.")
                .input_schema(post_input_schema(true))
                .handler(
                    [](const Json& input) { return create_post_pr(input); }))
      .run();
}

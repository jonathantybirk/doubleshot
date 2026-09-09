#!/usr/bin/env python3
"""Make a reproducible source archive and formula. Does not publish anything."""
import argparse
import gzip
import hashlib
import io
import pathlib
import re
import tarfile

repo = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--local", action="store_true", help="formula uses a local archive for a local Homebrew trial")
args = parser.parse_args()
version = re.search(r'#define VERSION "([^"]+)"', (repo / "src/dshot.h").read_text())[1]
dist = repo / "dist"
dist.mkdir(exist_ok=True)
archive = dist / f"doubleshot-{version}.tar.gz"
included = [repo / p for p in ("README.md", "LICENSE", "Makefile", "CHANGELOG.md", ".gitignore")]
for directory in ("src", "tests", "scripts", "share", "docs", ".github"):
    included.extend(p for p in (repo / directory).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
raw = io.BytesIO()
with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as tar:
    for path in sorted(included):
        data = path.read_bytes()
        info = tarfile.TarInfo(f"doubleshot-{version}/{path.relative_to(repo)}")
        info.size = len(data); info.mode = 0o644; info.mtime = 0
        tar.addfile(info, io.BytesIO(data))
archive.write_bytes(gzip.compress(raw.getvalue(), mtime=0))
sha = hashlib.sha256(archive.read_bytes()).hexdigest()
url = archive.as_uri() if args.local else f"https://github.com/jonathantybirk/doubleshot/releases/download/v{version}/{archive.name}"
formula = f'''class Doubleshot < Formula
  desc "Caffeinate-style CLI for closed-lid Mac sessions with automatic cleanup"
  homepage "https://github.com/jonathantybirk/doubleshot"
  url "{url}"
  sha256 "{sha}"
  license "MIT"

  depends_on :macos
  on_macos do
    depends_on macos: :sonoma
  end

  def install
    system "make", "CC=#{{ENV.cc}}"
    bin.install "build/dshot"
    bin.install_symlink "dshot" => "doubleshot"
    man1.install "share/dshot.1"
    man1.install_symlink "dshot.1" => "doubleshot.1"
  end

  def caveats
    <<~EOS
      Run dshot to begin. First-run setup asks for administrator authentication.

      Before uninstalling this formula, remove the service with:
        dshot uninstall
    EOS
  end

  test do
    assert_equal shell_output("#{{bin}}/dshot --help"), shell_output("#{{bin}}/doubleshot --help")
    assert_match "Doubleshot {version}", shell_output("#{{bin}}/doubleshot --version")
    %w[dshot doubleshot].each do |command|
      assert_match "invalid -t value", shell_output("#{{bin}}/#{{command}} -t invalid 2>&1", 2)
    end
  end
end
'''
(dist / "doubleshot.rb").write_text(formula)
(dist / "SHA256SUMS").write_text(f"{sha}  {archive.name}\n")
print(archive)
print(dist / "doubleshot.rb")
print(sha)

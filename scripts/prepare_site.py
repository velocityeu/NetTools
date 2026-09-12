"""Prepare GitHub Pages from the static site and verified public release assets.

Uses only Python's standard library. API/verification failures stop deployment,
leaving the previously deployed site intact. No Windows application is built.
"""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'velocityeu/NetTools'
RELEASES_URL = f'https://github.com/{REPOSITORY}/releases'
EXECUTABLE = 'VelocityNetTools-x64.exe'
START = '<!-- RELEASE_DOWNLOAD_BLOCK -->'
END = '<!-- END_RELEASE_DOWNLOAD_BLOCK -->'


def api_json(path, missing_ok=False):
    headers = {'Accept': 'application/vnd.github+json',
               'X-GitHub-Api-Version': '2026-03-10',
               'User-Agent': 'Velocity-NetTools-Pages'}
    token = os.environ.get('GITHUB_TOKEN')
    if token:
        headers['Authorization'] = f'Bearer {token}'
    request = Request(f'https://api.github.com/repos/{REPOSITORY}/{path}', headers=headers)
    try:
        with urlopen(request, timeout=30) as response:
            return json.load(response)
    except HTTPError as error:
        if missing_ok and error.code == 404:
            return None
        raise


def fetch_release_state():
    # GitHub's explicitly selected latest stable release also supports a deliberate
    # maintainer rollback. Preview selection uses SemVer, not publication time.
    latest = api_json('releases/latest', missing_ok=True)
    releases, page = [], 1
    while True:
        batch = api_json(f'releases?per_page=100&page={page}')
        if not isinstance(batch, list):
            raise ValueError('GitHub returned an invalid release list')
        releases.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    return {'latest': latest, 'releases': releases}


def executable_asset(release):
    if not isinstance(release, dict) or release.get('draft') is not False:
        return None
    if not release.get('published_at'):
        return None
    assets = [asset for asset in release.get('assets', [])
              if asset.get('name') == EXECUTABLE]
    if len(assets) > 1:
        raise ValueError('Release contains duplicate executable assets')
    return assets[0] if assets else None


def verify_release(release):
    asset = executable_asset(release)
    if asset is None:
        return None
    tag = release.get('tag_name')
    if not isinstance(tag, str) or not tag or any(ord(char) < 32 for char in tag):
        raise ValueError('Release has an invalid version tag')
    if release.get('immutable') is not True:
        raise ValueError(f'{tag}: enable release immutability before linking an executable')
    version_path = quote(tag, safe='')
    url = f'{RELEASES_URL}/download/{version_path}/{EXECUTABLE}'
    notes = f'{RELEASES_URL}/tag/{version_path}'
    if asset.get('browser_download_url') != url or release.get('html_url') != notes:
        raise ValueError(f'{tag}: asset or release URL is not the expected repository/version')
    digest = asset.get('digest', '')
    size = asset.get('size')
    if (asset.get('state') != 'uploaded' or not isinstance(digest, str)
            or not re.fullmatch(r'sha256:[0-9a-f]{64}', digest)
            or type(size) is not int or size <= 0):
        raise ValueError(f'{tag}: executable is incomplete or lacks a SHA-256 digest')
    # Do not forward the API token to public asset URLs or their CDN redirects.
    sha, received = hashlib.sha256(), 0
    with urlopen(Request(url, headers={'User-Agent': 'Velocity-NetTools-Pages'}), timeout=60) as response:
        while chunk := response.read(1024 * 1024):
            received += len(chunk)
            if received > size:
                raise ValueError(f'{tag}: executable exceeds its recorded size')
            sha.update(chunk)
    if received != size or sha.hexdigest() != digest.removeprefix('sha256:'):
        raise ValueError(f'{tag}: downloaded executable does not match GitHub metadata')
    return {'version': tag, 'url': url, 'notes': notes, 'size': size,
            'sha256': sha.hexdigest(), 'published_at': release['published_at']}


def preview_precedence(tag):
    """Return SemVer 2.0 prerelease precedence; ignore build metadata.

    The repository permits an optional v tag prefix. Executable previews must
    include a prerelease suffix so GitHub's channel and the version agree.
    """
    match = re.fullmatch(
        r'v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)'
        r'-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)'
        r'(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?',
        tag) if isinstance(tag, str) else None
    if match is None:
        raise ValueError(f'{tag!r}: executable preview needs a valid SemVer prerelease tag')
    identifiers = []
    for identifier in match[4].split('.'):
        if identifier.isdigit():
            if len(identifier) > 1 and identifier.startswith('0'):
                raise ValueError(f'{tag!r}: numeric SemVer prerelease identifiers cannot have leading zeroes')
            identifiers.append((0, int(identifier)))
        else:
            identifiers.append((1, identifier))
    return tuple(int(match[index]) for index in (1, 2, 3)) + (tuple(identifiers),)


def select_downloads(state):
    if not isinstance(state, dict) or not isinstance(state.get('releases'), list):
        raise ValueError('Release state needs a releases list and latest stable release')
    latest = state.get('latest')
    if latest is not None and not isinstance(latest, dict):
        raise ValueError('Latest stable metadata must be a release object')
    stable = None
    if executable_asset(latest) is not None:
        if latest.get('prerelease') is not False:
            raise ValueError('Latest stable metadata must not identify a preview')
        tag = latest.get('tag_name')
        if not isinstance(tag, str) or not re.fullmatch(
                r'v?(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)'
                r'(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?', tag):
            raise ValueError(f'{tag!r}: executable stable release needs a valid SemVer tag without prerelease identifiers')
        # Validate the designated latest release without replacing an intentional
        # rollback with the numerically highest stable version in the release list.
        stable = verify_release(latest)
    previews = [release for release in state['releases']
                if isinstance(release, dict) and release.get('prerelease') is True
                and executable_asset(release) is not None]
    by_precedence = {}
    for preview in previews:
        precedence = preview_precedence(preview.get('tag_name'))
        existing = by_precedence.get(precedence)
        if existing is not None and existing['tag_name'] != preview['tag_name']:
            raise ValueError('Ambiguous preview SemVer precedence: '
                             f'{existing["tag_name"]!r} and {preview["tag_name"]!r}')
        by_precedence[precedence] = preview
    newest = by_precedence[max(by_precedence)] if by_precedence else None
    return {'stable': stable, 'preview': verify_release(newest) if newest else None}


def release_card(download, preview=False):
    escape = html.escape
    title = 'Preview' if preview else 'Stable release'
    label = 'Download preview for Windows x64' if preview else 'Download for Windows x64'
    button = 'secondary' if preview else 'primary'
    return (
        f'<div class="release-card"><h3>{title} {escape(download["version"])}</h3>'
        f'<p>Windows x64 · {download["size"]:,} bytes</p>'
        f'<a class="button {button}" href="{escape(download["url"], quote=True)}">{label}</a> '
        f'<a class="text-link" href="{escape(download["notes"], quote=True)}">Release notes</a>'
        f'<details><summary>Verify SHA-256</summary><code class="release-checksum">'
        f'{download["sha256"]}</code></details></div>'
    )


def replace_class_content(document, tag, class_name, content):
    pattern = (rf'(<{tag}\b[^>]*\bclass="[^"]*\b{class_name}\b[^"]*"[^>]*>)'
               rf'.*?(</{tag}>)')
    updated, count = re.subn(pattern, lambda match: match[1] + content + match[2],
                             document, flags=re.DOTALL)
    if count != 1:
        raise ValueError(f'Expected exactly one {class_name} status element')
    return updated


def render_site(document, downloads):
    if document.count(START) != 1 or document.count(END) != 1:
        raise ValueError('Site must contain one matching release download marker pair')
    before, remaining = document.split(START)
    fallback, after = remaining.split(END)
    stable, preview = downloads['stable'], downloads['preview']
    if not stable and not preview:
        return document  # Keep the current no-release copy exactly as authored.
    status = 'Verified Windows x64 builds are available on GitHub Releases.' if stable else 'Preview available; no stable release yet.'
    cards = (release_card(stable) if stable else '') + (release_card(preview, True) if preview else '')
    block = ('<section id="download" class="download-strip"><div class="wrap download-strip-inner">'
             f'<div><h2>Download Velocity NetTools</h2><p>{status}</p></div>'
             f'<div class="release-downloads">{cards}<a class="text-link" href="{RELEASES_URL}">'
             'All versions on GitHub</a></div></div></section>')
    document = before + START + block + END + after
    document = replace_class_content(document, 'div', 'preview-note',
                                      'Learning centre <span>Windows builds are available on GitHub.</span>')
    version = html.escape((stable or preview)['version'])
    platform = f'Windows x64 · {"Stable release" if stable else "Preview"} {version}'
    document = replace_class_content(document, 'p', 'platform', platform)
    # Navigation remains an in-page link; only its label follows the channel state.
    label = 'Download for Windows' if stable else 'Download preview'
    document = document.replace('>Download status</a>', f'>{label}</a>')
    panel_pattern = r'(<div\b[^>]*class="download-panel"[^>]*>.*?)<p>.*?</p>'
    document, count = re.subn(panel_pattern, lambda match: match[1] + f'<p>{status}</p>',
                              document, count=1, flags=re.DOTALL)
    if count != 1:
        raise ValueError('Expected a download-panel status paragraph')
    return document


def build_site(source, output, state):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output.exists() or output == source or source in output.parents or output in source.parents:
        raise ValueError('Output must be a new directory outside the static source tree')
    if not source.is_dir() or any(path.is_symlink() for path in source.rglob('*')):
        raise ValueError('Static source must be a directory without symbolic links')
    downloads = select_downloads(state)
    document = render_site((source / 'index.html').read_text(encoding='utf-8'), downloads)
    shutil.copytree(source, output)
    (output / 'index.html').write_text(document, encoding='utf-8')
    (output / 'release-data.json').write_text(json.dumps(downloads, indent=2) + '\n', encoding='utf-8')
    (output / '.nojekyll').touch()
    return downloads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'website-preview/dist')
    parser.add_argument('--output', type=Path, default=ROOT / '_site')
    parser.add_argument('--releases-json', type=Path,
                        help='Saved {latest, releases} API response for local verification')
    args = parser.parse_args()
    try:
        state = (json.loads(args.releases_json.read_text(encoding='utf-8'))
                 if args.releases_json else fetch_release_state())
        downloads = build_site(args.source, args.output, state)
    except (OSError, ValueError, KeyError, TypeError, URLError) as error:
        raise SystemExit(f'Site preparation failed; existing deployment is unchanged: {error}') from error
    channels = ', '.join(channel for channel, release in downloads.items() if release)
    print(f'Prepared static site: {channels or "no executable releases; design copy preserved"}.')


if __name__ == '__main__':
    main()

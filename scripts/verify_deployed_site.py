"""Fail deployment if GitHub Pages keeps serving metadata from an older artifact."""
import argparse
import hashlib
import re
import time
from urllib.error import URLError
from urllib.request import Request, urlopen

SITE_URL = "https://velocityeu.github.io/NetTools/"
MAX_METADATA_BYTES = 1024 * 1024


def fetch_metadata(url, timeout):
    request = Request(url, headers={"User-Agent": "Velocity-NetTools-Pages-Verification",
                                  "Cache-Control": "no-cache", "Pragma": "no-cache"})
    with urlopen(request, timeout=timeout) as response:
        data = response.read(MAX_METADATA_BYTES + 1)
    if len(data) > MAX_METADATA_BYTES:
        raise ValueError("Deployed metadata exceeds its size limit")
    return data


def verify_deployment(url, expected, timeout=600, interval=15,
                      fetch=fetch_metadata, now=time.monotonic, wait=time.sleep, asset="release-data.json"):
    if url != SITE_URL:
        raise ValueError("Deployment URL must be the configured NetTools HTTPS site")
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("Expected metadata digest must be SHA-256")
    if not (0 < timeout <= 600 and 0 < interval <= 60):
        raise ValueError("Verification timeout or polling interval is outside its bounds")
    if asset not in ("release-data.json", "app/sw.js"):
        raise ValueError("Unsupported deployment artifact")
    deadline = now() + timeout
    attempts = 0
    detail = "No response"
    while now() < deadline:
        attempts += 1
        request_url = url + asset + "?verify=" + expected + "-" + str(attempts)
        try:
            data = fetch(request_url, min(30, max(0.1, deadline - now())))
            actual = hashlib.sha256(data).hexdigest()
            if actual == expected:
                print(f"Verified public {asset} SHA-256 {expected} after {attempts} request(s).")
                return
            detail = "The site still serves a different release metadata digest: " + actual
        except (URLError, TimeoutError, OSError) as error:
            detail = "Metadata request failed: " + str(error)
        remaining = deadline - now()
        if remaining <= 0:
            break
        wait(min(interval, remaining))
    raise RuntimeError(f"Public site verification failed after {attempts} request(s). {detail}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--interval", type=float, default=15)
    parser.add_argument("--asset", choices=["release-data.json", "app/sw.js"], default="release-data.json")
    args = parser.parse_args()
    verify_deployment(args.url, args.sha256, args.timeout, args.interval, asset=args.asset)

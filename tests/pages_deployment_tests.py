"""Exercise stale-site detection, propagation retries and bounded network fetching."""
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import sys
import threading
import unittest
from urllib.error import URLError

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from verify_deployed_site import SITE_URL, fetch_metadata, verify_deployment


class DeploymentTests(unittest.TestCase):
    def run_responses(self, responses, expected=b"current", timeout=30):
        clock = [0]
        requests = []
        def now():
            return clock[0]
        def wait(seconds):
            clock[0] += seconds
        def fetch(url, request_timeout):
            requests.append((url, request_timeout))
            result = responses[min(len(requests)-1, len(responses)-1)]
            if isinstance(result, Exception):
                raise result
            return result
        digest = hashlib.sha256(expected).hexdigest()
        verify_deployment(SITE_URL, digest, timeout, 5, fetch, now, wait)
        return requests, clock[0]

    def test_pwa_artifact_is_verified_independently(self):
        requested=[]
        def fetch(url, timeout):
            requested.append(url)
            return b'worker'
        verify_deployment(SITE_URL, hashlib.sha256(b'worker').hexdigest(), fetch=fetch, asset='app/sw.js')
        self.assertTrue(requested[0].startswith(SITE_URL+'app/sw.js?verify='))
        with self.assertRaises(ValueError):
            verify_deployment(SITE_URL, '0'*64, fetch=fetch, asset='../other')

    def test_current_artifact_passes_without_wait(self):
        requests, elapsed = self.run_responses([b"current"])
        self.assertEqual((len(requests), elapsed), (1, 0))

    def test_stale_artifact_retries_until_exact_bytes_match(self):
        requests, elapsed = self.run_responses([b"old", b"old", b"current"])
        self.assertEqual((len(requests), elapsed), (3, 10))
        self.assertEqual(len(set(url for url, _ in requests)), 3)
        self.assertTrue(all(url.startswith(SITE_URL + "release-data.json?verify=") for url, _ in requests))

    def test_transient_fetch_error_recovers(self):
        requests, _ = self.run_responses([URLError("temporary"), b"current"])
        self.assertEqual(len(requests), 2)

    def test_false_green_old_deployment_fails_at_deadline(self):
        with self.assertRaisesRegex(RuntimeError, "after 3 request"):
            self.run_responses([b"old"], timeout=12)

    def test_untrusted_url_and_invalid_digest_rejected_before_fetch(self):
        def forbidden(*args):
            self.fail("must not fetch untrusted URL or invalid metadata")
        for url, digest in [("https://example.invalid/", "0"*64), (SITE_URL, "bad")]:
            with self.assertRaises(ValueError):
                verify_deployment(url, digest, fetch=forbidden)

    def test_http_fetch_preserves_cache_bypass_headers_and_bytes(self):
        observed = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                observed.append((self.path, self.headers.get("Cache-Control"), self.headers.get("Pragma")))
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"exact bytes\n")
            def log_message(self, *args):
                pass
        server = HTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            result = fetch_metadata(f"http://127.0.0.1:{server.server_port}/release-data.json?verify=fixture", 2)
            self.assertEqual(result, b"exact bytes\n")
            self.assertEqual(observed, [("/release-data.json?verify=fixture", "no-cache", "no-cache")])
        finally:
            server.shutdown()
            worker.join()
            server.server_close()


if __name__ == "__main__":
    unittest.main()

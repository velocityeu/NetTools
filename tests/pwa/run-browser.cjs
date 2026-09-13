// Reproducible real-browser integration runner. npm install --prefix out/pwa-test playwright@1.63.0
const http = require("node:http"),
  fs = require("node:fs"),
  path = require("node:path"),
  assert = require("node:assert/strict");
const {
  chromium,
  webkit,
} = require("../../out/pwa-test/node_modules/playwright");
const check = require("./browser-check.cjs");
let version = 0,
  serverUnavailable = false;
const root = path.resolve(__dirname, "../../website-preview/dist");
const server = http.createServer((req, res) => {
  if (serverUnavailable) {
    req.socket.destroy();
    return;
  }
  const url = new URL(req.url, "http://localhost");
  let name = decodeURIComponent(url.pathname);
  if (name.endsWith("/")) name += "index.html";
  const file = path.resolve(root, "." + name);
  if (!file.startsWith(root + path.sep)) {
    res.writeHead(403).end();
    return;
  }
  try {
    let body = fs.readFileSync(file);
    if (name.endsWith("/sw.js") && version)
      body = Buffer.from(
        body
          .toString()
          .replace(
            /const VERSION = "([^"]+)";/,
            `const VERSION = "$1-test${version}";`,
          ),
      );
    res.setHeader(
      "Content-Type",
      {
        ".html": "text/html",
        ".mjs": "text/javascript",
        ".js": "text/javascript",
        ".wasm": "application/wasm",
        ".css": "text/css",
        ".json": "application/json",
        ".webmanifest": "application/manifest+json",
        ".png": "image/png",
      }[path.extname(file)] || "application/octet-stream",
    );
    res.setHeader("Cache-Control", "no-store");
    res.end(body);
  } catch {
    res.writeHead(404).end();
  }
});
async function publicChecks(browser, base) {
  const context = await browser.newContext({ serviceWorkers: "block" }),
    page = await context.newPage();
  let apiCalls = 0;
  await context.route(/https:\/\/api6?\.ipify\.org/, (route) => {
    apiCalls++;
    return route.fulfill({
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify({
        ip: route.request().url().includes("api6.")
          ? "2001:db8::42"
          : "203.0.113.42",
      }),
    });
  });
  try {
    await page.goto(base);
    assert.equal(apiCalls, 0, "no IP disclosure before choice");
    await page.locator("[data-view=public]").click();
    await page.locator("#ip-check").click();
    await page
      .locator("#ip-ipv4")
      .filter({ hasText: "203.0.113.42" })
      .waitFor();
    await page
      .locator("#ip-ipv6")
      .filter({ hasText: "2001:db8::42" })
      .waitFor();
    assert.equal(apiCalls, 2);
    await page.locator("#ip-auto").check();
    await page.reload();
    await page.waitForFunction(() =>
      document.querySelector("#ip-ipv4").textContent.includes("203.0.113.42"),
    );
    assert.equal(apiCalls, 4, "opted-in launch checks both families");
    await page.locator("[data-view=public]").click();
    await page.locator("#ip-auto").uncheck();
    return ["manual and opted-in public IP"];
  } finally {
    await context.close();
  }
}
async function integration(browser, base, browserName) {
  const useServerOutage =
    process.platform === "win32" && browserName === "WebKit";
  const context = await browser.newContext(),
    page = await context.newPage();
  try {
    await page.goto(base);
    await page.evaluate(() => navigator.serviceWorker.ready);
    await page.reload();
    await page.waitForFunction(
      () => navigator.serviceWorker.controller !== null,
    );
    if (useServerOutage) serverUnavailable = true;
    else await context.setOffline(true);
    await page.reload();
    await page.locator("#calc-example").click();
    await page.locator("#calc-form button[type=submit]").click();
    await page
      .locator("#calc-results")
      .filter({ hasText: "192.168.10.0/26" })
      .waitFor();
    await page.locator("[data-view=learn]").click();
    await page
      .locator("#cache-status")
      .filter({ hasText: "Offline ready" })
      .waitFor();
    assert(
      (await page.locator("#help-topics").innerText()).includes(
        "Subnetting by hand",
      ),
    );
    serverUnavailable = false;
    await context.setOffline(false);
    const clean = await context.newPage();
    await clean.goto(base);
    version++;
    await clean.evaluate(async () => {
      const r = await navigator.serviceWorker.getRegistration();
      await r.update();
    });
    await clean.waitForFunction(
      async () => !!(await navigator.serviceWorker.getRegistration())?.waiting,
    );
    await clean.waitForTimeout(3600);
    assert.equal(
      await page.locator("#address").inputValue(),
      "192.168.10.42",
      "dirty tab survives another tab update",
    );
    assert.equal(
      await page.evaluate(() => navigator.serviceWorker.controller.scriptURL),
      base + "sw.js",
    );
    await page.locator("[data-view=calculate]").click();
    await page.locator("#calc-clear").click();
    await clean.locator("#update-now").click();
    await clean.waitForFunction(() =>
      document.querySelector("#version").textContent.includes("test"),
    );
    await page.waitForFunction(() =>
      document.querySelector("#version").textContent.includes("test"),
    );
    return [
      "manual and opted-in public IP",
      "offline reload + WASM + help",
      "dirty second-tab update protection",
      "automatic coordinated update",
    ];
  } catch (e) {
    console.error(
      "Browser integration failed; state omitted to keep observations private.",
    );
    throw e;
  } finally {
    serverUnavailable = false;
    await context.close();
  }
}
(async () => {
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const base = `http://127.0.0.1:${server.address().port}/app/`;
  try {
    for (const [name, type] of [
      ["Chromium", chromium],
      ["WebKit", webkit],
    ]) {
      const browser = await type.launch({ headless: true });
      try {
        const a = await check(browser, base);
        const p = await publicChecks(browser, base);
        const b = await integration(browser, base, name);
        console.log(
          name +
            ": " +
            (a.length + b.length + p.length) +
            " browser checks passed",
        );
      } finally {
        await browser.close();
      }
    }
  } finally {
    server.close();
  }
})().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});

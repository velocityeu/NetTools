// Responsive desktop regression checks; real arithmetic and UI, no source-text assertions.
module.exports = async function (browser, base) {
  const context = await browser.newContext({ serviceWorkers: "block" }),
    page = await context.newPage(),
    checks = [];
  const verify = (ok, label) => {
    if (!ok) throw Error(label);
    checks.push(label);
  };
  try {
    await page.setViewportSize({ width: 1280, height: 609 });
    await page.goto(base);
    await page.locator("#calc-example").click();
    await page.locator("#calc-form button[type=submit]").click();
    await page
      .locator("#calc-results")
      .filter({ hasText: "192.168.10.0/26" })
      .waitFor();
    const size = await page.evaluate(() => ({
      font: parseFloat(getComputedStyle(document.querySelector("h1")).fontSize),
      actions: document.querySelector("#calc-actions").getBoundingClientRect()
        .bottom,
    }));
    verify(size.font <= 26, "desktop heading is compact");
    verify(size.actions <= 609, "main calculator actions fit this PC viewport");
    await page.locator("[data-view=plan]").click();
    await page.locator("[data-mode=vlsm]").click();
    await page.locator("#vlsm-example").click();
    await page
      .locator("#prompt-actions")
      .getByRole("button", { name: "Discard", exact: true })
      .click();
    await page.locator("#allocate").click();
    await page
      .locator("#plan-rows")
      .filter({ hasText: "192.168.10.96/31" })
      .waitFor();
    const planScroll = await page.locator("main").evaluate((e) => e.scrollTop);
    await page.locator("[data-view=learn]").click();
    await page.locator("main").evaluate((e) => (e.scrollTop = e.scrollHeight));
    await page.locator("[data-view=plan]").click();
    verify(
      Math.abs(
        (await page.locator("main").evaluate((e) => e.scrollTop)) - planScroll,
      ) < 2,
      "navigation restores each workspace scroll position",
    );
    await page.locator("[data-view=calculate]").click();
    await page.locator("[data-family=ipv6]").click();
    await page
      .locator("#address")
      .fill("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128");
    await page.locator("#calc-form button[type=submit]").click();
    await page
      .locator("#calc-results")
      .filter({ hasText: "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128" })
      .waitFor();
    for (const [width, height] of [
      [390, 844],
      [650, 700],
      [768, 640],
      [960, 600],
      [1024, 600],
      [1280, 609],
      [1366, 650],
      [1440, 760],
      [1920, 900],
      [2560, 1200],
      [800, 480],
      [1280, 400],
    ]) {
      await page.setViewportSize({ width, height });
      for (const view of ["calculate", "plan", "public", "learn"]) {
        await page.locator(`[data-view=${view}]`).click();
        const dims = await page.evaluate(() => ({
          overflow: document.documentElement.scrollWidth > innerWidth,
          mainOverflow:
            document.querySelector("main").scrollWidth >
            document.querySelector("main").clientWidth,
        }));
        verify(
          !dims.overflow && !dims.mainOverflow,
          `${view} fits ${width}x${height}`,
        );
      }
      if (width > 650) {
        const before = await page.locator(".topbar").boundingBox();
        await page
          .locator("main")
          .evaluate((e) => (e.scrollTop = e.scrollHeight));
        await page.evaluate(() => window.scrollTo(0, 1000));
        const after = await page.locator(".topbar").boundingBox(),
          nav = await page.locator(".sidebar").boundingBox();
        verify(
          Math.abs(before.y - after.y) < 1 &&
            Math.abs(nav.y - after.y - after.height) < 2,
          `header and navigation remain attached at ${width}px`,
        );
        await page.locator("[data-view=calculate]").click();
        verify(
          await page.locator("#view-calculate").isVisible(),
          "navigation still switches after scrolling",
        );
      }
    }
    await page.setViewportSize({ width: 390, height: 844 });
    verify(
      (await page
        .locator("#address")
        .evaluate((e) => getComputedStyle(e).fontSize)) === "16px",
      "phone input size retained",
    );
    return checks;
  } finally {
    await context.close();
  }
};

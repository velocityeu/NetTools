// Browser integration assertions. Supply a Playwright browser and a served app URL.
module.exports = async function (browser, base) {
  const context = await browser.newContext({ serviceWorkers: "block" }),
    page = await context.newPage(),
    checks = [];
  const assert = (value, label) => {
    if (!value) throw Error(label);
    checks.push(label);
  };
  const waitText = async (selector, value) =>
    page.locator(selector).filter({ hasText: value }).waitFor();
  try {
    await page.goto(base);
    await page.locator("#calc-example").click();
    await page.locator("#calc-form button[type=submit]").click();
    await waitText("#calc-results", "192.168.10.0/26");
    assert(
      (await page.locator("#calc-results").innerText()).includes("62"),
      "IPv4 result endpoints",
    );
    await page.locator("#address").fill("192.168.10.43");
    assert(
      await page.locator("#calc-actions").isHidden(),
      "edited calculation disables stale actions",
    );
    await page.locator("[data-family=ipv6]").click();
    await page.locator("#address").fill("::/0");
    await page.locator("#calc-form button[type=submit]").click();
    await waitText(
      "#calc-results",
      "340,282,366,920,938,463,463,374,607,431,768,211,456",
    );
    checks.push("IPv6 /0 exact count");
    await page.locator("[data-view=plan]").click();
    await page.locator("#split-example").click();
    await page.locator("#split-run").click();
    await waitText("#plan-rows", "192.168.10.192/26");
    assert(
      await page.locator("#split-next").isDisabled(),
      "equal split boundary disables next",
    );
    await page.locator("[data-mode=vlsm]").click();
    await page.locator("#vlsm-example").click();
    await page.getByRole("button", { name: "Discard", exact: true }).click();
    await page.locator("#allocate").click();
    await waitText("#plan-rows", "192.168.10.96/31");
    assert(
      await page.locator("#plan-save").isDisabled(),
      "preview cannot save as applied",
    );
    await page.locator("#apply").click();
    assert(
      await page.locator("#apply").isHidden(),
      "allocation explicitly applied",
    );
    await page.locator("#plan-save").click();
    await page.locator("#prompt-name").fill("Browser test plan");
    await page
      .locator("#prompt-actions")
      .getByRole("button", { name: "Save", exact: true })
      .click();
    await waitText("#plan-state", "Saved on this device");
    checks.push("named plan explicit save");
    await page.locator("#saved-side").click();
    await page
      .locator("#library-list")
      .getByRole("button", { name: "Open", exact: true })
      .click();
    await waitText("#plan-output-title", "Applied allocation");
    checks.push("saved plan reopens and recomputes");
    await page.evaluate(() => {
      const key = "veu.nettools.plans.v1",
        data = JSON.parse(localStorage.getItem(key));
      data.plans[0].requirements[0].sequence = "9007199254740992";
      localStorage.setItem(key, JSON.stringify(data));
    });
    await page.locator("#saved-side").click();
    await page
      .locator("#library-list")
      .getByRole("button", { name: "Open", exact: true })
      .click();
    await waitText("#plan-output-title", "Applied allocation");
    await page.locator("#add-request").click();
    const req = page.locator("#requirements .requirement").last();
    await req.locator("input").nth(0).fill("Extra");
    await req.locator("input").nth(1).fill("1");
    await page.locator("#allocate").click();
    await waitText("#plan-rows", "Extra");
    assert(
      !(await page.locator("#plan-error").innerText()),
      "large insertion sequence remains exact",
    );
    await page.locator("[data-view=learn]").click();
    await page.locator("#help-search").fill("five steps");
    await page.locator("#help-topics summary").first().click();
    assert(
      (await page.locator("#help-topics").innerText()).includes(
        "192.168.10.42/26",
      ),
      "canonical offline lesson renders",
    );
    await page.locator("#help-search").fill("");
    assert(
      (await page.locator("#help-topics li").count()) > 0,
      "canonical list items remain semantic",
    );
    assert(
      (await page.locator('#help-topics a[href^="https://"]').count()) > 0,
      "online resource links retain destinations",
    );
    for (const width of [390, 768, 1440]) {
      await page.setViewportSize({ width, height: 844 });
      await page.locator("[data-view=calculate]").click();
      assert(
        await page.evaluate(
          () => document.documentElement.scrollWidth <= innerWidth,
        ),
        `no horizontal overflow at ${width}px`,
      );
    }
    return checks;
  } finally {
    await context.close();
  }
};

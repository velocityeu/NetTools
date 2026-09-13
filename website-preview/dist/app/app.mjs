import { EngineClient } from "./engine-client.mjs";
import {
  loadPlans,
  savePlan,
  deletePlan,
  importPlan,
  validatePlan,
} from "./plans.mjs";
import {
  PublicIpController,
  getLaunchPreference,
  setLaunchPreference,
} from "./public-ip.mjs";
const $ = (id) => document.getElementById(id),
  engine = new EngineClient();
const clone = (x) => structuredClone(x),
  uuid = () => crypto.randomUUID(),
  count = (x) => BigInt(x).toLocaleString("en-US");
let calcFamily = "ipv4",
  calcResult = null,
  calcRevision = 0,
  calcDirty = false;
const drafts = {
  ipv4: { address: "", prefix: "" },
  ipv6: { address: "", prefix: "" },
};
let mode = "split",
  requirements = [],
  reservations = [],
  proposal = null,
  applied = null,
  planRevision = 0,
  planDirty = false,
  planId = null,
  planName = "",
  sequence = 0n,
  splitResult = null,
  removed = null,
  jobs = 0;
let updateLocked = false,
  unlockTimer;
let registration = null,
  updateDismissed = false,
  updateReload = false,
  lastUpdateCheck = 0;
function text(tag, value, cls = "") {
  const n = document.createElement(tag);
  n.textContent = value;
  if (cls) n.className = cls;
  return n;
}
function notice(message) {
  $("notice").textContent = message;
}
function button(label, handler, cls = "") {
  const b = text("button", label, cls);
  b.type = "button";
  b.onclick = () => Promise.resolve(handler()).catch((e) => notice(e.message));
  return b;
}
function row(label, value) {
  if (value === undefined || value === "") return;
  const n = text("div", "", "result-row");
  n.append(text("span", label), text("span", value, "mono"));
  return n;
}
function rows(target, values) {
  target.replaceChildren(...values.map(([a, b]) => row(a, b)).filter(Boolean));
}
async function run(request) {
  if (updateLocked) throw Error("Updating app. Please wait.");
  jobs++;
  try {
    return await engine.run(request);
  } finally {
    jobs--;
  }
}
let currentView = "calculate";
const desktopScroll = new Map();
function showView(name) {
  const desktop = matchMedia("(min-width:651px)").matches;
  if (desktop) desktopScroll.set(currentView, $("main").scrollTop);
  document
    .querySelectorAll("main>section")
    .forEach((s) => (s.hidden = s.id !== `view-${name}`));
  document.querySelectorAll("[data-view]").forEach((b) => {
    if (b.dataset.view === name) b.setAttribute("aria-current", "page");
    else b.removeAttribute("aria-current");
  });
  currentView = name;
  if (desktop) $("main").scrollTop = desktopScroll.get(name) || 0;
}
document
  .querySelectorAll("[data-view]")
  .forEach((b) => (b.onclick = () => showView(b.dataset.view)));
function ask(title, message, choices, name = null) {
  return new Promise((resolve) => {
    const d = $("prompt-dialog");
    $("prompt-title").textContent = title;
    $("prompt-text").textContent = message;
    $("prompt-name").hidden = $("prompt-label").hidden = name === null;
    $("prompt-name").value = name ?? "";
    $("prompt-actions").replaceChildren(
      ...choices.map(([value, label]) => {
        const b = button(
          label,
          () => {
            d.returnValue = value;
            d.close();
          },
          value === "save" ? "primary" : "",
        );
        return b;
      }),
    );
    d.onclose = () =>
      resolve({ action: d.returnValue, name: $("prompt-name").value.trim() });
    d.returnValue = "cancel";
    d.showModal();
  });
}
async function copy(value) {
  try {
    await navigator.clipboard.writeText(value);
    notice("Copied.");
  } catch {
    await ask(
      "Copy unavailable",
      "Select and copy the value directly from the result.",
      [["cancel", "Close"]],
    );
  }
}
function calcEdited() {
  calcRevision++;
  calcDirty = true;
  calcResult = null;
  $("calc-state").textContent = "Edited · Calculate again";
  $("calc-results").classList.add("stale");
  $("calc-actions").hidden = true;
  $("maths").hidden = true;
  drafts[calcFamily] = {
    address: $("address").value,
    prefix: $("prefix").value,
  };
}
function family(value, restore = true) {
  if (restore)
    drafts[calcFamily] = {
      address: $("address").value,
      prefix: $("prefix").value,
    };
  calcFamily = value;
  document
    .querySelectorAll("[data-family]")
    .forEach((b) =>
      b.setAttribute("aria-pressed", String(b.dataset.family === value)),
    );
  $("mask-details").hidden = value === "ipv6";
  $("mask").value = "";
  $("prefix-range").textContent =
    value === "ipv4" ? "0–32 network bits" : "0–128 network bits";
  $("address").placeholder =
    value === "ipv4" ? "192.168.10.42/26" : "2001:db8::1/64";
  if (restore) {
    $("address").value = drafts[value].address;
    $("prefix").value = drafts[value].prefix;
    calcEdited();
  }
}
document
  .querySelectorAll("[data-family]")
  .forEach((b) => (b.onclick = () => family(b.dataset.family)));
$("address").oninput = $("prefix").oninput = () => {
  calcEdited();
  $("mask").value = "";
};
$("mask").oninput = () => calcEdited();
async function normalizeInput() {
  const raw = $("address").value.trim();
  if (!raw) return;
  const revision = calcRevision;
  try {
    const r = await run({
      op: "calculate",
      address: raw,
      prefix: $("mask").value || $("prefix").value,
    });
    if (revision !== calcRevision) return;
    if (raw.includes("/")) notice("Prefix taken from CIDR.");
    if (r.family !== calcFamily)
      notice(
        `Selected ${r.family === "ipv4" ? "IPv4" : "IPv6"} to match the address.`,
      );
    family(r.family, false);
    $("address").value = r.entered;
    $("prefix").value = r.prefix;
    drafts[calcFamily] = { address: r.entered, prefix: r.prefix };
  } catch {
    /* A partial edit is validated on submission. */
  }
}
$("address").onblur = normalizeInput;
$("address").onpaste = () => setTimeout(normalizeInput, 0);
$("mask").onblur = async () => {
  if (!$("mask").value) return;
  try {
    const revision = calcRevision;
    const r = await run({
      op: "calculate",
      address: $("address").value || "0.0.0.0",
      prefix: $("mask").value,
    });
    if (revision === calcRevision) {
      $("prefix").value = r.prefix;
      $("mask").value = r.mask;
    }
  } catch (e) {
    $("calc-error").textContent = e.message;
  }
};
async function calculate() {
  const revision = calcRevision;
  $("calc-error").textContent = "";
  try {
    const r = await run({
      op: "calculate",
      address: $("address").value,
      prefix: $("mask").value || $("prefix").value,
    });
    if (revision !== calcRevision) return;
    calcResult = r;
    notice("");
    family(r.family, false);
    $("address").value = r.entered;
    $("prefix").value = r.prefix;
    drafts[calcFamily] = { address: r.entered, prefix: r.prefix };
    $("calc-state").textContent = "Calculated";
    $("calc-results").classList.remove("stale");
    const hero = text("div", r.network, "network-value"),
      detail = text("div", "");
    rows(detail, [
      ["Total addresses", count(r.total)],
      ["Subnet mask", r.mask],
      ["First host / endpoint", r.firstHost],
      ["Last host / endpoint", r.lastHost],
      ["Broadcast", r.broadcast],
      ["Last address", r.broadcast ? "" : r.last],
      ["Conventional capacity", r.capacity ? count(r.capacity) : ""],
    ]);
    const extra = document.createElement("details");
    extra.className = "calc-secondary";
    extra.open = matchMedia("(max-width:650px)").matches;
    const extraRows = text("div", "");
    rows(extraRows, [
      ["Wildcard", r.wildcard],
      ["Last address", r.broadcast ? r.last : ""],
      ["Host offset", count(r.offset)],
    ]);
    extra.append(
      text("summary", "More details & address guidance"),
      extraRows,
      text("p", r.note),
      text("p", r.classification, "hint"),
    );
    $("calc-results").replaceChildren(hero, detail, extra);
    $("calc-actions").hidden = false;
    if (matchMedia("(max-width:650px)").matches) {
      document.activeElement?.blur();
      $("calc-results")
        .closest(".card")
        .scrollIntoView({ block: "start", behavior: "auto" });
    }
    $("maths").hidden = false;
    const bits = (r.family === "ipv4" ? 32 : 128) - Number(r.prefix);
    $("maths-body").replaceChildren(
      text(
        "p",
        `${r.family === "ipv4" ? 32 : 128} address bits − ${r.prefix} network bits = ${bits} host bits. 2^${bits} = ${count(r.total)} total addresses.`,
      ),
      text(
        "p",
        `Keep the network bits and set the host bits to zero to get ${r.network}. Set those host bits to one to get the last address, ${r.last}.`,
      ),
      text(
        "p",
        r.family === "ipv6"
          ? "IPv6 has no broadcast. This count is an address-space size, not a promise that every address is deployable."
          : r.note,
      ),
    );
  } catch (e) {
    if (revision === calcRevision) {
      $("calc-error").textContent = e.message;
      $("calc-state").textContent = "Check input";
    }
  }
}
$("calc-form").onsubmit = (e) => {
  e.preventDefault();
  calculate();
};
$("calc-example").onclick = () => {
  family(calcFamily, false);
  $("address").value = calcFamily === "ipv4" ? "192.168.10.42" : "2001:db8::1";
  $("prefix").value = calcFamily === "ipv4" ? "26" : "64";
  calcEdited();
  notice("Example loaded. Choose Calculate.");
};
$("calc-clear").onclick = () => {
  $("address").value = $("prefix").value = $("mask").value = "";
  calcEdited();
  calcDirty = false;
  $("calc-results").replaceChildren(
    text("p", "Enter an address and prefix.", "empty"),
  );
  $("calc-state").textContent = "Ready for input";
  $("calc-error").textContent = "";
};
function calculationText() {
  return calcResult
    ? Object.entries(calcResult)
        .filter(([, v]) => v !== "")
        .map(([k, v]) => `${k}: ${v}`)
        .join("\n")
    : "";
}
$("calc-copy").onclick = () => copy(calculationText());
$("calc-share").onclick = async () => {
  if (!calcResult) return;
  try {
    if (navigator.share)
      await navigator.share({
        title: "Velocity NetTools subnet",
        text: calculationText(),
      });
    else await copy(calculationText());
  } catch (e) {
    if (e.name !== "AbortError")
      notice("Sharing unavailable. Use Copy result.");
  }
};
function editPlan() {
  planRevision++;
  planDirty = true;
  proposal = null;
  splitResult = null;
  $("plan-output").hidden = true;
  $("plan-state").textContent = `${planName || "New plan"} · Unsaved changes`;
  $("plan-error").textContent = "";
}
function setMode(value) {
  mode = value;
  document
    .querySelectorAll("[data-mode]")
    .forEach((b) =>
      b.setAttribute("aria-pressed", String(b.dataset.mode === value)),
    );
  $("split-editor").hidden = value !== "split";
  $("vlsm-editor").hidden = value !== "vlsm";
}
document.querySelectorAll("[data-mode]").forEach(
  (b) =>
    (b.onclick = () => {
      setMode(b.dataset.mode);
      editPlan();
    }),
);
$("parent").oninput = $("child").oninput = editPlan;
function field(label, value, handler, options = null) {
  const box = text("div", ""),
    id = "f-" + uuid(),
    lab = text("label", label);
  lab.htmlFor = id;
  const input = document.createElement(options ? "select" : "input");
  input.id = id;
  if (options)
    options.forEach(([v, l]) => {
      const o = text("option", l);
      o.value = v;
      input.append(o);
    });
  input.value = value;
  if (!options) {
    input.maxLength = label === "Name" ? 200 : 100;
    input.autocomplete = "off";
    input.spellcheck = false;
    input.setAttribute("autocapitalize", "none");
  }
  input[options ? "onchange" : "oninput"] = () => handler(input.value);
  box.append(lab, input);
  return box;
}
function renderRequirements() {
  const nodes = requirements.map((r) => {
    const card = text("article", "", "requirement"),
      grid = text("div", "", "requirement-fields");
    grid.append(
      field("Name", r.name, (v) => {
        r.name = v;
        editPlan();
      }),
      field(
        "Role",
        r.kind,
        (v) => {
          r.kind = v;
          if (v === "ipv6") r.quantity = "64";
          editPlan();
          renderRequirements();
        },
        [
          ["lan", "LAN hosts"],
          ["ptp", "Point-to-point"],
          ["host", "Host route"],
          ["ipv6", "IPv6 prefix"],
        ],
      ),
      field(
        r.kind === "ipv6" ? "Prefix" : "Hosts / endpoints",
        r.quantity,
        (v) => {
          r.quantity = v;
          editPlan();
        },
      ),
    );
    const advanced = text("div", "", "advanced-row");
    advanced.append(
      field("Assigned CIDR (optional)", r.assigned, (v) => {
        r.assigned = v;
        editPlan();
      }),
    );
    const label = text("label", "", "inline-label"),
      check = document.createElement("input");
    check.type = "checkbox";
    check.checked = r.pinned;
    check.onchange = () => {
      r.pinned = check.checked;
      editPlan();
    };
    label.append(check, text("span", "Pinned"));
    advanced.append(
      label,
      button("Remove", () => {
        removed = { row: clone(r), index: requirements.indexOf(r) };
        requirements = requirements.filter((x) => x.id !== r.id);
        $("undo-delete").hidden = false;
        editPlan();
        renderRequirements();
      }),
    );
    card.append(grid, advanced);
    return card;
  });
  $("requirements").replaceChildren(...nodes);
  if (!nodes.length)
    $("requirements").append(
      text("p", "Add the networks you need, or load an example.", "empty"),
    );
}
function renderReservations() {
  $("reservations").replaceChildren(
    ...reservations.map((r) => {
      const card = text("div", "", "reservation");
      card.append(
        field("Reservation name", r.name, (v) => {
          r.name = v;
          editPlan();
        }),
        field("Reserved CIDR", r.cidr, (v) => {
          r.cidr = v;
          editPlan();
        }),
        button("Remove", () => {
          reservations = reservations.filter((x) => x.id !== r.id);
          editPlan();
          renderReservations();
        }),
      );
      return card;
    }),
  );
}
$("add-request").onclick = () => {
  if (sequence > 18446744073709551615n)
    return notice("Insertion order limit reached. Start a new plan.");
  if (requirements.length >= 500)
    return notice("A plan supports up to 500 requirements.");
  requirements.push({
    id: uuid(),
    name: "",
    sequence: String(sequence++),
    kind: $("parent").value.includes(":") ? "ipv6" : "lan",
    quantity: $("parent").value.includes(":") ? "64" : "",
    assigned: "",
    pinned: false,
  });
  editPlan();
  renderRequirements();
};
$("add-reservation").onclick = () => {
  if (reservations.length >= 500)
    return notice("A plan supports up to 500 reservations.");
  reservations.push({ id: uuid(), name: "", cidr: "" });
  editPlan();
  renderReservations();
};
$("undo-delete").onclick = () => {
  if (removed) {
    requirements.splice(removed.index, 0, removed.row);
    removed = null;
    editPlan();
    renderRequirements();
    $("undo-delete").hidden = true;
  }
};
async function guardPlan() {
  if (!planDirty) return true;
  const answer = await ask(
    "Unsaved plan",
    "Save this draft before replacing it?",
    [
      ["save", "Save draft"],
      ["discard", "Discard"],
      ["cancel", "Cancel"],
    ],
  );
  if (answer.action === "save") return await saveCurrent("draft");
  return answer.action === "discard";
}
function resetPlan() {
  requirements = [];
  reservations = [];
  proposal = applied = splitResult = null;
  planId = null;
  planName = "";
  sequence = 0n;
  removed = null;
  $("undo-delete").hidden = true;
  $("parent").value = $("child").value = "";
  editPlan();
  planDirty = false;
  $("plan-state").textContent = "New plan · Session only";
  renderRequirements();
  renderReservations();
}
$("plan-new").onclick = async () => {
  if (await guardPlan()) resetPlan();
};
$("split-example").onclick = async () => {
  if (!(await guardPlan())) return;
  resetPlan();
  setMode("split");
  $("parent").value = "192.168.10.0/24";
  $("child").value = "26";
  editPlan();
};
$("vlsm-example").onclick = async () => {
  if (!(await guardPlan())) return;
  resetPlan();
  setMode("vlsm");
  $("parent").value = "192.168.10.0/24";
  requirements = [
    ["Office", "lan", "50"],
    ["Voice", "lan", "25"],
    ["Link", "ptp", "2"],
  ].map(([name, kind, quantity], i) => ({
    id: uuid(),
    name,
    kind,
    quantity,
    sequence: String(i),
    assigned: "",
    pinned: false,
  }));
  sequence = 3n;
  renderRequirements();
  editPlan();
};
async function split(index = "0") {
  const revision = planRevision;
  $("plan-error").textContent = "";
  try {
    const r = await run({
      op: "split",
      parent: $("parent").value,
      child: $("child").value,
      index,
    });
    if (revision !== planRevision) return;
    splitResult = r;
    $("plan-output").hidden = false;
    $("plan-output-title").textContent = "Equal subnets";
    $("plan-summary").replaceChildren(
      text("p", `${count(r.count)} subnets · Parent ${r.parent}`),
    );
    $("plan-rows").replaceChildren(
      ...r.rows.map((cidr, i) => {
        const n = text("div", "", "allocation-row");
        n.append(
          text(
            "span",
            `Subnet ${(BigInt(r.index) + BigInt(i) + 1n).toString()} (one-based)`,
          ),
          text("span", cidr, "mono"),
        );
        return n;
      }),
    );
    $("split-index").value = r.index;
    $("split-pages").hidden = false;
    $("apply").hidden = true;
    $("plan-save").disabled = false;
    $("plan-copy").disabled = false;
    $("split-first").disabled = $("split-prev").disabled = r.index === "0";
    $("split-next").disabled = $("split-last").disabled =
      BigInt(r.index) + BigInt(r.rows.length) >= BigInt(r.count);
  } catch (e) {
    $("plan-error").textContent = e.message;
  }
}
$("split-run").onclick = () => split();
$("split-first").onclick = () => split();
$("split-prev").onclick = () =>
  split(
    (BigInt(splitResult.index) > 50n
      ? BigInt(splitResult.index) - 50n
      : 0n
    ).toString(),
  );
$("split-next").onclick = () =>
  split((BigInt(splitResult.index) + 50n).toString());
$("split-last").onclick = () =>
  split((((BigInt(splitResult.count) - 1n) / 50n) * 50n).toString());
$("split-jump").onclick = () => split($("split-index").value);
async function allocate(reallocate = false) {
  const revision = planRevision;
  proposal = null;
  $("plan-output").hidden = true;
  $("plan-error").textContent = "";
  $("cancel-work").hidden = false;
  try {
    if (!requirements.length) throw new Error("Add at least one requirement.");
    const r = await run({
      op: "allocate",
      parent: $("parent").value,
      requirements,
      reservations,
      reallocate,
    });
    if (revision !== planRevision) return;
    proposal = r;
    renderAllocation(r, false);
  } catch (e) {
    if (revision === planRevision) $("plan-error").textContent = e.message;
  } finally {
    $("cancel-work").hidden = true;
  }
}
function renderAllocation(r, isApplied) {
  $("plan-output").hidden = false;
  $("plan-output-title").textContent = isApplied
    ? "Applied allocation"
    : "Review allocation preview";
  $("split-pages").hidden = true;
  const stats = text("div", "");
  rows(stats, [
    ["Allocated addresses", count(r.allocated)],
    ["Reserved addresses", count(r.reserved)],
    ["Unallocated addresses", count(r.free)],
    ["Largest free block", r.largestFree || "None"],
  ]);
  const free = document.createElement("details");
  free.append(
    text("summary", "Free blocks"),
    text("p", r.freeBlocks.join(", ") || "None", "mono"),
  );
  $("plan-summary").replaceChildren(stats, free);
  $("plan-rows").replaceChildren(
    ...r.allocations.map((a) => {
      const req = requirements.find((x) => x.id === a.id),
        n = text("div", "", "allocation-row");
      const label = text("div", req?.name || "Unnamed requirement");
      label.append(
        text(
          "small",
          `${req?.quantity} requested · ${req?.kind === "ipv6" ? "prefix" : req?.kind} · ${a.pinned ? "Pinned" : a.preserved ? "Preserved" : "Proposed"}`,
        ),
      );
      const value = text("div", a.cidr, "mono");
      if (req?.assigned && req.assigned !== a.cidr)
        value.append(text("small", `Was ${req.assigned}`));
      value.append(text("small", `${count(a.addresses)} addresses`));
      n.append(label, value);
      return n;
    }),
    ...r.unallocated.map((a) =>
      text(
        "p",
        `${requirements.find((x) => x.id === a.id)?.name || "Requirement"}: ${a.reason}`,
        "error",
      ),
    ),
  );
  $("apply").hidden = isApplied;
  $("apply").disabled = r.partial || r.unallocated.length > 0;
  $("plan-save").disabled = !isApplied;
  $("plan-copy").disabled = false;
  if (r.partial)
    notice(
      "Not all requirements fit. Apply is disabled; adjust requirements or parent space.",
    );
}
$("allocate").onclick = () => allocate();
$("reallocate").onclick = () => allocate(true);
$("cancel-work").onclick = () => {
  planRevision++;
  engine.cancel();
  $("cancel-work").hidden = true;
  notice("Calculation cancelled. Applied assignments are unchanged.");
};
$("apply").onclick = () => {
  if (!proposal || proposal.partial || proposal.unallocated.length) return;
  requirements = requirements.map((r) => ({
    ...r,
    assigned:
      proposal.allocations.find((a) => a.id === r.id)?.cidr || r.assigned,
  }));
  applied = clone(proposal);
  proposal = null;
  planDirty = true;
  renderRequirements();
  renderAllocation(applied, true);
  $("plan-state").textContent =
    `${planName || "New plan"} · Applied · Not saved`;
};
$("plan-copy").onclick = () =>
  copy(
    mode === "split"
      ? splitResult?.rows.join("\n") || ""
      : $("plan-output-title").textContent +
          "\n" +
          $("plan-summary").innerText +
          "\n" +
          $("plan-rows").innerText,
  );
function planData(state) {
  return {
    version: 1,
    id: planId || uuid(),
    name: planName || "Untitled plan",
    type: mode,
    family: $("parent").value.includes(":") ? "ipv6" : "ipv4",
    input: {
      parent: $("parent").value,
      child: mode === "split" ? $("child").value : "",
      index: mode === "split" ? splitResult?.index || "0" : "0",
    },
    requirements: clone(requirements),
    reservations: clone(reservations),
    state,
    updatedAt: new Date().toISOString(),
  };
}
async function saveCurrent(state = "applied") {
  try {
    if (
      state === "applied" &&
      ((mode === "vlsm" && (!applied || proposal)) ||
        (mode === "split" && !splitResult))
    )
      throw new Error(
        "Calculate and apply the current plan first, or choose Save draft.",
      );
    const answer = await ask(
      "Save on this device",
      "Export important plans from Saved plans to keep a backup.",
      [
        ["save", "Save"],
        ["cancel", "Cancel"],
      ],
      planName,
    );
    if (answer.action !== "save") return false;
    if (!answer.name) throw new Error("Enter a plan name.");
    const data = planData(state);
    data.name = answer.name;
    const saved = savePlan(data);
    planId = saved.id;
    planName = saved.name;
    planDirty = false;
    $("plan-state").textContent =
      `${planName} · ${state === "draft" ? "Draft" : "Applied"} · Saved on this device`;
    notice("Plan saved on this device.");
    return true;
  } catch (e) {
    notice(e.message);
    return false;
  }
}
$("plan-save").onclick = () => saveCurrent();
$("save-draft").onclick = () => saveCurrent("draft");
$("calc-save").onclick = async () => {
  if (!calcResult) return;
  try {
    const answer = await ask(
      "Save calculation",
      "Give this result a name.",
      [
        ["save", "Save"],
        ["cancel", "Cancel"],
      ],
      calcResult.network,
    );
    if (answer.action !== "save") return;
    savePlan({
      version: 1,
      id: uuid(),
      name: answer.name,
      type: "calculate",
      family: calcResult.family,
      input: { address: calcResult.entered, prefix: calcResult.prefix },
      requirements: [],
      reservations: [],
      state: "applied",
      updatedAt: new Date().toISOString(),
    });
    calcDirty = false;
    notice("Calculation saved.");
  } catch (e) {
    notice(e.message);
  }
};
function exportPlan(p) {
  const blob = new Blob([JSON.stringify(p, null, 2)], {
      type: "application/json",
    }),
    url = URL.createObjectURL(blob),
    a = document.createElement("a");
  a.href = url;
  a.download = "velocity-nettools-plan.json";
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
async function validateSemantics(p) {
  if (p.state === "draft") return;
  if (p.type !== "calculate") {
    const parent = await run({ op: "calculate", address: p.input.parent });
    if (parent.family !== p.family)
      throw new Error("Plan family does not match parent network.");
  }
  if (p.type === "calculate") {
    const r = await run({ op: "calculate", ...p.input });
    if (r.family !== p.family)
      throw new Error("Plan family does not match address.");
  } else if (p.type === "split") {
    await run({ op: "split", ...p.input });
  } else {
    if (!p.requirements.length || p.requirements.some((r) => !r.assigned))
      throw new Error("Applied plans must contain assigned requirements.");
    const r = await run({
      op: "allocate",
      parent: p.input.parent,
      requirements: p.requirements,
      reservations: p.reservations,
    });
    if (
      r.partial ||
      r.allocations.some(
        (a) => p.requirements.find((q) => q.id === a.id)?.assigned !== a.cidr,
      )
    )
      throw new Error(
        "Applied assignments do not match requirements. Import as a valid PWA backup.",
      );
  }
}
async function openPlan(p) {
  await validateSemantics(p);
  if (p.type === "calculate") {
    if (calcDirty) {
      const a = await ask(
        "Unsaved calculation",
        "Replace the current calculation?",
        [
          ["discard", "Replace"],
          ["cancel", "Cancel"],
        ],
      );
      if (a.action !== "discard") return;
    }
    family(p.family, false);
    $("address").value = p.input.address || "";
    $("prefix").value = p.input.prefix || "";
    calcEdited();
    showView("calculate");
    $("library").close();
    const revision = calcRevision;
    if (p.state === "applied") await calculate();
    if (revision === calcRevision) calcDirty = false;
  } else {
    if (!(await guardPlan())) return;
    resetPlan();
    setMode(p.type);
    $("parent").value = p.input.parent || "";
    $("child").value = p.input.child || "";
    requirements = clone(p.requirements);
    reservations = clone(p.reservations);
    sequence = requirements.reduce(
      (n, r) => (BigInt(r.sequence) >= n ? BigInt(r.sequence) + 1n : n),
      0n,
    );
    planId = p.id;
    planName = p.name;
    planDirty = false;
    renderRequirements();
    renderReservations();
    $("plan-state").textContent =
      `${p.name} · ${p.state} · Saved on this device`;
    showView("plan");
    $("library").close();
    if (p.state === "applied") {
      if (p.type === "split") await split(p.input.index || "0");
      else {
        const revision = planRevision;
        const result = await run({
          op: "allocate",
          parent: p.input.parent,
          requirements: clone(requirements),
          reservations: clone(reservations),
        });
        if (revision === planRevision) {
          applied = result;
          renderAllocation(applied, true);
        }
      }
    }
  }
}
function library() {
  try {
    $("library-error").textContent = "";
    $("library-list").replaceChildren(
      ...loadPlans().map((p) => {
        const n = text("article", "");
        n.append(
          text("h3", p.name),
          text(
            "p",
            `${p.type} · ${p.family} · ${p.state} · ${new Date(p.updatedAt).toLocaleString()}`,
          ),
        );
        const actions = text("div", "", "actions");
        actions.append(
          button("Open", () => openPlan(p)),
          button("Export", () => exportPlan(p)),
          button("Delete", async () => {
            const a = await ask(
              "Delete saved plan?",
              `Remove “${p.name}” from this device?`,
              [
                ["delete", "Delete"],
                ["cancel", "Cancel"],
              ],
            );
            if (a.action === "delete") {
              deletePlan(p.id);
              library();
            }
          }),
        );
        n.append(actions);
        return n;
      }),
    );
    if (!$("library-list").childElementCount)
      $("library-list").append(text("p", "No saved plans yet."));
  } catch (e) {
    $("library-error").textContent = e.message;
  }
  if (!$("library").open) $("library").showModal();
}
$("saved-top").onclick = $("saved-side").onclick = library;
$("library-close").onclick = () => $("library").close();
$("import-file").onchange = async () => {
  try {
    const f = $("import-file").files[0];
    if (!f) return;
    if (f.size > 1048576) throw new Error("PWA backup exceeds 1 MiB.");
    const p = importPlan(await f.text());
    await validateSemantics(p);
    if (loadPlans().some((x) => x.id === p.id)) {
      const a = await ask(
        "Replace saved plan?",
        `A plan with this ID exists. Replace it with “${p.name}”?`,
        [
          ["replace", "Replace"],
          ["cancel", "Cancel"],
        ],
      );
      if (a.action !== "replace") return;
    }
    savePlan(p);
    library();
    notice("PWA backup imported. Open it from Saved plans.");
  } catch (e) {
    $("library-error").textContent = e.message;
  } finally {
    $("import-file").value = "";
  }
};
const publicIp = new PublicIpController({
  validateAddress: async (address, family) =>
    (await run({ op: "address", address, family })).address,
  onChange: renderIp,
});
function renderIp(states) {
  for (const family of ["ipv4", "ipv6"]) {
    const s = states[family],
      box = $("ip-" + family),
      nodes = [];
    nodes.push(
      text(
        "div",
        s.address || s.previous?.address || "Not checked",
        "network-value",
      ),
    );
    nodes.push(
      text(
        "p",
        s.status === "verified"
          ? "Verified observation"
          : s.status === "checking"
            ? "Checking…"
            : s.status === "failed"
              ? "Could not verify"
              : "Ready to check",
      ),
    );
    if (s.checkedAt)
      nodes.push(
        text("p", `Checked ${new Date(s.checkedAt).toLocaleString()}`, "hint"),
      );
    if (s.previous)
      nodes.push(
        text(
          "p",
          `Previous observation · ${new Date(s.previous.checkedAt).toLocaleString()}`,
          "hint",
        ),
      );
    if (s.error) nodes.push(text("p", s.error, "error"));
    if (s.address)
      nodes.push(
        button("Copy " + (family === "ipv4" ? "IPv4" : "IPv6"), () =>
          copy(s.address),
        ),
      );
    nodes.push(
      text(
        "p",
        family === "ipv4"
          ? "Provider: api.ipify.org"
          : "Provider: api6.ipify.org",
        "hint",
      ),
    );
    box.replaceChildren(...nodes);
  }
  const checking = Object.values(states).some((s) => s.status === "checking");
  $("ip-check").disabled = checking;
  $("ip-check").textContent = "Recheck both addresses";
  $("ip-cancel").hidden = !checking;
}
renderIp(publicIp.states);
$("ip-check").onclick = () => publicIp.check();
$("ip-cancel").onclick = () => publicIp.cancel();
$("ip-auto").onchange = () => {
  try {
    setLaunchPreference($("ip-auto").checked);
    notice(
      $("ip-auto").checked
        ? "Launch checks enabled."
        : "Automatic public-IP checks disabled.",
    );
  } catch (e) {
    $("ip-auto").checked = false;
    notice("Preference was not remembered: " + e.message);
  }
};
try {
  $("ip-auto").checked = getLaunchPreference();
  if ($("ip-auto").checked) publicIp.check();
} catch {
  notice(
    "Settings cannot be read in this browser. Manual public-IP checks remain available.",
  );
}
function helpNode(b) {
  if (typeof b === "string") return document.createTextNode(b);
  const mapping = {
    paragraph: "p",
    heading: "h3",
    "inline-code": "code",
    code: "pre",
    "code-block": "pre",
    strong: "strong",
    emphasis: "em",
    list: "ul",
    "ordered-list": "ol",
    "list-item": "li",
    item: "li",
    span: "span",
    group: "div",
    table: "table",
    "table-row": "tr",
    "table-cell": "td",
    "table-header": "th",
    link: "a",
  };
  const node = document.createElement(
    b.variant === "calculation" ? "pre" : mapping[b.type] || "div",
  );
  if (b.type === "link") {
    const url = b.href || b.url || "";
    if (/^https:\/\//.test(url)) node.href = url;
  }
  for (const c of b.children || []) node.append(helpNode(c));
  return node;
}
let topics = [];
function renderHelp() {
  const query = $("help-search").value.toLowerCase();
  $("help-topics").replaceChildren(
    ...topics
      .filter((t) => (t.title + " " + t.keywords).toLowerCase().includes(query))
      .map((t) => {
        const d = document.createElement("details");
        d.className = "lesson";
        d.append(text("summary", t.title), ...t.blocks.map(helpNode));
        return d;
      }),
  );
}
fetch("./help.json")
  .then((r) => {
    if (!r.ok) throw Error();
    return r.json();
  })
  .then((data) => {
    topics = data.topics;
    renderHelp();
  })
  .catch(() =>
    $("help-topics").append(
      text(
        "p",
        "Offline lessons are unavailable. Connect once to cache the complete app.",
      ),
    ),
  );
$("help-search").oninput = renderHelp;
window.addEventListener("beforeunload", (e) => {
  if ((calcDirty || planDirty) && !updateReload) {
    e.preventDefault();
    e.returnValue = "";
  }
});
async function cacheStatus() {
  const worker = registration?.active;
  if (!worker) return;
  const channel = new MessageChannel();
  channel.port1.onmessage = ({ data }) => {
    $("cache-status").textContent = data.ready
      ? "Offline ready · Calculations and bundled lessons are cached."
      : "Offline cache incomplete. Connect and check for updates.";
    $("version").textContent = `Web preview 0.1.0 · ${data.version || ""}`;
    channel.port1.close();
  };
  worker.postMessage({ type: "STATUS" }, [channel.port2]);
}
function activeWork() {
  return (
    calcDirty ||
    planDirty ||
    jobs > 0 ||
    Object.values(publicIp.states).some((s) => s.status === "checking") ||
    document.querySelector("dialog[open]")
  );
}
function lockUpdate(locked) {
  updateLocked = locked;
  for (const selector of ["main", ".sidebar", ".topbar"])
    document.querySelector(selector).inert = locked;
  clearTimeout(unlockTimer);
  if (locked) {
    notice("Installing update…");
    unlockTimer = setTimeout(() => lockUpdate(false), 6000);
  }
}
navigator.serviceWorker?.addEventListener("message", (event) => {
  if (event.data?.type === "UPDATE_QUERY") {
    const ready = !activeWork();
    if (ready) lockUpdate(true);
    event.source.postMessage({
      type: "UPDATE_VOTE",
      nonce: event.data.nonce,
      ready,
    });
  } else if (event.data?.type === "UPDATE_BLOCKED") {
    updateReload = false;
    lockUpdate(false);
    $("update-banner").hidden = false;
    notice(
      "Update downloaded. Finish or save work in all NetTools tabs, then choose Update now.",
    );
  }
});
function updateReady() {
  if (!registration?.waiting || !navigator.serviceWorker.controller) return;
  if (!activeWork()) {
    registration.waiting.postMessage({ type: "ACTIVATE" });
  } else if (!updateDismissed) $("update-banner").hidden = false;
}
async function checkUpdates(manual = false) {
  if (!registration) {
    if (manual)
      notice(
        "Updates require a secure supported browser and an internet connection.",
      );
    return;
  }
  const now = Date.now();
  if (!manual && now - lastUpdateCheck < 15 * 60 * 1000) return;
  lastUpdateCheck = now;
  try {
    await registration.update();
    updateReady();
    await cacheStatus();
    if (manual)
      notice(
        registration.waiting
          ? "A new version is ready."
          : "Update check completed.",
      );
  } catch {
    if (manual)
      notice(
        "Could not check for updates. The cached version is still available.",
      );
  }
}
$("update-later").onclick = () => {
  updateDismissed = true;
  $("update-banner").hidden = true;
};
$("update-now").onclick = async () => {
  if (!registration?.waiting) return;
  if (
    jobs ||
    Object.values(publicIp.states).some((s) => s.status === "checking")
  )
    return notice("Wait for active checks to finish before updating.");
  if (calcDirty || planDirty) {
    const a = await ask(
      "Update available",
      "Save calculations and plans before updating, or explicitly discard unsaved work.",
      [
        ["discard", "Discard & update"],
        ["cancel", "Keep working"],
      ],
    );
    if (a.action !== "discard") return;
    calcDirty = false;
    planDirty = false;
  }
  registration.waiting.postMessage({ type: "ACTIVATE" });
};
$("check-updates").onclick = () => checkUpdates(true);
if ("serviceWorker" in navigator) {
  let hadController = !!navigator.serviceWorker.controller;
  navigator.serviceWorker.addEventListener("controllerchange", () => {
    if (hadController) {
      if (updateLocked || !activeWork()) {
        updateReload = true;
        location.reload();
      } else {
        lockUpdate(false);
        notice("Update activated. Save your work before reloading this tab.");
      }
    } else {
      hadController = true;
      lockUpdate(false);
      cacheStatus();
    }
  });
  navigator.serviceWorker
    .register("./sw.js", { scope: "./", updateViaCache: "none" })
    .then((r) => {
      registration = r;
      r.addEventListener("updatefound", () => {
        const worker = r.installing;
        worker?.addEventListener("statechange", () => {
          if (worker.state === "installed") {
            cacheStatus();
            updateReady();
          }
        });
      });
      navigator.serviceWorker.ready.then(cacheStatus);
      checkUpdates();
      updateReady();
    })
    .catch(
      () =>
        ($("cache-status").textContent =
          "Offline installation failed. Connect and reload to retry."),
    );
} else
  $("cache-status").textContent =
    "Offline installation is not supported in this browser.";
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    checkUpdates();
    if (Object.values(publicIp.states).some((s) => s.address))
      notice(
        "Connection may have changed while away. Use Recheck for a fresh public-IP observation.",
      );
  }
});
setInterval(
  () => {
    if (document.visibilityState === "visible") checkUpdates();
  },
  15 * 60 * 1000,
);
renderRequirements();
renderReservations();

if (location.hash === "#learn") showView("learn");

$("repair-cache").onclick = async () => {
  const worker = registration?.active;
  if (!worker)
    return notice("No offline installation exists yet. Connect and reload.");
  const channel = new MessageChannel();
  $("repair-cache").disabled = true;
  const timer = setTimeout(() => {
    $("repair-cache").disabled = false;
    notice("Cache repair timed out. Check your connection and retry.");
    channel.port1.close();
  }, 30000);
  channel.port1.onmessage = ({ data }) => {
    clearTimeout(timer);
    $("repair-cache").disabled = false;
    notice(
      data.ready
        ? "Offline cache repaired."
        : data.error || "Cache repair failed. Check for updates.",
    );
    cacheStatus();
    channel.port1.close();
  };
  worker.postMessage({ type: "REPAIR" }, [channel.port2]);
};

// Keep supplemental results expanded in the approved phone layout.
matchMedia("(max-width:650px)").addEventListener("change", (event) => {
  const details = document.querySelector(".calc-secondary");
  if (details) details.open = event.matches;
});

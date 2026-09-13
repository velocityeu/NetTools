// VEU — explicit, versioned device plan storage. Arithmetic validation belongs to the core.
const KEY = "veu.nettools.plans.v1";
const MAX_TEXT = 1048576;
const MAX_ROWS = 500;
const MAX_PLANS = 100;
const fail = (message) => {
  throw new Error(message);
};
function object(value, keys, label) {
  if (
    !value ||
    typeof value !== "object" ||
    Array.isArray(value) ||
    ![Object.prototype, null].includes(Object.getPrototypeOf(value))
  )
    fail(`Invalid ${label}`);
  for (const key of Object.keys(value))
    if (!keys.includes(key)) fail(`Unknown ${label} field: ${key}`);
}
function string(value, limit, label, allowEmpty = true) {
  if (
    typeof value !== "string" ||
    value.length > limit ||
    (!allowEmpty && !value.trim()) ||
    /[\u0000-\u001f\u007f]/u.test(value)
  )
    fail(`Invalid ${label}`);
  return value;
}
function decimal(value, label, max = null, allowEmpty = true) {
  string(value, 39, label);
  if (allowEmpty && value === "") return value;
  if (
    !/^(0|[1-9][0-9]*)$/.test(value) ||
    BigInt(value) > (max ?? (1n << 128n) - 1n)
  )
    fail(`Invalid ${label}`);
  return value;
}
function rows(value, label, validate) {
  if (!Array.isArray(value) || value.length > MAX_ROWS)
    fail(`Invalid ${label} count`);
  const ids = new Set();
  return value.map((row) => {
    const result = validate(row);
    if (ids.has(result.id)) fail(`Duplicate ${label} ID`);
    ids.add(result.id);
    return result;
  });
}
export function validatePlan(plan) {
  object(
    plan,
    [
      "version",
      "id",
      "name",
      "type",
      "family",
      "input",
      "requirements",
      "reservations",
      "state",
      "updatedAt",
    ],
    "plan",
  );
  if (plan.version !== 1) fail("Unsupported PWA plan version");
  if (
    !["calculate", "split", "vlsm"].includes(plan.type) ||
    !["ipv4", "ipv6"].includes(plan.family) ||
    !["draft", "applied"].includes(plan.state)
  )
    fail("Invalid plan type, family or state");
  string(plan.id, 100, "plan ID", false);
  string(plan.name, 200, "plan name", false);
  string(plan.updatedAt, 40, "update time", false);
  if (
    !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/.test(plan.updatedAt) ||
    !Number.isFinite(Date.parse(plan.updatedAt))
  )
    fail("Invalid update time");
  object(
    plan.input,
    ["address", "prefix", "parent", "child", "index"],
    "input",
  );
  const input = {};
  for (const [key, value] of Object.entries(plan.input))
    input[key] = ["prefix", "child"].includes(key)
      ? decimal(value, key, plan.family === "ipv4" ? 32n : 128n)
      : key === "index"
        ? decimal(value, key)
        : string(value, 100, key);
  const requirements = rows(
    Object.hasOwn(plan, "requirements") ? plan.requirements : [],
    "requirements",
    (r) => {
      object(
        r,
        ["id", "name", "sequence", "kind", "quantity", "assigned", "pinned"],
        "requirement",
      );
      if (
        !["lan", "ptp", "host", "ipv6"].includes(r.kind) ||
        typeof r.pinned !== "boolean"
      )
        fail("Invalid requirement role or pin");
      return {
        id: string(r.id, 100, "requirement ID", false),
        name: string(r.name, 200, "requirement name"),
        sequence: decimal(r.sequence, "sequence", 18446744073709551615n, false),
        kind: r.kind,
        quantity: decimal(r.quantity, "quantity"),
        assigned: string(r.assigned, 100, "assigned CIDR"),
        pinned: r.pinned,
      };
    },
  );
  if (new Set(requirements.map((r) => r.sequence)).size !== requirements.length)
    fail("Duplicate requirement sequence");
  const reservations = rows(
    Object.hasOwn(plan, "reservations") ? plan.reservations : [],
    "reservations",
    (r) => {
      object(r, ["id", "name", "cidr"], "reservation");
      return {
        id: string(r.id, 100, "reservation ID", false),
        name: string(r.name, 200, "reservation name"),
        cidr: string(r.cidr, 100, "reservation CIDR"),
      };
    },
  );
  return {
    version: 1,
    id: plan.id,
    name: plan.name,
    type: plan.type,
    family: plan.family,
    input,
    requirements,
    reservations,
    state: plan.state,
    updatedAt: plan.updatedAt,
  };
}
export function importPlan(text) {
  if (
    typeof text !== "string" ||
    new TextEncoder().encode(text).byteLength > MAX_TEXT
  )
    fail("PWA plan backup exceeds size limit");
  return validatePlan(JSON.parse(text));
}
export function loadPlans(storage = globalThis.localStorage) {
  const raw = storage.getItem(KEY);
  if (raw === null) return [];
  if (raw.length > MAX_TEXT * 4) fail("Saved plans exceed size limit");
  const envelope = JSON.parse(raw);
  object(envelope, ["version", "plans"], "library");
  if (
    envelope.version !== 1 ||
    !Array.isArray(envelope.plans) ||
    envelope.plans.length > MAX_PLANS
  )
    fail("Unsupported or invalid plan library");
  const result = envelope.plans.map(validatePlan);
  if (new Set(result.map((p) => p.id)).size !== result.length)
    fail("Duplicate saved plan ID");
  return result;
}
function write(plans, storage) {
  if (plans.length > MAX_PLANS) fail("Saved plan limit reached");
  const text = JSON.stringify({ version: 1, plans });
  if (text.length > MAX_TEXT * 4) fail("Saved plans exceed size limit");
  // A single setItem is atomic: quota/access failures retain the previous envelope.
  storage.setItem(KEY, text);
}
export function savePlan(plan, storage = globalThis.localStorage) {
  const saved = validatePlan(plan),
    all = loadPlans(storage),
    index = all.findIndex((p) => p.id === saved.id);
  if (index < 0) all.push(saved);
  else all[index] = saved;
  write(all, storage);
  return saved;
}
export function deletePlan(id, storage = globalThis.localStorage) {
  string(id, 100, "plan ID", false);
  write(
    loadPlans(storage).filter((p) => p.id !== id),
    storage,
  );
}

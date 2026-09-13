// VEU — direct, opt-in public egress observations; never cached or polled.
const PREFERENCE = "veu.nettools.public-ip.launch.v1";
const ENDPOINTS = {
  ipv4: "https://api.ipify.org?format=json",
  ipv6: "https://api6.ipify.org?format=json",
};
const fresh = () => ({
  status: "not-checked",
  address: "",
  checkedAt: "",
  error: "",
  previous: null,
});
export function getLaunchPreference(storage = globalThis.localStorage) {
  return storage.getItem(PREFERENCE) === "true";
}
export function setLaunchPreference(
  enabled,
  storage = globalThis.localStorage,
) {
  if (typeof enabled !== "boolean") throw Error("Invalid launch preference");
  storage.setItem(PREFERENCE, String(enabled));
}
async function readBounded(response, maxBytes, signal) {
  if (!response.ok) throw Error("Provider request failed");
  const length = response.headers.get("content-length");
  if (length && (!/^\d+$/.test(length) || Number(length) > maxBytes))
    throw Error("Provider response exceeds size limit");
  if (!response.body?.getReader)
    throw Error("Provider response cannot be read safely");
  const reader = response.body.getReader();
  const chunks = [];
  let size = 0;
  const abort = () => {
    reader.cancel().catch(() => {});
  };
  signal.addEventListener("abort", abort, { once: true });
  try {
    while (true) {
      if (signal.aborted) throw Error("Cancelled");
      const { done, value } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > maxBytes) throw Error("Provider response exceeds size limit");
      chunks.push(value);
    }
    const bytes = new Uint8Array(size);
    let offset = 0;
    for (const part of chunks) {
      bytes.set(part, offset);
      offset += part.byteLength;
    }
    return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } finally {
    signal.removeEventListener("abort", abort);
    reader.cancel().catch(() => {});
  }
}
export class PublicIpController {
  constructor({
    fetchImpl = globalThis.fetch,
    validateAddress,
    onChange = () => {},
    timeoutMs = 10000,
    maxBytes = 1024,
  } = {}) {
    if (typeof validateAddress !== "function")
      throw Error("Address validator required");
    if (
      !Number.isFinite(timeoutMs) ||
      timeoutMs <= 0 ||
      !Number.isInteger(maxBytes) ||
      maxBytes < 1 ||
      maxBytes > 1024
    )
      throw Error("Invalid request limits");
    this.fetchImpl = fetchImpl.bind(globalThis);
    this.validateAddress = validateAddress;
    this.onChange = onChange;
    this.timeoutMs = timeoutMs;
    this.maxBytes = maxBytes;
    this.states = { ipv4: fresh(), ipv6: fresh() };
    this.revision = 0;
    this.active = null;
  }
  emit() {
    this.onChange(structuredClone(this.states));
  }
  check() {
    if (this.active) return this.active.promise;
    const revision = ++this.revision;
    const active = { controllers: [], cancellations: [], promise: null };
    this.active = active;
    for (const family of Object.keys(ENDPOINTS)) {
      const old = this.states[family];
      this.states[family] = {
        ...fresh(),
        status: "checking",
        previous: old.address
          ? { address: old.address, checkedAt: old.checkedAt }
          : old.previous,
      };
    }
    // Defer execution so active.promise exists even for reentrant UI callbacks.
    active.promise = Promise.resolve().then(async () => {
      if (revision !== this.revision) return;
      this.emit();
      await Promise.all(
        Object.keys(ENDPOINTS).map((f) => this.request(f, revision, active)),
      );
      if (this.active === active) this.active = null;
    });
    return active.promise;
  }
  async request(family, revision, active) {
    const controller = new AbortController();
    active.controllers.push(controller);
    let timer;
    const stopped = new Promise((_, reject) => {
      active.cancellations.push(() => reject(Error("Cancelled")));
      timer = setTimeout(() => {
        reject(Error("Request timed out"));
        controller.abort();
      }, this.timeoutMs);
    });
    const fetchAddress = async () => {
      const response = await this.fetchImpl(ENDPOINTS[family], {
        method: "GET",
        mode: "cors",
        credentials: "omit",
        cache: "no-store",
        redirect: "error",
        headers: { Accept: "application/json" },
        signal: controller.signal,
      });
      const data = JSON.parse(
        await readBounded(response, this.maxBytes, controller.signal),
      );
      if (
        !data ||
        typeof data !== "object" ||
        Array.isArray(data) ||
        Object.keys(data).length !== 1 ||
        typeof data.ip !== "string" ||
        data.ip.length > 64 ||
        !data.ip ||
        data.ip.trim() !== data.ip
      )
        throw Error("Invalid provider response");
      const canonical = await this.validateAddress(data.ip, family);
      if (typeof canonical !== "string" || !canonical)
        throw Error("Invalid address");
      return canonical;
    };
    try {
      const address = await Promise.race([fetchAddress(), stopped]);
      if (revision !== this.revision) return;
      this.states[family] = {
        status: "verified",
        address,
        checkedAt: new Date().toISOString(),
        error: "",
        previous: null,
      };
    } catch (error) {
      if (revision !== this.revision) return;
      this.states[family] = {
        ...this.states[family],
        status: "failed",
        error:
          error instanceof Error ? error.message : "Public IP check failed",
      };
    } finally {
      clearTimeout(timer);
      if (revision === this.revision) this.emit();
    }
  }
  cancel() {
    if (!this.active) return;
    ++this.revision;
    const active = this.active;
    this.active = null;
    for (const cancel of active.cancellations) cancel();
    for (const controller of active.controllers) controller.abort();
    for (const family of Object.keys(ENDPOINTS))
      if (this.states[family].status === "checking")
        this.states[family] = {
          ...this.states[family],
          status: "failed",
          error: "Cancelled",
        };
    this.emit();
  }
}

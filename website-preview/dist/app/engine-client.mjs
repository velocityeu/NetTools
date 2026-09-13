export class EngineClient {
  constructor() {
    this.pending = new Map();
    this.next = 0;
    this.start();
  }
  start() {
    this.worker = new Worker(new URL("./worker.mjs", import.meta.url), {
      type: "module",
    });
    this.worker.onmessage = ({ data }) => {
      const p = this.pending.get(data.id);
      if (!p) return;
      clearTimeout(p.timer);
      this.pending.delete(data.id);
      data.ok ? p.resolve(data.result) : p.reject(new Error(data.error));
    };
    this.worker.onerror = () =>
      this.cancel("Calculation engine failed. Please retry.");
  }
  run(request) {
    return new Promise((resolve, reject) => {
      const id = ++this.next;
      const timer = setTimeout(
        () =>
          this.cancel(
            "Calculation exceeded 30 seconds. Reduce the plan and retry.",
          ),
        30000,
      );
      this.pending.set(id, { resolve, reject, timer });
      this.worker.postMessage({ id, request });
    });
  }
  cancel(message = "Calculation cancelled.") {
    this.worker.terminate();
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(new Error(message));
    }
    this.pending.clear();
    this.start();
  }
}

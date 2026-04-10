# HSYS — Firmware Messaging Architecture
## Requirements (Scoped: Message Bus, Modules, Buffer Pool)

| Field        | Detail                  |
|--------------|-------------------------|
| Document ID  | HSYS-SRS-001            |
| Version      | 0.2 (Draft)             |
| Date         | 2026-04-10              |
| Status       | Draft — In Discussion   |

---

## Modules

- A **Module** is a self-contained unit of application functionality.
- A module is designed to be **standalone** — it can be included in any firmware built on this architecture without breaking the build or the system.
- A module may **not fully function** if the messages it depends on (published by other modules) are not present; this is expected and acceptable behaviour.
- A firmware image can contain **any number of modules** — each module represents one piece of functionality (e.g. LoRa driver, Wi-Fi manager, sensor reader, display handler).
- Modules communicate with each other **exclusively through messages** — no direct function calls between modules at runtime.
- A module declares which **message IDs it publishes** and which **message IDs it subscribes to**; this forms the module's communication contract.

---

## Tasks and Task Sharing

- A module does **not own a dedicated RTOS task** by default.
- Multiple modules can **share a single RTOS task** — the task dispatches messages to whichever modules are bound to it.
- Grouping is by **functional domain** — for example, a "communication task" can host both a LoRa module and a Wi-Fi module.
- The task runs a **message dispatch loop**: it blocks on its queue, receives a message, identifies the target module, and calls that module's message handler.
- A module can be **re-assigned to a different task** at configuration time without changing the module's own code.

```
  RTOS Task: "comm_task"
  ┌──────────────────────────────────────────┐
  │  Queue (inbox)                           │
  │  ┌────────────────────────────────────┐  │
  │  │  msg → [LoRa Module handler]       │  │
  │  │  msg → [WiFi Module handler]       │  │
  │  │  msg → [LoRa Module handler]       │  │
  │  └────────────────────────────────────┘  │
  └──────────────────────────────────────────┘
```

---

## Messages

- A **message** is the only interface through which modules communicate.
- Every message has a **Message ID** — a unique identifier that represents the type/topic of the message.
- Any module can **publish** (send) a message with a given Message ID.
- Any module can **subscribe** to one or more Message IDs — it will receive all messages published with those IDs.
- A message carries the following fields:
  - `message_id` — identifies the type/topic of the message
  - `sender_id` — the module ID that published the message
  - `payload_ptr` — pointer to the message data buffer
  - `payload_size` — size of the payload in bytes
  - `timestamp` — system tick at time of publish
  - `subscribers` — resolved at dispatch time from the subscription table (not stored in the message itself)
- A message itself has **metadata** beyond just the payload — subscriber list resolution, priority, and routing are properties managed by the message bus, not the sender.
- Messages support **priority levels**: at minimum `LOW`, `NORMAL`, `HIGH`.
- Sending modes:
  - **Publish** (broadcast to all subscribers of that Message ID)
  - **Direct send** (to a specific module by Module ID)
  - **ISR-safe post** (callable from interrupt context)

```
  Publisher (Module A)
       │
       │  publish(MSG_SENSOR_DATA, payload)
       ▼
  ┌─────────────────────────────────┐
  │         Message Bus             │
  │  ┌───────────────────────────┐  │
  │  │  Subscription Table       │  │
  │  │  MSG_SENSOR_DATA → [B, C] │  │
  │  └───────────────────────────┘  │
  └────────┬──────────────┬─────────┘
           │              │
           ▼              ▼
     Module B inbox  Module C inbox
```

---

## Message Handler

- Each module registers a **message handler function** — a callback invoked when a subscribed message arrives.
- The handler is called from within the task that the module is bound to.
- The handler must be **non-blocking** — it must not wait indefinitely; long operations must be deferred or delegated to another task.
- A module can register **one handler per Message ID** or a **single catch-all handler** that switches on Message ID internally.
- The message bus delivers messages to handlers **in the order they were enqueued** per task queue (FIFO within same priority).

---

## Buffer Pool & Pool Manager

- All message payloads are allocated from a **pre-allocated buffer pool** — no heap allocation at runtime for message data.
- The pool contains **multiple size classes** of fixed-size buffers:

  | Size Class  | Default Count | Configurable |
  |-------------|:-------------:|:------------:|
  | 1 byte      | TBD           | Yes          |
  | 16 bytes    | TBD           | Yes          |
  | 32 bytes    | TBD           | Yes          |
  | 64 bytes    | TBD           | Yes          |
  | 512 bytes   | TBD           | Yes          |
  | 2048 bytes  | TBD           | Yes          |

- The **count of buffers per size class is configurable** at compile time (e.g. via Kconfig or a config header) — a firmware that frequently uses 128-byte payloads should be able to add a 128-byte class with a high count.
- Additional size classes (e.g. 128 bytes, 256 bytes) can be **added to the configuration** without changing library code.
- The **Pool Manager** is responsible for:
  - Allocating a buffer from the smallest fitting size class.
  - Returning a buffer to its correct pool on release.
  - Tracking allocation counts and free counts per size class.
  - Reporting exhaustion (a pool class running to zero free buffers) as a system-level warning event.
- Buffer allocation and release must be **thread-safe** and **ISR-safe**.
- A buffer must not be freed while it is still referenced by a message in any queue (**ownership rules** must be enforced).

```
  Pool Manager
  ┌────────────────────────────────────────────────┐
  │  [ 1B  pool ] [ 16B pool ] [ 32B  pool ]       │
  │  [ 64B pool ] [ 512B pool] [ 2048B pool ]      │
  │                                                │
  │  alloc(size) → finds smallest fitting class    │
  │             → returns ptr from that pool       │
  │                                                │
  │  free(ptr)   → identifies class from ptr       │
  │             → returns block to pool            │
  └────────────────────────────────────────────────┘
```

---

## Revision History

| Version | Date       | Description                                          |
|---------|------------|------------------------------------------------------|
| 0.1     | 2026-04-10 | Initial draft — broad architecture scope             |
| 0.2     | 2026-04-10 | Narrowed scope to modules, message bus, buffer pool  |

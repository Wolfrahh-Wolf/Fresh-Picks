# 🧺 Fresh Picks: The Hybrid Grocery Engine

**CodeCrafters SDP-1 Project** *Speed of C. Flexibility of Python. Elegance of VS Code Dark.*

Fresh Picks is a high-performance, intra-net hosted e-commerce platform designed for local grocery management. Unlike standard web apps, Fresh Picks offloads its core business logic and state management to compiled C binaries, utilizing custom-built data structures for maximum efficiency.

---

## 🛠️ Core Technology Stack
* **Backend:** Flask (Python 3) serving as a high-speed routing layer.
* **Logic Engine:** Compiled C binaries (`auth`, `order`, `inventory`).
* **Frontend:** Vanilla JS with Bootstrap 5, featuring a custom **VS Code-inspired** dark theme.
* **Security:** Native Intra-net hosting with **Self-Signed HTTPS/SSL** certificates.

---

## 🧠 Core Data Structures & Algorithmic Optimizations
The backend architecture deliberately avoids standard relational databases, instead leveraging custom-built data structures in C to achieve highly optimized memory management and execution speeds:

* **Singly Linked List (SLL):** Serves as the foundational memory-allocation architecture. It dynamically deserializes and chains database records (Users, Products, Orders) directly from flat binary `.dat` files at runtime, allowing the dataset to scale without rigid array constraints.
* **Doubly Linked List (DLL):** Drives the real-time shopping cart state. Bi-directional node traversal enables $O(1)$ mid-list item deletions and instantaneous quantity mutations, completely bypassing the shifting overhead native to standard arrays.
* **Min-Heap (Priority Queue):** Powers the Admin Dispatch Engine. It actively constructs a priority tree to sort queued orders based on delivery urgency, ensuring that high-priority "Morning" slots continuously bubble to the root node for $O(\log N)$ extraction and dispatch.
* **Circular Linked List (CLL):** Facilitates the automated logistics engine. It implements an infinite, wrap-around round-robin scheduling algorithm, moving a continuous pointer to guarantee mathematically equitable order distribution among active delivery personnel.
* **Custom O(1) Pointer Table (Direct Indexing):** To eliminate the performance latency of standard $O(N)$ linked list searches, the backend implements a specialized lookup table. Upon initializing, the C engine parses the numeric component of entity IDs (e.g., extracting `41` from `U1042` by subtracting the BASE ID “1001”) and maps the memory address of the corresponding SLL node to that specific index in a global pointer array. This enables instantaneous, collision-free data retrieval and updates.

---

## ✨ Implemented Features
* **5-Step Checkout Machine:** A seamless flow from Cart review to Delivery Slot selection and Invoice Preview.
* **JIT (Just-In-Time) Promotion:** Admin dashboard auto-promotes orders based on the current delivery slot.
* **Live PDF Engine:** Real-time generation of dark-theme receipts using `html2pdf.js`.
* **Dynamic Inventory:** Real-time stock monitoring with low-stock visual indicators.
* **Network Resilience:** Specifically configured for hosting over a local network (Wi-Fi/LAN) with encrypted traffic.

---

## 🗺️ Roadmap (Upcoming Features)
* **🚚 Delivery Boy Dashboard:** A mobile-optimized portal for agents to track assigned routes and update delivery statuses.
* **📊 Advanced Statistics:** Graphical sales analytics, inventory heatmaps, and demand forecasting.
* **🛡️ Admin Control Center:** Full CRUD management for Users, Delivery Personnel, and Shop metadata.
* **✉️ Automated Communications:** Direct-to-email PDF bills and secure **SMS OTP** authentication for logins and orders.
* **📦 Stock Alerts:** Push notifications for admins when inventory falls below a specific threshold.

---

## 🚀 Getting Started

### Prerequisites
* GCC Compiler (for C binaries)
* Python 3.x
* OpenSSL (for HTTPS certificates)

### Build Instructions
1.  **Compile the Logic Engine:** ```bash
    bash build.sh
    ```
2.  **Generate SSL Certificates (if not present):**
    ```bash
    # Follow the project documentation for self-signing instructions
    ```
3.  **Launch the HTTPS Server:**
    ```bash
    python app.py
    ```
4.  **Access on LAN:** Access via the IP provided in the terminal (e.g., `https://192.168.xx.xx:5000`).

---

## 🐞 Troubleshooting
* **Blank PDFs:** Ensure the receipt clone in JS uses hardcoded hex colors to avoid CSS variable stripping.
* **404 Errors:** Verify that the API routes in `app.py` match the `fetch` URLs in your HTML templates.
* **Syntax Errors:** Check your HTML script blocks for unclosed tags or Markdown artifacts.

---
*Developed with ❤️ by Team CodeCrafters for the SDP-1 Project.*

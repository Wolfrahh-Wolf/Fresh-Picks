/*
 * delivery.c - Fresh Picks: Post-Order Delivery Management (v4)
 * ==============================================================
 * REFACTORED OUT OF order.c (Session 6)
 * This binary handles everything that happens AFTER an order is placed:
 *   - Updating order status (dispatch, deliver, cancel)
 *   - Assigning / re-assigning delivery agents
 *   - Fetching active orders for the delivery dashboard
 *   - Batch slot promotion (JIT auto-dispatch)
 *   - Cancellation with fee deduction logic
 *
 * Called by Flask (app.py) via subprocess.run() like this:
 *   ./delivery <command> [arguments...]
 *
 * v4 MIGRATION NOTES:
 *   - All data files are now binary .dat structs accessed via utils.c.
 *   - Direct fopen/fread/fwrite/strtok over .txt files is FORBIDDEN.
 *   - SLL loading/freeing is centralised in main(); cmd_ functions
 *     receive pre-built pointer tables and SLL heads as arguments.
 *   - Order lookups use O(1) build_order_table() / get_index_from_id().
 *   - Delivery boy lookups retain O(N) SLL traversal (no table builder).
 *
 * ─────────────────────────────────────────────────────────────────
 * COMMANDS (argv[1]):
 *
 *   update_status <order_id> <new_status>
 *   cancel_order <order_id>
 *   get_active_orders
 *   assign_agent <order_id> <boy_id>
 *   batch_promote_slot <slot_name>
 *   list_all_orders
 *   list_all_orders_sorted
 *
 * ─────────────────────────────────────────────────────────────────
 * OUTPUT FORMAT:
 *   Always "SUCCESS|message/count" or "ERROR|reason"
 *   Flask reads stdout and passes it back to the frontend as JSON.
 *
 * Team: CodeCrafters | Project: Fresh Picks | SDP-1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "models.h"


/* ═════════════════════════════════════════════════════════════
   SECTION 1: HELPER FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/* Walk the DeliveryBoyNode SLL and fill out_name/out_phone for boy_id; falls back to "Unknown"/"N/A" */
static void find_boy_in_sll(DeliveryBoyNode* head, const char* boy_id,
                             char* out_name, char* out_phone) {
    strncpy(out_name,  "Unknown", MAX_STR_LEN - 1);
    strncpy(out_phone, "N/A",     MAX_STR_LEN - 1);
    out_name [MAX_STR_LEN - 1] = '\0';
    out_phone[MAX_STR_LEN - 1] = '\0';

    DeliveryBoyNode* curr = head;
    while (curr != NULL) {
        if (strcmp(curr->data.boy_id, boy_id) == 0) {
            strncpy(out_name,  curr->data.name,  MAX_STR_LEN - 1);
            strncpy(out_phone, curr->data.phone, MAX_STR_LEN - 1);
            out_name [MAX_STR_LEN - 1] = '\0';
            out_phone[MAX_STR_LEN - 1] = '\0';
            return;
        }
        curr = curr->next;
    }
}

/* Map a delivery slot name to a numeric sort priority: Morning=1, Afternoon=2, Evening=3 */
static int get_slot_priority(const char* slot) {
    if (strcmp(slot, "Morning")   == 0) return 1;
    if (strcmp(slot, "Afternoon") == 0) return 2;
    return 3;
}

/* qsort comparator: slot priority ASC, then timestamp ASC as tiebreaker */
static int compare_orders_priority(const void* a, const void* b) {
    const Order* oa = (const Order*)a;
    const Order* ob = (const Order*)b;
    if (oa->slot_priority != ob->slot_priority)
        return oa->slot_priority - ob->slot_priority;
    return strcmp(oa->timestamp, ob->timestamp);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 2: COMMAND HANDLER FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/* Change the status field of ONE order via O(1) table lookup; validates status before touching data */
void cmd_update_status(const char* order_id, const char* new_status,
                       OrderNode** ord_table, int ord_table_size,
                       OrderNode* ord_head) {
    const char* VALID[] = {
        "Order Placed", "Out for Delivery", "Delivered", "Cancelled"
    };
    int valid = 0;
    for (int i = 0; i < 4; i++) {
        if (strcmp(new_status, VALID[i]) == 0) { valid = 1; break; }
    }
    if (!valid) {
        PRINT_ERROR("Invalid status value");
        return;
    }

    int idx = get_index_from_id(order_id);
    if (idx < 0 || idx >= ord_table_size || !ord_table[idx]) {
        PRINT_ERROR("Order not found");
        return;
    }

    OrderNode* match = ord_table[idx];
    strncpy(match->data.status, new_status, MAX_STR_LEN - 1);
    match->data.status[MAX_STR_LEN - 1] = '\0';

    save_order_sll(ord_head);
    PRINT_SUCCESS("Status updated");
}

/* Cancel ONE "Order Placed" order via O(1) table lookup; rejects any other status */
void cmd_cancel_order(const char* order_id,
                      OrderNode** ord_table, int ord_table_size,
                      OrderNode* ord_head) {
    int idx = get_index_from_id(order_id);
    if (idx < 0 || idx >= ord_table_size || !ord_table[idx]) {
        PRINT_ERROR("Order not found");
        return;
    }

    OrderNode* match = ord_table[idx];
    if (strcmp(match->data.status, "Order Placed") != 0) {
        PRINT_ERROR("Only Order Placed orders can be cancelled");
        return;
    }

    strncpy(match->data.status, "Cancelled", MAX_STR_LEN - 1);
    match->data.status[MAX_STR_LEN - 1] = '\0';

    save_order_sll(ord_head);
    PRINT_SUCCESS("Order cancelled");
}

/* Return all "Order Placed"/"Out for Delivery" orders enriched with boy_name and boy_phone */
void cmd_get_active_orders(OrderNode* ord_head, DeliveryBoyNode* boy_head) {
    /* Count active orders first to print the header */
    int        active = 0;
    OrderNode* curr   = ord_head;
    while (curr != NULL) {
        if (strcmp(curr->data.status, "Order Placed")     == 0 ||
            strcmp(curr->data.status, "Out for Delivery") == 0)
            active++;
        curr = curr->next;
    }

    printf("SUCCESS|%d\n", active);

    curr = ord_head;
    while (curr != NULL) {
        if (strcmp(curr->data.status, "Order Placed")     != 0 &&
            strcmp(curr->data.status, "Out for Delivery") != 0) {
            curr = curr->next;
            continue;
        }

        char boy_name [MAX_STR_LEN] = "Unknown";
        char boy_phone[MAX_STR_LEN] = "N/A";
        find_boy_in_sll(boy_head, curr->data.delivery_boy_id,
                        boy_name, boy_phone);

        printf("%s|%s|%.2f|%s|%s|%s|%s|%s|%s|%s\n",
            curr->data.order_id,
            curr->data.user_id,
            curr->data.total_amount,
            curr->data.delivery_slot,
            curr->data.delivery_boy_id,
            curr->data.status,
            curr->data.timestamp,
            curr->data.items_string,
            boy_name,
            boy_phone
        );
        curr = curr->next;
    }
}

/* Override delivery_boy_id on one order via O(1) table lookup */
void cmd_assign_agent(const char* order_id, const char* boy_id,
                      OrderNode** ord_table, int ord_table_size,
                      OrderNode* ord_head) {
    int idx = get_index_from_id(order_id);
    if (idx < 0 || idx >= ord_table_size || !ord_table[idx]) {
        PRINT_ERROR("Order not found");
        return;
    }

    OrderNode* match = ord_table[idx];
    strncpy(match->data.delivery_boy_id, boy_id, MAX_ID_LEN - 1);
    match->data.delivery_boy_id[MAX_ID_LEN - 1] = '\0';

    save_order_sll(ord_head);
    PRINT_SUCCESS("Agent assigned");
}

/* Flip all "Order Placed" orders for a given slot to "Out for Delivery"; returns count promoted */
void cmd_batch_promote_slot(const char* slot_name,
                            OrderNode* ord_head) {
    if (strcmp(slot_name, "Morning")   != 0 &&
        strcmp(slot_name, "Afternoon") != 0 &&
        strcmp(slot_name, "Evening")   != 0) {
        PRINT_ERROR("Invalid slot name");
        return;
    }

    /* O(N) traversal is unavoidable here — all matching orders must be promoted, no single ID target */
    int        promoted = 0;
    OrderNode* curr     = ord_head;
    while (curr != NULL) {
        if (strcmp(curr->data.delivery_slot, slot_name) == 0 &&
            strcmp(curr->data.status, "Order Placed")   == 0) {
            strncpy(curr->data.status, "Out for Delivery", MAX_STR_LEN - 1);
            curr->data.status[MAX_STR_LEN - 1] = '\0';
            promoted++;
        }
        curr = curr->next;
    }

    save_order_sll(ord_head);

    char result[32];
    snprintf(result, sizeof(result), "%d", promoted);
    PRINT_SUCCESS(result);
}

/* Dump every order newest-first, enriched with boy_name and boy_phone; used by admin full-view */
void cmd_list_all_orders(OrderNode* ord_head, DeliveryBoyNode* boy_head) {
    int total = sll_count_orders(ord_head);
    printf("SUCCESS|%d\n", total);

    if (total == 0) return;

    /* Collect node pointers for reverse iteration */
    OrderNode** ptrs = (OrderNode**)malloc(sizeof(OrderNode*) * total);
    if (!ptrs) return;

    int        idx  = 0;
    OrderNode* curr = ord_head;
    while (curr != NULL) {
        ptrs[idx++] = curr;
        curr = curr->next;
    }

    for (int i = idx - 1; i >= 0; i--) {
        char boy_name [MAX_STR_LEN] = "Unknown";
        char boy_phone[MAX_STR_LEN] = "N/A";
        find_boy_in_sll(boy_head, ptrs[i]->data.delivery_boy_id,
                        boy_name, boy_phone);

        printf("%s|%s|%.2f|%s|%s|%s|%s|%s|%s|%s\n",
            ptrs[i]->data.order_id,
            ptrs[i]->data.user_id,
            ptrs[i]->data.total_amount,
            ptrs[i]->data.delivery_slot,
            ptrs[i]->data.delivery_boy_id,
            ptrs[i]->data.status,
            ptrs[i]->data.timestamp,
            ptrs[i]->data.items_string,
            boy_name,
            boy_phone
        );
    }

    free(ptrs);
}

/* Dump all orders sorted by slot priority then timestamp ASC, enriched with boy_name and boy_phone */
void cmd_list_all_orders_sorted(OrderNode* ord_head, DeliveryBoyNode* boy_head) {
    int total = sll_count_orders(ord_head);
    printf("SUCCESS|%d\n", total);

    if (total == 0) return;

    /* Copy SLL data into a flat array for qsort */
    Order* arr = (Order*)malloc(sizeof(Order) * total);
    if (!arr) return;

    int        idx  = 0;
    OrderNode* curr = ord_head;
    while (curr != NULL) {
        arr[idx]               = curr->data;
        arr[idx].slot_priority = get_slot_priority(curr->data.delivery_slot);
        idx++;
        curr = curr->next;
    }

    qsort(arr, total, sizeof(Order), compare_orders_priority);

    for (int i = 0; i < total; i++) {
        char boy_name [MAX_STR_LEN] = "Unknown";
        char boy_phone[MAX_STR_LEN] = "N/A";
        find_boy_in_sll(boy_head, arr[i].delivery_boy_id,
                        boy_name, boy_phone);

        printf("%s|%s|%.2f|%s|%s|%s|%s|%s|%s|%s\n",
            arr[i].order_id,
            arr[i].user_id,
            arr[i].total_amount,
            arr[i].delivery_slot,
            arr[i].delivery_boy_id,
            arr[i].status,
            arr[i].timestamp,
            arr[i].items_string,
            boy_name,
            boy_phone
        );
    }

    free(arr);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 3: MAIN — Command Dispatcher
   ═════════════════════════════════════════════════════════════ */

/* Entry point: loads SLLs, builds pointer tables, dispatches to handlers, then frees everything */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command. Usage: ./delivery <command> [args]");
        return 1;
    }

    /* ── Load SLLs (single source of truth for this process lifetime) ── */
    OrderNode*       ord_head = load_order_sll();
    DeliveryBoyNode* boy_head = load_delivery_boy_sll();

    /* ── Build pointer table for O(1) order lookups ── */
    int          ord_table_size = 0;
    OrderNode**  ord_table      = NULL;

    if (ord_head) {
        ord_table = build_order_table(ord_head, &ord_table_size);
        if (!ord_table) {
            free_order_sll(ord_head);
            free_delivery_boy_sll(boy_head);
            PRINT_ERROR("Failed to allocate order table");
            return 1;
        }
    }

    /* ── Dispatch ── */
    const char* cmd = argv[1];
    int ret = 0;

    if (strcmp(cmd, "update_status") == 0) {
        if (argc < 4) {
            PRINT_ERROR("Usage: update_status <order_id> <new_status>");
            ret = 1;
        } else if (!ord_head) {
            PRINT_ERROR("No orders found");
        } else {
            cmd_update_status(argv[2], argv[3],
                              ord_table, ord_table_size, ord_head);
        }

    } else if (strcmp(cmd, "cancel_order") == 0) {
        if (argc < 3) {
            PRINT_ERROR("Usage: cancel_order <order_id>");
            ret = 1;
        } else if (!ord_head) {
            PRINT_ERROR("No orders found");
        } else {
            cmd_cancel_order(argv[2],
                             ord_table, ord_table_size, ord_head);
        }

    } else if (strcmp(cmd, "get_active_orders") == 0) {
        cmd_get_active_orders(ord_head, boy_head);

    } else if (strcmp(cmd, "assign_agent") == 0) {
        if (argc < 4) {
            PRINT_ERROR("Usage: assign_agent <order_id> <boy_id>");
            ret = 1;
        } else if (!ord_head) {
            PRINT_ERROR("No orders found");
        } else {
            cmd_assign_agent(argv[2], argv[3],
                             ord_table, ord_table_size, ord_head);
        }

    } else if (strcmp(cmd, "batch_promote_slot") == 0) {
        if (argc < 3) {
            PRINT_ERROR("Usage: batch_promote_slot <slot_name>");
            ret = 1;
        } else {
            /* Pass ord_head directly; zero orders is valid (promotes 0) */
            cmd_batch_promote_slot(argv[2], ord_head);
        }

    } else if (strcmp(cmd, "list_all_orders") == 0) {
        cmd_list_all_orders(ord_head, boy_head);

    } else if (strcmp(cmd, "list_all_orders_sorted") == 0) {
        cmd_list_all_orders_sorted(ord_head, boy_head);

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
        ret = 1;
    }

    /* ── Teardown: free table first, then the SLLs it pointed into ── */
    free(ord_table);
    free_order_sll(ord_head);
    free_delivery_boy_sll(boy_head);

    return ret;
}

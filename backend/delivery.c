/*
 * delivery.c - Fresh Picks: Post-Order Delivery Management 
 * ==============================================================
 * COMMANDS (argv[1]):
 *
 *   update_status <order_id> <new_status>
 *     → Change the status field of one order in orders.dat.
 *       Valid values: "Order Placed", "Out for Delivery",
 *                     "Delivered", "Cancelled"
 *
 *   cancel_order <order_id>
 *     → Set status to "Cancelled". Only "Order Placed" orders
 *       may be cancelled. ₹50 fee note handled at Flask/UI level.
 *
 *   get_active_orders
 *     → Dump all orders whose status is "Order Placed" or
 *       "Out for Delivery". Used by the delivery dashboard.
 *       Output: SUCCESS|<count> then one row per line.
 *
 *   assign_agent <order_id> <boy_id>
 *     → Override the delivery_boy_id on one specific order.
 *
 *   batch_promote_slot <slot_name>
 *     → Flip all "Order Placed" orders for a given slot to
 *       "Out for Delivery". Returns the count promoted.
 *
 *   list_all_orders
 *     → Dump EVERY order, newest-first, with boy_name + boy_phone
 *       joined from delivery_boys.dat. Used by admin full-view.
 *
 *   list_all_orders_sorted
 *     → Dump ALL orders sorted by slot priority (Morning first),
 *       then timestamp ASC as tiebreaker.
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
#include "models.h"   /* Struct definitions, SLL node types, utils.c prototypes */


/* ═════════════════════════════════════════════════════════════
   SECTION 1: HELPER FUNCTIONS
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: find_boy_in_sll
 * PURPOSE:  Walk a DeliveryBoyNode SLL and fill out_name / out_phone
 *           for the matching boy_id.  Falls back to "Unknown" / "N/A".
 * PARAMS:   head      — head of the already-loaded DeliveryBoyNode SLL
 *           boy_id    — ID to search for (e.g. "D1001")
 *           out_name  — buffer to receive the boy's name
 *           out_phone — buffer to receive the boy's phone
 * OUTPUT:   (none — writes into out_name / out_phone)
 * SCHEMA:   (none)
 */
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

/*
 * FUNCTION: get_slot_priority
 * PURPOSE:  Map a delivery slot name to a numeric priority for sorting.
 *           Morning = 1 (most urgent), Afternoon = 2, Evening = 3.
 * PARAMS:   slot — delivery slot string
 * OUTPUT:   (none — returns int)
 * SCHEMA:   (none)
 */
static int get_slot_priority(const char* slot) {
    if (strcmp(slot, "Morning")   == 0) return 1;
    if (strcmp(slot, "Afternoon") == 0) return 2;
    return 3;   /* Evening or unrecognised */
}

/*
 * FUNCTION: compare_orders_priority
 * PURPOSE:  qsort comparator — slot priority ASC, then timestamp ASC.
 * PARAMS:   a — pointer to first Order
 *           b — pointer to second Order
 * OUTPUT:   (none — returns int for qsort)
 * SCHEMA:   (none)
 */
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

/*
 * FUNCTION: cmd_update_status
 * PURPOSE:  Change the status field of ONE order in the SLL and persist.
 *           Validates the status string before touching any data.
 * PARAMS:   order_id   — e.g. "ORD1001"
 *           new_status — one of the four valid status strings
 * OUTPUT:   SUCCESS|Status updated
 *           ERROR|Invalid status value
 *           ERROR|No orders found
 *           ERROR|Order not found
 * SCHEMA:   SUCCESS|Status updated
 */
void cmd_update_status(const char* order_id, const char* new_status) {
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

    OrderNode* head = load_order_sll();
    if (!head) {
        PRINT_ERROR("No orders found");
        return;
    }

    OrderNode* curr  = head;
    int        found = 0;
    while (curr != NULL) {
        if (strcmp(curr->data.order_id, order_id) == 0) {
            strncpy(curr->data.status, new_status, MAX_STR_LEN - 1);
            curr->data.status[MAX_STR_LEN - 1] = '\0';
            found = 1;
            break;
        }
        curr = curr->next;
    }

    if (!found) {
        free_order_sll(head);
        PRINT_ERROR("Order not found");
        return;
    }

    save_order_sll(head);
    free_order_sll(head);
    PRINT_SUCCESS("Status updated");
}

/*
 * FUNCTION: cmd_cancel_order
 * PURPOSE:  Business-rule-aware cancellation.  Only "Order Placed"
 *           orders may be cancelled through this path.
 *           The ₹50 cancellation fee is noted in the frontend
 *           disclaimer; the actual deduction lives outside C scope.
 * PARAMS:   order_id — e.g. "ORD1001"
 * OUTPUT:   SUCCESS|Order cancelled
 *           ERROR|Only Order Placed orders can be cancelled
 *           ERROR|Order not found
 *           ERROR|No orders found
 * SCHEMA:   SUCCESS|Order cancelled
 */
void cmd_cancel_order(const char* order_id) {
    OrderNode* head = load_order_sll();
    if (!head) {
        PRINT_ERROR("No orders found");
        return;
    }

    OrderNode* curr  = head;
    int        found = 0;
    while (curr != NULL) {
        if (strcmp(curr->data.order_id, order_id) == 0) {
            if (strcmp(curr->data.status, "Order Placed") != 0) {
                free_order_sll(head);
                PRINT_ERROR("Only Order Placed orders can be cancelled");
                return;
            }
            strncpy(curr->data.status, "Cancelled", MAX_STR_LEN - 1);
            curr->data.status[MAX_STR_LEN - 1] = '\0';
            found = 1;
            break;
        }
        curr = curr->next;
    }

    if (!found) {
        free_order_sll(head);
        PRINT_ERROR("Order not found");
        return;
    }

    save_order_sll(head);
    free_order_sll(head);
    PRINT_SUCCESS("Order cancelled");
}

/*
 * FUNCTION: cmd_get_active_orders
 * PURPOSE:  Return all orders whose status is "Order Placed" or
 *           "Out for Delivery", enriched with boy_name + boy_phone.
 *           Used by the delivery dashboard.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|<count>
 *           order_id|user_id|total|slot|boy_id|status|timestamp|items|boy_name|boy_phone
 *           ...
 * SCHEMA:   order_id|user_id|total|slot|boy_id|status|timestamp|items_string|boy_name|boy_phone
 */
void cmd_get_active_orders(void) {
    DeliveryBoyNode* boy_head = load_delivery_boy_sll();
    OrderNode*       ord_head = load_order_sll();

    /* Count active orders first so we can print the header */
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

    free_order_sll(ord_head);
    free_delivery_boy_sll(boy_head);
}

/*
 * FUNCTION: cmd_assign_agent
 * PURPOSE:  Override the delivery_boy_id on one specific order.
 *           Used when an admin manually re-assigns a delivery agent
 *           (e.g. original agent is sick).
 * PARAMS:   order_id — e.g. "ORD1001"
 *           boy_id   — e.g. "D1002"
 * OUTPUT:   SUCCESS|Agent assigned
 *           ERROR|Order not found
 *           ERROR|No orders found
 * SCHEMA:   SUCCESS|Agent assigned
 */
void cmd_assign_agent(const char* order_id, const char* boy_id) {
    OrderNode* head = load_order_sll();
    if (!head) {
        PRINT_ERROR("No orders found");
        return;
    }

    OrderNode* curr  = head;
    int        found = 0;
    while (curr != NULL) {
        if (strcmp(curr->data.order_id, order_id) == 0) {
            strncpy(curr->data.delivery_boy_id, boy_id, MAX_ID_LEN - 1);
            curr->data.delivery_boy_id[MAX_ID_LEN - 1] = '\0';
            found = 1;
            break;
        }
        curr = curr->next;
    }

    if (!found) {
        free_order_sll(head);
        PRINT_ERROR("Order not found");
        return;
    }

    save_order_sll(head);
    free_order_sll(head);
    PRINT_SUCCESS("Agent assigned");
}

/*
 * FUNCTION: cmd_batch_promote_slot
 * PURPOSE:  Flip ALL "Order Placed" orders for a given slot to
 *           "Out for Delivery".  Returns the count promoted.
 * PARAMS:   slot_name — "Morning" | "Afternoon" | "Evening"
 * OUTPUT:   SUCCESS|<n>
 *           ERROR|Invalid slot name
 * SCHEMA:   SUCCESS|<promoted_count>
 */
void cmd_batch_promote_slot(const char* slot_name) {
    if (strcmp(slot_name, "Morning")   != 0 &&
        strcmp(slot_name, "Afternoon") != 0 &&
        strcmp(slot_name, "Evening")   != 0) {
        PRINT_ERROR("Invalid slot name");
        return;
    }

    OrderNode* head = load_order_sll();
    if (!head) {
        PRINT_SUCCESS("0");
        return;
    }

    int        promoted = 0;
    OrderNode* curr     = head;
    while (curr != NULL) {
        if (strcmp(curr->data.delivery_slot, slot_name) == 0 &&
            strcmp(curr->data.status, "Order Placed")   == 0) {
            strncpy(curr->data.status, "Out for Delivery", MAX_STR_LEN - 1);
            curr->data.status[MAX_STR_LEN - 1] = '\0';
            promoted++;
        }
        curr = curr->next;
    }

    save_order_sll(head);
    free_order_sll(head);

    char result[32];
    snprintf(result, sizeof(result), "%d", promoted);
    PRINT_SUCCESS(result);
}

/*
 * FUNCTION: cmd_list_all_orders
 * PURPOSE:  Dump EVERY order from the SLL, newest-first, enriched with
 *           boy_name + boy_phone.  Used by Flask's full admin view.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|<count>
 *           order_id|user_id|total|slot|boy_id|status|timestamp|items|boy_name|boy_phone
 *           ...
 * SCHEMA:   order_id|user_id|total|slot|boy_id|status|timestamp|items_string|boy_name|boy_phone
 */
void cmd_list_all_orders(void) {
    DeliveryBoyNode* boy_head = load_delivery_boy_sll();
    OrderNode*       ord_head = load_order_sll();

    int total = sll_count_orders(ord_head);
    printf("SUCCESS|%d\n", total);

    if (total == 0) {
        free_order_sll(ord_head);
        free_delivery_boy_sll(boy_head);
        return;
    }

    /* Collect node pointers for reverse iteration */
    OrderNode** ptrs = (OrderNode**)malloc(sizeof(OrderNode*) * total);
    if (!ptrs) {
        free_order_sll(ord_head);
        free_delivery_boy_sll(boy_head);
        return;
    }

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
    free_order_sll(ord_head);
    free_delivery_boy_sll(boy_head);
}

/*
 * FUNCTION: cmd_list_all_orders_sorted
 * PURPOSE:  Dump ALL orders sorted by slot priority (Morning first),
 *           then timestamp ASC as tiebreaker.  Includes ALL statuses.
 *           Enriched with boy_name + boy_phone via SLL join.
 * PARAMS:   (none)
 * OUTPUT:   SUCCESS|<count>
 *           order_id|user_id|total|slot|boy_id|status|timestamp|items|boy_name|boy_phone
 *           ...
 * SCHEMA:   order_id|user_id|total|slot|boy_id|status|timestamp|items_string|boy_name|boy_phone
 */
void cmd_list_all_orders_sorted(void) {
    DeliveryBoyNode* boy_head = load_delivery_boy_sll();
    OrderNode*       ord_head = load_order_sll();

    int total = sll_count_orders(ord_head);

    printf("SUCCESS|%d\n", total);

    if (total == 0) {
        free_order_sll(ord_head);
        free_delivery_boy_sll(boy_head);
        return;
    }

    /* Copy SLL data into a flat array for qsort */
    Order* arr = (Order*)malloc(sizeof(Order) * total);
    if (!arr) {
        free_order_sll(ord_head);
        free_delivery_boy_sll(boy_head);
        return;
    }

    int        idx  = 0;
    OrderNode* curr = ord_head;
    while (curr != NULL) {
        arr[idx]              = curr->data;
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
    free_order_sll(ord_head);
    free_delivery_boy_sll(boy_head);
}


/* ═════════════════════════════════════════════════════════════
   SECTION 3: MAIN — Command Dispatcher
   ═════════════════════════════════════════════════════════════ */

/*
 * FUNCTION: main
 * PURPOSE:  Entry point.  Reads argv[1] and dispatches to the matching
 *           command handler.  Flask calls this binary via:
 *             subprocess.run(["./delivery", "update_status", ...], ...)
 * PARAMS:   argc — argument count, argv — argument vector
 * OUTPUT:   Delegates entirely to handler functions.
 * SCHEMA:   (none — dispatcher only)
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        PRINT_ERROR("No command. Usage: ./delivery <command> [args]");
        return 1;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "update_status") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: update_status <order_id> <new_status>"); return 1; }
        cmd_update_status(argv[2], argv[3]);

    } else if (strcmp(cmd, "cancel_order") == 0) {
        if (argc < 3) { PRINT_ERROR("Usage: cancel_order <order_id>"); return 1; }
        cmd_cancel_order(argv[2]);

    } else if (strcmp(cmd, "get_active_orders") == 0) {
        cmd_get_active_orders();

    } else if (strcmp(cmd, "assign_agent") == 0) {
        if (argc < 4) { PRINT_ERROR("Usage: assign_agent <order_id> <boy_id>"); return 1; }
        cmd_assign_agent(argv[2], argv[3]);

    } else if (strcmp(cmd, "batch_promote_slot") == 0) {
        if (argc < 3) { PRINT_ERROR("Usage: batch_promote_slot <slot_name>"); return 1; }
        cmd_batch_promote_slot(argv[2]);

    } else if (strcmp(cmd, "list_all_orders") == 0) {
        cmd_list_all_orders();

    } else if (strcmp(cmd, "list_all_orders_sorted") == 0) {
        cmd_list_all_orders_sorted();

    } else {
        char err[MAX_STR_LEN];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        PRINT_ERROR(err);
        return 1;
    }

    return 0;
}
